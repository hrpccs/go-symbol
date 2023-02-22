#include <go/symbol/reader.h>
#include <elf/symbol.h>
#include <zero/log.h>
#include <algorithm>
#include <fcntl.h>

#ifndef PAGE_SIZE
#define PAGE_SIZE 0x1000
#endif

constexpr auto SYMBOL_SECTION = "gopclntab";
constexpr auto BUILD_INFO_SECTION = "buildinfo";
constexpr auto INTERFACE_SECTION = "itablink";

constexpr auto BUILD_INFO_MAGIC = "\xff Go buildinf:";
constexpr auto BUILD_INFO_MAGIC_SIZE = 14;

constexpr auto TYPES_SYMBOL = "runtime.types";
constexpr auto VERSION_SYMBOL = "runtime.buildVersion";

constexpr auto SYMBOL_MAGIC_12 = 0xfffffffb;
constexpr auto SYMBOL_MAGIC_116 = 0xfffffffa;
constexpr auto SYMBOL_MAGIC_118 = 0xfffffff0;
constexpr auto SYMBOL_MAGIC_120 = 0xfffffff1;

go::symbol::Reader::Reader(elf::Reader reader, std::filesystem::path path)
        : mReader(std::move(reader)), mPath(std::move(path)) {

}

size_t go::symbol::Reader::ptrSize() {
    return mReader.header()->ident()[EI_CLASS] == ELFCLASS64 ? 8 : 4;
}

elf::endian::Type go::symbol::Reader::endian() {
    return mReader.header()->ident()[EI_DATA] == ELFDATA2MSB ? elf::endian::Big : elf::endian::Little;
}

std::optional<go::Version> go::symbol::Reader::version() {
    std::optional<go::symbol::BuildInfo> buildInfo = this->buildInfo();

    if (buildInfo)
        return buildInfo->version();

    std::vector<std::shared_ptr<elf::ISection>> sections = mReader.sections();

    auto it = std::find_if(sections.begin(), sections.end(), [](const auto &section) {
        return section->type() == SHT_SYMTAB;
    });

    if (it == sections.end())
        return std::nullopt;

    elf::SymbolTable symbolTable(mReader, *it);

    auto symbolIterator = std::find_if(symbolTable.begin(), symbolTable.end(), [](const auto &symbol) {
        return symbol->name() == VERSION_SYMBOL;
    });

    if (symbolIterator == symbolTable.end())
        return std::nullopt;

    std::unique_ptr<elf::IHeader> header = mReader.header();

    size_t ptrSize = this->ptrSize();
    endian::Converter converter(endian());

    std::optional<std::vector<std::byte>> buffer = mReader.readVirtualMemory(
            symbolIterator.operator*()->value(),
            ptrSize * 2
    );

    if (!buffer)
        return std::nullopt;

    buffer = mReader.readVirtualMemory(
            converter(buffer->data(), ptrSize),
            converter(buffer->data() + ptrSize, ptrSize)
    );

    if (!buffer)
        return std::nullopt;

    return parseVersion({(char *) buffer->data(), buffer->size()});
}

std::optional<go::symbol::BuildInfo> go::symbol::Reader::buildInfo() {
    std::vector<std::shared_ptr<elf::ISection>> sections = mReader.sections();

    auto it = std::find_if(
            sections.begin(),
            sections.end(),
            [](const auto &section) {
                return section->name().find(BUILD_INFO_SECTION) != std::string::npos;
            }
    );

    if (it == sections.end()) {
        LOG_ERROR("build info section not found");
        return std::nullopt;
    }

    if (memcmp(it->operator*().data(), BUILD_INFO_MAGIC, BUILD_INFO_MAGIC_SIZE) != 0) {
        LOG_ERROR("invalid build info magic");
        return std::nullopt;
    }

    return BuildInfo(mReader, *it);
}

