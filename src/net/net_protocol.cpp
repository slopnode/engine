#include "net/net_protocol.hpp"

#include <algorithm>
#include <cmath>

namespace slopengine {

namespace {

constexpr std::size_t kMaxNameBytes = 128;
constexpr std::size_t kMaxPackages = 64;
constexpr std::size_t kMaxClipBytes = 64;

NetWriter begin(NetMessageKind kind) {
    NetWriter writer;
    writer.u8(static_cast<std::uint8_t>(kind));
    return writer;
}

bool expect(NetReader& reader, NetMessageKind kind) {
    std::uint8_t raw = 0;
    return reader.u8(raw) && raw == static_cast<std::uint8_t>(kind);
}

void writeFloats(NetWriter& writer, const float* values, int count) {
    for (int i = 0; i < count; ++i) {
        writer.f32(values[i]);
    }
}

bool readFloats(NetReader& reader, float* values, int count) {
    for (int i = 0; i < count; ++i) {
        if (!reader.f32(values[i])) {
            return false;
        }
    }
    return true;
}

bool readU32(NetReader& reader, std::uint32_t& out) {
    std::uint64_t raw = 0;
    if (!reader.varint(raw) || raw > 0xffffffffull) {
        return false;
    }
    out = static_cast<std::uint32_t>(raw);
    return true;
}

constexpr std::uint8_t kEntityAnimation = 1;
constexpr std::uint8_t kEntityFields = 2;
constexpr std::uint8_t kEntityHold = 4;
constexpr std::uint8_t kEntityPlaying = 8;
constexpr std::uint8_t kEntityLoop = 16;
constexpr std::uint8_t kEntitySpeed = 32;
constexpr std::uint8_t kEntityFlags = 63;
constexpr float kRotationRange = 0.70710678f;
constexpr std::uint32_t kRotationSteps = 1023;
constexpr float kMaxNetClipSeconds = 1.0e6f;

std::uint64_t zigzag(std::int64_t value) {
    return (static_cast<std::uint64_t>(value) << 1) ^ static_cast<std::uint64_t>(value >> 63);
}

std::int64_t unzigzag(std::uint64_t value) {
    return static_cast<std::int64_t>(value >> 1) ^ -static_cast<std::int64_t>(value & 1);
}

std::uint64_t clipMillis(float time) {
    if (!std::isfinite(time) || time <= 0.0f) {
        return 0;
    }
    return static_cast<std::uint64_t>(std::lround(std::min(time, kMaxNetClipSeconds) * 1000.0f));
}

void writePose(NetWriter& writer, const NetPose& pose) {
    for (std::int64_t axis : pose.position) {
        writer.varint(zigzag(axis));
    }
    for (int shift = 24; shift >= 0; shift -= 8) {
        writer.u8(static_cast<std::uint8_t>((pose.rotation >> shift) & 0xffu));
    }
}

bool readPose(NetReader& reader, NetPose& pose) {
    const auto limit = static_cast<std::int64_t>(kMaxNetCoordinate * kNetPositionScale);
    for (std::int64_t& axis : pose.position) {
        std::uint64_t raw = 0;
        if (!reader.varint(raw)) {
            return false;
        }
        axis = unzigzag(raw);
        if (axis > limit || axis < -limit) {
            return false;
        }
    }
    pose.rotation = 0;
    for (int i = 0; i < 4; ++i) {
        std::uint8_t byte = 0;
        if (!reader.u8(byte)) {
            return false;
        }
        pose.rotation = (pose.rotation << 8) | byte;
    }
    return true;
}

bool readFinite(NetReader& reader, float& out, float limit) {
    return reader.f32(out) && std::isfinite(out) && std::fabs(out) <= limit;
}

constexpr std::uint8_t kIntroSpawn = 1;
constexpr std::uint8_t kIntroSevered = 2;
constexpr std::uint8_t kIntroRagdolled = 4;
constexpr std::uint8_t kIntroCorpse = 8;
constexpr std::uint8_t kIntroHidden = 16;
constexpr std::uint8_t kIntroAttachments = 32;
constexpr std::size_t kMaxAssetPathBytes = 128;
constexpr std::size_t kMaxTagBytes = 32;
constexpr std::size_t kMaxBoneBytes = 64;

void writeAttachment(NetWriter& writer, const NetAttachment& attachment) {
    writer.string(attachment.slot);
    writer.string(attachment.bone);
    writer.string(attachment.geo);
    writeFloats(writer, attachment.place, 7);
}

bool readAttachment(NetReader& reader, NetAttachment& attachment) {
    if (!reader.string(attachment.slot, kMaxBoneBytes) || attachment.slot.empty() ||
        !reader.string(attachment.bone, kMaxBoneBytes) || attachment.bone.empty() ||
        !reader.string(attachment.geo, kMaxAssetPathBytes) || attachment.geo.empty()) {
        return false;
    }
    for (float& value : attachment.place) {
        if (!readFinite(reader, value, 1.0e3f)) {
            return false;
        }
    }
    return true;
}

void writeEntity(NetWriter& writer, const NetEntityState& entity, const std::vector<const NetField*>& playerFields) {
    const bool speed = entity.hasAnimation && entity.speed != 1.0f;
    std::uint8_t flags = 0;
    flags |= entity.hasAnimation ? kEntityAnimation : 0;
    flags |= entity.fields.empty() ? 0 : kEntityFields;
    flags |= entity.hold != 0 ? kEntityHold : 0;
    flags |= entity.hasAnimation && entity.playing ? kEntityPlaying : 0;
    flags |= entity.hasAnimation && entity.loop ? kEntityLoop : 0;
    flags |= speed ? kEntitySpeed : 0;
    writer.varint(entity.netId);
    writer.u8(flags);
    writePose(writer, entity.pose);
    if (entity.hold != 0) {
        writer.varint(entity.hold);
    }
    if (entity.hasAnimation) {
        writer.varint(entity.clipId);
        writer.varint(clipMillis(entity.time));
        if (speed) {
            writer.f32(entity.speed);
        }
    }
    if (!entity.fields.empty()) {
        writer.varint(entity.fields.size());
        for (const auto& [slot, value] : entity.fields) {
            writer.varint(static_cast<std::uint64_t>(slot));
            writer.value(value, playerFields[static_cast<std::size_t>(slot)]->type);
        }
    }
}

bool readEntity(NetReader& reader, const std::vector<const NetField*>& playerFields, NetEntityState& entity) {
    std::uint8_t flags = 0;
    if (!readU32(reader, entity.netId) || !reader.u8(flags) || (flags & ~kEntityFlags) != 0) {
        return false;
    }
    if ((flags & kEntityAnimation) == 0 && (flags & (kEntityPlaying | kEntityLoop | kEntitySpeed)) != 0) {
        return false;
    }
    if (!readPose(reader, entity.pose)) {
        return false;
    }
    if ((flags & kEntityHold) != 0 && (!readU32(reader, entity.hold) || entity.hold == 0 || entity.hold > kMaxNetHoldTicks)) {
        return false;
    }
    entity.hasAnimation = (flags & kEntityAnimation) != 0;
    if (entity.hasAnimation) {
        std::uint64_t millis = 0;
        if (!readU32(reader, entity.clipId) || entity.clipId >= kMaxNetClips || !reader.varint(millis) ||
            millis > static_cast<std::uint64_t>(kMaxNetClipSeconds) * 1000) {
            return false;
        }
        entity.time = static_cast<float>(millis) / 1000.0f;
        entity.playing = (flags & kEntityPlaying) != 0;
        entity.loop = (flags & kEntityLoop) != 0;
        if ((flags & kEntitySpeed) != 0 && (!reader.f32(entity.speed) || !std::isfinite(entity.speed) ||
                                            std::fabs(entity.speed) > 1000.0f)) {
            return false;
        }
    }
    if ((flags & kEntityFields) != 0) {
        std::size_t count = 0;
        if (!reader.count(count, playerFields.size()) || count == 0) {
            return false;
        }
        for (std::size_t j = 0; j < count; ++j) {
            std::size_t slot = 0;
            NetValue value;
            if (playerFields.empty() || !reader.count(slot, playerFields.size() - 1) ||
                !reader.value(value, playerFields[slot]->type)) {
                return false;
            }
            entity.fields.emplace_back(static_cast<int>(slot), std::move(value));
        }
    }
    return true;
}

} // namespace

bool packNetPose(const float position[3], const float rotation[4], NetPose& out) {
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(position[i]) || std::fabs(position[i]) > kMaxNetCoordinate) {
            return false;
        }
        out.position[i] = std::llround(static_cast<double>(position[i]) * kNetPositionScale);
    }
    float q[4]{rotation[0], rotation[1], rotation[2], rotation[3]};
    float length = 0.0f;
    for (float component : q) {
        if (!std::isfinite(component)) {
            return false;
        }
        length += component * component;
    }
    if (length < 1.0e-12f) {
        q[0] = 0.0f;
        q[1] = 0.0f;
        q[2] = 0.0f;
        q[3] = 1.0f;
        length = 1.0f;
    }
    length = std::sqrt(length);
    int largest = 0;
    for (int i = 0; i < 4; ++i) {
        q[i] /= length;
        if (std::fabs(q[i]) > std::fabs(q[largest])) {
            largest = i;
        }
    }
    const float sign = q[largest] < 0.0f ? -1.0f : 1.0f;
    std::uint32_t packed = static_cast<std::uint32_t>(largest) << 30;
    int shift = 20;
    for (int i = 0; i < 4; ++i) {
        if (i == largest) {
            continue;
        }
        const float unit = std::clamp(sign * q[i] / kRotationRange, -1.0f, 1.0f) * 0.5f + 0.5f;
        packed |= static_cast<std::uint32_t>(std::lround(unit * kRotationSteps)) << shift;
        shift -= 10;
    }
    out.rotation = packed;
    return true;
}

