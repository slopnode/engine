#include "input/bind_code.hpp"

#include <raylib.h>

#include <cctype>
#include <cstdio>
#include <string>

namespace slopengine {

bool isMouseBind(int code) {
    return code >= kMouseBindBase;
}

int mouseButtonFromBind(int code) {
    return code - kMouseBindBase;
}

int bindFromMouseButton(int button) {
    return kMouseBindBase + button;
}

namespace {

int parseMouseToken(std::string_view token) {
    if (token == "mouse1" || token == "lmb") {
        return bindFromMouseButton(MOUSE_BUTTON_LEFT);
    }
    if (token == "mouse2" || token == "rmb") {
        return bindFromMouseButton(MOUSE_BUTTON_RIGHT);
    }
    if (token == "mouse3" || token == "mmb") {
        return bindFromMouseButton(MOUSE_BUTTON_MIDDLE);
    }
    if (token == "mouse4") {
        return bindFromMouseButton(MOUSE_BUTTON_SIDE);
    }
    if (token == "mouse5") {
        return bindFromMouseButton(MOUSE_BUTTON_EXTRA);
    }
    return KEY_NULL;
}

}

int parseBindToken(std::string_view token) {
    if (token.empty()) {
        return KEY_NULL;
    }

    if (const int mouse = parseMouseToken(token); mouse != KEY_NULL) {
        return mouse;
    }

    if (token == "space") {
        return KEY_SPACE;
    }
    if (token == "grave" || token == "`") {
        return KEY_GRAVE;
    }
    if (token == "tab") {
        return KEY_TAB;
    }
    if (token == "enter" || token == "return") {
        return KEY_ENTER;
    }
    if (token == "escape" || token == "esc") {
        return KEY_ESCAPE;
    }
    if (token.size() >= 2 && (token[0] == 'f' || token[0] == 'F')) {
        bool digits = true;
        for (std::size_t i = 1; i < token.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(token[i]))) {
                digits = false;
                break;
            }
        }
        if (digits) {
            const int index = std::stoi(std::string(token.substr(1)));
            if (index >= 1 && index <= 12) {
                return KEY_F1 + (index - 1);
            }
        }
    }

    if (token.size() == 1) {
        const unsigned char ch = static_cast<unsigned char>(token[0]);
        if (ch >= 'a' && ch <= 'z') {
            return KEY_A + (ch - 'a');
        }
        if (ch >= 'A' && ch <= 'Z') {
            return KEY_A + (ch - 'A');
        }
        if (ch >= '0' && ch <= '9') {
            return KEY_ZERO + (ch - '0');
        }
    }

    bool allDigits = true;
    for (char ch : token) {
        if (!std::isdigit(static_cast<unsigned char>(ch)) && ch != '-') {
            allDigits = false;
            break;
        }
    }
    if (allDigits) {
        try {
            return std::stoi(std::string(token));
        } catch (...) {
            return KEY_NULL;
        }
    }

    return KEY_NULL;
}

std::string formatBindToken(int code) {
    if (code == KEY_NULL) {
        return "0";
    }
    if (isMouseBind(code)) {
        switch (mouseButtonFromBind(code)) {
        case MOUSE_BUTTON_LEFT:
            return "mouse1";
        case MOUSE_BUTTON_RIGHT:
            return "mouse2";
        case MOUSE_BUTTON_MIDDLE:
            return "mouse3";
        case MOUSE_BUTTON_SIDE:
            return "mouse4";
        case MOUSE_BUTTON_EXTRA:
            return "mouse5";
        default:
            break;
        }
    }
    if (code >= KEY_F1 && code <= KEY_F12) {
        return "f" + std::to_string(code - KEY_F1 + 1);
    }
    return std::to_string(code);
}

const char* bindDisplayName(int code, char* buffer, std::size_t bufferSize) {
    if (code == KEY_NULL) {
        return "Unbound";
    }

    if (isMouseBind(code)) {
        switch (mouseButtonFromBind(code)) {
        case MOUSE_BUTTON_LEFT:
            return "LMB";
        case MOUSE_BUTTON_RIGHT:
            return "RMB";
        case MOUSE_BUTTON_MIDDLE:
            return "MMB";
        case MOUSE_BUTTON_SIDE:
            return "Mouse4";
        case MOUSE_BUTTON_EXTRA:
            return "Mouse5";
        default:
            std::snprintf(buffer, bufferSize, "Mouse %d", mouseButtonFromBind(code));
            return buffer;
        }
    }

    if (code == KEY_SPACE) {
        return "Space";
    }
    if (code == KEY_ESCAPE) {
        return "Escape";
    }
    if (code == KEY_ENTER) {
        return "Enter";
    }
    if (code == KEY_TAB) {
        return "Tab";
    }
    if (code == KEY_LEFT_SHIFT || code == KEY_RIGHT_SHIFT) {
        return "Shift";
    }
    if (code == KEY_LEFT_CONTROL || code == KEY_RIGHT_CONTROL) {
        return "Ctrl";
    }
    if (code == KEY_LEFT_ALT || code == KEY_RIGHT_ALT) {
        return "Alt";
    }
    if (code == KEY_UP) {
        return "Up";
    }
    if (code == KEY_DOWN) {
        return "Down";
    }
    if (code == KEY_LEFT) {
        return "Left";
    }
    if (code == KEY_RIGHT) {
        return "Right";
    }
    if (code == KEY_GRAVE) {
        return "`";
    }
    if (code >= KEY_F1 && code <= KEY_F12) {
        std::snprintf(buffer, bufferSize, "F%d", code - KEY_F1 + 1);
        return buffer;
    }

    const char* name = GetKeyName(code);
    if (name != nullptr && name[0] != '\0') {
        return name;
    }

    std::snprintf(buffer, bufferSize, "Key %d", code);
    return buffer;
}

}
