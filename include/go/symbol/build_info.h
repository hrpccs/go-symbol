#ifndef GO_SYMBOL_BUILD_INFO_H
#define GO_SYMBOL_BUILD_INFO_H

#include <elf/reader.h>
#include <optional>
#include <list>

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
        std::optional<std::string> version();
        std::optional<std::tuple<int, int>> versionNumber();
        std::optional<ModuleInfo> moduleInfo();

    private:
        std::optional<std::string> readString(const std::byte *data);
        std::optional<std::vector<std::byte>> peek(uint64_t address, uint64_t length);

    private:
        size_t mPtrSize;
        bool mBigEndian;
        bool mPointerFree;

    private:
        elf::Reader mReader;
        std::shared_ptr<elf::ISection> mSection;
    };
}

#endif //GO_SYMBOL_BUILD_INFO_H
