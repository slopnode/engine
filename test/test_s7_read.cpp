#include "test_assert.hpp"

#include "assets/asset_store.hpp"
#include "game/app_config.hpp"
#include "map/things_script.hpp"
#include "script/s7_read.hpp"
#include "script/script_scope.hpp"

#include <s7.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace slopengine {

namespace {

s7_pointer g_t_read(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "t-read", args);
    const std::string name = in.string("name string");
    const float x = in.realf("x number");
    const float y = in.optRealf(10.0f);
    const bool flag = in.optFlag(true);
    return makeAlist(
        sc,
        {
            {"name", s7_make_string(sc, name.c_str())},
            {"sum", s7_make_real(sc, static_cast<double>(x + y))},
            {"flag", s7_make_boolean(sc, flag)},
            {"rest", in.rest()},
        });
}

s7_pointer g_t_vec(s7_scheme* sc, s7_pointer args) {
    ArgReader in(sc, "t-vec", args);
    const Vector3 a = in.vec3("number");
    const Vector3 b = in.vec3List("(x y z) list");
    return makeVec3List(sc, a.x + b.x, a.y + b.y, a.z + b.z);
}

s7_pointer g_t_mutate(s7_scheme* sc, s7_pointer) {
    return s7_t(sc);
}

constexpr SchemeBinding kTestBindings[] = {
    {"t-read", g_t_read, 2, 2, false, "(t-read name x [y] [flag])", ScriptCap::ReadWorld},
    {"t-vec", g_t_vec, 4, 0, false, "(t-vec x y z list)"},
    {"t-mutate", g_t_mutate, 0, 0, false, "(t-mutate)", ScriptCap::WorldMutate},
};

constexpr ClauseSpec kTestClauses[] = {
    {"at", 3, 0, false, "(at x y z)"},
    {"cost", 1, 0, false, "(cost value)", ClauseArg::Number},
    {"ease", 1, 0, false, "(ease name)", ClauseArg::Name},
    {"closed", 1, 0, false, "(closed boolean)", ClauseArg::Boolean},
    {"tags", 0, 0, true, "(tags values...)"},
};

std::string evalToString(s7_scheme* sc, const char* form) {
    const std::string wrapped = std::string("(object->string (catch #t (lambda () ") + form +
                                ") (lambda args (car args))))";
    s7_pointer result = s7_eval_c_string(sc, wrapped.c_str());
    return s7_is_string(result) ? s7_string(result) : "";
}

std::filesystem::path makeTempPackageRoot() {
    std::random_device rd;
    const auto root = std::filesystem::temp_directory_path() / ("sloptest-s7-read-" + std::to_string(rd()));
    std::filesystem::create_directories(root);
    return root;
}

void writeFile(const std::filesystem::path& path, std::string_view contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << contents;
}

void runReaderTests(s7_scheme* sc) {
    registerBindings(sc, kTestBindings);
    registerClauseBuilders(sc, kTestClauses);

    ScriptScopeGuard scope(ScriptScope::World);
    CHECK_EQ(evalToString(sc, "(cdr (assq 'sum (t-read \"a\" 1)))"), std::string("11.0"));
    CHECK_EQ(evalToString(sc, "(cdr (assq 'name (t-read \"a\" 1)))"), std::string("\"a\""));
    CHECK_EQ(evalToString(sc, "(cdr (assq 'flag (t-read \"a\" 1)))"), std::string("#t"));
    CHECK_EQ(evalToString(sc, "(cdr (assq 'sum (t-read \"a\" 1 2 #f)))"), std::string("3.0"));
    CHECK_EQ(evalToString(sc, "(cdr (assq 'flag (t-read \"a\" 1 2 #f)))"), std::string("#f"));
    CHECK_EQ(evalToString(sc, "(cdr (assq 'flag (t-read \"a\" 1 2 0)))"), std::string("#f"));
    CHECK_EQ(evalToString(sc, "(cdr (assq 'rest (t-read \"a\" 1 2 0)))"), std::string("()"));
    CHECK_EQ(evalToString(sc, "(cdr (assq 'rest (t-read \"a\" 1 \"z\")))"), std::string("(\"z\")"));
    CHECK_EQ(evalToString(sc, "(t-read 5 1)"), std::string("wrong-type-arg"));
    CHECK_EQ(evalToString(sc, "(t-read \"a\" \"b\")"), std::string("wrong-type-arg"));
    CHECK_EQ(evalToString(sc, "(t-vec 1 2 3 '(10 20 30))"), std::string("(11.0 22.0 33.0)"));
    CHECK_EQ(evalToString(sc, "(t-vec 1 2 3 '(10 20))"), std::string("wrong-type-arg"));
    CHECK_EQ(evalToString(sc, "(t-mutate)"), std::string("#t"));

    CHECK_EQ(evalToString(sc, "(at 1 2 3)"), std::string("(at 1 2 3)"));
    CHECK_EQ(evalToString(sc, "(cost 2.5)"), std::string("(cost 2.5)"));
    CHECK_EQ(evalToString(sc, "(cost \"x\")"), std::string("wrong-type-arg"));
    CHECK_EQ(evalToString(sc, "(ease 'smooth)"), std::string("(ease smooth)"));
    CHECK_EQ(evalToString(sc, "(ease \"smooth\")"), std::string("(ease \"smooth\")"));
    CHECK_EQ(evalToString(sc, "(ease 4)"), std::string("wrong-type-arg"));
    CHECK_EQ(evalToString(sc, "(closed #t)"), std::string("(closed #t)"));
    CHECK_EQ(evalToString(sc, "(closed 1)"), std::string("wrong-type-arg"));
    CHECK_EQ(evalToString(sc, "(tags \"a\" 'b)"), std::string("(tags \"a\" b)"));
    CHECK_EQ(evalToString(sc, "(tags)"), std::string("(tags)"));
    CHECK_EQ(evalToString(sc, "(at 1 2)"), std::string("wrong-number-of-args"));
}

