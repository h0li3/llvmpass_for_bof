#include <llvm/IR/PassManager.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Passes/PassPlugin.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/MemoryBufferRef.h>
#include <llvm/Object/Archive.h>
#include <llvm/IR/GlobalValue.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>

using namespace llvm;

static cl::opt<std::string> lib_path("bl", cl::desc("static library path to load symbols"), cl::value_desc("path"), cl::init("/clang64/lib"), cl::Optional);

class Library
{
public:
    bool find(StringRef func)
    {
        auto E = archive_.findSym(func);
        if (!E) {
            errs() << "failed to find symbol: " << toString(E.takeError()) << "\n";
            return false;
        }
        return E->has_value();
    }

    static std::unique_ptr<Library> load(std::string const& lib_name)
    {
        std::string path = lib_path + "/lib" + lib_name + ".a";

        auto errOrBuf = MemoryBuffer::getFile(path);
        if (!errOrBuf) {
            errs() << "failed to open " << path << ": " << errOrBuf.getError().message() << "\n";
            return nullptr;
        }

        auto err = Error::success();
        auto buf = std::move(errOrBuf.get());

        auto library = new Library(std::move(buf), err);
        if (err) {
            errs() << "failed to load " << path << ": " << err << "\n";
            return nullptr;
        }

        // errs() << "symbols loaded from " << path << "\n";
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
        archive_loader.load_archive("advapi32");
        archive_loader.load_archive("cabinet");
        archive_loader.load_archive("crypt32");
        archive_loader.load_archive("gdi32");
        archive_loader.load_archive("gdiplus");
        archive_loader.load_archive("kernel32");
        archive_loader.load_archive("msvcrt");
        archive_loader.load_archive("msvcp60");
        archive_loader.load_archive("mswsock");
        archive_loader.load_archive("ntdll");
        archive_loader.load_archive("ole32");
        archive_loader.load_archive("rpcrt4");
        archive_loader.load_archive("secur32");
        archive_loader.load_archive("shell32");
        archive_loader.load_archive("user32");
        archive_loader.load_archive("winhttp");
        archive_loader.load_archive("wininet");
        archive_loader.load_archive("ws2_32");
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

        if (cf_name.find("llvm.memcpy", 0, 11) == 0) {
            // errs() << "------- " << cf_name << "\n";
        new_func_name = "msvcrt$memcpy";
        }
        else if (cf_name.find("llvm.memset", 0, 11) == 0) {
            // errs() << "------- " << cf_name << "\n";
        new_func_name = "msvcrt$memset";
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
        // errs() << "rename " << cf_name << " to " << new_func_name << "\n";
        set_called_function(inst, new_func_name);
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


/*
        if (!libs.empty()) {
            for (auto& lib : libs) {
                if (ArchiveLoader::get().load_archive(lib)) {
                    errs() << "static library loaded " << lib << "\n";
                }
            }
            libs.clear();
        }
        */
