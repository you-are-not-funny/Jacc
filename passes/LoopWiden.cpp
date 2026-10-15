// SPDX-License-Identifier: GPL-2.0-only
//
// Replace a per-iteration __jacc_check with one __jacc_check_span in
// the loop preheader, when the address is an affine function of the
// induction variable and the trip count is known.
//
//   for (i = 0; i < n; i++) s += a[i];
//
// has a check on a + 8*i in every iteration. SCEV says that address
// runs a, a+8, ... a+8*(n-1), so the loop only needs the two end
// points checked against a's object, once, before it starts.
//
// Only touches loops where:
//   - the one exit is the latch (rotated, do-while shape), so a block
//     that dominates the latch runs exactly BTC+1 times and the
//     preheader is only reached when the body runs at least once;
//   - the check block dominates the latch;
//   - nothing in the loop can unwind or exit the process early,
//     otherwise the hoisted check could trap on an iteration the
//     program would never have reached.
// Hoisting still makes the trap earlier than it used to be. With
// -jacc-precise-traps we refuse to hoist past any call at all.

#include "JaccCommon.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/DomTreeUpdater.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/ScalarEvolutionExpander.h"

#include <cstdlib>

using namespace llvm;
using namespace jacc;

static cl::opt<bool> Verbose("jacc-verbose", cl::init(false),
			     cl::desc("report hoisted bounds checks"));
static cl::opt<bool> PreciseTraps("jacc-precise-traps", cl::init(false),
				  cl::desc("never hoist a check past a call"));

// the cl::opt isn't reachable through clang (plugins load after option
// parsing), so the environment works too
static bool verbose()
{
	return Verbose || getenv("JACC_VERBOSE");
}

static bool no(const char *why)
{
	if (verbose())
		errs() << "jacc:   not hoisted: " << why << "\n";
	return false;
}

// 0: not a loop-hoistable check, 1: heap check, 2: stack/global check
static int checkKind(const CallBase *CB)
{
	const Function *F = CB->getCalledFunction();
	if (!F)
		return 0;
	if (F->getName() == "__jacc_check")
		return 1;
	if (F->getName() == "__jacc_check_known")
		return 2;
	return 0;
}

static bool loopIsStraightforward(Loop *L)
{
	if (!L->getLoopPreheader() || !L->getLoopLatch())
		return false;
	if (L->getExitingBlock() != L->getLoopLatch())
		return false;

	for (BasicBlock *BB : L->blocks()) {
		for (Instruction &I : *BB) {
			auto *CB = dyn_cast<CallBase>(&I);
			if (!CB)
				continue;
			const Function *F = CB->getCalledFunction();
			if (F && isRuntimeName(F->getName()))
				continue;
			if (PreciseTraps)
				return false;
			if (!CB->doesNotThrow() || !CB->willReturn())
				return false;
		}
	}
	return true;
}

/*
 * SCEV can't prove that 8*i doesn't overflow when n is an arbitrary
 * i64, and we can't either. What we do know is that the access which
 * follows the check goes through inbounds GEPs and runs every time the
 * check does. An inbounds GEP that leaves its object is undefined, so
 * if the offset were to wrap, the program was already broken and we
 * are free to assume it doesn't.
 */
static bool wrapIsUB(CallInst *CI, Value *P, Value *Base)
{
	for (Value *V = P; V != Base;) {
		auto *G = dyn_cast<GetElementPtrInst>(V);
		if (!G || !G->isInBounds())
			return false;
		V = G->getPointerOperand();
	}

	for (Instruction *I = CI->getNextNode(); I; I = I->getNextNode()) {
		if (auto *L = dyn_cast<LoadInst>(I))
			if (L->getPointerOperand() == P)
				return true;
		if (auto *S = dyn_cast<StoreInst>(I))
			if (S->getPointerOperand() == P)
				return true;
		if (auto *CB = dyn_cast<CallBase>(I)) {
			const Function *F = CB->getCalledFunction();
			if (!(F && isRuntimeName(F->getName())) &&
			    !isGuaranteedToTransferExecutionToSuccessor(I))
				return false;
		}
	}
	return false;
}