void runCapabilityTests(s7_scheme* sc) {
    ScriptScopeGuard scope(ScriptScope::Hud);
    CHECK_EQ(evalToString(sc, "(t-mutate)"), std::string("capability"));
    CHECK_EQ(evalToString(sc, "(cdr (assq 'sum (t-read \"a\" 1)))"), std::string("11.0"));
}

void runAlistTests(s7_scheme* sc) {
    s7_pointer alist = s7_eval_c_string(
        sc, "'((name . \"n\") (speed . 2.5) (count . 3) (on . #t) (at 1 2 3) (sym . word))");
    std::string name;
    CHECK(assocString(sc, alist, "name", name));
    CHECK_EQ(name, std::string("n"));
    CHECK(assocString(sc, alist, "sym", name));
    CHECK_EQ(name, std::string("word"));
    float speed = 0.0f;
    CHECK(assocReal(sc, alist, "speed", speed));
    CHECK_EQ(speed, 2.5f);
    int count = 0;
    CHECK(assocInt(sc, alist, "count", count));
    CHECK_EQ(count, 3);
    bool on = false;
    CHECK(assocBool(sc, alist, "on", on));
    CHECK(on);
    Vector3 at{};
    CHECK(assocVec3(sc, alist, "at", at));
    CHECK_EQ(at.z, 3.0f);
    CHECK_FALSE(assocReal(sc, alist, "missing", speed));
    CHECK_FALSE(assocReal(sc, alist, "name", speed));
    CHECK(assocValue(sc, s7_make_integer(sc, 4), "name") == nullptr);

    int seen = 0;
    s7_pointer rejected = rejectedAlistEntry(alist, [&](const char* key, s7_pointer) {
        ++seen;
        return std::string(key) != "on";
    });
    CHECK(rejected != nullptr);
    CHECK_EQ(seen, 4);
}

void runThingsDslTests() {
    const std::filesystem::path root = makeTempPackageRoot();
    writeFile(
        root / "package.meta",
        "(package\n"
        "  (id \"sloptest-s7-read\")\n"
        "  (name \"s7 read test package\")\n"
        "  (version \"0.1.0\")\n"
        "  (depends))\n");
    writeFile(
        root / "maps" / "testmap" / "map.meta",
        "(map\n"
        "  (id \"testmap\")\n"
        "  (name \"testmap\")\n"
        "  (author \"\")\n"
        "  (description \"\")\n"
        "  (depends))\n");
    writeFile(
        root / "maps" / "testmap" / "things.s7",
        "(player-start (id \"start\") (at 1 2 3) (yaw 0.5))\n"
        "(prop (id \"crate\") (at 4 5 6) (sprite \"props/crate\") (anim \"idle\" #f) (tags \"a\" \"b\"))\n"
        "(marker (id \"m\") (at 7 8 9))\n");

    AppConfig config{};
    config.game = root;
    AssetStore assets(config);
    s7_scheme* scheme = s7_init();
    CHECK(scheme != nullptr);
    auto doc = loadMapThings(scheme, assets, "testmap");
    CHECK(doc.has_value());
    if (doc.has_value()) {
        CHECK_EQ(doc->things.size(), 3u);
        if (doc->things.size() == 3) {
            CHECK_EQ(doc->things[0].id, std::string("start"));
            CHECK_EQ(doc->things[0].at.z, 3.0f);
            CHECK_EQ(doc->things[0].yaw, 0.5f);
            CHECK_EQ(doc->things[1].id, std::string("crate"));
            CHECK_EQ(doc->things[1].sprite, std::string("props/crate"));
            CHECK(doc->things[1].haveAnim);
            CHECK_FALSE(doc->things[1].animLoop);
            CHECK_EQ(doc->things[1].animClip, std::string("idle"));
            CHECK_EQ(doc->things[2].at.x, 7.0f);
        }
    }
    s7_quit(scheme);
    std::filesystem::remove_all(root);
}

} // namespace

void runS7ReadTests() {
    s7_scheme* sc = s7_init();
    CHECK(sc != nullptr);
    runReaderTests(sc);
    runCapabilityTests(sc);
    runAlistTests(sc);
    s7_quit(sc);

    s7_scheme* again = s7_init();
    CHECK(again != nullptr);
    runReaderTests(again);
    s7_quit(again);

    runThingsDslTests();
}

}