void unpackNetPose(const NetPose& pose, float position[3], float rotation[4]) {
    for (int i = 0; i < 3; ++i) {
        position[i] = static_cast<float>(static_cast<double>(pose.position[i]) / kNetPositionScale);
    }
    const int largest = static_cast<int>(pose.rotation >> 30);
    float sum = 0.0f;
    int shift = 20;
    for (int i = 0; i < 4; ++i) {
        if (i == largest) {
            continue;
        }
        const float unit = static_cast<float>((pose.rotation >> shift) & kRotationSteps) / kRotationSteps;
        rotation[i] = (unit * 2.0f - 1.0f) * kRotationRange;
        sum += rotation[i] * rotation[i];
        shift -= 10;
    }
    rotation[largest] = std::sqrt(std::max(0.0f, 1.0f - sum));
}

std::optional<NetMessageKind> netMessageKind(std::span<const std::byte> data) {
    if (data.empty()) {
        return std::nullopt;
    }
    const auto raw = static_cast<std::uint8_t>(data[0]);
    if (raw < static_cast<std::uint8_t>(NetMessageKind::Hello) || raw > static_cast<std::uint8_t>(NetMessageKind::Effects)) {
        return std::nullopt;
    }
    return static_cast<NetMessageKind>(raw);
}

std::vector<std::byte> encodeNetHello(const NetHello& hello) {
    NetWriter writer = begin(NetMessageKind::Hello);
    writer.varint(hello.protocol);
    writer.string(hello.engineVersion);
    writer.u64(hello.schemaHash);
    writer.varint(hello.packages.size());
    for (const NetPackageVersion& package : hello.packages) {
        writer.string(package.id);
        writer.string(package.version);
    }
    return writer.data();
}

