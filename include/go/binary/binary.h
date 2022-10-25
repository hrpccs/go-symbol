#ifndef GO_SYMBOL_BINARY_H
#define GO_SYMBOL_BINARY_H

#include <cstdint>
#include <cstddef>
#include <optional>

namespace go::binary {
    std::optional<std::pair<int64_t, int>> varInt(const std::byte *buffer);
    std::optional<std::pair<uint64_t, int>> uVarInt(const std::byte *buffer);
}

#endif //GO_SYMBOL_BINARY_H
