// SPDX-License-Identifier: GPL-2.0-only
//
// Last step: turn per-access check calls into a compare and a branch
// to a cold block. Runs after the vectorizer, so none of the earlier
// passes ever see the branches. Loop range checks (the _span calls)
// stay as calls; there's only one per loop.
//
// Every check boils down to
//
//     d = p - start;  trap if d > limit || n > limit - d
//
// where start and limit describe the object. For a heap pointer they
// come from a table lookup on the base address (lowfat.h); for a stack
// or global object start is the base and limit the known size. They
// depend only on the base, so when the base doesn't change inside a
// loop they are computed once in the preheader and the loop body keeps
// just the compare.

#include "JaccCommon.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

using namespace llvm;
using namespace jacc;

namespace {

struct Bounds {
	Value *Start, *Limit;
};

struct Lower {
	Function &F;
	LoopInfo &LI;
	Module &M;
	Type *I64, *I128;
	Constant *First, *Sizes, *Magics;
	ArrayType *Tab;
	MDNode *Unlikely;
	DenseMap<std::pair<Value *, Value *>, Bounds> cache;

	Lower(Function &F, LoopInfo &LI)
		: F(F), LI(LI), M(*F.getParent()),
		  I64(Type::getInt64Ty(M.getContext())),
		  I128(Type::getInt128Ty(M.getContext())),
		  Tab(ArrayType::get(I64, LF_NREG))
	{
		First = M.getOrInsertGlobal("lf_first", I64);
		Sizes = M.getOrInsertGlobal("lf_size", Tab);
		Magics = M.getOrInsertGlobal("lf_magic", Tab);
		Unlikely = MDBuilder(M.getContext()).createBranchWeights(1, 4000);
	}

	// where the bounds for Base (and Size, if any) can be computed:
	// the preheader of the outermost loop around CI that leaves them
	// alone, or right at CI
	Instruction *home(Instruction *CI, Value *Base, Value *Size)
	{
		Instruction *At = CI;

		for (Loop *L = LI.getLoopFor(CI->getParent());
		     L && L->getLoopPreheader() && L->isLoopInvariant(Base) &&
		     (!Size || L->isLoopInvariant(Size));
		     L = L->getParentLoop())
			At = L->getLoopPreheader()->getTerminator();
		return At;
	}

	Bounds known(Value *Base, Value *Size, Instruction *At)
	{
		IRBuilder<> B(At);
		return {B.CreatePtrToInt(Base, I64), Size};
	}

	// heap: size and slot start come from the address (see lowfat.h).
	// Anything we don't own gets start 0 and limit ~0, which no sane
	// access can exceed.
	Bounds heap(Value *BasePtr, Instruction *At)
	{
		IRBuilder<> B(At);
		Value *Base = B.CreatePtrToInt(BasePtr, I64);
		Value *First0 = B.CreateLoad(I64, First);
		Value *Idx = B.CreateSub(B.CreateLShr(Base, LF_SHIFT), First0);
		Value *Ours = B.CreateICmpULT(Idx, B.getInt64(LF_NREG));
		Value *Safe = B.CreateSelect(Ours, Idx, B.getInt64(0));

		Value *SzP = B.CreateInBoundsGEP(Tab, Sizes, {B.getInt64(0), Safe});
		Value *MgP = B.CreateInBoundsGEP(Tab, Magics, {B.getInt64(0), Safe});
		Value *Size = B.CreateLoad(I64, SzP);
		Value *Magic = B.CreateLoad(I64, MgP);

		Value *Rel = B.CreateAnd(Base, (1ULL << LF_SHIFT) - 1);
		Value *Wide = B.CreateMul(B.CreateZExt(Rel, I128), B.CreateZExt(Magic, I128));
		Value *Quot = B.CreateTrunc(B.CreateLShr(Wide, 64), I64);
		Value *Off = B.CreateSub(Rel, B.CreateMul(Quot, Size));

		Value *Known = B.CreateAnd(Ours, B.CreateICmpNE(Size, B.getInt64(0)));
		return {B.CreateSelect(Known, B.CreateSub(Base, Off), B.getInt64(0)),
			B.CreateSelect(Known, Size, B.getInt64(-1))};
	}

	void lower(CallInst *CI, bool isHeap)
	{
		Value *Base = CI->getArgOperand(0);
		Value *Size = isHeap ? nullptr : CI->getArgOperand(3);
		Instruction *At = home(CI, Base, Size);
		auto key = std::make_pair(Base, (Value *)At->getParent());
		Bounds BD;

		auto it = cache.find(key);
		if (it != cache.end() && At != CI) {
			BD = it->second;
		} else {
			BD = isHeap ? heap(Base, At) : known(Base, Size, At);
			if (At != CI)
				cache[key] = BD;
		}

		IRBuilder<> B(CI);
		Value *P = B.CreatePtrToInt(CI->getArgOperand(1), I64);
		Value *N = CI->getArgOperand(2);
		Value *D = B.CreateSub(P, BD.Start);
		Value *Bad = B.CreateOr(B.CreateICmpUGT(D, BD.Limit),
					B.CreateICmpUGT(N, B.CreateSub(BD.Limit, D)));

		Instruction *T = SplitBlockAndInsertIfThen(
			Bad, CI->getIterator(), true, Unlikely, nullptr, &LI);
		IRBuilder<> FB(T);
		FB.CreateCall(getFailFn(M));
	}
};

} // namespace

PreservedAnalyses jacc::LowerChecksPass::run(Function &F, FunctionAnalysisManager &AM)
{
	SmallVector<std::pair<CallInst *, bool>, 16> work;

	for (BasicBlock &BB : F)
		for (Instruction &I : BB)
			if (auto *CI = dyn_cast<CallInst>(&I))
				if (Function *C = CI->getCalledFunction()) {
					if (C->getName() == "__jacc_check")
						work.push_back({CI, true});
					else if (C->getName() == "__jacc_check_known")
						work.push_back({CI, false});
				}

	if (work.empty())
		return PreservedAnalyses::all();

	Lower L(F, AM.getResult<LoopAnalysis>(F));
	for (auto &w : work) {
		L.lower(w.first, w.second);
		w.first->eraseFromParent();
	}
	return PreservedAnalyses::none();
}
