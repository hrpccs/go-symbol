#ifndef GO_SYMBOL_READER_H
#define GO_SYMBOL_READER_H

#include "symbol.h"
#include "build_info.h"

namespace go::symbol {
    enum AccessMethod {
        FileMapping,
        AnonymousMemory,
        Attached
    };

    class Reader {
    public:
        bool load(const std::string &path);

    public:
        std::optional<BuildInfo> buildInfo();
        std::optional<SymbolTable> symbols(AccessMethod method, uint64_t base = 0);

    private:
        elf::Reader mReader;
    };
}

#endif //GO_SYMBOL_READER_H
