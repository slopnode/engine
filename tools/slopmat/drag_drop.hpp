#pragma once

#include <string>

namespace slopmat {

enum class DragPayload {
    Texture,
    TextureGraph,
    TextureAnim,
    Material,
};

const char* dragPayloadType(DragPayload payload);

/** Makes the last ImGui item a drag source carrying @p path. */
void beginDragSource(DragPayload payload, const std::string& path);

/** Makes the last ImGui item accept a dropped @p payload; returns true and writes @p out when one lands. */
bool acceptDroppedPath(DragPayload payload, std::string& out);

}
