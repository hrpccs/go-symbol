#include <go/symbol/reader.h>
#include <elf/symbol.h>
#include <zero/log.h>
#include <algorithm>

#ifndef PAGE_SIZE
#define PAGE_SIZE 0x1000
#endif

constexpr auto SYMBOL_SECTION = "gopclntab";
constexpr auto BUILD_INFO_SECTION = "buildinfo";

constexpr auto BUILD_INFO_MAGIC = "\xff Go buildinf:";
constexpr auto BUILD_INFO_MAGIC_SIZE = 14;

constexpr auto VERSION_SYMBOL = "runtime.buildVersion";

constexpr auto SYMBOL_MAGIC_12 = 0xfffffffb;
constexpr auto SYMBOL_MAGIC_116 = 0xfffffffa;
constexpr auto SYMBOL_MAGIC_118 = 0xfffffff0;
constexpr auto SYMBOL_MAGIC_120 = 0xfffffff1;

std::optional<go::symbol::Version> go::symbol::Reader::version() {
    std::optional<go::symbol::BuildInfo> buildInfo = this->buildInfo();

    if (buildInfo)
        return buildInfo->version();

    std::vector<std::shared_ptr<elf::ISection>> sections = this->sections();

    auto it = std::find_if(sections.begin(), sections.end(), [](const auto &section) {
        return section->type() == SHT_SYMTAB;
    });

    if (it == sections.end())
        return std::nullopt;

    elf::SymbolTable symbolTable(*this, *it);

    auto symbolIterator = std::find_if(symbolTable.begin(), symbolTable.end(), [](const auto &symbol) {
        return symbol->name() == VERSION_SYMBOL;
    });

    if (symbolIterator == symbolTable.end())
        return std::nullopt;

    std::unique_ptr<elf::IHeader> header = this->header();

    size_t ptrSize = header->ident()[EI_CLASS] == ELFCLASS64 ? 8 : 4;
    bool bigEndian = header->ident()[EI_DATA] == ELFDATA2MSB;

    auto read = [=](const std::byte *ptr) -> uint64_t {
        if (ptrSize == 4)
            return bigEndian ? be32toh(*(uint32_t *) ptr) : le32toh(*(uint32_t *) ptr);

        return bigEndian ? be64toh(*(uint64_t *) ptr) : le64toh(*(uint64_t *) ptr);
    };

    std::optional<std::vector<std::byte>> buffer = readVirtualMemory(symbolIterator.operator*()->value(), ptrSize * 2);

    if (!buffer)
        return std::nullopt;

    buffer = readVirtualMemory(read(buffer->data()), read(buffer->data() + ptrSize));

    if (!buffer)
        return std::nullopt;

    return Version({(char *) buffer->data(), buffer->size()});
}

std::optional<go::symbol::BuildInfo> go::symbol::Reader::buildInfo() {
    std::vector<std::shared_ptr<elf::ISection>> sections = this->sections();

    auto it = std::find_if(
            sections.begin(),
            sections.end(),
            [](const auto &section) {
                return zero::strings::containsIC(section->name(), BUILD_INFO_SECTION);
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

    return BuildInfo(*this, *it);
}

std::optional<go::symbol::SymbolTable> go::symbol::Reader::symbols(AccessMethod method, uint64_t base) {
    std::vector<std::shared_ptr<elf::ISection>> sections = this->sections();

    auto it = std::find_if(
            sections.begin(),
            sections.end(),
            [](const auto &section) {
                return zero::strings::containsIC(section->name(), SYMBOL_SECTION);
            }
    );

    if (it == sections.end()) {
        LOG_ERROR("symbol section not found");
        return std::nullopt;
    }

    const std::byte *data = it->operator*().data();

    bool bigEndian = data[0] == std::byte{0xff};
    uint32_t magic = bigEndian ? be32toh(*(uint32_t *) data) : le32toh(*(uint32_t *) data);

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

    bool dynamic = header()->type() == ET_DYN;

    std::vector<std::shared_ptr<elf::ISegment>> loads;
    std::vector<std::shared_ptr<elf::ISegment>> segments = this->segments();

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

    if (method == FileMapping) {
        return SymbolTable(version, bigEndian, *it, dynamic ? base - minVA : 0);
    } else if (method == AnonymousMemory) {
        std::unique_ptr<std::byte[]> buffer = std::make_unique<std::byte[]>(it->operator*().size());
        memcpy(buffer.get(), it->operator*().data(), it->operator*().size());
        return SymbolTable(version, bigEndian, std::move(buffer), dynamic ? base - minVA : 0);
    }

    if (!dynamic)
        return SymbolTable(version, bigEndian, (const std::byte *) it->operator*().address(), 0);

    return SymbolTable(version, bigEndian, (const std::byte *) base + it->operator*().address() - minVA, 0);
}