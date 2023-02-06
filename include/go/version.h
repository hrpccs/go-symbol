#ifndef GO_SYMBOL_VERSION_H
#define GO_SYMBOL_VERSION_H

#include <optional>
#include <string_view>

namespace go {
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
}

#endif //GO_SYMBOL_VERSION_H
