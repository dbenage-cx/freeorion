#ifndef SEMVER_H
#define SEMVER_H


#include <boost/tokenizer.hpp>
#include <boost/optional.hpp>

#include <string>
#include <tuple>


#pragma push_macro("major")
#undef major
#pragma push_macro("minor")
#undef minor


struct SemVer {
    SemVer(int major_, int minor_, int patch_, std::string errata_) :
        major(major_),
        minor(minor_),
        patch(patch_),
        errata(errata_)
    {}

    bool operator<(const SemVer& rhs) const
    { return std::tie(major, minor, patch, errata) < std::tie(rhs.major, rhs.minor, rhs.patch, errata); }
    bool operator>(const SemVer& rhs) const
    { return rhs < *this; }
    bool operator<=(const SemVer& rhs) const
    { return !(*this > rhs); }
    bool operator>=(const SemVer& rhs) const
    { return !(*this < rhs); }
    bool operator==(const SemVer& rhs) const
    { return std::tie(major, minor, patch, errata) == std::tie(rhs.major, rhs.minor, rhs.patch, errata); }
    bool operator!=(const SemVer& rhs) const
    { return !(*this == rhs); }

    int                                                 major = 0;
    int                                                 minor = 0;
    int                                                 patch = 0;
    std::string                                         errata;
};

std::string SemVerToString(const SemVer& semver) {
    return std::to_string(semver.major) + "." +
           std::to_string(semver.minor) + "." +
           std::to_string(semver.patch) + semver.errata;
}

namespace {
    boost::optional<int> StrToInt(const std::string& str) {
        try {
            return std::stoi(str);
        } catch (std::invalid_argument) {
        } catch (std::out_of_range) {
        }
        return boost::none;
    }

    std::string NextToken(const std::string& source, const char& token, std::size_t& pos) {
        if (pos == std::string::npos)
            return "";

        auto prev_pos = pos;
        pos = source.find(prev_pos, token);
        if (pos == std::string::npos)
            return source.substr(prev_pos);

        return source.substr(prev_pos, pos);
    }
}

SemVer SemVerFromString(const std::string& str) {
    SemVer retval { 0, 0, 1, "" };
    std::size_t next_pos = 0;

    auto next_str = NextToken(str, '.', next_pos);
    auto next_val = StrToInt(next_str);
    if (next_val == boost::none) {
        retval.errata = next_str;
        return retval;
    }
    retval.major = next_val.value();

    next_str = NextToken(str, '.', next_pos);
    next_val = StrToInt(next_str);
    if (next_val == boost::none) {
        retval.errata = next_str;
        return retval;
    }
    retval.minor = next_val.value();

    next_str = NextToken(str, '.', next_pos);
    next_val = StrToInt(next_str);
    if (next_val == boost::none) {
        retval.errata = next_str;
        return retval;
    }
    retval.patch = next_val.value();
    retval.errata = str.substr(next_pos);

    return retval;
}

#pragma pop_macro("minor")
#pragma pop_macro("major")

#endif
