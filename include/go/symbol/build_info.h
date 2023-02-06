#ifndef GO_SYMBOL_BUILD_INFO_H
#define GO_SYMBOL_BUILD_INFO_H

#include <list>
#include <elf/reader.h>
#include <go/version.h>

namespace go::symbol {
    struct Module {
        std::string path;
        std::string version;
        std::string sum;
        std::unique_ptr<Module> replace;
    };

    struct ModuleInfo {
        std::string path;
        Module main;
        std::list<Module> deps;
    };

    class BuildInfo {
    public:
        BuildInfo(elf::Reader reader, std::shared_ptr<elf::ISection> section);

    public:
        std::optional<Version> version();
        std::optional<ModuleInfo> moduleInfo();

    private:
        std::optional<std::string> readString(const std::byte *data);

    private:
        size_t mPtrSize;
        bool mPointerFree;
        elf::endian::Type mEndian;

    private:
        elf::Reader mReader;
        std::shared_ptr<elf::ISection> mSection;
    };
}

#endif //GO_SYMBOL_BUILD_INFO_H
