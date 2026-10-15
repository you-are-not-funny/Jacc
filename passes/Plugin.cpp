// SPDX-License-Identifier: GPL-2.0-only
#include "JaccCommon.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Transforms/Utils/LoopSimplify.h"

using namespace llvm;

static void hook(PassBuilder &PB)
{
	PB.registerPipelineParsingCallback(
		[](StringRef Name, FunctionPassManager &FPM,
		   ArrayRef<PassBuilder::PipelineElement>) {
			if (Name == "jacc-instrument") {
				FPM.addPass(jacc::InstrumentPass());
				return true;
			}
			if (Name == "jacc-lower") {
				FPM.addPass(jacc::LowerChecksPass());
				return true;
			}
			if (Name == "jacc-widen") {
				FPM.addPass(jacc::LoopWidenPass());
				return true;
			}
			return false;
		});

	// instrument before anything else touches the IR...
	PB.registerPipelineStartEPCallback(
		[](ModulePassManager &MPM, OptimizationLevel) {
			MPM.addPass(createModuleToFunctionPassAdaptor(
				jacc::InstrumentPass()));
		});

	// ...and widen once loops are rotated and SCEV has something to say
	PB.registerScalarOptimizerLateEPCallback(
		[](FunctionPassManager &FPM, OptimizationLevel) {
			// the guard of a rotated loop usually leaves it
			// without a dedicated preheader; make one
			FPM.addPass(LoopSimplifyPass());
			FPM.addPass(jacc::LoopWidenPass());
		});

	// after the vectorizer: checks that are left become cmp + cold branch
	PB.registerOptimizerLastEPCallback(
		[](ModulePassManager &MPM, OptimizationLevel) {
			MPM.addPass(createModuleToFunctionPassAdaptor(
				jacc::LowerChecksPass()));
		});
}

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo()
{
	return {LLVM_PLUGIN_API_VERSION, "jacc", "0.1", hook};
}
