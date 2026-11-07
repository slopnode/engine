#include "net/net_protocol.hpp"

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

} // namespace

std::optional<NetMessageKind> netMessageKind(std::span<const std::byte> data) {
    if (data.empty()) {
        return std::nullopt;
    }
    const auto raw = static_cast<std::uint8_t>(data[0]);
    if (raw < static_cast<std::uint8_t>(NetMessageKind::Hello) || raw > static_cast<std::uint8_t>(NetMessageKind::Events)) {
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
    return writer.data();
}

bool decodeNetMapChange(std::span<const std::byte> data, NetMapChange& out) {
    NetReader reader(data);
    out = NetMapChange{};
    return expect(reader, NetMessageKind::MapChange) && reader.string(out.mapId, kMaxNameBytes) &&
        reader.string(out.reason, kMaxNameBytes) && reader.atEnd();
}

std::vector<std::byte> encodeNetIntroduce(const std::vector<NetIntroduction>& entries) {
    NetWriter writer = begin(NetMessageKind::Introduce);
    writer.varint(entries.size());
    for (const NetIntroduction& entry : entries) {
        writer.varint(entry.netId);
        writer.string(entry.name);
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
        if (!readU32(reader, entry.netId) || !reader.string(entry.name, kMaxNameBytes)) {
            return false;
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

std::vector<std::byte> encodeNetCommands(const std::vector<NetCommandFrame>& frames) {
    NetWriter writer = begin(NetMessageKind::Commands);
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

bool decodeNetCommands(std::span<const std::byte> data, std::vector<NetCommandFrame>& out) {
    NetReader reader(data);
    out.clear();
    std::size_t frames = 0;
    if (!expect(reader, NetMessageKind::Commands) || !reader.count(frames, kMaxNetCommandsPerMessage)) {
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
    writer.varint(snapshot.playerFields.size());
    for (const auto& [slot, value] : snapshot.playerFields) {
        writer.varint(static_cast<std::uint64_t>(slot));
        writer.value(value, playerFields[static_cast<std::size_t>(slot)]->type);
    }
    writer.varint(snapshot.entities.size());
    for (const NetEntityState& entity : snapshot.entities) {
        writer.varint(entity.netId);
        writeFloats(writer, entity.position, 3);
        writeFloats(writer, entity.rotation, 4);
        writer.u8(entity.hasAnimation ? 1 : 0);
        if (entity.hasAnimation) {
            writer.string(entity.clip);
            writer.f32(entity.time);
            writer.f32(entity.speed);
            writer.u8(static_cast<std::uint8_t>((entity.playing ? 1 : 0) | (entity.loop ? 2 : 0)));
        }
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
    std::size_t entities = 0;
    if (!reader.count(entities, kMaxNetEntities)) {
        return false;
    }
    for (std::size_t i = 0; i < entities; ++i) {
        NetEntityState entity;
        std::uint8_t hasAnimation = 0;
        if (!readU32(reader, entity.netId) || !readFloats(reader, entity.position, 3) ||
            !readFloats(reader, entity.rotation, 4) || !reader.u8(hasAnimation) || hasAnimation > 1) {
            return false;
        }
        entity.hasAnimation = hasAnimation == 1;
        if (entity.hasAnimation) {
            std::uint8_t flags = 0;
            if (!reader.string(entity.clip, kMaxClipBytes) || !reader.f32(entity.time) || !reader.f32(entity.speed) ||
                !reader.u8(flags) || flags > 3) {
                return false;
            }
            entity.playing = (flags & 1) != 0;
            entity.loop = (flags & 2) != 0;
        }
        out.entities.push_back(std::move(entity));
    }
    return reader.atEnd();
}

}
