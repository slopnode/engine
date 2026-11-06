#include "test_assert.hpp"

#include "map/map_meta.hpp"

namespace slopengine {

void runMapMetaTests() {
    {
        MapMeta meta;
        CHECK(parseMapMeta("(map (id \"m\") (name \"M\"))", meta));
        CHECK_FALSE(meta.hasVis);
        CHECK_FALSE(meta.vis.fast);
        CHECK(formatMapMeta(meta).find("(vis") == std::string::npos);
    }
    {
        MapMeta meta;
        CHECK(parseMapMeta("(map (id \"m\") (name \"M\") (vis (mode fast)))", meta));
        CHECK(meta.hasVis);
        CHECK(meta.vis.fast);
        MapMeta again;
        CHECK(parseMapMeta(formatMapMeta(meta), again));
        CHECK(again.hasVis);
        CHECK(again.vis.fast);
    }
    {
        MapMeta meta;
        CHECK(parseMapMeta("(map (id \"m\") (name \"M\") (vis (mode full)))", meta));
        CHECK(meta.hasVis);
        CHECK_FALSE(meta.vis.fast);
    }
    {
        MapMeta meta;
        CHECK_FALSE(parseMapMeta("(map (id \"m\") (name \"M\") (vis (mode loose)))", meta));
    }
}

}