bool decodeNetHello(std::span<const std::byte> data, NetHello& out) {
    NetReader reader(data);
    out = NetHello{};
    std::size_t packages = 0;
    if (!expect(reader, NetMessageKind::Hello) || !readU32(reader, out.protocol) ||
        !reader.string(out.engineVersion, kMaxNameBytes) || !reader.u64(out.schemaHash) ||
        !reader.count(packages, kMaxPackages)) {
        return false;
    }
    for (std::size_t i = 0; i < packages; ++i) {
        NetPackageVersion package;
        if (!reader.string(package.id, kMaxNameBytes) || !reader.string(package.version, kMaxNameBytes)) {
            return false;
        }
        out.packages.push_back(std::move(package));
    }
    return reader.atEnd();
}

std::vector<std::byte> encodeNetMapChange(const NetMapChange& change) {
    NetWriter writer = begin(NetMessageKind::MapChange);
    writer.string(change.mapId);
    writer.string(change.reason);
    writer.string(change.yourPlayer);
    return writer.data();
}

bool decodeNetMapChange(std::span<const std::byte> data, NetMapChange& out) {
    NetReader reader(data);
    out = NetMapChange{};
    return expect(reader, NetMessageKind::MapChange) && reader.string(out.mapId, kMaxNameBytes) &&
        reader.string(out.reason, kMaxNameBytes) && reader.string(out.yourPlayer, kMaxNameBytes) && reader.atEnd();
}

