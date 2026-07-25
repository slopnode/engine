#pragma once

#include "editor.hpp"
#include "map/handler_binding.hpp"
#include "map/map_handler_registry.hpp"

#include <functional>
#include <optional>
#include <string>

namespace slopmap {

/** Draw handler id combo + typed params. Returns true if binding changed. */
bool drawHandlerBindingEditor(
    Editor& editor,
    const char* label,
    const char* imguiId,
    slopengine::MapHandlerKind kind,
    const std::optional<slopengine::HandlerBinding>& common,
    const std::function<void(slopengine::HandlerBinding&)>& apply);

std::string resolvedFaceId(
    const slopengine::Brush& brush,
    const slopengine::BrushFace& face,
    std::size_t faceIndex);

}