static bool widen(CallInst *CI, Loop *L, ScalarEvolution &SE, DominatorTree &DT,
		  LoopInfo &LI, const DataLayout &DL, bool &Guarded)
{
	Value *Base = CI->getArgOperand(0);
	Value *P = CI->getArgOperand(1);
	Value *N = CI->getArgOperand(2);
	Value *Size = checkKind(CI) == 2 ? CI->getArgOperand(3) : nullptr;

	if (!L->isLoopInvariant(Base) || !L->isLoopInvariant(N) ||
	    (Size && !L->isLoopInvariant(Size)))
		return no("base or size not invariant");
	if (!DT.dominates(CI->getParent(), L->getLoopLatch()))
		return no("check does not dominate latch");

	const SCEV *BTC = SE.getBackedgeTakenCount(L);
	if (isa<SCEVCouldNotCompute>(BTC))
		return no("trip count unknown");

	// work on the integer offset from the base, so we can sum it up
	// without SCEV getting confused about pointer arithmetic
	const SCEV *PS = SE.getSCEV(P);
	const SCEV *PBase = SE.getPointerBase(PS);
	auto *AR = dyn_cast<SCEVAddRecExpr>(SE.getMinusSCEV(PS, PBase));
	if (!AR || AR->getLoop() != L || !AR->isAffine())
		return no("address is not affine in this loop");
	// -fwrapv turns inbounds GEPs into plain ones, so often all we have
	// is "it might wrap". Then we guard the trip count at runtime below.
	bool needGuard = !wrapIsUB(CI, P, Base);

	const SCEV *Step = AR->getStepRecurrence(SE);
	auto *StepC = dyn_cast<SCEVConstant>(Step);
	if (needGuard && !StepC)
		return no("variable stride and can't show wrapping is undefined");
	const SCEV *FirstOff = AR->getStart();
	const SCEV *LastOff = AR->evaluateAtIteration(BTC, SE);
	if (isa<SCEVCouldNotCompute>(LastOff))
		return no("last address unknown");

	const SCEV *LoOff, *HiOff;
	if (SE.isKnownNonNegative(Step)) {
		LoOff = FirstOff;
		HiOff = LastOff;
	} else if (SE.isKnownNegative(Step)) {
		LoOff = LastOff;
		HiOff = FirstOff;
	} else {
		return no("step sign unknown");
	}
	const SCEV *Lo = SE.getAddExpr(PBase, LoOff);
	const SCEV *Hi = SE.getAddExpr(PBase, HiOff);

	BasicBlock *Pre = L->getLoopPreheader();
	Instruction *At = Pre->getTerminator();
	SCEVExpander Exp(SE, DL, "jacc.widen");
	if (!Exp.isSafeToExpandAt(Lo, At) || !Exp.isSafeToExpandAt(Hi, At))
		return no("bounds not expandable in preheader");

	/*
	 * No UB to lean on, so make sure step*BTC can't wrap. An object is
	 * nowhere near 2^61 bytes, so a loop that really would run that far
	 * is out of bounds anyway and trapping up front is the right answer.
	 */
	if (needGuard && !StepC->isZero()) {
		APInt Abs = StepC->getAPInt().abs();
		uint64_t Limit = (1ULL << 61) / Abs.getZExtValue();
		Value *Cnt = Exp.expandCodeFor(BTC, nullptr, At);
		IRBuilder<> GB(At);
		Value *Wide = GB.CreateZExtOrTrunc(Cnt, GB.getInt64Ty());
		Value *Bad = GB.CreateICmpUGT(Wide, GB.getInt64(Limit));
		DomTreeUpdater DTU(&DT, DomTreeUpdater::UpdateStrategy::Eager);
		Instruction *T = SplitBlockAndInsertIfThen(
			Bad, At->getIterator(), true, nullptr, &DTU, &LI);
		IRBuilder<> FB(T);
		FB.CreateCall(getFailFn(*CI->getModule()));
		Guarded = true;
	}

	Type *PtrTy = P->getType();
	Value *LoV = Exp.expandCodeFor(Lo, PtrTy, At);
	Value *HiV = Exp.expandCodeFor(Hi, PtrTy, At);

	IRBuilder<> B(At);
	if (Size)
		B.CreateCall(getSpanKnownFn(*CI->getModule()), {Base, LoV, HiV, N, Size});
	else
		B.CreateCall(getSpanFn(*CI->getModule()), {Base, LoV, HiV, N});
	CI->eraseFromParent();
	return true;
}

PreservedAnalyses jacc::LoopWidenPass::run(Function &F, FunctionAnalysisManager &AM)
{
	auto &LI = AM.getResult<LoopAnalysis>(F);
	auto &SE = AM.getResult<ScalarEvolutionAnalysis>(F);
	auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
	const DataLayout &DL = F.getParent()->getDataLayout();
	unsigned hoisted = 0, seen = 0;
	bool guarded = false;

	// preorder reversed: inner loops before the loops around them
	auto loops = LI.getLoopsInPreorder();
	for (auto it = loops.rbegin(); it != loops.rend(); ++it) {
		Loop *L = *it;
		SmallVector<CallInst *, 8> checks;

		if (!loopIsStraightforward(L))
			continue;
		for (BasicBlock *BB : L->blocks()) {
			if (LI.getLoopFor(BB) != L)
				continue;
			for (Instruction &I : *BB)
				if (auto *CI = dyn_cast<CallInst>(&I))
					if (checkKind(CI))
						checks.push_back(CI);
		}
		for (CallInst *CI : checks) {
			seen++;
			if (widen(CI, L, SE, DT, LI, DL, guarded))
				hoisted++;
		}
	}

	if (verbose() && seen)
		errs() << "jacc: " << F.getName() << ": hoisted " << hoisted
		       << " of " << seen << " loop checks\n";
	if (!hoisted)
		return PreservedAnalyses::all();

	if (guarded)
		return PreservedAnalyses::none();
	PreservedAnalyses PA;
	PA.preserveSet<CFGAnalyses>();
	return PA;
}