std::optional<go::symbol::SymbolTable> go::symbol::Reader::symbols(uint64_t base) {
    std::vector<std::shared_ptr<elf::ISection>> sections = mReader.sections();

    auto it = std::find_if(
            sections.begin(),
            sections.end(),
            [](const auto &section) {
                return section->name().find(SYMBOL_SECTION) != std::string::npos;
            }
    );

    if (it == sections.end()) {
        LOG_ERROR("symbol section not found");
        return std::nullopt;
    }

    const std::byte *data = it->operator*().data();

    elf::endian::Type endian = this->endian();
    endian::Converter converter(endian);

    uint32_t magic = converter(*(uint32_t *) data);

    SymbolVersion version;

    switch (magic) {
        case SYMBOL_MAGIC_12:
            version = VERSION12;
            break;

        case SYMBOL_MAGIC_116:
            version = VERSION116;
            break;

        case SYMBOL_MAGIC_118:
            version = VERSION118;
            break;

        case SYMBOL_MAGIC_120:
            version = VERSION120;
            break;

        default:
            return std::nullopt;
    }

    bool dynamic = mReader.header()->type() == ET_DYN;

    std::vector<std::shared_ptr<elf::ISegment>> loads;
    std::vector<std::shared_ptr<elf::ISegment>> segments = mReader.segments();

    std::copy_if(
            segments.begin(),
            segments.end(),
            std::back_inserter(loads),
            [](const auto &segment) {
                return segment->type() == PT_LOAD;
            }
    );

    Elf64_Addr minVA = std::min_element(
            loads.begin(),
            loads.end(),
            [](const auto &i, const auto &j) {
                return i->virtualAddress() < j->virtualAddress();
            }
    )->operator*().virtualAddress() & ~(PAGE_SIZE - 1);

    int fd = open(mPath.string().c_str(), O_RDONLY);

    if (fd < 0) {
        LOG_ERROR("open %s failed: %s", mPath.string().c_str(), strerror(errno));
        return std::nullopt;
    }

    return SymbolTable(
            version,
            converter,
            fd,
            (off64_t) it.operator*()->offset(),
            it->operator*().address(),
            dynamic ? base - minVA : 0
    );
}

std::optional<go::symbol::InterfaceTable> go::symbol::Reader::interfaces(uint64_t base) {
    std::optional<Version> version = this->version();

    if (!version)
        return std::nullopt;

    if (version < Version{1, 7}) {
        LOG_ERROR("golang %d.%d is not supported", version->major, version->minor);
        return std::nullopt;
    }

    std::vector<std::shared_ptr<elf::ISection>> sections = mReader.sections();

    auto it = std::find_if(
            sections.begin(),
            sections.end(),
            [](const auto &section) {
                return section->name().find(INTERFACE_SECTION) != std::string::npos;
            }
    );

    if (it == sections.end()) {
        LOG_ERROR("interface section not found");
        return std::nullopt;
    }

    std::shared_ptr<elf::ISection> section = *it;

    it = std::find_if(sections.begin(), sections.end(), [](const auto &section) {
        return section->type() == SHT_SYMTAB;
    });

    if (it == sections.end())
        return std::nullopt;

    elf::SymbolTable symbolTable(mReader, *it);

    auto symbolIterator = std::find_if(symbolTable.begin(), symbolTable.end(), [](const auto &symbol) {
        return symbol->name() == TYPES_SYMBOL;
    });

    if (symbolIterator == symbolTable.end()) {
        LOG_ERROR("runtime.types not found");
        return std::nullopt;
    }

    bool dynamic = mReader.header()->type() == ET_DYN;

    std::vector<std::shared_ptr<elf::ISegment>> loads;
    std::vector<std::shared_ptr<elf::ISegment>> segments = mReader.segments();

    std::copy_if(
            segments.begin(),
            segments.end(),
            std::back_inserter(loads),
            [](const auto &segment) {
                return segment->type() == PT_LOAD;
            }
    );

    Elf64_Addr minVA = std::min_element(
            loads.begin(),
            loads.end(),
            [](const auto &i, const auto &j) {
                return i->virtualAddress() < j->virtualAddress();
            }
    )->operator*().virtualAddress() & ~(PAGE_SIZE - 1);

    return InterfaceTable(
            mReader,
            section,
            *version,
            symbolIterator.operator*()->value(),
            dynamic ? base - minVA : 0,
            ptrSize(),
            endian::Converter(endian())
    );
}

std::optional<go::symbol::Reader> go::symbol::openFile(const std::filesystem::path &path) {
    std::optional<elf::Reader> reader = elf::openFile(path);

    if (!reader) {
        LOG_ERROR("open elf file failed");
        return std::nullopt;
    }

    return Reader(*reader, path);
}