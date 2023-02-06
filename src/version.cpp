#include <go/version.h>
#include <zero/strings/strings.h>
#include <regex>

bool go::Version::operator==(const Version &rhs) const {
    return major == rhs.major && minor == rhs.minor;
}

bool go::Version::operator!=(const Version &rhs) const {
    return !operator==(rhs);
}

bool go::Version::operator<(const Version &rhs) const {
    return major < rhs.major || (major == rhs.major && minor < rhs.minor);
}

bool go::Version::operator>(const Version &rhs) const {
    return major > rhs.major || (major == rhs.major && minor > rhs.minor);
}

bool go::Version::operator<=(const Version &rhs) const {
    return !operator>(rhs);
}

bool go::Version::operator>=(const Version &rhs) const {
    return !operator<(rhs);
}

std::optional<go::Version> go::parseVersion(std::string_view str) {
    std::match_results<std::string_view::const_iterator> match;

    if (!std::regex_match(str.begin(), str.end(), match, std::regex(R"(^go(\d+)\.(\d+).*)")))
        return std::nullopt;

    std::optional<int> major = zero::strings::toNumber<int>(match.str(1));
    std::optional<int> minor = zero::strings::toNumber<int>(match.str(2));

    if (!major || !minor)
        return std::nullopt;

    return Version{*major, *minor};
}