std::vector<std::byte> encodeNetIntroduce(const std::vector<NetIntroduction>& entries) {
    NetWriter writer = begin(NetMessageKind::Introduce);
    writer.varint(entries.size());
    for (const NetIntroduction& entry : entries) {
        writer.varint(entry.netId);
        writer.string(entry.name);
        writer.u8(static_cast<std::uint8_t>(entry.kind));
        std::uint8_t flags = 0;
        flags |= entry.spawn.kind != NetSpawnKind::None ? kIntroSpawn : 0;
        flags |= entry.severed.empty() ? 0 : kIntroSevered;
        flags |= entry.ragdolled ? kIntroRagdolled : 0;
        flags |= entry.corpse.empty() ? 0 : kIntroCorpse;
        flags |= entry.hidden.empty() ? 0 : kIntroHidden;
        flags |= entry.attachments.empty() ? 0 : kIntroAttachments;
        writer.u8(flags);
        if (entry.spawn.kind != NetSpawnKind::None) {
            writer.u8(static_cast<std::uint8_t>(entry.spawn.kind));
            writer.u8(entry.spawn.sprite ? 1 : 0);
            writer.string(entry.spawn.path);
            writer.varint(entry.spawn.tags.size());
            for (const std::string& tag : entry.spawn.tags) {
                writer.string(tag);
            }
            writePose(writer, entry.spawn.pose);
        }
        if (!entry.severed.empty()) {
            writer.varint(entry.severed.size());
            for (const std::string& bone : entry.severed) {
                writer.string(bone);
            }
        }
        if (!entry.hidden.empty()) {
            writer.varint(entry.hidden.size());
            for (const std::string& bone : entry.hidden) {
                writer.string(bone);
            }
        }
        if (!entry.attachments.empty()) {
            writer.varint(entry.attachments.size());
            for (const NetAttachment& attachment : entry.attachments) {
                writeAttachment(writer, attachment);
            }
        }
        if (!entry.corpse.empty()) {
            writer.varint(entry.corpse.size());
            for (const NetPose& pose : entry.corpse) {
                writePose(writer, pose);
            }
        }
    }
    return writer.data();
}

