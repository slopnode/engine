#include "test_assert.hpp"

#include "assets/geo_loader.hpp"
#include "assets/rigged_assets.hpp"
#include "map/csg_compile.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace slopengine {

namespace {

/** Appends one axis-aligned triangle's worth of vertex data as a new primitive. */
void pushTrianglePrimitive(
    GeoAsset& asset,
    VertBuffer& buffer,
    const std::string& name,
    const std::string& material,
    bool transparent) {
    GeoPrimitive primitive;
    primitive.name = name;
    primitive.material = material;
    primitive.transparent = transparent;
    primitive.vertexOffset = buffer.positions.size();
    primitive.vertexCount = 3;
    primitive.indexOffset = buffer.indices.size();
    primitive.indexCount = 3;

    const std::uint32_t base = static_cast<std::uint32_t>(primitive.vertexOffset);
    for (int i = 0; i < 3; ++i) {
        buffer.positions.push_back(Vector3{static_cast<float>(i), 0.0f, 0.0f});
        buffer.normals.push_back(Vector3{0.0f, 1.0f, 0.0f});
        buffer.texcoords.push_back(Vector2{static_cast<float>(i), 0.0f});
        buffer.texcoords2.push_back(Vector2{static_cast<float>(i), 0.0f});
    }
    buffer.indices.push_back(base + 0);
    buffer.indices.push_back(base + 1);
    buffer.indices.push_back(base + 2);

    asset.primitives.push_back(std::move(primitive));
}

std::size_t totalIndexCount(const GeoAsset& asset) {
    std::size_t total = 0;
    for (const GeoPrimitive& primitive : asset.primitives) {
        total += primitive.indexCount;
    }
    return total;
}

/** Every index in every primitive must resolve to a position within [offset, offset+count). */
bool indicesStayWithinOwnPrimitive(const GeoAsset& asset, const VertBuffer& buffer) {
    for (const GeoPrimitive& primitive : asset.primitives) {
        for (std::size_t i = 0; i < primitive.indexCount; ++i) {
            const std::uint32_t index = buffer.indices[primitive.indexOffset + i];
            if (index < primitive.vertexOffset || index >= primitive.vertexOffset + primitive.vertexCount) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

void runCsgCompileTests() {
    // Same material/atlas/detail merges into one primitive; vertex/index data is preserved.
    {
        GeoAsset asset;
        VertBuffer buffer;
        pushTrianglePrimitive(asset, buffer, "face-a", "tech/wall01", false);
        pushTrianglePrimitive(asset, buffer, "face-b", "tech/wall01", false);
        pushTrianglePrimitive(asset, buffer, "face-c", "tech/wall01", false);

        const std::size_t originalIndexCount = totalIndexCount(asset);
        const std::size_t originalVertexCount = buffer.positions.size();

        mergeGeoPrimitivesByKey(asset, buffer, [](const GeoPrimitive& primitive) { return primitive.material; });

        CHECK_EQ(asset.primitives.size(), static_cast<std::size_t>(1));
        CHECK_EQ(buffer.positions.size(), originalVertexCount);
        CHECK_EQ(totalIndexCount(asset), originalIndexCount);
        CHECK(indicesStayWithinOwnPrimitive(asset, buffer));
    }

    // Different materials never merge.
    {
        GeoAsset asset;
        VertBuffer buffer;
        pushTrianglePrimitive(asset, buffer, "face-a", "tech/wall01", false);
        pushTrianglePrimitive(asset, buffer, "face-b", "tech/wall02", false);

        mergeGeoPrimitivesByKey(asset, buffer, [](const GeoPrimitive& primitive) { return primitive.material; });

        CHECK_EQ(asset.primitives.size(), static_cast<std::size_t>(2));
        CHECK(indicesStayWithinOwnPrimitive(asset, buffer));
    }

    // Transparent faces stay unmerged even when the key policy would otherwise combine them.
    {
        GeoAsset asset;
        VertBuffer buffer;
        pushTrianglePrimitive(asset, buffer, "glass-a", "tech/glass", true);
        pushTrianglePrimitive(asset, buffer, "glass-b", "tech/glass", true);

        mergeGeoPrimitivesByKey(asset, buffer, [](const GeoPrimitive& primitive) {
            if (primitive.transparent) {
                return "t:" + primitive.name;
            }
            return primitive.material;
        });

        CHECK_EQ(asset.primitives.size(), static_cast<std::size_t>(2));
        CHECK(asset.primitives[0].transparent);
        CHECK(asset.primitives[1].transparent);
    }

    // A merged group exceeding the 16-bit index cap splits into multiple primitives,
    // none of which exceed it.
    {
        GeoAsset asset;
        VertBuffer buffer;
        constexpr int kTriangleCount = 25000; // 3 verts each -> 75000 verts total, over 65535
        for (int i = 0; i < kTriangleCount; ++i) {
            pushTrianglePrimitive(asset, buffer, "face-" + std::to_string(i), "tech/wall01", false);
        }

        const std::size_t originalIndexCount = totalIndexCount(asset);

        mergeGeoPrimitivesByKey(asset, buffer, [](const GeoPrimitive& primitive) { return primitive.material; });

        CHECK(asset.primitives.size() >= static_cast<std::size_t>(2));
        CHECK_EQ(totalIndexCount(asset), originalIndexCount);
        for (const GeoPrimitive& primitive : asset.primitives) {
            CHECK(primitive.vertexCount <= 65535);
        }
        CHECK(indicesStayWithinOwnPrimitive(asset, buffer));
    }
}

}
