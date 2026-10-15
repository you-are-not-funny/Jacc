// SPDX-License-Identifier: GPL-2.0-only
//
// Puts a check in front of every memory access we can't prove stays
// inside its object: loads, stores, the memcpy family, and a few libc
// string calls. Anything cleverer (dominating checks, loops, range
// analysis) is left to the later passes, so this one stays dumb.
//
// Stack and global objects have a size we know at compile time, so
// they get exact bounds. Heap objects get whatever the allocator's
// size class says (runtime/lowfat.h).

#include "JaccCommon.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"

using namespace llvm;
using namespace jacc;

namespace {

struct Ctx {
	Module &M;
	const DataLayout &DL;
	Type *I64;
	DenseMap<AllocaInst *, Value *> allocaSize;

	explicit Ctx(Module &M)
		: M(M), DL(M.getDataLayout()), I64(Type::getInt64Ty(M.getContext())) {}
};

} // namespace

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
		if (G->isDeclaration() || !G->getValueType()->isSized())
			return false;
		Size = DL.getTypeAllocSize(G->getValueType());
	} else {
		return false;
	}
	return Off.isNonNegative() && Off.ule(Size) && N <= Size - Off.getZExtValue();
}

// Size in bytes of a stack or global object, or null if we don't know.
// A variable-length alloca gets its size computed once, right after the
// alloca, so the value is loop invariant wherever it's used.
static Value *objectSize(Value *Obj, Ctx &X)
{
	if (auto *A = dyn_cast<AllocaInst>(Obj)) {
		auto it = X.allocaSize.find(A);
		if (it != X.allocaSize.end())
			return it->second;

		TypeSize elt = X.DL.getTypeAllocSize(A->getAllocatedType());
		if (elt.isScalable())
			return nullptr;

		Value *Sz;
		if (auto *C = dyn_cast<ConstantInt>(A->getArraySize())) {
			Sz = ConstantInt::get(X.I64, C->getZExtValue() * elt.getFixedValue());
		} else {
			IRBuilder<> B(A->getNextNode());
			Value *cnt = B.CreateZExtOrTrunc(A->getArraySize(), X.I64);
			Sz = B.CreateMul(cnt, B.getInt64(elt.getFixedValue()));
		}
		X.allocaSize[A] = Sz;
		return Sz;
	}
	if (auto *G = dyn_cast<GlobalVariable>(Obj)) {
		Type *T = G->getValueType();
		if (!T->isSized())
			return nullptr;
		uint64_t s = X.DL.getTypeAllocSize(T).getFixedValue();
		return s ? ConstantInt::get(X.I64, s) : nullptr;
	}
	return nullptr;
}

// N is an i64 byte count
static bool emitCheck(Ctx &X, IRBuilder<> &B, Value *P, Value *N)
{
	if (auto *C = dyn_cast<ConstantInt>(N))
		if (staysInObject(P, C->getZExtValue(), X.DL))
			return false;

	Value *Base = getUnderlyingObject(P);
	if (Value *Size = objectSize(Base, X))
		B.CreateCall(getKnownFn(X.M), {Base, P, N, Size});
	else
		B.CreateCall(getCheckFn(X.M), {Base, P, N});
	return true;
}

static bool libcCall(Ctx &X, CallBase *CB)
{
	Function *F = CB->getCalledFunction();
	if (!F || F->isIntrinsic())
		return false;

	StringRef Name = F->getName();
	IRBuilder<> B(CB);
	auto len = [&](unsigned i) {
		return B.CreateZExtOrTrunc(CB->getArgOperand(i), X.I64);
	};

	if ((Name == "memcpy" || Name == "memmove") && CB->arg_size() == 3) {
		Value *N = len(2);
		bool a = emitCheck(X, B, CB->getArgOperand(0), N);
		bool b = emitCheck(X, B, CB->getArgOperand(1), N);
		return a || b;
	}
	if ((Name == "memset" || Name == "strncpy") && CB->arg_size() == 3)
		return emitCheck(X, B, CB->getArgOperand(0), len(2));

	if ((Name == "strcpy" || Name == "strcat") && CB->arg_size() == 2) {
		Value *D = CB->getArgOperand(0);
		FunctionCallee Fn = Name == "strcpy" ? getStrcpyFn(X.M) : getStrcatFn(X.M);
		Value *Base = getUnderlyingObject(D);
		Value *Size = objectSize(Base, X);
		B.CreateCall(Fn, {Base, D, CB->getArgOperand(1),
				  Size ? Size : B.getInt64(0)});
		return true;
	}
	return false;
}

PreservedAnalyses jacc::InstrumentPass::run(Function &F, FunctionAnalysisManager &)
{
	if (F.isDeclaration() || isRuntimeName(F.getName()))
		return PreservedAnalyses::all();

	Ctx X(*F.getParent());
	SmallVector<Instruction *, 64> work;

	for (BasicBlock &BB : F)
		for (Instruction &I : BB)
			if (isa<LoadInst>(I) || isa<StoreInst>(I) || isa<MemIntrinsic>(I) ||
			    isa<CallInst>(I))
				work.push_back(&I);

	bool changed = false;
	for (Instruction *I : work) {
		IRBuilder<> B(I);

		if (auto *L = dyn_cast<LoadInst>(I)) {
			uint64_t n = X.DL.getTypeStoreSize(L->getType()).getFixedValue();
			changed |= emitCheck(X, B, L->getPointerOperand(), B.getInt64(n));
		} else if (auto *S = dyn_cast<StoreInst>(I)) {
			Type *Ty = S->getValueOperand()->getType();
			uint64_t n = X.DL.getTypeStoreSize(Ty).getFixedValue();
			changed |= emitCheck(X, B, S->getPointerOperand(), B.getInt64(n));
		} else if (auto *MI = dyn_cast<MemIntrinsic>(I)) {
			Value *N = B.CreateZExtOrTrunc(MI->getLength(), X.I64);
			changed |= emitCheck(X, B, MI->getRawDest(), N);
			if (auto *T = dyn_cast<MemTransferInst>(MI))
				changed |= emitCheck(X, B, T->getRawSource(), N);
		} else {
			changed |= libcCall(X, cast<CallBase>(I));
		}
	}
	return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
