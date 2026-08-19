#include "package_details_tab.hpp"

#include "core/semver.hpp"

#include "imgui.h"

#include <algorithm>

namespace sloplauncher {

namespace {

constexpr ImVec4 kGreen{0.4f, 0.85f, 0.45f, 1.0f};
constexpr ImVec4 kRed{0.9f, 0.35f, 0.35f, 1.0f};
constexpr ImVec4 kOrange{0.95f, 0.65f, 0.25f, 1.0f};

void drawDependencyRow(const LauncherState& state, const slopengine::PackageDependency& dep) {
    const slopengine::Package* resolved = state.findPackage(dep.id);

    std::string label = dep.id;
    if (!dep.versionConstraint.empty()) {
        label += " " + dep.versionConstraint;
    }

    ImVec4 color = kGreen;
    std::string status = "OK";
    if (resolved == nullptr) {
        color = kRed;
        status = "missing";
    } else if (!dep.versionConstraint.empty()
        && !slopengine::satisfiesVersionConstraint(resolved->meta().version, dep.versionConstraint)) {
        color = kOrange;
        status = "have " + resolved->meta().version;
    }

    ImGui::TextUnformatted(label.c_str());
    ImGui::SameLine();
    ImGui::TextColored(color, "[%s]", status.c_str());
}

}

void drawPackageDetailsTab(LauncherState& state) {
    const slopengine::Package* package = state.findPackage(state.selectedPackageId);
    if (package == nullptr) {
        ImGui::TextDisabled("Click a package on the left to see its details here.");
        return;
    }

    const slopengine::PackageMeta& meta = package->meta();

    ImGui::TextDisabled("Package");
    ImGui::Separator();
    ImGui::Text("Name: %s", meta.name.empty() ? "(unnamed)" : meta.name.c_str());
    ImGui::Text("ID: %s", meta.id.c_str());
    ImGui::Text("Version: %s", meta.version.c_str());
    ImGui::TextWrapped("Root: %s", package->root().string().c_str());

    ImGui::Spacing();
    if (state.baseGameId == meta.id) {
        ImGui::TextColored(kGreen, "Role: base game");
    } else if (std::find(state.modIds.begin(), state.modIds.end(), meta.id) != state.modIds.end()) {
        ImGui::TextColored(kGreen, "Role: mod");
    } else {
        ImGui::TextDisabled("Role: not selected");
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Dependencies");
    ImGui::Separator();
    if (meta.depends.empty()) {
        ImGui::TextDisabled("No dependencies declared.");
    } else {
        for (const slopengine::PackageDependency& dep : meta.depends) {
            drawDependencyRow(state, dep);
        }
    }
}

}
