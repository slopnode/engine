#pragma once

#include "net/net_protocol.hpp"

#include <flecs.h>
#include <raylib.h>

#include <string>
#include <string_view>
#include <vector>

struct s7_scheme;

namespace slopengine {

void bindThingRuntimeApi(flecs::world& world, s7_scheme* scheme);
void queueThingDespawn(flecs::world& world, std::string_view id);
flecs::entity severActorPart(flecs::entity entity, const std::string& boneName, Vector3 push, float speed, float spin,
                             float lifetime, bool makePiece);
bool ragdollActorBody(flecs::entity entity, Vector3 push, float speed, const std::string& hitBoneName, float lifetime);
flecs::entity spawnNetPresentation(flecs::world& world, const std::string& name, const NetSpawnRecipe& recipe);
std::vector<NetPose> captureCorpsePose(flecs::entity entity);
bool applyCorpsePose(flecs::entity entity, const std::vector<NetPose>& bones);
bool hideActorBone(flecs::entity entity, const std::string& boneName);
bool attachActorGeo(flecs::entity host, const NetAttachment& attachment);
bool detachActorGeo(flecs::entity host, const std::string& slot);

}
