#include "test_assert.hpp"

#include "render/attachment_path.hpp"
#include "render/bone_attachment.hpp"
#include "render/components.hpp"

#include <flecs.h>

#include <set>
#include <string>

namespace slopengine {

namespace {

flecs::entity attach(flecs::world& world, flecs::entity host, const char* name) {
    flecs::entity child = world.entity().child_of(host);
    child.set_name(name);
    child.set<BoneAttachment>({}).set<Model3D>({});
    return child;
}

}

void runAttachmentPathTests() {
    const std::set<std::string> anchors{"guard", "room1/guard", "weapon"};
    const auto isAnchor = [&](std::string_view prefix) {
        return anchors.count(std::string(prefix)) > 0;
    };

    const auto plain = splitAttachmentPath("guard", isAnchor);
    CHECK(plain.has_value());
    CHECK_EQ(plain->anchor, "guard");
    CHECK(plain->names.empty());

    const auto nested = splitAttachmentPath("guard/held/sight", isAnchor);
    CHECK(nested.has_value());
    CHECK_EQ(nested->anchor, "guard");
    CHECK_EQ(nested->names.size(), 2u);
    CHECK_EQ(nested->names[0], "held");
    CHECK_EQ(nested->names[1], "sight");

    const auto prefab = splitAttachmentPath("room1/guard/held", isAnchor);
    CHECK(prefab.has_value());
    CHECK_EQ(prefab->anchor, "room1/guard");
    CHECK_EQ(prefab->names.size(), 1u);

    CHECK(!splitAttachmentPath("", isAnchor).has_value());
    CHECK(!splitAttachmentPath("nobody/held", isAnchor).has_value());
    CHECK(!splitAttachmentPath("guard//held", isAnchor).has_value());
    CHECK(!splitAttachmentPath("guard/held/", isAnchor).has_value());
    CHECK(!splitAttachmentPath("/guard", isAnchor).has_value());

    flecs::world world;
    flecs::entity actor = world.entity("guard");
    flecs::entity held = attach(world, actor, "held");
    flecs::entity sight = attach(world, held, "sight");
    flecs::entity plainChild = world.entity().child_of(actor);
    plainChild.set_name("light");

    CHECK(descendAttachmentPath(actor, {}) == actor);
    CHECK(descendAttachmentPath(actor, {"held"}) == held);
    CHECK(descendAttachmentPath(actor, {"held", "sight"}) == sight);
    CHECK(!descendAttachmentPath(actor, {"sight"}).is_valid());
    CHECK(!descendAttachmentPath(actor, {"light"}).is_valid());
    CHECK(!descendAttachmentPath(flecs::entity{}, {"held"}).is_valid());

    CHECK_EQ(attachmentEventSource(actor), "guard");
    CHECK_EQ(attachmentEventSource(held), "guard/held");
    CHECK_EQ(attachmentEventSource(sight), "guard/held/sight");

    flecs::entity fpRoot = world.entity("PlayerFp");
    flecs::entity socket = world.entity().child_of(fpRoot);
    socket.set_name("weapon");
    flecs::entity socketGeo = world.entity().child_of(socket);
    socketGeo.set<Model3D>({});
    flecs::entity fpHeld = attach(world, socketGeo, "held");
    CHECK_EQ(attachmentEventSource(socketGeo), "weapon");
    CHECK_EQ(attachmentEventSource(fpHeld), "weapon/held");

    CHECK_EQ(attachmentEventSource(world.entity()), "");

    destroyAttachmentTree(held);
    CHECK(!held.is_alive());
    CHECK(!sight.is_alive());
    CHECK(actor.is_alive());
    CHECK(plainChild.is_alive());
}

}
