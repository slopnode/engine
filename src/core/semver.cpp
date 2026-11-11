#include "core/semver.hpp"

#include <cctype>

namespace slopengine {

std::optional<SemVer> parseSemVer(std::string_view text) {
    SemVer version;
    int* const fields[3] = {&version.major, &version.minor, &version.patch};

    std::size_t start = 0;
    for (int field = 0; field < 3; ++field) {
        const std::size_t dot = text.find('.', start);
        const std::string_view part = (dot == std::string_view::npos)
            ? text.substr(start)
            : text.substr(start, dot - start);

        if (part.empty()) {
            return field == 0 ? std::nullopt : std::make_optional(version);
        }

        int value = 0;
        for (const char c : part) {
            if (!std::isdigit(static_cast<unsigned char>(c))) {
                return std::nullopt;
            }
            value = value * 10 + (c - '0');
        }
        *fields[field] = value;

        if (dot == std::string_view::npos) {
            return version;
        }
        start = dot + 1;
    }

    return version;
}

int compareSemVer(const SemVer& a, const SemVer& b) {
    if (a.major != b.major) {
        return a.major < b.major ? -1 : 1;
    }
    if (a.minor != b.minor) {
        return a.minor < b.minor ? -1 : 1;
    }
    if (a.patch != b.patch) {
        return a.patch < b.patch ? -1 : 1;
    }
    return 0;
}

namespace {

bool satisfiesClause(const SemVer& have, std::string_view clause) {
    std::string_view op = "=";
    std::string_view rest = clause;
    if (clause.rfind(">=", 0) == 0) {
        op = ">=";
        rest = clause.substr(2);
    } else if (clause.rfind("<=", 0) == 0) {
        op = "<=";
        rest = clause.substr(2);
    } else if (clause.rfind(">", 0) == 0) {
        op = ">";
        rest = clause.substr(1);
    } else if (clause.rfind("<", 0) == 0) {
        op = "<";
        rest = clause.substr(1);
    } else if (clause.rfind("=", 0) == 0) {
        rest = clause.substr(1);
    }

    const auto want = parseSemVer(rest);
    if (!want) {
        return false;
    }

    const int cmp = compareSemVer(have, *want);
    if (op == ">=") {
        return cmp >= 0;
    }
    if (op == "<=") {
        return cmp <= 0;
    }
    if (op == ">") {
        return cmp > 0;
    }
    if (op == "<") {
        return cmp < 0;
    }
    return cmp == 0;
}

bool isClauseSeparator(char c) {
    return c == ',' || std::isspace(static_cast<unsigned char>(c)) != 0;
}

}

bool satisfiesVersionConstraint(std::string_view version, std::string_view constraint) {
    if (constraint.empty()) {
        return true;
    }

    const auto have = parseSemVer(version);
    if (!have) {
        return false;
    }

    bool sawClause = false;
    std::size_t pos = 0;
    while (pos < constraint.size()) {
        while (pos < constraint.size() && isClauseSeparator(constraint[pos])) {
            ++pos;
        }
        if (pos >= constraint.size()) {
            break;
        }
        std::size_t end = pos;
        while (end < constraint.size() && !isClauseSeparator(constraint[end])) {
            ++end;
        }
        if (!satisfiesClause(*have, constraint.substr(pos, end - pos))) {
            return false;
        }
        sawClause = true;
        pos = end;
    }
    return sawClause;
}

}
