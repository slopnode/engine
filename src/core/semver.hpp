#pragma once

#include <optional>
#include <string_view>

namespace slopengine {

/** A parsed MAJOR.MINOR.PATCH version (missing components default to 0). */
struct SemVer {
    int major = 0;
    int minor = 0;
    int patch = 0;
};

/** Parses "MAJOR[.MINOR[.PATCH]]"; nullopt on anything non-numeric. */
std::optional<SemVer> parseSemVer(std::string_view text);

/** <0 if a<b, 0 if equal, >0 if a>b. */
int compareSemVer(const SemVer& a, const SemVer& b);

/**
 * Checks whether @p version satisfies @p constraint.
 *
 * @p constraint is a version optionally prefixed with an operator: "=",
 * ">=", ">", "<=", "<" (no prefix means "="), e.g. ">=0.4.0". An empty
 * constraint always satisfies. Returns false if @p version or the
 * constraint's version cannot be parsed as a SemVer.
 */
bool satisfiesVersionConstraint(std::string_view version, std::string_view constraint);

}
