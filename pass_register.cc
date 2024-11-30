#include <llvm/IR/PassManager.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Passes/PassPlugin.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/CommandLine.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Frontend/FrontendPluginRegistry.h>
#include <clang/Frontend/CompilerInstance.h>
#include "llvm/IR/LegacyPassManager.h"

#include "pass_bof_rename_func.h"

using namespace llvm;

BofPassOptions bof_options;

void registerPasses(FunctionPassManager &FPM, OptimizationLevel level)
{
    if (bof_options.enable_rename) {
        if (bof_options.enable_verbose) {
            errs() << "[BOF] INFO - pass BofRenameFuncPass enabled.\n";
        }
        FPM.addPass(BofRenameFuncPass());
    }
}

PassPluginLibraryInfo getPluginInfo() {
    return {
        LLVM_PLUGIN_API_VERSION, "cannopass", "0.1.1",
        [](PassBuilder &PB) {
            PB.registerVectorizerStartEPCallback(registerPasses);
        }
    };
}

extern "C" ::PassPluginLibraryInfo
LLVM_ATTRIBUTE_WEAK llvmGetPassPluginInfo() {
    return getPluginInfo();
}
