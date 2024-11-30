#include <llvm/IR/PassManager.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Passes/PassPlugin.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/MemoryBufferRef.h>
#include <llvm/Object/Archive.h>
#include <llvm/IR/GlobalValue.h>
#include <clang/Frontend/FrontendPluginRegistry.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>

using namespace llvm;

const char* static_libs[] = {
    "advapi32",
    "cabinet",
    "crypt32",
    "gdi32",
    "gdiplus",
    "kernel32",
    "msvcrt",
    "msvcp60",
    "mswsock",
    "ntdll",
    "ole32",
    "oleaut32",
    "rpcrt4",
    "secur32",
    "shell32",
    "shlwapi",
    "user32",
    "winhttp",
    "wininet",
    "ws2_32",
    nullptr,
};

struct BofPassOptions
{
    cl::opt<std::string> lib_path{"bl", cl::desc("static library path to load symbols"), cl::value_desc("library path"), cl::init("c:/msys64/clang64/lib"), cl::Optional};
    cl::opt<bool> enable_rename{"bren", cl::desc("enable external functions renaming for BOF"), cl::value_desc("bool"), cl::Optional};
    cl::opt<bool> enable_verbose{"bverbose", cl::desc("enable verbose mode"), cl::value_desc("bool"), cl::Optional};
};

extern BofPassOptions bof_options;

class Library
{
public:
    bool find(StringRef func)
    {
        auto E = archive_.findSym(func);
        if (!E) {
            errs() << "failed to find symbol: " << toString(E.takeError()) << '\n';
            return false;
        }
        return E->has_value();
    }

    static std::unique_ptr<Library> load(std::string const& lib_name)
    {
        std::string path = bof_options.lib_path + "/lib" + lib_name + ".a";

        auto errOrBuf = MemoryBuffer::getFile(path);
        if (!errOrBuf) {
            errs() << "failed to open " << path << ": " << errOrBuf.getError().message() << '\n';
            return nullptr;
        }

        auto err = Error::success();
        auto buf = std::move(errOrBuf.get());

        auto library = new Library(std::move(buf), err);
        if (err) {
            errs() << "failed to load " << path << ": " << err << '\n';
            return nullptr;
        }

        if (bof_options.enable_verbose)
            errs() << "symbols loaded from " << path << '\n';
        return std::unique_ptr<Library>(library);
    }

private:
    Library(std::unique_ptr<MemoryBuffer> buffer, Error& err)
        : buffer_(std::move(buffer)),
        archive_(buffer_->getMemBufferRef(), err)
    {}

    std::unique_ptr<MemoryBuffer> buffer_;
    object::Archive archive_;
};

class ArchiveLoader
{
public:
    void init()
    {
        if (loaded_) {
            return;
        }

        if (bof_options.enable_verbose) {
            errs() << "[BOF] INFO - static library path: " << bof_options.lib_path << '\n';
        }

        for (int i = 0; static_libs[i]; ++i) {
            archive_loader.load_archive(static_libs[i]);
        }

        loaded_ = true;
    }

    bool load_archive(std::string const& name)
    {
        auto entry = archives_.find(name);
        if (entry != archives_.cend()) {
            return true;
        }
        auto lib = Library::load(name);
        if (lib) {
            archives_[name] = std::move(lib);
            return true;
        }
        return false;
    }

    std::string const& find_library(StringRef func_name) {
        for (auto& entry : archives_) {
            if (entry.second->find(func_name)) {
                return entry.first;
            }
        }
        return not_found_;
    }

    static ArchiveLoader& get() { return archive_loader; }

private:
    std::string not_found_{};
    std::map<std::string, std::unique_ptr<Library>> archives_{};
    bool loaded_{false};

    static ArchiveLoader archive_loader;
};

ArchiveLoader ArchiveLoader::archive_loader;

class BofRenameFuncPass : public PassInfoMixin<BofRenameFuncPass>
{
public:
    BofRenameFuncPass()
    {
        ArchiveLoader::get().init();
    }

    PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM)
    {
        for (auto& bb : F) {
            for (auto& i : bb) {
                if (i.getOpcode() == Instruction::Call) {
                    rename_function(cast<CallInst>(i));
                }
            }
        }
        return PreservedAnalyses::all();
    }

    void rename_function(CallInst& inst)
    {
        auto CF = inst.getCalledFunction();
        if (!CF) {
            return;
        }

        std::string new_func_name;
        std::string cf_name(CF->getName());

        if (cf_name.find("llvm.", 0, 5) == 0) {
            new_func_name = rename_llvm_function(cf_name);
            if (new_func_name.empty()) {
                if (bof_options.enable_verbose)
                    errs() << "[BOF] WARN - failed to resolve llvm function: " << cf_name << '\n';
                return;
            }
        }
        else {
            // Unmangle function name forcely
            if (cf_name[0] == 0x01 && cf_name[1] == '_') {
                auto n = cf_name.find('@');
                if (n > 3) {
                    cf_name = cf_name.substr(2, n - 2);
                }
            }
            auto libname = ArchiveLoader::get().find_library(cf_name);
            if (libname.empty()) {
                return;
            }
            new_func_name = formatv("{0}${1}", libname, cf_name);
        }

        if (bof_options.enable_verbose)
            errs() << "[BOF] INFO - renamed " << cf_name << " to " << new_func_name << '\n';
        set_called_function(inst, new_func_name);
    }

    static std::string rename_llvm_function(std::string const& name)
    {
        static std::array<std::string, 3> reserved_names = {
            "memcpy",
            "memset",
            "memmove"
        };

        for (auto const& rn : reserved_names) {
            if (name.find(rn, 5) == 5) {
                return "msvcrt$" + rn;
            }
        }

        return {};
    }

    static void set_called_function(CallInst& inst, std::string& name)
    {
        auto CF = inst.getCalledFunction();
        auto func = inst.getFunction()
            ->getParent()
            ->getOrInsertFunction(name, CF->getFunctionType());
        // Set stdcall or others...
        auto* callee = func.getCallee();
        dyn_cast<Function>(callee)->setCallingConv(CF->getCallingConv());
        dyn_cast<GlobalValue>(callee)->setDLLStorageClass(GlobalValue::DLLStorageClassTypes::DLLImportStorageClass);
        inst.setCalledFunction(func);
    }

    static bool isRequired() { return true; }
};
