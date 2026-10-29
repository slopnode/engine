#include "drag_drop.hpp"

#include "imgui.h"

#include <cstring>

namespace slopmat {

const char* dragPayloadType(DragPayload payload) {
    switch (payload) {
    case DragPayload::Texture: return "SLOPMAT_TEXTURE";
    case DragPayload::TextureGraph: return "SLOPMAT_GRAPH";
    case DragPayload::TextureAnim: return "SLOPMAT_ANIM";
    case DragPayload::Material: return "SLOPMAT_MATERIAL";
    }
    return "SLOPMAT";
}

void beginDragSource(DragPayload payload, const std::string& path) {
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        ImGui::SetDragDropPayload(dragPayloadType(payload), path.c_str(), path.size() + 1);
        ImGui::TextUnformatted(path.c_str());
        ImGui::EndDragDropSource();
    }
}

bool acceptDroppedPath(DragPayload payload, std::string& out) {
    bool accepted = false;
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* data = ImGui::AcceptDragDropPayload(dragPayloadType(payload))) {
            out.assign(static_cast<const char*>(data->Data), static_cast<std::size_t>(data->DataSize) - 1);
            accepted = true;
        }
        ImGui::EndDragDropTarget();
    }
    return accepted;
}

}