bool decodeNetIntroduce(std::span<const std::byte> data, std::vector<NetIntroduction>& out) {
    NetReader reader(data);
    out.clear();
    std::size_t entries = 0;
    if (!expect(reader, NetMessageKind::Introduce) || !reader.count(entries, kMaxNetEntities)) {
        return false;
    }
    for (std::size_t i = 0; i < entries; ++i) {
        NetIntroduction entry;
        std::uint8_t kind = 0;
        if (!readU32(reader, entry.netId) || !reader.string(entry.name, kMaxNameBytes) || !reader.u8(kind) || kind > 1) {
            return false;
        }
        entry.kind = static_cast<NetEntityKind>(kind);
        std::uint8_t flags = 0;
        if (!reader.u8(flags) || (flags & ~(kIntroSpawn | kIntroSevered | kIntroRagdolled | kIntroCorpse | kIntroHidden | kIntroAttachments)) != 0) {
            return false;
        }
        if ((flags & kIntroSpawn) != 0) {
            std::uint8_t spawnKind = 0;
            std::uint8_t sprite = 0;
            std::size_t tags = 0;
            if (!reader.u8(spawnKind) || spawnKind < 1 || spawnKind > 2 || !reader.u8(sprite) || sprite > 1 ||
                !reader.string(entry.spawn.path, kMaxAssetPathBytes) || !reader.count(tags, kMaxNetSpawnTags)) {
                return false;
            }
            entry.spawn.kind = static_cast<NetSpawnKind>(spawnKind);
            entry.spawn.sprite = sprite == 1;
            for (std::size_t t = 0; t < tags; ++t) {
                std::string tag;
                if (!reader.string(tag, kMaxTagBytes)) {
                    return false;
                }
                entry.spawn.tags.push_back(std::move(tag));
            }
            if (!readPose(reader, entry.spawn.pose)) {
                return false;
            }
        }
        if ((flags & kIntroSevered) != 0) {
            std::size_t bones = 0;
            if (!reader.count(bones, kMaxNetSeveredBones) || bones == 0) {
                return false;
            }
            for (std::size_t b = 0; b < bones; ++b) {
                std::string bone;
                if (!reader.string(bone, kMaxBoneBytes) || bone.empty()) {
                    return false;
                }
                entry.severed.push_back(std::move(bone));
            }
        }
        entry.ragdolled = (flags & kIntroRagdolled) != 0;
        if ((flags & kIntroHidden) != 0) {
            std::size_t bones = 0;
            if (!reader.count(bones, kMaxNetSeveredBones) || bones == 0) {
                return false;
            }
            for (std::size_t b = 0; b < bones; ++b) {
                std::string bone;
                if (!reader.string(bone, kMaxBoneBytes) || bone.empty()) {
                    return false;
                }
                entry.hidden.push_back(std::move(bone));
            }
        }
        if ((flags & kIntroAttachments) != 0) {
            std::size_t count = 0;
            if (!reader.count(count, kMaxNetAttachments) || count == 0) {
                return false;
            }
            entry.attachments.resize(count);
            for (NetAttachment& attachment : entry.attachments) {
                if (!readAttachment(reader, attachment)) {
                    return false;
                }
            }
        }
        if ((flags & kIntroCorpse) != 0) {
            std::size_t bones = 0;
            if (!entry.ragdolled || !reader.count(bones, kMaxNetCorpseBones) || bones == 0) {
                return false;
            }
            entry.corpse.resize(bones);
            for (NetPose& pose : entry.corpse) {
                if (!readPose(reader, pose)) {
                    return false;
                }
            }
        }
        out.push_back(std::move(entry));
    }
    return reader.atEnd();
}

std::vector<std::byte> encodeNetForget(const std::vector<std::uint32_t>& netIds) {
    NetWriter writer = begin(NetMessageKind::Forget);
    writer.varint(netIds.size());
    for (std::uint32_t netId : netIds) {
        writer.varint(netId);
    }
    return writer.data();
}

bool decodeNetForget(std::span<const std::byte> data, std::vector<std::uint32_t>& out) {
    NetReader reader(data);
    out.clear();
    std::size_t entries = 0;
    if (!expect(reader, NetMessageKind::Forget) || !reader.count(entries, kMaxNetEntities)) {
        return false;
    }
    for (std::size_t i = 0; i < entries; ++i) {
        std::uint32_t netId = 0;
        if (!readU32(reader, netId)) {
            return false;
        }
        out.push_back(netId);
    }
    return reader.atEnd();
}

std::vector<std::byte> encodeNetCommands(const std::vector<NetCommandFrame>& frames, std::uint64_t ackedSnapshot) {
    NetWriter writer = begin(NetMessageKind::Commands);
    writer.varint(ackedSnapshot);
    writer.varint(frames.size());
    for (const NetCommandFrame& frame : frames) {
        writer.varint(frame.tick);
        const float axes[5]{frame.moveForward, frame.moveStrafe, frame.moveUp, frame.yaw, frame.pitch};
        writeFloats(writer, axes, 5);
        writer.bits(frame.down);
        writer.bits(frame.pressed);
    }
    return writer.data();
}

