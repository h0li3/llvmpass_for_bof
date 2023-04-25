#include <llvm/IR/PassManager.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Passes/PassPlugin.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/CommandLine.h>

#include "pass_bof_rename_func.h"

using namespace llvm;

static cl::opt<bool> enable_rename("bren", cl::desc("enable external functions renaming for BOF"), cl::value_desc("bool"), cl::Optional);

PassPluginLibraryInfo getPluginInfo() {
    return {
        LLVM_PLUGIN_API_VERSION, "cannopass", "0.0.1",
        [](PassBuilder &PB) {
            PB.registerVectorizerStartEPCallback(
                [](FunctionPassManager &FPM, OptimizationLevel level) {
                    if (enable_rename) {
                        FPM.addPass(BofRenameFuncPass());
		    }
            });
        }
    };
}

extern "C" ::PassPluginLibraryInfo
LLVM_ATTRIBUTE_WEAK llvmGetPassPluginInfo() {
    return getPluginInfo();
}
