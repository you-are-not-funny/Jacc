// SPDX-License-Identifier: GPL-2.0-only
//
// Puts a __jacc_check in front of every load and store that we can't
// prove stays inside a stack or global object. Anything fancier than
// that (range analysis, dominating checks, loops) is left to the
// later passes, so this one stays dumb on purpose.

#include "JaccCommon.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"

using namespace llvm;
using namespace jacc;

static bool staysInObject(Value *P, uint64_t N, const DataLayout &DL)
{
	APInt Off(DL.getIndexTypeSizeInBits(P->getType()), 0);
	Value *Obj = P->stripAndAccumulateConstantOffsets(DL, Off, true);
	uint64_t Size;

	if (auto *A = dyn_cast<AllocaInst>(Obj)) {
		auto S = A->getAllocationSize(DL);
		if (!S || S->isScalable())
			return false;
		Size = S->getFixedValue();
	} else if (auto *G = dyn_cast<GlobalVariable>(Obj)) {
		if (G->isDeclaration())
			return false;
		Size = DL.getTypeAllocSize(G->getValueType());
	} else {
		return false;
	}
	return Off.isNonNegative() && Off.ule(Size) && N <= Size - Off.getZExtValue();
}

PreservedAnalyses jacc::InstrumentPass::run(Function &F, FunctionAnalysisManager &)
{
	if (F.isDeclaration() || isRuntimeName(F.getName()))
		return PreservedAnalyses::all();

	Module &M = *F.getParent();
	const DataLayout &DL = M.getDataLayout();
	SmallVector<Instruction *, 64> work;

	for (BasicBlock &BB : F)
		for (Instruction &I : BB)
			if (isa<LoadInst>(I) || isa<StoreInst>(I))
				work.push_back(&I);

	FunctionCallee Check = getCheckFn(M);
	bool changed = false;

	for (Instruction *I : work) {
		Value *P;
		Type *Ty;

		if (auto *L = dyn_cast<LoadInst>(I)) {
			P = L->getPointerOperand();
			Ty = L->getType();
		} else {
			auto *S = cast<StoreInst>(I);
			P = S->getPointerOperand();
			Ty = S->getValueOperand()->getType();
		}
		uint64_t N = DL.getTypeStoreSize(Ty).getFixedValue();
		if (staysInObject(P, N, DL))
			continue;

		IRBuilder<> B(I);
		B.CreateCall(Check, {getUnderlyingObject(P), P, B.getInt64(N)});
		changed = true;
	}
	return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