bool decodeNetCommands(std::span<const std::byte> data, std::vector<NetCommandFrame>& out, std::uint64_t& ackedSnapshot) {
    NetReader reader(data);
    out.clear();
    ackedSnapshot = 0;
    std::size_t frames = 0;
    if (!expect(reader, NetMessageKind::Commands) || !reader.varint(ackedSnapshot) ||
        !reader.count(frames, kMaxNetCommandsPerMessage)) {
        return false;
    }
    for (std::size_t i = 0; i < frames; ++i) {
        NetCommandFrame frame;
        float axes[5]{};
        if (!reader.varint(frame.tick) || !readFloats(reader, axes, 5) || !reader.bits(frame.down, kMaxNetActions) ||
            !reader.bits(frame.pressed, kMaxNetActions)) {
            return false;
        }
        frame.moveForward = axes[0];
        frame.moveStrafe = axes[1];
        frame.moveUp = axes[2];
        frame.yaw = axes[3];
        frame.pitch = axes[4];
        out.push_back(std::move(frame));
    }
    return reader.atEnd();
}

std::vector<std::byte> encodeNetEvents(const std::vector<NetEventMessage>& events, const std::vector<NetEventDef>& defs) {
    NetWriter writer = begin(NetMessageKind::Events);
    writer.varint(events.size());
    for (const NetEventMessage& event : events) {
        const NetEventDef& def = defs[event.eventId];
        writer.varint(event.eventId);
        writer.u8(static_cast<std::uint8_t>(event.where));
        if (event.where == NetEventWhere::Point) {
            writeFloats(writer, event.point, 3);
        } else if (event.where == NetEventWhere::Player) {
            writer.string(event.player);
        }
        for (std::size_t i = 0; i < def.args.size(); ++i) {
            writer.value(event.args[i], def.args[i].type);
        }
        if (def.predict) {
            writer.varint(event.causeTick);
        }
    }
    return writer.data();
}

bool decodeNetEvents(
    std::span<const std::byte> data,
    const std::vector<NetEventDef>& defs,
    std::vector<NetEventMessage>& out) {
    NetReader reader(data);
    out.clear();
    std::size_t count = 0;
    if (!expect(reader, NetMessageKind::Events) || !reader.count(count, kMaxNetEntities)) {
        return false;
    }
    for (std::size_t i = 0; i < count; ++i) {
        NetEventMessage event;
        std::uint8_t where = 0;
        if (!readU32(reader, event.eventId) || event.eventId >= defs.size() || !reader.u8(where) || where > 2) {
            return false;
        }
        event.where = static_cast<NetEventWhere>(where);
        if (event.where == NetEventWhere::Point && !readFloats(reader, event.point, 3)) {
            return false;
        }
        if (event.where == NetEventWhere::Player && !reader.string(event.player, kMaxNameBytes)) {
            return false;
        }
        for (const NetEventArg& arg : defs[event.eventId].args) {
            NetValue value;
            if (!reader.value(value, arg.type)) {
                return false;
            }
            event.args.push_back(std::move(value));
        }
        if (defs[event.eventId].predict && !reader.varint(event.causeTick)) {
            return false;
        }
        out.push_back(std::move(event));
    }
    return reader.atEnd();
}

