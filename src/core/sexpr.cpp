#include "core/sexpr.hpp"

#include <cctype>
#include <charconv>
#include <cstdio>

namespace slopengine {

namespace {

struct Parser {
    std::string_view source;
    std::size_t index = 0;
    int line = 1;
    int column = 1;
    SexprParseError error{};

    bool atEnd() const { return index >= source.size(); }

    char peek() const { return atEnd() ? '\0' : source[index]; }

    char get() {
        if (atEnd()) {
            return '\0';
        }
        const char c = source[index++];
        if (c == '\n') {
            ++line;
            column = 1;
        } else {
            ++column;
        }
        return c;
    }

    void setError(std::string message) {
        error.message = std::move(message);
        error.line = line;
        error.column = column;
    }

    void skipSpaceAndComments() {
        while (!atEnd()) {
            const char c = peek();
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                get();
                continue;
            }
            if (c == ';') {
                while (!atEnd() && peek() != '\n') {
                    get();
                }
                continue;
            }
            break;
        }
    }

    bool parseString(Sexpr& out) {
        out.kind = SexprKind::String;
        out.line = line;
        out.column = column;
        if (get() != '"') {
            setError("expected string");
            return false;
        }
        std::string value;
        while (!atEnd()) {
            const char c = get();
            if (c == '"') {
                out.text = std::move(value);
                return true;
            }
            if (c == '\\') {
                if (atEnd()) {
                    setError("unterminated string escape");
                    return false;
                }
                const char esc = get();
                if (esc == 'n') {
                    value.push_back('\n');
                } else if (esc == 't') {
                    value.push_back('\t');
                } else if (esc == '"' || esc == '\\') {
                    value.push_back(esc);
                } else {
                    value.push_back(esc);
                }
                continue;
            }
            if (c == '\n') {
                setError("unterminated string");
                return false;
            }
            value.push_back(c);
        }
        setError("unterminated string");
        return false;
    }

    bool parseAtomOrNumber(Sexpr& out) {
        out.line = line;
        out.column = column;
        std::string token;
        while (!atEnd()) {
            const char c = peek();
            if (c == '(' || c == ')' || c == '"' || c == ';' ||
                c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                break;
            }
            token.push_back(get());
        }
        if (token.empty()) {
            setError("expected atom");
            return false;
        }

        double number = 0.0;
        const auto* begin = token.data();
        const auto* end = token.data() + token.size();
        const auto result = std::from_chars(begin, end, number);
        if (result.ec == std::errc{} && result.ptr == end) {
            out.kind = SexprKind::Number;
            out.number = number;
            out.text = std::move(token);
            return true;
        }

        out.kind = SexprKind::Atom;
        out.text = std::move(token);
        return true;
    }

    bool parseList(Sexpr& out) {
        out.kind = SexprKind::List;
        out.line = line;
        out.column = column;
        if (get() != '(') {
            setError("expected '('");
            return false;
        }
        while (true) {
            skipSpaceAndComments();
            if (atEnd()) {
                setError("unterminated list");
                return false;
            }
            if (peek() == ')') {
                get();
                return true;
            }
            Sexpr child{};
            if (!parseForm(child)) {
                return false;
            }
            out.list.push_back(std::move(child));
        }
    }

    bool parseForm(Sexpr& out) {
        skipSpaceAndComments();
        if (atEnd()) {
            setError("unexpected end of input");
            return false;
        }
        const char c = peek();
        if (c == '(') {
            return parseList(out);
        }
        if (c == '"') {
            return parseString(out);
        }
        if (c == ')') {
            setError("unexpected ')'");
            return false;
        }
        return parseAtomOrNumber(out);
    }
};

} // namespace

bool Sexpr::isAtom(std::string_view name) const {
    return kind == SexprKind::Atom && text == name;
}

SexprParseResult parseSexprs(std::string_view source) {
    SexprParseResult result{};
    Parser parser{};
    parser.source = source;
    parser.skipSpaceAndComments();
    while (!parser.atEnd()) {
        Sexpr form{};
        if (!parser.parseForm(form)) {
            result.error = parser.error;
            result.ok = false;
            return result;
        }
        result.forms.push_back(std::move(form));
        parser.skipSpaceAndComments();
    }
    result.ok = true;
    return result;
}

std::string formatSexprError(std::string_view path, const SexprParseError& error) {
    char buffer[512];
    std::snprintf(
        buffer,
        sizeof(buffer),
        "%.*s:%d:%d: %s",
        static_cast<int>(path.size()),
        path.data(),
        error.line,
        error.column,
        error.message.c_str());
    return buffer;
}

}
