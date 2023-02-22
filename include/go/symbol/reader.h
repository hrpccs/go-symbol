#ifndef GO_SYMBOL_READER_H
#define GO_SYMBOL_READER_H

#include "symbol.h"
#include "interface.h"
#include "build_info.h"

namespace go::symbol {
    enum AccessMethod {
        FileMapping,
        AnonymousMemory,
        Attached
    };

    class Reader {
    public:
        explicit Reader(elf::Reader reader);

    private:
        size_t ptrSize();
        elf::endian::Type endian();

    public:
        std::optional<Version> version();

    public:
        std::optional<BuildInfo> buildInfo();
        std::optional<SymbolTable> symbols(AccessMethod method, uint64_t base = 0);
        std::optional<InterfaceTable> interfaces(uint64_t base = 0);

    private:
        elf::Reader mReader;
    };

    std::optional<Reader> openFile(const std::filesystem::path &path);
}

#endif //GO_SYMBOL_READER_H
