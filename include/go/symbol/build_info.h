#ifndef GO_SYMBOL_BUILD_INFO_H
#define GO_SYMBOL_BUILD_INFO_H

#include <elf/reader.h>
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

    struct Version {
        int major;
        int minor;

        bool operator==(const Version &rhs) const;
        bool operator!=(const Version &rhs) const;
        bool operator<(const Version &rhs) const;
        bool operator>(const Version &rhs) const;
        bool operator<=(const Version &rhs) const;
        bool operator>=(const Version &rhs) const;
    };

    std::optional<Version> parseVersion(std::string_view str);

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
        bool mBigEndian;
        bool mPointerFree;

    private:
        elf::Reader mReader;
        std::shared_ptr<elf::ISection> mSection;
    };
}

#endif //GO_SYMBOL_BUILD_INFO_H
