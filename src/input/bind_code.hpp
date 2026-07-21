#pragma once

#include <string>
#include <string_view>

namespace slopengine {

constexpr int kMouseBindBase = 1000;

bool isMouseBind(int code);
int mouseButtonFromBind(int code);
int bindFromMouseButton(int button);

/** Parses a bind token ("f", "mouse1", "32") into a bind code. Returns KEY_NULL on failure. */
int parseBindToken(std::string_view token);

/** Formats a bind for settings.cfg (mouse tokens or numeric key codes). */
std::string formatBindToken(int code);

/** Human-readable bind label for UI. Writes into @p buffer when needed. */
const char* bindDisplayName(int code, char* buffer, std::size_t bufferSize);

}
