// SPDX-License-Identifier: GPL-2.0-only
#ifndef JACC_COMMON_H
#define JACC_COMMON_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/ModRef.h"

namespace jacc {

// has to match runtime/lowfat.h
constexpr unsigned LF_SHIFT = 35;
constexpr unsigned LF_NREG = 144;

// Runtime entry points, all void. Pointer arguments first, then i64s.
//
//   __jacc_check(base, p, n)                    heap, size from address
//   __jacc_check_known(base, p, n, size)        stack/global, size known
//   __jacc_check_span[_known](base, lo, hi, n[, size])   loop versions
//   __jacc_check_strcpy(base, dst, src, size)   size 0: ask the allocator
//   __jacc_check_strcat(base, dst, src, size)
//
// They only touch runtime-private state, so LICM, GVN and friends can
// move ordinary loads and stores across them. They can trap, so they
// are not willreturn and DCE leaves them alone. The pointers don't
// escape, which keeps SROA and alias analysis from giving up on the
// objects being checked.
inline llvm::FunctionCallee getRuntimeFn(llvm::Module &M, llvm::StringRef Name,
					 unsigned NPtr, unsigned NInt)
{
	llvm::LLVMContext &C = M.getContext();
	llvm::Type *Ptr = llvm::PointerType::get(C, 0);
	llvm::SmallVector<llvm::Type *, 6> Args(NPtr, Ptr);
	Args.append(NInt, llvm::Type::getInt64Ty(C));

	auto FC = M.getOrInsertFunction(
		Name, llvm::FunctionType::get(llvm::Type::getVoidTy(C), Args, false));
	if (auto *F = llvm::dyn_cast<llvm::Function>(FC.getCallee())) {
		F->setMemoryEffects(llvm::MemoryEffects::inaccessibleMemOnly());
		F->setDoesNotThrow();
		for (unsigned i = 0; i < NPtr; i++)
			F->addParamAttr(i, llvm::Attribute::NoCapture);
	}
	return FC;
}

inline llvm::FunctionCallee getCheckFn(llvm::Module &M)
{
	return getRuntimeFn(M, "__jacc_check", 2, 1);
}

inline llvm::FunctionCallee getKnownFn(llvm::Module &M)
{
	return getRuntimeFn(M, "__jacc_check_known", 2, 2);
}

inline llvm::FunctionCallee getSpanFn(llvm::Module &M)
{
	return getRuntimeFn(M, "__jacc_check_span", 3, 1);
}

inline llvm::FunctionCallee getSpanKnownFn(llvm::Module &M)
{
	return getRuntimeFn(M, "__jacc_check_span_known", 3, 2);
}

inline llvm::FunctionCallee getStrcpyFn(llvm::Module &M)
{
	return getRuntimeFn(M, "__jacc_check_strcpy", 3, 1);
}

inline llvm::FunctionCallee getStrcatFn(llvm::Module &M)
{
	return getRuntimeFn(M, "__jacc_check_strcat", 3, 1);
}

// cold, never returns; the inlined checks branch here
inline llvm::FunctionCallee getFailFn(llvm::Module &M)
{
	llvm::LLVMContext &C = M.getContext();
	auto FC = M.getOrInsertFunction(
		"__jacc_fail",
		llvm::FunctionType::get(llvm::Type::getVoidTy(C), false));
	if (auto *F = llvm::dyn_cast<llvm::Function>(FC.getCallee())) {
		F->setDoesNotReturn();
		F->setDoesNotThrow();
		F->addFnAttr(llvm::Attribute::Cold);
	}
	return FC;
}

inline bool isRuntimeName(llvm::StringRef N)
{
	return N.starts_with("__jacc_") || N.starts_with("jalloc_") ||
	       N == "jacc_trap";
}

struct InstrumentPass : llvm::PassInfoMixin<InstrumentPass> {
	// clang marks every function optnone at -O0; run anyway
	static bool isRequired() { return true; }
	llvm::PreservedAnalyses run(llvm::Function &F,
				    llvm::FunctionAnalysisManager &AM);
};

struct LoopWidenPass : llvm::PassInfoMixin<LoopWidenPass> {
	llvm::PreservedAnalyses run(llvm::Function &F,
				    llvm::FunctionAnalysisManager &AM);
};

struct LowerChecksPass : llvm::PassInfoMixin<LowerChecksPass> {
	// clang marks every function optnone at -O0; run anyway
	static bool isRequired() { return true; }
	llvm::PreservedAnalyses run(llvm::Function &F,
				    llvm::FunctionAnalysisManager &AM);
};

} // namespace jacc

#endif