std::vector<std::byte> encodeNetSnapshot(const NetSnapshot& snapshot, const std::vector<const NetField*>& playerFields) {
    NetWriter writer = begin(NetMessageKind::Snapshot);
    writer.varint(snapshot.serverTick);
    writer.varint(snapshot.ackedCommand);
    writer.u8(snapshot.hasEye ? 1 : 0);
    if (snapshot.hasEye) {
        writeFloats(writer, snapshot.eye, 3);
    }
    writer.u8(snapshot.hasSelf ? 1 : 0);
    if (snapshot.hasSelf) {
        const NetSelfState& self = snapshot.self;
        writeFloats(writer, self.feet, 3);
        writeFloats(writer, self.velocity, 3);
        writer.f32(self.moveSpeed);
        writer.f32(self.gravity);
        writer.f32(self.eyeHeight);
        writer.f32(self.radius);
        writer.f32(self.height);
        writer.u8(static_cast<std::uint8_t>((self.allowMove ? 1 : 0) | (self.allowLook ? 2 : 0)));
        writer.varint(self.motorEnabled);
        writer.varint(self.motorActive);
        writer.varint(static_cast<std::uint64_t>(self.motorShaped + 1));
        if (self.motorShaped >= 0) {
            writer.f32(self.standRadius);
            writer.f32(self.standHeight);
            writer.f32(self.standEye);
        }
    }
    writer.varint(snapshot.playerFields.size());
    for (const auto& [slot, value] : snapshot.playerFields) {
        writer.varint(static_cast<std::uint64_t>(slot));
        writer.value(value, playerFields[static_cast<std::size_t>(slot)]->type);
    }
    writer.varint(snapshot.clipNames.size());
    for (const auto& [clipId, name] : snapshot.clipNames) {
        writer.varint(clipId);
        writer.string(name);
    }
    writer.varint(snapshot.entities.size());
    for (const NetEntityState& entity : snapshot.entities) {
        writeEntity(writer, entity, playerFields);
    }
    return writer.data();
}

bool decodeNetSnapshot(
    std::span<const std::byte> data,
    const std::vector<const NetField*>& playerFields,
    NetSnapshot& out) {
    NetReader reader(data);
    out = NetSnapshot{};
    std::uint8_t hasEye = 0;
    if (!expect(reader, NetMessageKind::Snapshot) || !reader.varint(out.serverTick) ||
        !reader.varint(out.ackedCommand) || !reader.u8(hasEye) || hasEye > 1) {
        return false;
    }
    out.hasEye = hasEye == 1;
    if (out.hasEye && !readFloats(reader, out.eye, 3)) {
        return false;
    }
    std::uint8_t hasSelf = 0;
    if (!reader.u8(hasSelf) || hasSelf > 1) {
        return false;
    }
    out.hasSelf = hasSelf == 1;
    if (out.hasSelf) {
        NetSelfState& self = out.self;
        std::uint8_t allowMove = 0;
        if (!readFloats(reader, self.feet, 3) || !readFloats(reader, self.velocity, 3) || !reader.f32(self.moveSpeed) ||
            !reader.f32(self.gravity) || !reader.f32(self.eyeHeight) || !reader.f32(self.radius) ||
            !reader.f32(self.height) || !reader.u8(allowMove) || allowMove > 3 ||
            !readU32(reader, self.motorEnabled) || !readU32(reader, self.motorActive)) {
            return false;
        }
        std::uint32_t shaped = 0;
        if (!readU32(reader, shaped) || shaped > 32) {
            return false;
        }
        self.motorShaped = static_cast<int>(shaped) - 1;
        if (self.motorShaped >= 0 &&
            (!reader.f32(self.standRadius) || !reader.f32(self.standHeight) || !reader.f32(self.standEye))) {
            return false;
        }
        self.allowMove = (allowMove & 1) != 0;
        self.allowLook = (allowMove & 2) != 0;
    }
    std::size_t fields = 0;
    if (!reader.count(fields, playerFields.size())) {
        return false;
    }
    for (std::size_t i = 0; i < fields; ++i) {
        std::size_t slot = 0;
        NetValue value;
        if (!reader.count(slot, playerFields.size() - 1) || playerFields.empty() ||
            !reader.value(value, playerFields[slot]->type)) {
            return false;
        }
        out.playerFields.emplace_back(static_cast<int>(slot), std::move(value));
    }
    std::size_t clips = 0;
    if (!reader.count(clips, kMaxNetClips)) {
        return false;
    }
    for (std::size_t i = 0; i < clips; ++i) {
        std::uint32_t clipId = 0;
        std::string name;
        if (!readU32(reader, clipId) || clipId >= kMaxNetClips || !reader.string(name, kMaxClipBytes) || name.empty()) {
            return false;
        }
        out.clipNames.emplace_back(clipId, std::move(name));
    }
    std::size_t entities = 0;
    if (!reader.count(entities, kMaxNetEntities)) {
        return false;
    }
    for (std::size_t i = 0; i < entities; ++i) {
        NetEntityState entity;
        if (!readEntity(reader, playerFields, entity)) {
            return false;
        }
        out.entities.push_back(std::move(entity));
    }
    return reader.atEnd();
}

