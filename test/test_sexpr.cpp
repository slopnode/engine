#include "test_assert.hpp"

#include "core/sexpr.hpp"

namespace slopengine {

void runSexprTests() {
    {
        const SexprParseResult r = parseSexprs("hello");
        CHECK(r.ok);
        CHECK_EQ(r.forms.size(), 1u);
        CHECK(r.forms[0].kind == SexprKind::Atom);
        CHECK_EQ(r.forms[0].text, std::string("hello"));
    }

    {
        const SexprParseResult r = parseSexprs("\"hi there\"");
        CHECK(r.ok);
        CHECK_EQ(r.forms.size(), 1u);
        CHECK(r.forms[0].isString());
        CHECK_EQ(r.forms[0].text, std::string("hi there"));
    }

    {
        const SexprParseResult r = parseSexprs("42");
        CHECK(r.ok);
        CHECK_EQ(r.forms.size(), 1u);
        CHECK(r.forms[0].isNumber());
        CHECK_EQ(r.forms[0].number, 42.0);
    }

    {
        const SexprParseResult r = parseSexprs("(a \"b\" 3)");
        CHECK(r.ok);
        CHECK_EQ(r.forms.size(), 1u);
        CHECK(r.forms[0].isList());
        CHECK_EQ(r.forms[0].list.size(), 3u);
        CHECK(r.forms[0].list[0].isAtom("a"));
        CHECK(r.forms[0].list[1].isString());
        CHECK(r.forms[0].list[2].isNumber());
    }

    {
        const SexprParseResult r = parseSexprs("; comment\n(foo 1)");
        CHECK(r.ok);
        CHECK_EQ(r.forms.size(), 1u);
        CHECK(r.forms[0].isList());
        CHECK(r.forms[0].list[0].isAtom("foo"));
    }

    {
        const SexprParseResult r = parseSexprs("(a b");
        CHECK_FALSE(r.ok);
    }

    {
        const SexprParseResult r = parseSexprs("\"unterminated");
        CHECK_FALSE(r.ok);
    }
}

}
