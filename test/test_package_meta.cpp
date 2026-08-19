#include "core/package_meta.hpp"
#include "core/semver.hpp"

#include "test_assert.hpp"

namespace slopengine {

namespace {

void testSemVerParsing() {
    const auto full = parseSemVer("1.2.3");
    CHECK_TRUE(full.has_value());
    CHECK_EQ(full->major, 1);
    CHECK_EQ(full->minor, 2);
    CHECK_EQ(full->patch, 3);

    const auto short_ = parseSemVer("1.2");
    CHECK_TRUE(short_.has_value());
    CHECK_EQ(short_->patch, 0);

    CHECK_FALSE(parseSemVer("").has_value());
    CHECK_FALSE(parseSemVer("1.x.0").has_value());
}

void testSemVerCompare() {
    CHECK_TRUE(compareSemVer(*parseSemVer("1.0.0"), *parseSemVer("1.0.0")) == 0);
    CHECK_TRUE(compareSemVer(*parseSemVer("1.0.0"), *parseSemVer("1.0.1")) < 0);
    CHECK_TRUE(compareSemVer(*parseSemVer("1.1.0"), *parseSemVer("1.0.9")) > 0);
    CHECK_TRUE(compareSemVer(*parseSemVer("2.0.0"), *parseSemVer("1.9.9")) > 0);
}

void testSatisfiesVersionConstraint() {
    CHECK_TRUE(satisfiesVersionConstraint("0.4.3", ""));
    CHECK_TRUE(satisfiesVersionConstraint("0.4.3", ">=0.4.0"));
    CHECK_TRUE(satisfiesVersionConstraint("0.4.0", ">=0.4.0"));
    CHECK_FALSE(satisfiesVersionConstraint("0.3.9", ">=0.4.0"));
    CHECK_TRUE(satisfiesVersionConstraint("0.4.0", "0.4.0"));
    CHECK_FALSE(satisfiesVersionConstraint("0.4.1", "0.4.0"));
    CHECK_TRUE(satisfiesVersionConstraint("0.4.1", ">0.4.0"));
    CHECK_FALSE(satisfiesVersionConstraint("0.4.0", ">0.4.0"));
    CHECK_TRUE(satisfiesVersionConstraint("0.4.0", "<=0.4.0"));
    CHECK_FALSE(satisfiesVersionConstraint("0.4.1", "<0.4.1"));
    CHECK_FALSE(satisfiesVersionConstraint("not-a-version", ">=0.4.0"));
}

void testParsePackageMetaDependsWithConstraint() {
    const std::string_view source =
        "(package\n"
        "  (id \"slopenstein\")\n"
        "  (name \"Slopenstein\")\n"
        "  (version \"0.1.0\")\n"
        "  (depends \"slopengine.engine@>=0.4.0\" \"unconstrained.package\"))\n";

    PackageMeta meta;
    CHECK_TRUE(parsePackageMeta(source, meta));
    CHECK_EQ(meta.id, std::string("slopenstein"));
    CHECK_EQ(meta.depends.size(), static_cast<std::size_t>(2));
    CHECK_EQ(meta.depends[0].id, std::string("slopengine.engine"));
    CHECK_EQ(meta.depends[0].versionConstraint, std::string(">=0.4.0"));
    CHECK_EQ(meta.depends[1].id, std::string("unconstrained.package"));
    CHECK_EQ(meta.depends[1].versionConstraint, std::string(""));
}

} // namespace

void runPackageMetaTests() {
    testSemVerParsing();
    testSemVerCompare();
    testSatisfiesVersionConstraint();
    testParsePackageMetaDependsWithConstraint();
}

}