std::size_t netEntityStateBytes(const NetEntityState& entity, const std::vector<const NetField*>& playerFields) {
    NetWriter writer;
    writeEntity(writer, entity, playerFields);
    return writer.data().size();
}

std::size_t netClipNameBytes(std::uint32_t clipId, const std::string& name) {
    NetWriter writer;
    writer.varint(clipId);
    writer.string(name);
    return writer.data().size();
}

std::vector<std::byte> encodeNetEffects(const std::vector<NetEffect>& effects) {
    NetWriter writer = begin(NetMessageKind::Effects);
    writer.varint(effects.size());
    for (const NetEffect& effect : effects) {
        writer.u8(static_cast<std::uint8_t>(effect.kind));
        writer.varint(effect.netId);
        writer.string(effect.bone);
        writeFloats(writer, effect.push, 3);
        writer.f32(effect.speed);
        writer.f32(effect.spin);
        writer.f32(effect.lifetime);
        if (effect.kind == NetEffectKind::Attach) {
            writeAttachment(writer, effect.attachment);
        } else if (effect.kind == NetEffectKind::Detach) {
            writer.string(effect.attachment.slot);
        }
    }
    return writer.data();
}

bool decodeNetEffects(std::span<const std::byte> data, std::vector<NetEffect>& out) {
    NetReader reader(data);
    out.clear();
    std::size_t count = 0;
    if (!expect(reader, NetMessageKind::Effects) || !reader.count(count, kMaxNetEffectsPerMessage)) {
        return false;
    }
    for (std::size_t i = 0; i < count; ++i) {
        NetEffect effect;
        std::uint8_t kind = 0;
        if (!reader.u8(kind) || kind < 1 || kind > 5 || !readU32(reader, effect.netId) ||
            !reader.string(effect.bone, kMaxBoneBytes)) {
            return false;
        }
        effect.kind = static_cast<NetEffectKind>(kind);
        const bool needsBone = effect.kind == NetEffectKind::Sever || effect.kind == NetEffectKind::Hide;
        if (needsBone && effect.bone.empty()) {
            return false;
        }
        for (float& axis : effect.push) {
            if (!readFinite(reader, axis, 1.0e3f)) {
                return false;
            }
        }
        if (!readFinite(reader, effect.speed, 1.0e3f) || !readFinite(reader, effect.spin, 1.0e3f) ||
            !readFinite(reader, effect.lifetime, 1.0e4f)) {
            return false;
        }
        if (effect.kind == NetEffectKind::Attach && !readAttachment(reader, effect.attachment)) {
            return false;
        }
        if (effect.kind == NetEffectKind::Detach &&
            (!reader.string(effect.attachment.slot, kMaxBoneBytes) || effect.attachment.slot.empty())) {
            return false;
        }
        out.push_back(std::move(effect));
    }
    return reader.atEnd();
}

bool safeNetAssetPath(std::string_view path) {
    if (path.empty() || path.size() > kMaxAssetPathBytes || path.front() == '/') {
        return false;
    }
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = std::min(path.find('/', start), path.size());
        const std::string_view segment = path.substr(start, end - start);
        if (segment.empty() || segment == "." || segment == "..") {
            return false;
        }
        for (char c : segment) {
            const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
                c == '-' || c == '.';
            if (!ok) {
                return false;
            }
        }
        start = end + 1;
    }
    return true;
}

}
