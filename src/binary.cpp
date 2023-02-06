#include <go/binary.h>

constexpr auto MAX_VAR_INT_LENGTH = 10;

std::optional<std::pair<int64_t, int>> go::binary::varInt(const std::byte *buffer) {
    std::optional<std::pair<uint64_t, int>> result = uVarInt(buffer);

    if (!result)
        return std::nullopt;

    if (result->first & 1)
        return std::pair<int64_t, int>{~(result->first >> 1), result->second};

    return std::pair<int64_t, int>{result->first >> 1, result->second};
}

std::optional<std::pair<uint64_t, int>> go::binary::uVarInt(const std::byte *buffer) {
    uint64_t v = 0;
    uint32_t shift = 0;

    for (int i = 0; i < MAX_VAR_INT_LENGTH; i++) {
        auto b = (uint64_t) buffer[i];

        if (b < 0x80) {
            if (i == MAX_VAR_INT_LENGTH - 1 && b > 1)
                return std::nullopt;

            return std::pair<uint64_t, int>{v | b << shift, i + 1};
        }

        v |= (b & 0x7f) << shift;
        shift += 7;
    }

    return std::nullopt;
}