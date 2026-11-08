#include "test_assert.hpp"

#include "net/net_protocol.hpp"

#include <s7.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>

namespace slopengine {

namespace {

template <typename Decode>
bool rejectsEveryTruncation(const std::vector<std::byte>& data, Decode decode) {
    for (std::size_t size = 0; size < data.size(); ++size) {
        if (decode(std::span<const std::byte>(data.data(), size))) {
            return false;
        }
    }
    std::vector<std::byte> longer = data;
    longer.push_back(std::byte{0});
    return !decode(std::span<const std::byte>(longer));
}

} // namespace

void runNetProtocolTests() {
    s7_scheme* sc = s7_init();
    NetSchema schema;
    std::vector<std::string> messages;
    schema.addPackageFields(
        sc,
        s7_eval_c_string(sc,
            "'((player (health (type int 0 999) (default 100)) (owned (type (list (symbol a b c) 3)))"
            " (ammo (type (table (string 8) (int 0 9999) 4)))))"),
        "game",
        PackageRole::Base,
        messages);
    schema.finalize();
    const std::vector<const NetField*> fields = schema.fieldsIn(NetScope{NetScopeKind::Player, {}});
    CHECK_EQ(static_cast<int>(fields.size()), 3);

    {
        NetHello hello;
        hello.engineVersion = "0.9.0";
        hello.schemaHash = 0x0123456789abcdefull;
        hello.packages = {{"engine", "0.9.0"}, {"slopdemo", "0.1.0"}};
        const std::vector<std::byte> data = encodeNetHello(hello);
        NetHello back;
        CHECK(decodeNetHello(data, back));
        CHECK(back.protocol == kNetProtocolVersion && back.engineVersion == "0.9.0");
        CHECK(back.schemaHash == hello.schemaHash && back.packages == hello.packages);
        CHECK(netMessageKind(data) == NetMessageKind::Hello);
        CHECK(rejectsEveryTruncation(data, [](auto span) { NetHello h; return decodeNetHello(span, h); }));
    }

    {
        const std::vector<std::byte> data = encodeNetMapChange({"demoroom", "fresh", "player-2"});
        NetMapChange back;
        CHECK(decodeNetMapChange(data, back) && back.mapId == "demoroom" && back.reason == "fresh");
        CHECK(back.yourPlayer == "player-2");
        CHECK(rejectsEveryTruncation(data, [](auto span) { NetMapChange m; return decodeNetMapChange(span, m); }));
        NetHello wrong;
        CHECK_FALSE(decodeNetHello(data, wrong));
    }

    {
        const std::vector<std::byte> data =
            encodeNetIntroduce({{1, "door-1", NetEntityKind::Thing}, {300, "player-2", NetEntityKind::Player}});
        std::vector<NetIntroduction> back;
        CHECK(decodeNetIntroduce(data, back) && back.size() == 2 && back[1].netId == 300 && back[1].name == "player-2");
        CHECK(back.size() == 2 && back[0].kind == NetEntityKind::Thing && back[1].kind == NetEntityKind::Player);
        CHECK(rejectsEveryTruncation(data, [](auto span) { std::vector<NetIntroduction> v; return decodeNetIntroduce(span, v); }));
        const std::vector<std::byte> forget = encodeNetForget({7, 9000});
        std::vector<std::uint32_t> ids;
        CHECK(decodeNetForget(forget, ids) && ids.size() == 2 && ids[1] == 9000);
    }

    {
        NetCommandFrame frame;
        frame.tick = 1234;
        frame.moveForward = 1.0f;
        frame.yaw = -2.5f;
        frame.down = {1, 0, 0, 1, 0, 0, 0, 0, 1, 1};
        frame.pressed = {0, 0, 1};
        const std::vector<std::byte> data = encodeNetCommands({frame, frame}, 777);
        std::vector<NetCommandFrame> back;
        std::uint64_t acked = 0;
        CHECK(decodeNetCommands(data, back, acked) && back.size() == 2 && acked == 777);
        CHECK(!back.empty() && back[0].tick == 1234 && back[0].yaw == -2.5f && back[0].down == frame.down &&
              back[0].pressed == frame.pressed);
        CHECK(rejectsEveryTruncation(data, [](auto span) {
            std::vector<NetCommandFrame> v;
            std::uint64_t a = 0;
            return decodeNetCommands(span, v, a);
        }));
        std::vector<NetCommandFrame> tooMany(kMaxNetCommandsPerMessage + 1, frame);
        CHECK_FALSE(decodeNetCommands(encodeNetCommands(tooMany, 0), back, acked));
    }

    {
        NetSnapshot snapshot;
        snapshot.serverTick = 99;
        snapshot.ackedCommand = 1200;
        snapshot.hasEye = true;
        snapshot.eye[1] = 1.7f;
        snapshot.hasSelf = true;
        snapshot.self.feet[2] = -4.0f;
        snapshot.self.velocity[1] = 7.7f;
        snapshot.self.moveSpeed = 9.6f;
        snapshot.self.height = 0.55f;
        snapshot.self.allowMove = false;
        NetValue health;
        health.kind = NetTypeKind::Int;
        health.integer = 87;
        NetValue owned;
        owned.kind = NetTypeKind::List;
        NetValue symbol;
        symbol.kind = NetTypeKind::Symbol;
        symbol.text = "c";
        owned.items.push_back(symbol);
        snapshot.playerFields = {{fields[0]->name == "health" ? 0 : 1, health}};
        for (std::size_t i = 0; i < fields.size(); ++i) {
            if (fields[i]->name == "health") {
                snapshot.playerFields = {{static_cast<int>(i), health}};
            }
            if (fields[i]->name == "owned") {
                snapshot.playerFields.emplace_back(static_cast<int>(i), owned);
            }
        }
        NetEntityState actor;
        actor.netId = 42;
        const float at[3]{3.0f, -1.25f, 2000.5f};
        const float facing[4]{0.0f, 0.38268343f, 0.0f, 0.92387953f};
        CHECK(packNetPose(at, facing, actor.pose));
        actor.hold = 12;
        actor.hasAnimation = true;
        actor.clipId = 3;
        actor.time = 0.5f;
        actor.speed = 1.5f;
        actor.playing = true;
        actor.loop = true;
        snapshot.clipNames = {{3, "Walk"}};
        snapshot.entities = {actor, NetEntityState{}};
        const std::vector<std::byte> data = encodeNetSnapshot(snapshot, fields);
        NetSnapshot back;
        CHECK(decodeNetSnapshot(data, fields, back));
        CHECK(back.serverTick == 99 && back.ackedCommand == 1200 && back.hasEye && back.eye[1] == 1.7f);
        CHECK(back.hasSelf && back.self.feet[2] == -4.0f && back.self.velocity[1] == 7.7f);
        CHECK(back.self.moveSpeed == 9.6f && back.self.height == 0.55f && !back.self.allowMove);
        CHECK(back.playerFields.size() == 2 && back.entities.size() == 2);
        CHECK(back.clipNames.size() == 1 && back.clipNames[0].first == 3 && back.clipNames[0].second == "Walk");
        CHECK(!back.entities.empty() && back.entities[0].clipId == 3 && back.entities[0].playing && back.entities[0].loop);
        CHECK(!back.entities.empty() && back.entities[0].hold == 12 && back.entities[0].speed == 1.5f);
        CHECK(!back.entities.empty() && back.entities[0].pose == actor.pose && std::fabs(back.entities[0].time - 0.5f) < 1.0e-3f);
        CHECK(back.entities.size() == 2 && !back.entities[1].hasAnimation && back.entities[1].hold == 0);
        CHECK(netEntityStateBytes(actor, fields) <= 24);
        bool sameFields = back.playerFields.size() == snapshot.playerFields.size();
        for (std::size_t i = 0; sameFields && i < back.playerFields.size(); ++i) {
            sameFields = back.playerFields[i] == snapshot.playerFields[i];
        }
        CHECK(sameFields);
        CHECK(rejectsEveryTruncation(data, [&](auto span) { NetSnapshot s; return decodeNetSnapshot(span, fields, s); }));
        NetSnapshot badClip = snapshot;
        badClip.entities[0].clipId = static_cast<std::uint32_t>(kMaxNetClips);
        CHECK_FALSE(decodeNetSnapshot(encodeNetSnapshot(badClip, fields), fields, back));
    }

    {
        std::mt19937 random(9);
        std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
        float worstPosition = 0.0f;
        float worstAngle = 0.0f;
        for (int i = 0; i < 2000; ++i) {
            const float at[3]{unit(random) * 3000.0f, unit(random) * 50.0f, unit(random) * 3000.0f};
            float q[4]{unit(random), unit(random), unit(random), unit(random)};
            const float length = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
            for (float& c : q) {
                c /= length;
            }
            NetPose pose;
            CHECK(packNetPose(at, q, pose));
            float backAt[3]{};
            float backQ[4]{};
            unpackNetPose(pose, backAt, backQ);
            for (int a = 0; a < 3; ++a) {
                worstPosition = std::max(worstPosition, std::fabs(backAt[a] - at[a]));
            }
            const float dot = std::fabs(q[0] * backQ[0] + q[1] * backQ[1] + q[2] * backQ[2] + q[3] * backQ[3]);
            worstAngle = std::max(worstAngle, 2.0f * std::acos(std::min(1.0f, dot)));
        }
        CHECK(worstPosition <= 0.5f / kNetPositionScale + 1.0e-4f);
        CHECK(worstAngle < 0.005f);
        const float nan[3]{std::nanf(""), 0.0f, 0.0f};
        const float identity[4]{0.0f, 0.0f, 0.0f, 1.0f};
        NetPose pose;
        CHECK_FALSE(packNetPose(nan, identity, pose));
        const float far[3]{2.0e6f, 0.0f, 0.0f};
        CHECK_FALSE(packNetPose(far, identity, pose));
    }

    {
        NetSchema events;
        std::vector<std::string> eventMessages;
        events.addPackageEvents(
            sc,
            s7_eval_c_string(sc, "'((blast (args (radius float) (kind (symbol small big)))) (hit (args (who entity))))"),
            "game",
            PackageRole::Base,
            eventMessages);
        events.finalize();
        NetEventMessage blast;
        blast.eventId = static_cast<std::uint32_t>(events.findEvent("blast")->id);
        blast.where = NetEventWhere::Point;
        blast.point[2] = 4.0f;
        NetValue radius;
        radius.kind = NetTypeKind::Float;
        radius.real = 6.5;
        NetValue kind;
        kind.kind = NetTypeKind::Symbol;
        kind.text = "big";
        blast.args = {radius, kind};
        NetEventMessage hit;
        hit.eventId = static_cast<std::uint32_t>(events.findEvent("hit")->id);
        hit.where = NetEventWhere::Player;
        hit.player = "Player";
        NetValue who;
        who.kind = NetTypeKind::Entity;
        who.text = "zombie-3";
        hit.args = {who};
        const std::vector<std::byte> data = encodeNetEvents({blast, hit}, events.events());
        std::vector<NetEventMessage> back;
        CHECK(decodeNetEvents(data, events.events(), back) && back.size() == 2);
        CHECK(back.size() == 2 && back[0].point[2] == 4.0f && back[0].args[0].real == 6.5 && back[0].args[1].text == "big");
        CHECK(back.size() == 2 && back[1].player == "Player" && back[1].args[0].text == "zombie-3");
        CHECK(rejectsEveryTruncation(data, [&](auto span) {
            std::vector<NetEventMessage> v;
            return decodeNetEvents(span, events.events(), v);
        }));
    }

    {
        std::mt19937 random(5);
        std::uniform_int_distribution<int> byte(0, 255);
        std::uniform_int_distribution<int> length(0, 64);
        int accepted = 0;
        for (int i = 0; i < 4000; ++i) {
            std::vector<std::byte> noise(static_cast<std::size_t>(length(random)));
            for (std::byte& b : noise) {
                b = static_cast<std::byte>(byte(random));
            }
            NetHello hello;
            NetMapChange change;
            std::vector<NetIntroduction> intro;
            std::vector<NetCommandFrame> commands;
            std::uint64_t acked = 0;
            NetSnapshot snapshot;
            accepted += decodeNetHello(noise, hello) + decodeNetMapChange(noise, change) +
                decodeNetIntroduce(noise, intro) + decodeNetCommands(noise, commands, acked) +
                decodeNetSnapshot(noise, fields, snapshot);
        }
        CHECK(accepted < 40);
    }

    const auto decodes = [](std::vector<std::uint8_t> raw, std::uint64_t& out) {
        std::vector<std::byte> data(raw.size());
        std::memcpy(data.data(), raw.data(), raw.size());
        NetReader reader(data);
        return reader.varint(out);
    };
    std::uint64_t value = 0;
    CHECK(decodes({0x00}, value) && value == 0);
    CHECK(decodes({0x96, 0x01}, value) && value == 150);
    CHECK_FALSE(decodes({0x80, 0x00}, value));
    CHECK_FALSE(decodes({0x96, 0x80, 0x00}, value));
    CHECK(decodes({0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01}, value) && value == ~std::uint64_t{0});
    CHECK_FALSE(decodes({0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x03}, value));
    NetWriter writer;
    writer.varint(~std::uint64_t{0});
    NetReader back(writer.data());
    CHECK(back.varint(value) && value == ~std::uint64_t{0});

    s7_quit(sc);
}

}
