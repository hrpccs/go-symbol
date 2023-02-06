#include <go/symbol/build_info.h>
#include <go/binary.h>
#include <go/endian.h>
#include <zero/log.h>
#include <algorithm>
#include <cstddef>

constexpr auto MAGIC_SIZE = 14;
constexpr auto INFO_OFFSET = 16;

constexpr auto POINTER_FREE_OFFSET = 32;
constexpr auto POINTER_FREE_FLAG = std::byte{0x2};

go::symbol::BuildInfo::BuildInfo(elf::Reader reader, std::shared_ptr<elf::ISection> section)
        : mReader(std::move(reader)), mSection(std::move(section)) {
    mPtrSize = std::to_integer<size_t>(mSection->data()[MAGIC_SIZE]);
    mEndian = std::to_integer<bool>(mSection->data()[MAGIC_SIZE + 1]) ? elf::endian::Big : elf::endian::Little;
    mPointerFree = std::to_integer<bool>(mSection->data()[MAGIC_SIZE + 1] & POINTER_FREE_FLAG);
}

std::optional<go::Version> go::symbol::BuildInfo::version() {
    const std::byte *buffer = mSection->data();

    if (!mPointerFree) {
        std::optional<std::string> str = readString(buffer + INFO_OFFSET);

        if (!str)
            return std::nullopt;

        return parseVersion(*str);
    }

    std::optional<std::pair<uint64_t, int>> result = binary::uVarInt(buffer + POINTER_FREE_OFFSET);

    if (!result)
        return std::nullopt;

    return parseVersion({(char *) buffer + POINTER_FREE_OFFSET + result->second, result->first});
}

std::optional<go::symbol::ModuleInfo> go::symbol::BuildInfo::moduleInfo() {
    const std::byte *buffer = mSection->data();
    std::string modInfo;

    if (!mPointerFree) {
        std::optional<std::string> str = readString(buffer + INFO_OFFSET + mPtrSize);

        if (!str)
            return std::nullopt;

        modInfo = std::move(*str);
    } else {
        std::optional<std::pair<uint64_t, int>> result = binary::uVarInt(buffer + POINTER_FREE_OFFSET);

        if (!result)
            return std::nullopt;

        const std::byte *ptr = buffer + POINTER_FREE_OFFSET + result->first + result->second;

        result = binary::uVarInt(ptr);

        if (!result)
            return std::nullopt;

        modInfo = {(char *) ptr + result->second, result->first};
    }

    if (modInfo.length() < 32) {
        LOG_ERROR("invalid module info");
        return std::nullopt;
    }

    auto readEntry = [](const std::string &module) -> std::optional<Module> {
        std::vector<std::string> tokens = zero::strings::split(module, "\t");

        if (tokens.size() != 4)
            return std::nullopt;

        return Module{tokens[1], tokens[2], tokens[3]};
    };

    ModuleInfo moduleInfo;

    for (const auto &m: zero::strings::split({modInfo.data() + 16, modInfo.length() - 32}, "\n")) {
        if (zero::strings::startsWith(m, "path")) {
            std::vector<std::string> tokens = zero::strings::split(m, "\t");

            if (tokens.size() != 2)
                continue;

            moduleInfo.path = tokens[1];
        } else if (zero::strings::startsWith(m, "mod")) {
            std::optional<Module> module = readEntry(m);

            if (!module)
                continue;

            moduleInfo.main = std::move(*module);
        } else if (zero::strings::startsWith(m, "dep")) {
            std::optional<Module> module = readEntry(m);

            if (!module)
                continue;

            moduleInfo.deps.push_back(std::move(*module));
        } else if (zero::strings::startsWith(m, "=>")) {
            std::optional<Module> module = readEntry(m);

            if (!module)
                continue;

            moduleInfo.deps.back().replace = std::make_unique<Module>(std::move(*module));
        }
    }

    return moduleInfo;
}

std::optional<std::string> go::symbol::BuildInfo::readString(const std::byte *data) {
    endian::Converter converter(mEndian);
    std::optional<std::vector<std::byte>> buffer = mReader.readVirtualMemory(converter(data, mPtrSize), mPtrSize * 2);

    if (!buffer)
        return std::nullopt;

    buffer = mReader.readVirtualMemory(
            converter(buffer->data(), mPtrSize),
            converter(buffer->data() + mPtrSize, mPtrSize)
    );

    if (!buffer)
        return std::nullopt;

    return std::string{(char *) buffer->data(), buffer->size()};
}