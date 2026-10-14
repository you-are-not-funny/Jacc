// SPDX-License-Identifier: GPL-2.0-only
#ifndef JACC_COMMON_H
#define JACC_COMMON_H

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/ModRef.h"

namespace jacc {

// void __jacc_check(ptr base, ptr p, i64 n)
// void __jacc_check_span(ptr base, ptr lo, ptr hi, i64 n)
//
// Both only touch runtime-private state, so LICM, GVN and friends may
// move ordinary loads and stores across them. They can trap, so they
// are not willreturn and DCE leaves them alone.
inline llvm::FunctionCallee getRuntimeFn(llvm::Module &M, llvm::StringRef Name,
					 unsigned NPtr)
{
	llvm::LLVMContext &C = M.getContext();
	llvm::Type *Ptr = llvm::PointerType::get(C, 0);
	llvm::SmallVector<llvm::Type *, 4> Args(NPtr, Ptr);
	Args.push_back(llvm::Type::getInt64Ty(C));

	auto FC = M.getOrInsertFunction(
		Name, llvm::FunctionType::get(llvm::Type::getVoidTy(C), Args, false));
	if (auto *F = llvm::dyn_cast<llvm::Function>(FC.getCallee())) {
		F->setMemoryEffects(llvm::MemoryEffects::inaccessibleMemOnly());
		F->setDoesNotThrow();
	}
	return FC;
}

inline llvm::FunctionCallee getCheckFn(llvm::Module &M)
{
	return getRuntimeFn(M, "__jacc_check", 2);
}

inline llvm::FunctionCallee getSpanFn(llvm::Module &M)
{
	return getRuntimeFn(M, "__jacc_check_span", 3);
}

inline bool isRuntimeName(llvm::StringRef N)
{
	return N.starts_with("__jacc_") || N.starts_with("jalloc_") ||
	       N == "jacc_trap";
}

struct InstrumentPass : llvm::PassInfoMixin<InstrumentPass> {
	llvm::PreservedAnalyses run(llvm::Function &F,
				    llvm::FunctionAnalysisManager &AM);
};

struct LoopWidenPass : llvm::PassInfoMixin<LoopWidenPass> {
	llvm::PreservedAnalyses run(llvm::Function &F,
				    llvm::FunctionAnalysisManager &AM);
};

} // namespace jacc

#endif
