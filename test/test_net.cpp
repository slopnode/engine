#include "test_assert.hpp"

#include "map/thing.hpp"
#include "map/thing_def_registry.hpp"
#include "net/net_schema.hpp"
#include "net/net_state.hpp"
#include "script/net_script.hpp"
#include "script/script_scope.hpp"

#include <flecs.h>
#include <s7.h>

#include <cmath>
#include <string>

namespace slopengine {

namespace {

constexpr const char* kBaseState =
    "'((player"
    "   (health (type int 0 200) (default 100) (send all) (travel #t))"
    "   (armor (type int 0 200) (send owner))"
    "   (owned (type (list bool 7)) (default (#t #f)) (travel #t))"
    "   (inventory (type (table (string 24) (int 0 99) 32)) (travel #t)))"
    "  (session"
    "   (difficulty (type (symbol easy normal hard)) (default normal) (save #t))"
    "   (fraction (type (real 0 1 1/4)) (default 0.3)))"
    "  ((thing monster)"
    "   (health (type int 0 4000) (default 50) (send none))))";

s7_pointer evalForm(s7_scheme* sc, const std::string& form) {
    return s7_eval_c_string(sc, form.c_str());
}

bool evalTrue(s7_scheme* sc, const std::string& form) {
    const s7_pointer result = evalForm(sc, "(let ((r " + form + ")) r)");
    return s7_is_boolean(result) && s7_boolean(sc, result);
}

std::int64_t evalInt(s7_scheme* sc, const std::string& form) {
    const s7_pointer result = evalForm(sc, form);
    return s7_is_integer(result) ? s7_integer(result) : -12345;
}

void loadSchema(s7_scheme* sc, NetSchema& schema, const char* state, std::vector<std::string>& messages) {
    schema.clear();
    schema.addPackageFields(sc, evalForm(sc, state), "game", PackageRole::Base, messages);
    schema.finalize();
}

} // namespace

void runNetTests() {
    s7_scheme* sc = s7_init();

    {
        NetSchema schema;
        std::vector<std::string> messages;
        loadSchema(sc, schema, kBaseState, messages);
        CHECK(messages.empty());
        CHECK_EQ(static_cast<int>(schema.fields().size()), 7);

        const NetField* health = schema.find(NetScope{NetScopeKind::Player, {}}, "health");
        CHECK(health != nullptr && health->defaultValue.integer == 100 && health->travel && health->save);
        CHECK(netTypeBits(health->type) == 8);
        const NetField* armor = schema.find(NetScope{NetScopeKind::Player, {}}, "armor");
        CHECK(armor != nullptr && armor->defaultValue.integer == 0 && !armor->save && armor->send == NetAudience::Owner);
        const NetField* owned = schema.find(NetScope{NetScopeKind::Player, {}}, "owned");
        CHECK(owned != nullptr && owned->defaultValue.items.size() == 2 && netTypeBits(owned->type) == 10);
        const NetField* fraction = schema.find(NetScope{NetScopeKind::Session, {}}, "fraction");
        CHECK(fraction != nullptr && std::fabs(fraction->defaultValue.real - 0.25) < 1.0e-9);
        const NetField* difficulty = schema.find(NetScope{NetScopeKind::Session, {}}, "difficulty");
        CHECK(difficulty != nullptr && difficulty->defaultValue.text == "normal" && difficulty->save);
        const NetField* monster = schema.find(NetScope{NetScopeKind::Thing, "monster"}, "health");
        CHECK(monster != nullptr && monster->defaultValue.integer == 50 && monster->send == NetAudience::None);

        const std::uint64_t hash = schema.hash();
        NetSchema again;
        loadSchema(sc, again, kBaseState, messages);
        CHECK(again.hash() == hash);
        std::string changed = kBaseState;
        changed.replace(changed.find("(default 100)"), 13, "(default 90)");
        loadSchema(sc, again, changed.c_str(), messages);
        CHECK(again.hash() != hash);
    }

    {
        NetSchema schema;
        std::vector<std::string> messages;
        loadSchema(
            sc,
            schema,
            "'((player (a (type int 5 1)) (b (type int 0 10) (default 11)) (c (type bool)) (c (type bool))"
            " (d (type (frob 3))) (e (default 1))) (nowhere (f (type bool))))",
            messages);
        CHECK_EQ(static_cast<int>(schema.fields().size()), 1);
        CHECK_EQ(static_cast<int>(messages.size()), 6);
    }

    {
        NetSchema schema;
        std::vector<std::string> messages;
        schema.setExtension(sc, evalForm(sc, "'((player (fields 2) (bytes 2)))"));
        CHECK(schema.addPackageFields(sc, evalForm(sc, "'((player (x (type bool))))"), "moda", PackageRole::Mod, messages));
        CHECK_FALSE(schema.addPackageFields(
            sc, evalForm(sc, "'((player (y (type int 0 70000))))"), "modb", PackageRole::Mod, messages));
        CHECK_FALSE(schema.addPackageFields(
            sc, evalForm(sc, "'((session (z (type bool))))"), "modc", PackageRole::Mod, messages));
        CHECK(schema.addPackageFields(sc, evalForm(sc, "'((player (w (type bool))))"), "modd", PackageRole::Mod, messages));
        schema.finalize();
        CHECK(schema.find(NetScope{NetScopeKind::Player, {}}, "moda/x") != nullptr);
        CHECK(schema.find(NetScope{NetScopeKind::Player, {}}, "modd/w") != nullptr);
        CHECK(schema.find(NetScope{NetScopeKind::Player, {}}, "modb/y") == nullptr);
        CHECK_EQ(static_cast<int>(messages.size()), 2);
        CHECK(schema.fields()[0].name == "moda/x" && schema.fields()[0].id == 0);
    }

    {
        NetSchema schema;
        std::vector<std::string> messages;
        CHECK_FALSE(schema.addSessionKinds(
            sc,
            evalForm(sc,
                "'((hub (players 1 64) (join (open 64)) (announce listed))"
                " (room (players 2 4))"
                " (huge (players 1 9999))"
                " (odd (players 4 2))"
                " (typo (playres 1 2))"
                " (room (players 1 2)))"),
            "game",
            messages));
        CHECK_EQ(static_cast<int>(messages.size()), 4);
        CHECK_EQ(static_cast<int>(schema.sessionKinds().size()), 2);
        const NetSessionKind* hub = schema.findSessionKind("hub");
        const NetSessionKind* room = schema.findSessionKind("room");
        CHECK(hub != nullptr && hub->minPlayers == 1 && hub->maxPlayers == 64);
        CHECK(room != nullptr && room->minPlayers == 2 && room->maxPlayers == 4);
        CHECK(schema.findSessionKind("huge") == nullptr);
    }
    {
        NetSchema schema;
        std::vector<std::string> messages;
        schema.setExtension(sc, evalForm(sc, "'((events (count 1) (bytes 8)))"));
        CHECK(schema.addPackageEvents(
            sc,
            evalForm(sc,
                "'((blast (args (radius float) (kind (symbol small big))) (to (near 40)))"
                " (pickup (args (item (string 24))) (to owner) (reliable #t))"
                " (secret (to all))"
                " (bad (to nowhere))"
                " (worse (args (x (frob)))))"),
            "game",
            PackageRole::Base,
            messages));
        CHECK_EQ(static_cast<int>(messages.size()), 2);
        CHECK(schema.addPackageEvents(sc, evalForm(sc, "'((ping (args (n (int 0 9)))))"), "moda", PackageRole::Mod, messages));
        CHECK_FALSE(schema.addPackageEvents(sc, evalForm(sc, "'((pong))"), "modb", PackageRole::Mod, messages));
        schema.finalize();
        const NetEventDef* blast = schema.findEvent("blast");
        CHECK(blast != nullptr && blast->to == NetEventAudience::Near && blast->nearRadius == 40.0f && !blast->reliable);
        CHECK(blast != nullptr && blast->args.size() == 2 && blast->args[0].type.kind == NetTypeKind::Float);
        const NetEventDef* pickup = schema.findEvent("pickup");
        CHECK(pickup != nullptr && pickup->to == NetEventAudience::Owner && pickup->reliable);
        CHECK(schema.findEvent("moda/ping") != nullptr && schema.findEvent("modb/pong") == nullptr);
        CHECK(schema.events()[0].id == 0 && schema.events()[0].packageId == "game");
        const std::uint64_t withEvents = schema.hash();
        NetSchema without;
        without.finalize();
        CHECK(withEvents != without.hash());
    }

    {
        NetType type;
        std::string error;
        CHECK(parseNetType(sc, evalForm(sc, "'float"), type, error) && type.kind == NetTypeKind::Float);
        NetValue number;
        CHECK(netValueFromScheme(sc, s7_make_real(sc, -2.25), type, number, error) && number.real == -2.25);
        CHECK_FALSE(netValueFromScheme(sc, evalForm(sc, "'x"), type, number, error));
        CHECK(parseNetType(sc, evalForm(sc, "'(int 0 10)"), type, error));
        NetValue value;
        CHECK(netValueFromScheme(sc, s7_make_real(sc, 7.6), type, value, error) && value.integer == 8);
        CHECK_FALSE(netValueFromScheme(sc, s7_make_integer(sc, 11), type, value, error));
        CHECK(parseNetType(sc, evalForm(sc, "'(table (string 4) (int 0 9) 2)"), type, error));
        CHECK(netValueFromScheme(sc, evalForm(sc, "'((\"key\" . 3))"), type, value, error));
        const s7_pointer back = netValueToScheme(sc, value);
        CHECK(s7_is_pair(back) && s7_is_pair(s7_car(back)) && s7_integer(s7_cdar(back)) == 3);
        CHECK_FALSE(netValueFromScheme(sc, evalForm(sc, "'((\"toolong\" . 3))"), type, value, error));
        CHECK_FALSE(netValueFromScheme(sc, evalForm(sc, "'((\"a\" . 1) (\"b\" . 2) (\"c\" . 3))"), type, value, error));
    }

    {
        std::vector<std::string> messages;
        loadSchema(sc, netSchema(), kBaseState, messages);
        thingDefRegistry().clear();
        ThingDef imp{};
        imp.id = "imp";
        imp.tags = {"monster"};
        thingDefRegistry().registerDef(imp);

        netSchema().addPackageEvents(
            sc,
            evalForm(sc, "'((boom (args (power (int 0 9)) (scale float))) (quiet))"),
            "game",
            PackageRole::Base,
            messages);
        netSchema().finalize();

        flecs::world world;
        registerNetState(world);
        bindNetApi(world, sc);
        world.entity("imp1").set<ThingTypeRef>({"imp"});

        ScriptScopeGuard scope(ScriptScope::World);
        CHECK_EQ(evalInt(sc, "(net-get \"Player\" 'health)"), 100);
        CHECK(evalTrue(sc, "(net-set! \"Player\" 'health 150)"));
        CHECK_EQ(evalInt(sc, "(net-get \"Player\" 'health)"), 150);
        CHECK(evalTrue(sc, "(catch 'net (lambda () (net-set! \"Player\" 'health 999) #f) (lambda args #t))"));
        CHECK(evalTrue(sc, "(catch 'net (lambda () (net-get \"Player\" 'nope) #f) (lambda args #t))"));
        CHECK_EQ(evalInt(sc, "(net-get \"Player\" 'health)"), 150);
        CHECK(evalTrue(sc, "(eq? (net-get 'session 'difficulty) 'normal)"));
        CHECK(evalTrue(sc, "(equal? (net-get \"Player\" 'owned) '(#t #f))"));
        CHECK_EQ(evalInt(sc, "(net-get \"imp1\" 'health)"), 50);
        CHECK(evalTrue(sc, "(net-set! \"imp1\" 'health 3000)"));
        CHECK_EQ(evalInt(sc, "(net-get \"imp1\" 'health)"), 3000);
        CHECK(evalTrue(sc, "(not (net-get \"nobody\" 'health))"));

        CHECK(evalTrue(sc, "(net-set! \"Player\" 'armor 40)"));
        resetNetMapFields(world);
        CHECK_EQ(evalInt(sc, "(net-get \"Player\" 'armor)"), 0);
        CHECK_EQ(evalInt(sc, "(net-get \"Player\" 'health)"), 150);

        CHECK(evalTrue(sc, "(net-set! 'session 'difficulty 'hard)"));
        evalForm(sc, "(define saved (net-save-state))");
        CHECK(evalTrue(sc, "(not (assq 'armor (cddr (assq 'player saved))))"));
        CHECK(evalTrue(sc, "(net-reset!)"));
        CHECK_EQ(evalInt(sc, "(net-get \"Player\" 'health)"), 100);
        CHECK_EQ(evalInt(sc, "(net-restore-state! saved)"), 4);
        CHECK_EQ(evalInt(sc, "(net-get \"Player\" 'health)"), 150);
        CHECK(evalTrue(sc, "(eq? (net-get 'session 'difficulty) 'hard)"));

        {
            ScriptScopeGuard hud(ScriptScope::Hud);
            CHECK_EQ(evalInt(sc, "(net-get \"Player\" 'health)"), 150);
            CHECK(evalTrue(sc, "(catch #t (lambda () (net-set! \"Player\" 'health 1) #f) (lambda args #t))"));
        }
        CHECK_EQ(evalInt(sc, "(net-get \"Player\" 'health)"), 150);

        evalForm(sc, "(define boom-seen #f)");
        evalForm(sc, "(define (on-event-boom where power scale) (set! boom-seen (list where power scale)))");
        CHECK(evalTrue(sc, "(net-event! 'boom '(1 2 3) 5 0.5)"));
        CHECK(evalTrue(sc, "(equal? boom-seen '((1.0 2.0 3.0) 5 0.5))"));
        CHECK(evalTrue(sc, "(net-event! 'boom \"Player\" 1 2)"));
        CHECK(evalTrue(sc, "(equal? (car boom-seen) \"Player\")"));
        CHECK(evalTrue(sc, "(net-event! 'quiet #f)"));
        CHECK(evalTrue(sc, "(catch 'net (lambda () (net-event! 'boom #f 99 1) #f) (lambda args #t))"));
        CHECK(evalTrue(sc, "(catch 'net (lambda () (net-event! 'boom #f 1) #f) (lambda args #t))"));
        CHECK(evalTrue(sc, "(catch 'net (lambda () (net-event! 'nothing #f) #f) (lambda args #t))"));
        CHECK(evalTrue(sc, "(catch 'net (lambda () (net-event! 'boom '(1 2) 1 1) #f) (lambda args #t))"));
        CHECK(world.get<NetEventQueue>().pending.empty());
        world.get_mut<NetEventQueue>().forward = true;
        CHECK(evalTrue(sc, "(net-event! 'boom #f 3 1)"));
        CHECK(world.get<NetEventQueue>().pending.size() == 1);

        thingDefRegistry().clear();
        netSchema().clear();
    }

    s7_quit(sc);
}

}
