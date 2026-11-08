#include "GaleshiEditorApp.hpp"

#include "GaleshiEngine/Animation/BoneAttachment.hpp"
#include "GaleshiEngine/Assets/AssetManager.hpp"

#include <algorithm>
#include <vector>

namespace {

std::string EntityLabel(Galeshi::Scene& scene, entt::entity entity) {
    auto& registry = scene.Registry();
    if (entity == entt::null || !registry.valid(entity)) return {};
    if (registry.all_of<Galeshi::NameComponent>(entity)) {
        return registry.get<Galeshi::NameComponent>(entity).name;
    }
    return "Entity " + std::to_string(
                           static_cast<uint32_t>(entity));
}

} // namespace

void GaleshiEditorApp::DrawBoneAttachmentSettings(entt::entity character) {
    auto& registry = m_scene.Registry();
    if (!registry.valid(character) ||
        !registry.all_of<Galeshi::SkeletalComponent>(character)) {
        return;
    }
    if (!m_assets) return;

    if (m_boneAttachCharacter != character) {
        m_boneAttachCharacter = character;
        m_boneAttachEntity = entt::null;
        m_boneAttachBone.clear();
    }
    if (m_boneAttachEntity != entt::null &&
        !registry.valid(m_boneAttachEntity)) {
        m_boneAttachEntity = entt::null;
    }

    const auto& skeleton =
        registry.get<Galeshi::SkeletalComponent>(character);
    std::vector<std::string> bones;
    if (!skeleton.sourcePath.empty()) {
        const auto& model = m_assets->LoadModelRef(skeleton.sourcePath);
        bones.reserve(model.boneMap.size());
        for (const auto& [name, index] : model.boneMap) {
            if (index >= 0 && index < 255) bones.push_back(name);
        }
        std::sort(bones.begin(), bones.end());
    }

    if (!ImGui::CollapsingHeader("Bone Attachments",
                                 ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    const std::string currentBone =
        m_boneAttachBone.empty() ? "(Select Bone)" : m_boneAttachBone;
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##attachment_bone", currentBone.c_str())) {
        if (ImGui::Selectable("(Select Bone)", m_boneAttachBone.empty())) {
            m_boneAttachBone.clear();
        }
        for (const std::string& bone : bones) {
            const bool selected = m_boneAttachBone == bone;
            if (ImGui::Selectable(bone.c_str(), selected)) {
                m_boneAttachBone = bone;
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Bone used as the parent for the entity below. The entity's "
            "Position/Rotation/Scale become bone-local.");
    }
    if (bones.empty()) {
        ImGui::TextDisabled(
            "This character has no bones loaded. Select the skeletal "
            "character, not the Player wrapper.");
    }

    const std::string label =
        m_boneAttachEntity == entt::null
            ? "(Drag an entity here)"
            : EntityLabel(m_scene, m_boneAttachEntity);
    ImGui::Button(label.c_str(), ImVec2(-1.0f, 0.0f));
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Drag an entity from the Outliner to attach it to the selected "
            "bone. Its current world transform is preserved.");
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload =
                ImGui::AcceptDragDropPayload("ENTITY")) {
            const entt::entity dragged =
                *static_cast<const entt::entity*>(payload->Data);
            if (dragged != entt::null && dragged != character &&
                registry.valid(dragged)) {
                m_boneAttachEntity = dragged;
            }
        }
        ImGui::EndDragDropTarget();
    }

    const bool canAttach = m_boneAttachEntity != entt::null &&
                           !m_boneAttachBone.empty() &&
                           m_boneAttachEntity != character;
    if (!canAttach) ImGui::BeginDisabled();
    if (ImGui::Button("Attach")) {
        PushHistory();
        if (Galeshi::BoneAttachmentSystem::Attach(
                m_scene, *m_assets, m_boneAttachEntity, character,
                m_boneAttachBone)) {
            spdlog::info(
                "[BoneAttachment] Attached '{}' to bone '{}'",
                EntityLabel(m_scene, m_boneAttachEntity),
                m_boneAttachBone);
        } else {
            spdlog::warn(
                "[BoneAttachment] Could not attach entity to bone '{}'",
                m_boneAttachBone);
        }
    }
    if (!canAttach) ImGui::EndDisabled();

    ImGui::SameLine();
    const bool canDetach =
        m_boneAttachEntity != entt::null &&
        registry.valid(m_boneAttachEntity) &&
        registry.all_of<Galeshi::BoneAttachmentComponent>(
            m_boneAttachEntity);
    if (!canDetach) ImGui::BeginDisabled();
    if (ImGui::Button("Detach")) {
        PushHistory();
        if (Galeshi::BoneAttachmentSystem::Detach(m_scene,
                                                  m_boneAttachEntity)) {
            spdlog::info("[BoneAttachment] Detached entity {}",
                         static_cast<uint32_t>(m_boneAttachEntity));
        }
    }
    if (!canDetach) ImGui::EndDisabled();

    if (m_boneAttachEntity != entt::null &&
        registry.valid(m_boneAttachEntity)) {
        if (const auto* attachment = registry.try_get<
                Galeshi::BoneAttachmentComponent>(m_boneAttachEntity)) {
            ImGui::TextDisabled(
                "Attached to bone '%s' on '%s'.",
                attachment->boneName.c_str(),
                EntityLabel(m_scene, attachment->character).c_str());
            ImGui::TextDisabled(
                "Edit Position/Rotation/Scale in the Transform section.");
        }
    }

    ImGui::Spacing();
}
