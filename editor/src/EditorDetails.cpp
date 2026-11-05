#include "GaleshiEditorApp.hpp"



void GaleshiEditorApp::DrawDetails() {
        ImGui::Begin("Details");

        auto selected = m_editor.SelectedAll(&m_scene);
        if (selected.empty()) { ImGui::TextDisabled("No selection"); ImGui::End(); return; }

        auto lowerAscii = [](std::string value) {
            for (char& c : value) {
                c = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(c)));
            }
            return value;
        };
        const bool searchActive = !m_detailsSearch.empty();
        const std::string searchLower = lowerAscii(m_detailsSearch);
        bool detailsAnyMatch = false;

        // Floating sticky search bar inside Details. It has its own rounded
        // background, is right-aligned, and stays visible while the component
        // list scrolls in the child below. This is not a separate window.
        {
            const float availableWidth = ImGui::GetContentRegionAvail().x;
            const float searchWidth = std::min(360.0f, availableWidth);
            const float clearWidth = 52.0f;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + availableWidth -
                                 searchWidth);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 7.0f);
            ImGui::PushStyleColor(ImGuiCol_ChildBg,
                                  ImVec4(0.075f, 0.095f, 0.12f, 0.94f));
            ImGui::BeginChild("##DetailsSearchFloat",
                              ImVec2(searchWidth,
                                     ImGui::GetFrameHeight() + 10.0f),
                              true, ImGuiWindowFlags_NoScrollbar |
                                        ImGuiWindowFlags_NoScrollWithMouse);
            ImGui::SetCursorPos(ImVec2(6.0f, 5.0f));
            ImGui::SetNextItemWidth(searchWidth - clearWidth - 16.0f);
            if (ImGui::InputTextWithHint(
                    "##DetailsSearch", "Search properties or materials...",
                    m_detailsSearchBuffer, sizeof(m_detailsSearchBuffer))) {
                m_detailsSearch = m_detailsSearchBuffer;
                m_detailsSearchScrollPending = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear##DetailsSearch")) {
                m_detailsSearch.clear();
                m_detailsSearchBuffer[0] = '\0';
                m_detailsSearchScrollPending = true;
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
        }
        ImGui::BeginChild("##DetailsScroll", ImVec2(0.0f, 0.0f), false);

        auto searchContains = [&](const char* text) {
            if (!searchActive) return true;
            const std::string value = text ? text : "";
            return lowerAscii(value).find(searchLower) != std::string::npos;
        };
        auto beginDetailsSection = [&](const char* title,
                                       ImGuiTreeNodeFlags flags,
                                       const char* keywords) -> bool {
            if (searchActive) {
                const std::string haystack =
                    lowerAscii(std::string(title ? title : "") + " " +
                               (keywords ? keywords : ""));
                if (haystack.find(searchLower) == std::string::npos)
                    return false;
                detailsAnyMatch = true;
                ImGui::SetNextItemOpen(true, ImGuiCond_Always);
            }
            const bool open = ImGui::CollapsingHeader(title, flags);
            if (open && searchActive && m_detailsSearchScrollPending) {
                ImGui::SetScrollHereY(0.0f);
                m_detailsSearchScrollPending = false;
            }
            return open;
        };

        // Collision-shape entities are physics-only: their details page should
        // not expose rendering materials or audio source controls.
        const bool selectedIsCollisionShape =
            selected[0].HasComponent<Galeshi::ColliderComponent>();

        // Texture Settings is a global texture-loading option, but it is only
        // relevant while inspecting a 3D model that actually uses textures.
        auto materialHasTextures = [](const Galeshi::MaterialComponent& matc) {
            if (!matc.material) return false;
            const auto& m = *matc.material;
            return m.albedoMap || m.normalMap || m.metallicRoughnessMap ||
                   m.metallicMap || m.roughnessMap || m.emissiveMap ||
                   m.occlusionMap || m.opacityMap || m.specularMap;
        };

        auto& reg = m_scene.Registry();
        const entt::entity selectedHandle = selected[0].Handle();
        const bool selectedIsLandscape =
            selected[0].HasComponent<Galeshi::LandscapeComponent>();
        bool selectionHasMesh = false;
        bool selectionHasTextures = false;
        bool selectionHasMaterials = false;
        for (auto entity : m_scene.AllEntities()) {
            entt::entity cur = entity.Handle();
            bool belongsToSelection = false;
            int depth = 0;
            while (cur != entt::null && depth++ < 128) {
                if (cur == selectedHandle) {
                    belongsToSelection = true;
                    break;
                }
                if (reg.all_of<Galeshi::ParentComponent>(cur)) {
                    cur = reg.get<Galeshi::ParentComponent>(cur).parent;
                } else {
                    break;
                }
            }
            if (!belongsToSelection) continue;

            const entt::entity handle = entity.Handle();
            if (reg.all_of<Galeshi::MeshComponent>(handle) ||
                reg.all_of<Galeshi::SkeletalComponent>(handle)) {
                selectionHasMesh = true;
            }
            if (reg.all_of<Galeshi::MaterialComponent>(handle)) {
                selectionHasMaterials = true;
                if (materialHasTextures(
                        reg.get<Galeshi::MaterialComponent>(handle))) {
                    selectionHasTextures = true;
                }
            }
        }

        if (!selected.empty() && selected[0].HasComponent<Galeshi::NameComponent>()) {
            auto& nc = selected[0].GetComponent<Galeshi::NameComponent>();
            if (searchContains(nc.name.c_str())) detailsAnyMatch = true;
            char buf[256]; std::snprintf(buf, sizeof(buf), "%s", nc.name.c_str());
            if (ImGui::InputText("Name", buf, sizeof(buf))) nc.name = buf;
        }

        // Character asset field. Existing FBX scenes can migrate to glTF by
        // dropping a GLB/glTF here (or on the character/capsule in the
        // viewport or Outliner). The hierarchy is preserved and all material
        // values are reloaded from the replacement file.
        if (Galeshi::Entity replacementRoot =
                FindSkeletalRootForReplacement(selectedHandle)) {
            auto& replacementSkeleton =
                reg.get<Galeshi::SkeletalComponent>(replacementRoot.Handle());
            if (beginDetailsSection(
                    "Character Model", ImGuiTreeNodeFlags_DefaultOpen,
                    "model asset gltf glb fbx swap replace character capsule "
                    "materials preserve migrate")) {
                ImGui::TextDisabled(
                    "Current: %s",
                    replacementSkeleton.sourcePath.empty()
                        ? "(none)"
                        : replacementSkeleton.sourcePath.c_str());
                if (replacementSkeleton.sourcePath.size() >= 4 &&
                    AssetExtensionLower(replacementSkeleton.sourcePath) == ".fbx") {
                    ImGui::TextColored(
                        ImVec4(1.0f, 0.72f, 0.30f, 1.0f),
                        "FBX source (legacy). Drop a glTF/GLB to migrate it.");
                }
                auto openCharacterModelPicker = [&]() {
                    m_characterModelTarget = replacementRoot.Handle();
                    std::string start = "assets";
                    const std::string projectPath =
                        Galeshi::ProjectManager::GetProjectPath();
                    if (!projectPath.empty()) {
                        start = (std::filesystem::path(projectPath) /
                                 "assets").string();
                    }
                    m_characterModelPicker.Open(
                        start, "Replace Character Model",
                        {".gltf", ".glb"});
                };
                const float browseWidth = 78.0f;
                if (ImGui::Button(
                        "Drop glTF/GLB model here",
                        ImVec2(std::max(120.0f,
                                        ImGui::GetContentRegionAvail().x -
                                            browseWidth -
                                            ImGui::GetStyle().ItemSpacing.x),
                               34.0f))) {
                    openCharacterModelPicker();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Replaces the model attached to this character/capsule.\n"
                        "The hierarchy and skeleton are preserved.\n"
                        "Material values are reloaded from the glTF.\n"
                        "FBX drag & drop is disabled.");
                }
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload =
                            ImGui::AcceptDragDropPayload("GALESHI_ASSET_PATH")) {
                        const char* path =
                            static_cast<const char*>(payload->Data);
                        if (path && IsGltfModelAssetPath(path)) {
                            ReplaceCharacterModel(replacementRoot.Handle(), path);
                        } else if (path) {
                            spdlog::warn(
                                "[ModelSwap] Only glTF/GLB can replace a "
                                "character model: {}",
                                path);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Browse...")) {
                    openCharacterModelPicker();
                }
            }
        }

        // Cast Shadows is shared by the whole imported model. Find the
        // nearest model root above the current selection.
        entt::entity modelRoot = entt::null;
        for (entt::entity cursor = selectedHandle;
             reg.valid(cursor);) {
            if (reg.all_of<Galeshi::ModelComponent>(cursor)) {
                modelRoot = cursor;
                break;
            }
            if (!reg.all_of<Galeshi::ParentComponent>(cursor)) break;
            cursor = reg.get<Galeshi::ParentComponent>(cursor).parent;
        }
        if (modelRoot != entt::null) {
            auto& model = reg.get<Galeshi::ModelComponent>(modelRoot);
            if (beginDetailsSection(
                    "Model", ImGuiTreeNodeFlags_DefaultOpen,
                    "cast shadows shadow model renderer")) {
                bool castsShadow = model.castsShadow;
                if (ImGui::Checkbox("Cast Shadows", &castsShadow)) {
                    PushHistory();
                    model.castsShadow = castsShadow;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Controls shadow casting for every mesh in this model.");
                }
            }
        }

        // Mesh LOD controls apply to the selected mesh and all mesh children.
        // This is especially useful for imported static models, which default
        // to LOD0; users can opt into generated LODs per model/sub-mesh.
        {
            std::vector<Galeshi::Entity> lodMeshes;
            for (auto entity : m_scene.AllEntities()) {
                if (!entity.HasComponent<Galeshi::MeshComponent>()) continue;
                bool belongsToSelection =
                    entity.Handle() == selectedHandle;
                entt::entity cursor = entity.Handle();
                for (int depth = 0;
                     !belongsToSelection && depth < 128; ++depth) {
                    if (!reg.all_of<Galeshi::ParentComponent>(cursor)) break;
                    cursor = reg.get<Galeshi::ParentComponent>(cursor).parent;
                    if (!reg.valid(cursor)) break;
                    if (cursor == selectedHandle) belongsToSelection = true;
                }
                if (belongsToSelection) lodMeshes.push_back(entity);
            }
            if (!lodMeshes.empty()) {
                if (beginDetailsSection(
                        "Mesh LOD", ImGuiTreeNodeFlags_DefaultOpen,
                        "lod level level of detail mesh detail generated "
                        "bias force auto")) {
                    auto& first =
                        lodMeshes.front().GetComponent<Galeshi::MeshComponent>();
                    bool enabled = first.lodEnabled;
                    bool mixedEnabled = false;
                    int forced = first.forcedLod;
                    bool mixedForced = false;
                    float bias = first.lodBias;
                    bool mixedBias = false;
                    for (size_t i = 1; i < lodMeshes.size(); ++i) {
                        const auto& mc =
                            lodMeshes[i].GetComponent<Galeshi::MeshComponent>();
                        mixedEnabled |= mc.lodEnabled != enabled;
                        mixedForced |= mc.forcedLod != forced;
                        mixedBias |= std::abs(mc.lodBias - bias) > 1e-4f;
                    }
                    if (mixedEnabled) ImGui::TextDisabled("Mixed values");
                    if (ImGui::Checkbox("Enable Generated LOD", &enabled)) {
                        for (auto& entity : lodMeshes)
                            entity.GetComponent<Galeshi::MeshComponent>()
                                .lodEnabled = enabled;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "Imported models default to authored LOD 0.\n"
                            "Enable to let the renderer generate cluster LODs.");
                    }

                    const char* lodItems[] = {
                        "Auto", "Force LOD 0", "Force LOD 1",
                        "Force LOD 2", "Force LOD 3"
                    };
                    int lodSelection = forced + 1;
                    if (lodSelection < 0) lodSelection = 0;
                    if (lodSelection > 4) lodSelection = 4;
                    if (mixedForced) {
                        ImGui::TextDisabled("Force LOD: mixed");
                    }
                    if (ImGui::Combo("LOD Override", &lodSelection,
                                     lodItems, IM_ARRAYSIZE(lodItems))) {
                        const int newForced = lodSelection - 1;
                        for (auto& entity : lodMeshes)
                            entity.GetComponent<Galeshi::MeshComponent>()
                                .forcedLod = newForced;
                    }

                    if (mixedBias) ImGui::TextDisabled("LOD Bias: mixed");
                    ImGui::SetNextItemWidth(160.0f);
                    if (ImGui::SliderFloat("LOD Bias", &bias, 0.25f,
                                           4.0f, "%.2f")) {
                        for (auto& entity : lodMeshes)
                            entity.GetComponent<Galeshi::MeshComponent>()
                                .lodBias = bias;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "Multiplier for automatic LOD selection.\n"
                            "Lower = more detail, higher = more aggressive LOD.");
                    }
                    ImGui::TextDisabled(
                        "%zu mesh(es), generated levels: %u",
                        lodMeshes.size(),
                        first.mesh ? first.mesh->LodCount() : 1u);
                }
            }
        }

        if (selectionHasMesh && selectionHasTextures) {
            if (beginDetailsSection(
                    "Texture Settings", 0,
                    "texture compression compress bc7 loading vram")) {
                bool compress = Galeshi::Texture::CompressionEnabled();
                if (ImGui::Checkbox("Compress textures on load (BC7)", &compress)) {
                    Galeshi::Texture::SetCompressionEnabled(compress);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("BC7 compression lowers VRAM usage but makes\n"
                                      "loading significantly slower. Disabled by\n"
                                      "default for fast scene loading.");
                }
                ImGui::TextDisabled("Current: %s",
                                    Galeshi::Texture::CompressionEnabled() ? "compressed" : "uncompressed");
            }
            ImGui::Separator();
        }

        if (beginDetailsSection(
                "Transform", ImGuiTreeNodeFlags_DefaultOpen,
                "position rotation scale uniform transform location orientation "
                "detach collision shape")) {
            glm::vec3 pos = selected[0].HasComponent<Galeshi::TransformComponent>() ? selected[0].GetComponent<Galeshi::TransformComponent>().position : glm::vec3(0.0f);
            glm::vec3 scl = selected[0].HasComponent<Galeshi::TransformComponent>() ? selected[0].GetComponent<Galeshi::TransformComponent>().scale : glm::vec3(1.0f);
            glm::vec3 rot = selected[0].HasComponent<Galeshi::TransformComponent>() ? glm::degrees(glm::eulerAngles(selected[0].GetComponent<Galeshi::TransformComponent>().rotation)) : glm::vec3(0.0f);

            if (ImGui::DragFloat3("Position", glm::value_ptr(pos), 0.05f)) { for (auto e : selected) if (e.HasComponent<Galeshi::TransformComponent>()) e.GetComponent<Galeshi::TransformComponent>().position = pos; }



            if (ImGui::DragFloat3("Scale", glm::value_ptr(scl), 0.05f)) { for (auto e : selected) if (e.HasComponent<Galeshi::TransformComponent>()) e.GetComponent<Galeshi::TransformComponent>().scale = scl; }
            float uniformScale = scl.x;
            if (ImGui::DragFloat("Uniform Scale", &uniformScale, 0.05f)) {
                scl = glm::vec3(uniformScale);
                for (auto e : selected) {
                    if (e.HasComponent<Galeshi::TransformComponent>()) {
                        e.GetComponent<Galeshi::TransformComponent>().scale = scl;
                    }
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Sets X, Y and Z scale together.");
            }
            if (ImGui::DragFloat3("Rotation", glm::value_ptr(rot), 1.0f)) { glm::quat q = glm::quat(glm::radians(rot)); for (auto e : selected) if (e.HasComponent<Galeshi::TransformComponent>()) e.GetComponent<Galeshi::TransformComponent>().rotation = q; }

            if (HasCollisionShapeParent(selectedHandle)) {
                if (ImGui::Button("Detach from Collision Shape")) {
                    DetachChildFromCollisionShape(selectedHandle);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Detach this child from its collision shape while "
                        "preserving the current world transform.");
                }
            }
            if (selected[0].HasComponent<Galeshi::ColliderComponent>() &&
                !selected[0].HasComponent<Galeshi::SkeletalComponent>()) {
                int directChildren = 0;
                auto childView = reg.view<Galeshi::ParentComponent>();
                for (auto child : childView) {
                    if (childView.get<Galeshi::ParentComponent>(child).parent ==
                        selectedHandle) {
                        ++directChildren;
                    }
                }
                if (directChildren > 0) {
                    if (ImGui::Button("Detach Children")) {
                        DetachCollisionShapeChildren(selectedHandle);
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "Detach all direct children of this collision "
                            "shape. Skeletal parents keep their children locked.");
                    }
                }
            }
        }

        // --- Gameplay camera ----------------------------------------------
        if (selected[0].HasComponent<Galeshi::CameraComponent>()) {
            auto& camera =
                selected[0].GetComponent<Galeshi::CameraComponent>();
            if (beginDetailsSection(
                    "Camera", ImGuiTreeNodeFlags_DefaultOpen,
                    "primary game camera priority fov field of view near far "
                    "clip local offset attached parent distance view mode third "
                    "first person strafing movement")) {
                ImGui::Checkbox("Primary (Game Camera)", &camera.primary);
                ImGui::DragInt("Priority", &camera.priority, 1.0f, -100, 100);
                ImGui::DragFloat("FOV (vertical)", &camera.fovY,
                                 0.1f, 1.0f, 179.0f, "%.1f deg");
                ImGui::DragFloat("Near Clip", &camera.nearZ,
                                 0.001f, 0.0001f, 100.0f, "%.4f");
                ImGui::DragFloat("Far Clip", &camera.farZ,
                                 1.0f, 0.1f, 100000.0f, "%.1f");
                if (selected[0].HasComponent<Galeshi::TransformComponent>()) {
                    auto& cameraTransform =
                        selected[0].GetComponent<Galeshi::TransformComponent>();
                    ImGui::DragFloat3("Local Offset",
                                      glm::value_ptr(cameraTransform.position),
                                      0.01f);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "Local translation from the parent (character). "
                            "Use Z for first/third-person distance.");
                    }
                }
                if (IsGameplayCameraAttachedToPlayer(selectedHandle)) {
                    int mode = std::clamp(GetGameplayCameraMode(), 0, 1);
                    const char* modeNames[] = {"Third Person", "First Person"};
                    if (ImGui::Combo("View Mode", &mode, modeNames,
                                     IM_ARRAYSIZE(modeNames))) {
                        SetGameplayCameraMode(mode);
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "Switch the player camera between third-person "
                            "orbit and first-person view.");
                    }
                    ImGui::Checkbox("Strafing (A/D)", &m_playerStrafing);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "When disabled, A/D turn the player.\n"
                            "When enabled, A/D move the player sideways.");
                    }
                }
                if (selected[0].HasComponent<Galeshi::ParentComponent>()) {
                    const entt::entity parent =
                        selected[0].GetComponent<Galeshi::ParentComponent>().parent;
                    if (m_scene.Registry().valid(parent) &&
                        m_scene.Registry().all_of<Galeshi::NameComponent>(parent)) {
                        ImGui::TextDisabled("Attached to: %s",
                            m_scene.Registry().get<Galeshi::NameComponent>(parent)
                                .name.c_str());
                    } else {
                        ImGui::TextDisabled("Attached to a parent");
                    }
                } else {
                    ImGui::TextDisabled("Not attached (world camera)");
                }
                ImGui::TextDisabled(
                    "Camera pose uses this entity's Transform. When parented, "
                    "Position/Rotation are local; translate it to set "
                    "first-person/third-person distance.");
                ImGui::TextDisabled(
                    "While Play is active, the highest-priority primary camera "
                    "overrides the viewport camera.");
            }
        }

        // --- Post-process volume inspector --------------------------------
        if (auto sel = m_editor.Selected(&m_scene); sel &&
            sel.HasComponent<Galeshi::PostProcessVolumeComponent>()) {
            auto& volume =
                sel.GetComponent<Galeshi::PostProcessVolumeComponent>();
            if (beginDetailsSection(
                    "Post-Process Volume", ImGuiTreeNodeFlags_DefaultOpen,
                    "post process bloom ssao ssr ssgi fxaa smaa taa motion blur "
                    "atmosphere fog god rays exposure contrast saturation "
                    "vignette chromatic aberration")) {
                DrawPostProcessVolumeSettings(volume);
            }
        }

        // --- Water inspector ---------------------------------------------
        if (auto sel = m_editor.Selected(&m_scene); sel &&
            sel.HasComponent<Galeshi::WaterComponent>()) {
            auto& water = sel.GetComponent<Galeshi::WaterComponent>();
            if (beginDetailsSection(
                    "Water", ImGuiTreeNodeFlags_DefaultOpen,
                    "water waves color depth foam reflection flow speed")) {
                DrawWaterSettings(water);
            }
        }

        // --- Groom hair inspector ----------------------------------------
        if (auto sel = m_editor.Selected(&m_scene); sel) {
            // Groom hair is only meaningful for skeletal characters. In
            // particular, never expose it for landscape entities even if an
            // old scene still contains a stale GroomComponent.
            if (IsSkeletalCharacter(sel)) {
                if (sel.HasComponent<Galeshi::GroomComponent>()) {
                    auto& groom = sel.GetComponent<Galeshi::GroomComponent>();
                    if (beginDetailsSection(
                            "Groom Hair", ImGuiTreeNodeFlags_DefaultOpen,
                            "groom hair strands guides density length width "
                            "gravity stiffness color")) {
                        DrawGroomSettings(groom);
                    }
                } else if (ImGui::Button("+ Add Groom Hair")) {
                    PushHistory();
                    sel.AddComponent<Galeshi::GroomComponent>();
                }
            }
        }

        // --- Collision shape inspector -----------------------------------
        if (auto sel = m_editor.Selected(&m_scene); sel &&
            sel.HasComponent<Galeshi::ColliderComponent>()) {
            auto& collider = sel.GetComponent<Galeshi::ColliderComponent>();
            if (beginDetailsSection(
                    "Collision Shape", ImGuiTreeNodeFlags_DefaultOpen,
                    "collision shape box sphere capsule half extents radius "
                    "height offset friction restitution trigger visible")) {
                const char* shapeNames[] = {"Box", "Sphere", "Capsule"};
                int shapeType = static_cast<int>(collider.shapeType);
                if (ImGui::Combo("Shape", &shapeType, shapeNames, 3)) {
                    collider.shapeType =
                        static_cast<Galeshi::ColliderShapeType>(shapeType);
                    RebuildPhysicsBody(sel);
                }

                bool changed = false;
                switch (collider.shapeType) {
                    case Galeshi::ColliderShapeType::Box:
                        changed |= ImGui::DragFloat3("Half Extents",
                            glm::value_ptr(collider.halfExtents), 0.01f, 0.001f, 1000.0f);
                        break;
                    case Galeshi::ColliderShapeType::Sphere:
                        changed |= ImGui::DragFloat("Radius", &collider.radius,
                                                    0.01f, 0.001f, 1000.0f);
                        break;
                    case Galeshi::ColliderShapeType::Capsule:
                        changed |= ImGui::DragFloat("Half Height", &collider.halfHeight,
                                                    0.01f, 0.001f, 1000.0f);
                        changed |= ImGui::DragFloat("Radius", &collider.radius,
                                                    0.01f, 0.001f, 1000.0f);
                        break;
                }

                // Collider placement relative to the entity. X/Y are exposed
                // separately for quick top-down placement.
                changed |= ImGui::DragFloat2("Offset XY", glm::value_ptr(collider.offset),
                                             0.01f);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Moves the collision shape on local X/Y\n"
                                      "without moving the entity.");
                }
                changed |= ImGui::DragFloat("Offset Z", &collider.offset.z,
                                            0.01f);

                changed |= ImGui::DragFloat("Friction", &collider.friction,
                                            0.01f, 0.0f, 2.0f);
                changed |= ImGui::DragFloat("Restitution", &collider.restitution,
                                            0.01f, 0.0f, 1.0f);
                changed |= ImGui::Checkbox("Is Trigger", &collider.isTrigger);

                // Editor visibility is a debug-draw option only. Do not feed
                // it into the physics rebuild path: toggling the wireframe
                // must not recreate the body or disturb a mesh parented to
                // the collider entity.
                ImGui::Checkbox("Visible in Editor", &collider.visibleInEditor);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Shows or hides only the collider wireframe.\n"
                        "Meshes attached to this entity remain visible.");
                }

                if (changed) RebuildPhysicsBody(sel);

                if (ImGui::Button("Remove Collision Shape")) {
                    sel.RemoveComponent<Galeshi::ColliderComponent>();
                }
            }
        }

        // --- Rigid body inspector ----------------------------------------
        if (auto sel = m_editor.Selected(&m_scene); sel &&
            sel.HasComponent<Galeshi::RigidBodyComponent>()) {
            auto& rb = sel.GetComponent<Galeshi::RigidBodyComponent>();
            if (beginDetailsSection(
                    "Rigid Body", 0,
                    "rigid body motion type static dynamic kinematic mass "
                    "gravity lock rotation rebuild physics")) {
                const char* motionNames[] = {"Static", "Dynamic", "Kinematic"};
                int motionType = static_cast<int>(rb.motionType);
                if (ImGui::Combo("Motion Type", &motionType, motionNames, 3)) {
                    rb.motionType =
                        static_cast<Galeshi::RigidBodyComponent::MotionType>(motionType);
                    RebuildPhysicsBody(sel);
                }

                bool changed = false;
                changed |= ImGui::DragFloat("Mass", &rb.mass, 0.05f, 0.001f, 100000.0f);
                changed |= ImGui::Checkbox("Use Gravity", &rb.useGravity);
                changed |= ImGui::Checkbox("Lock Rotation", &rb.lockRotation);
                if (changed) RebuildPhysicsBody(sel);

                ImGui::TextDisabled("Runtime body: %s",
                    rb.bodyId == 0xFFFFFFFFu ? "not created (created on Play)"
                                             : "active");
                if (ImGui::Button("Rebuild Physics Body")) {
                    RebuildPhysicsBody(sel);
                }
            }
        }

        if (auto sel = m_editor.Selected(&m_scene); sel) {
            bool isLight = sel.HasComponent<Galeshi::DirectionalLightComponent>() ||
                           sel.HasComponent<Galeshi::PointLightComponent>() ||
                           sel.HasComponent<Galeshi::SpotLightComponent>();
            bool isGround = false;
            if (sel.HasComponent<Galeshi::NameComponent>() && sel.GetComponent<Galeshi::NameComponent>().name == "Ground") {
                isGround = true;
            }
            const bool isLandscape =
                sel.HasComponent<Galeshi::LandscapeComponent>();

            if (!isLight && !isGround && !selectedIsCollisionShape) {
                if (!sel.HasComponent<Galeshi::AudioSourceComponent>()) {
                    if (!isLandscape &&
                        ImGui::Button("+ Add Audio Source")) {
                        sel.AddComponent<Galeshi::AudioSourceComponent>();
                    }
                } else {
                    auto& audio = sel.GetComponent<Galeshi::AudioSourceComponent>();
                    if (beginDetailsSection(
                            "Audio Source", 0,
                            "audio sound file drop wav mp3 bus autoplay loop "
                            "spatial volume pitch attenuation distance rolloff "
                            "doppler offset")) {
                        const auto openAudioPicker = [&]() {
                            m_audioPickerTarget = sel.Handle();
                            std::string start = "assets";
                            const std::string projectPath =
                                Galeshi::ProjectManager::GetProjectPath();
                            if (!projectPath.empty()) {
                                start = (std::filesystem::path(projectPath) /
                                         "assets").string();
                            }
                            m_audioPicker.Open(
                                start, "Select Sound", {".wav", ".mp3"});
                        };

                        const std::string soundLabel =
                            audio.soundPath.empty()
                                ? "Drop WAV/MP3 sound here"
                                : std::filesystem::path(audio.soundPath)
                                      .filename()
                                      .string();
                        const float browseWidth = 82.0f;
                        const float soundWidth = std::max(
                            120.0f,
                            ImGui::GetContentRegionAvail().x - browseWidth -
                                ImGui::GetStyle().ItemSpacing.x);
                        if (ImGui::Button(
                                soundLabel.c_str(),
                                ImVec2(soundWidth, 32.0f))) {
                            openAudioPicker();
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip(
                                "%s\nDrop a WAV/MP3 file from the Content "
                                "Browser, or click to browse.",
                                audio.soundPath.empty()
                                    ? "No sound assigned."
                                    : audio.soundPath.c_str());
                        }
                        if (ImGui::BeginDragDropTarget()) {
                            if (const ImGuiPayload* payload =
                                    ImGui::AcceptDragDropPayload(
                                        "GALESHI_ASSET_PATH")) {
                                const char* path =
                                    static_cast<const char*>(payload->Data);
                                if (path && IsAudioAssetPath(path)) {
                                    PushHistory();
                                    m_audio.StopSource(sel.Handle());
                                    audio.soundPath = m_assets
                                        ? m_assets->MakeRelativePath(path)
                                        : std::string(path);
                                    audio.playing = false;
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Browse...")) {
                            openAudioPicker();
                        }
                        if (!audio.soundPath.empty()) {
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Clear")) {
                                PushHistory();
                                m_audio.StopSource(sel.Handle());
                                audio.soundPath.clear();
                                audio.playing = false;
                            }
                        }

                        const char* busNames[Galeshi::kAudioBusCount] = {
                            "Master", "Music", "SFX", "Voice", "Ambient"};
                        int bus = static_cast<int>(audio.bus);
                        if (ImGui::Combo("Bus", &bus, busNames,
                                         Galeshi::kAudioBusCount)) {
                            audio.bus = static_cast<Galeshi::AudioBus>(bus);
                        }

                        ImGui::Checkbox("Autoplay", &audio.autoplay);
                        ImGui::Checkbox("Loop", &audio.loop);
                        ImGui::Checkbox("3D Spatialization", &audio.spatial);
                        ImGui::DragFloat("Volume", &audio.volume, 0.01f, 0.0f, 2.0f);
                        ImGui::DragFloat("Pitch", &audio.pitch, 0.01f, 0.05f, 4.0f);

                        if (audio.spatial) {
                            const char* attenuationNames[3] = {
                                "Inverse", "Linear", "Exponential"};
                            int attenuation = static_cast<int>(audio.attenuation);
                            if (ImGui::Combo("Attenuation", &attenuation,
                                             attenuationNames, 3)) {
                                audio.attenuation =
                                    static_cast<Galeshi::AudioAttenuationModel>(
                                        attenuation);
                            }
                            ImGui::DragFloat("Min Distance", &audio.minDistance,
                                             0.05f, 0.0f, 1000.0f);
                            ImGui::DragFloat("Max Distance", &audio.maxDistance,
                                             0.5f, 0.0f, 10000.0f);
                            ImGui::DragFloat("Rolloff", &audio.rolloff,
                                             0.01f, 0.01f, 10.0f);
                            ImGui::DragFloat("Doppler Factor", &audio.dopplerFactor,
                                             0.01f, 0.0f, 10.0f);
                            ImGui::DragFloat3("3D Offset",
                                              glm::value_ptr(audio.offset), 0.05f);
                        }

                        // World-space position of the sound for manual play.
                        glm::vec3 soundPosition(0.0f);
                        {
                            const glm::mat4 world = EntityWorldMatrix(sel.Handle());
                            glm::vec3 scale, translation, skew;
                            glm::vec4 perspective;
                            glm::quat rotation;
                            glm::decompose(world, scale, rotation, translation,
                                           skew, perspective);
                            soundPosition = translation + rotation * audio.offset;
                        }

                        if (ImGui::Button(audio.playing ? "Stop" : "Play")) {
                            if (audio.playing) {
                                m_audio.StopSource(sel.Handle());
                                audio.playing = false;
                            } else {
                                m_audio.PlaySource(sel.Handle(), audio,
                                                   soundPosition);
                                audio.playing =
                                    m_audio.IsSourcePlaying(sel.Handle());
                            }
                        }
                        ImGui::SameLine();
                        ImGui::Text("State: %s",
                                    audio.playing ? "playing" : "idle");
                        ImGui::SameLine();
                        ImGui::TextDisabled("(bus: %s)",
                                            Galeshi::AudioBusName(audio.bus));
                        if (ImGui::Button("Remove Audio Source")) {
                            m_audio.StopSource(sel.Handle());
                            sel.RemoveComponent<Galeshi::AudioSourceComponent>();
                        }
                    }
                }

                if (sel.HasComponent<Galeshi::AudioListenerComponent>()) {
                    auto& listener =
                        sel.GetComponent<Galeshi::AudioListenerComponent>();
                    if (beginDetailsSection(
                            "Audio Listener", 0,
                            "audio listener primary position orientation")) {
                        ImGui::Checkbox("Primary Listener", &listener.primary);
                        ImGui::TextDisabled(
                            "Uses this entity's world position and orientation.");
                        if (ImGui::Button("Remove Audio Listener")) {
                            sel.RemoveComponent<Galeshi::AudioListenerComponent>();
                        }
                    }
                } else if (!isLandscape &&
                           ImGui::Button("+ Add Audio Listener")) {
                    sel.AddComponent<Galeshi::AudioListenerComponent>();
                }
            }
        }

        if (selected[0].HasComponent<Galeshi::LandscapeComponent>() &&
            beginDetailsSection(
                "Landscape (Sculpting)", ImGuiTreeNodeFlags_DefaultOpen,
                "landscape sculpt brush raise lower smooth flatten noise terrace "
                "sharpen layer paint albedo orm normal texture blend trees grass "
                "radius strength falloff height terrain")) {
            auto& landscape = selected[0].GetComponent<Galeshi::LandscapeComponent>();
            const char* toolNames[] = {
                "Select", "Raise", "Lower", "Smooth", "Flatten", "Noise",
                "Terrace", "Sharpen", "Paint Landscape", "Paint Trees",
                "Paint Grass"
            };
            int currentTool = static_cast<int>(m_editor.toolMode);
            if (ImGui::Combo("Brush", &currentTool, toolNames, IM_ARRAYSIZE(toolNames))) {
                m_editor.toolMode = static_cast<Galeshi::EditorState::ToolMode>(currentTool);
            }
            ImGui::DragFloat("Brush Radius", &m_editor.brushRadius, 0.1f, 0.25f, 100.0f);
            ImGui::DragFloat("Brush Strength", &m_editor.brushStrength, 0.1f, 0.0f, 50.0f);
            ImGui::SliderFloat("Brush Falloff", &m_editor.brushFalloff, 0.0f, 1.0f);
            if (m_editor.toolMode == Galeshi::EditorState::ToolMode::Flatten) {
                ImGui::DragFloat("Flatten Height", &m_editor.flattenHeight, 0.1f, -500.0f, 500.0f);
                ImGui::SameLine();
                if (ImGui::Button("Use Cursor Height")) {
                    m_editor.flattenHeight = m_lastSculptHeight;
                }
            }
            if (m_editor.toolMode == Galeshi::EditorState::ToolMode::Noise) {
                ImGui::DragFloat("Noise Frequency", &m_editor.noiseFrequency, 0.005f, 0.01f, 1.0f);
            }
            if (m_editor.toolMode == Galeshi::EditorState::ToolMode::Terrace) {
                ImGui::DragFloat("Terrace Step", &m_editor.terraceStep,
                                 0.05f, 0.05f, 100.0f);
            }
            ImGui::Separator();
            ImGui::Text("Landscape Layers");
            ImGui::TextDisabled(
                "Paint Landscape paints the active layer. Baked albedo, ORM "
                "and normal maps are written to assets/textures/.");

            const size_t expectedVertices =
                static_cast<size_t>(landscape.sizeX) *
                static_cast<size_t>(landscape.sizeZ);
            // Migrate the previous single-layer material fields into the new
            // layer list the first time this landscape is inspected.
            if (landscape.layers.empty() &&
                !landscape.legacyLayerMigrated) {
                auto* legacyMaterial =
                    reg.try_get<Galeshi::MaterialComponent>(
                        selectedHandle);
                if (legacyMaterial && legacyMaterial->material) {
                    auto& legacy = *legacyMaterial->material;
                    const bool hasLegacyWeights =
                        landscape.paintWeights.size() ==
                            expectedVertices &&
                        std::any_of(
                            landscape.paintWeights.begin(),
                            landscape.paintWeights.end(),
                            [](float value) { return value > 1e-6f; });
                    if (legacy.landscapeAlbedoMap ||
                        legacy.landscapeOrmMap ||
                        legacy.landscapeNormalMap ||
                        hasLegacyWeights) {
                        Galeshi::LandscapeLayer layer;
                        layer.albedoMap = legacy.landscapeAlbedoMap;
                        layer.ormMap = legacy.landscapeOrmMap;
                        layer.normalMap = legacy.landscapeNormalMap;
                        layer.albedoSrgb = legacy.landscapeAlbedoSrgb;
                        layer.ormSrgb = legacy.landscapeOrmSrgb;
                        layer.normalSrgb = legacy.landscapeNormalSrgb;
                        landscape.layers.push_back(std::move(layer));
                        landscape.legacyLayerMigrated = true;
                        landscape.layerWeights.emplace_back(
                            expectedVertices, 0.0f);
                        if (landscape.paintWeights.size() ==
                            expectedVertices) {
                            landscape.layerWeights[0] =
                                landscape.paintWeights;
                        }
                    }
                }
            }
            if (landscape.layerWeights.size() != landscape.layers.size()) {
                landscape.layerWeights.resize(landscape.layers.size());
            }
            for (auto& weights : landscape.layerWeights) {
                if (weights.size() != expectedVertices) {
                    weights.resize(expectedVertices, 0.0f);
                }
            }
            if (landscape.layers.empty()) {
                landscape.activeLayer = 0;
            } else {
                landscape.activeLayer = std::clamp(
                    landscape.activeLayer, 0,
                    static_cast<int>(landscape.layers.size()) - 1);
            }

            if (ImGui::Button("+ Add Layer")) {
                PushHistory();
                landscape.layers.push_back(Galeshi::LandscapeLayer{});
                landscape.layerWeights.emplace_back(expectedVertices, 0.0f);
                landscape.activeLayer =
                    static_cast<int>(landscape.layers.size()) - 1;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%zu layer(s)", landscape.layers.size());

            int removeLayer = -1;
            for (size_t i = 0; i < landscape.layers.size(); ++i) {
                auto& layer = landscape.layers[i];
                ImGui::PushID(static_cast<int>(i));
                ImGui::Separator();
                const ImGuiTreeNodeFlags layerFlags =
                    i == 0 ? ImGuiTreeNodeFlags_DefaultOpen
                           : ImGuiTreeNodeFlags_None;
                if (ImGui::CollapsingHeader(
                        ("Layer " + std::to_string(i + 1)).c_str(),
                        layerFlags)) {
                    if (ImGui::RadioButton(
                            "Active paint layer",
                            landscape.activeLayer ==
                                static_cast<int>(i))) {
                        landscape.activeLayer = static_cast<int>(i);
                    }
                    DrawTextureSlot("Albedo", layer.albedoMap, true,
                                    &layer.albedoSrgb);
                    DrawTextureSlot("ORM", layer.ormMap, false,
                                    &layer.ormSrgb);
                    DrawTextureSlot("Normal", layer.normalMap, false,
                                    &layer.normalSrgb);
                    if (ImGui::Button("- Remove Layer")) {
                        removeLayer = static_cast<int>(i);
                    }
                }
                ImGui::PopID();
            }
            if (removeLayer >= 0) {
                PushHistory();
                landscape.layers.erase(
                    landscape.layers.begin() + removeLayer);
                if (landscape.layerWeights.size() ==
                    landscape.layers.size() + 1) {
                    landscape.layerWeights.erase(
                        landscape.layerWeights.begin() + removeLayer);
                }
                if (removeLayer < landscape.activeLayer) {
                    --landscape.activeLayer;
                }
                if (landscape.layers.empty()) {
                    landscape.activeLayer = 0;
                } else {
                    landscape.activeLayer = std::clamp(
                        landscape.activeLayer, 0,
                        static_cast<int>(landscape.layers.size()) - 1);
                }
            }

            if (ImGui::Button("Bake Layer Textures")) {
                PushHistory();
                BakeLandscapeTextures(selected[0]);
            }
            ImGui::SameLine();
            ImGui::TextDisabled(
                "Suggested: paint a stroke to bake automatically.");
            ImGui::TextDisabled("%d x %d vertices, scale %.2f", landscape.sizeX, landscape.sizeZ, landscape.scale);
            if (m_editor.toolMode == Galeshi::EditorState::ToolMode::Select) {
                ImGui::TextDisabled("Select a brush, then left-click and drag in the viewport.");
            } else if (m_editor.toolMode ==
                           Galeshi::EditorState::ToolMode::PaintLandscape ||
                       m_editor.toolMode ==
                           Galeshi::EditorState::ToolMode::PaintTrees ||
                       m_editor.toolMode ==
                           Galeshi::EditorState::ToolMode::PaintGrass) {
                ImGui::TextColored(
                    ImVec4(0.5f, 1.0f, 0.5f, 1.0f),
                    "%s with brush radius %.2f",
                    toolNames[currentTool], m_editor.brushRadius);
            } else {
                ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f),
                                   "Sculpting with %s brush", toolNames[currentTool]);
            }
            if (ImGui::Button("Reset Heights")) {
                PushHistory();
                Galeshi::LandscapeMesh::ResetHeights(landscape);
                if (selected[0].HasComponent<Galeshi::MeshComponent>()) {
                    auto& meshComponent = selected[0].GetComponent<Galeshi::MeshComponent>();
                    if (meshComponent.mesh) {
                        Galeshi::LandscapeMesh::UpdateMesh(landscape, *meshComponent.mesh);
                        Galeshi::LandscapeMesh::UpdatePaintWeights(
                            landscape, *meshComponent.mesh);
                    }
                }
                if (selected[0].HasComponent<Galeshi::VegetationComponent>()) {
                    selected[0].GetComponent<Galeshi::VegetationComponent>().dirty = true;
                }
            }
            ImGui::SameLine();
            if (!selected[0].HasComponent<Galeshi::VegetationComponent>()) {
                if (ImGui::Button("Add Vegetation")) {
                    PushHistory();
                    auto& vegetation = selected[0].AddComponent<Galeshi::VegetationComponent>();
                    Galeshi::VegetationGenerator::Generate(landscape, vegetation);
                }
            } else {
                ImGui::TextDisabled("Vegetation component is attached");
            }
        }

        if (selected[0].HasComponent<Galeshi::VegetationComponent>() &&
            beginDetailsSection(
                "Vegetation (Instanced)", ImGuiTreeNodeFlags_DefaultOpen,
                "vegetation trees grass instanced density scale slope seed "
                "colors models painting shadows leaf leaves foliage textures "
                "albedo orm normal")) {
            auto& veg = selected[0].GetComponent<Galeshi::VegetationComponent>();
            bool changed = false;
            changed |= ImGui::Checkbox("Enabled", &veg.enabled);
            ImGui::SameLine();
            changed |= ImGui::Checkbox("Tree Shadows", &veg.castShadows);
            ImGui::SameLine();
            changed |= ImGui::Checkbox("Grass Shadows", &veg.grassCastShadows);
            ImGui::SetNextItemWidth(170.0f);
            changed |= ImGui::DragInt("Trees", &veg.treeCount, 1.0f, 0, 20000);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Base generated tree count. Each tree species scales its "
                    "count by its Density.");
            }
            ImGui::SetNextItemWidth(170.0f);
            changed |= ImGui::DragInt("Grass", &veg.grassCount, 10.0f, 0, 50000);
            changed |= ImGui::DragFloatRange2("Tree Scale", &veg.treeMinScale, &veg.treeMaxScale,
                                              0.01f, 0.05f, 10.0f, "Min %.2f", "Max %.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Scale range for default procedural trees. Custom tree "
                    "types use their own Size.");
            }
            changed |= ImGui::DragFloatRange2("Grass Scale", &veg.grassMinScale, &veg.grassMaxScale,
                                              0.01f, 0.05f, 10.0f, "Min %.2f", "Max %.2f");
            changed |= ImGui::DragFloat("Slope Limit", &veg.slopeLimit, 0.01f, 0.0f, 5.0f);
            changed |= ImGui::InputScalar("Seed", ImGuiDataType_U32, &veg.seed);
            changed |= ImGui::ColorEdit4("Trunk Color", glm::value_ptr(veg.trunkColor));
            changed |= ImGui::ColorEdit4("Canopy Color", glm::value_ptr(veg.canopyColor));
            changed |= ImGui::ColorEdit4("Grass Color", glm::value_ptr(veg.grassColor));

            ImGui::Separator();
            ImGui::Text("Tree Models");
            changed |= ImGui::Checkbox("Hide Default Instances",
                                       &veg.hideDefaultInstances);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Hide generated procedural trees and grass. Authored tree "
                    "species and painted vegetation stay visible.");
            }

            const float dropWidth = ImGui::GetContentRegionAvail().x;
            for (int typeIndex = 0;
                 typeIndex < static_cast<int>(veg.treeTypes.size());
                 ++typeIndex) {
                auto& type = veg.treeTypes[static_cast<size_t>(typeIndex)];
                EnsureTreeTypeMaterial(type, typeIndex);
                ImGui::PushID(typeIndex);
                ImGui::Separator();
                ImGui::Text("Tree Type %d", typeIndex + 1);
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove")) {
                    PushHistory();
                    veg.treeTypes.erase(veg.treeTypes.begin() + typeIndex);
                    if (m_paintTreeType >= typeIndex) --m_paintTreeType;
                    changed = true;
                    ImGui::PopID();
                    break;
                }

                ImGui::InvisibleButton("##tree_type_drop",
                                       ImVec2(dropWidth, 44.0f));
                const ImVec2 dropMin = ImGui::GetItemRectMin();
                const ImVec2 dropMax = ImGui::GetItemRectMax();
                const bool dropHovered = ImGui::IsItemHovered();
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                drawList->AddRectFilled(
                    dropMin, dropMax,
                    dropHovered ? IM_COL32(44, 48, 56, 255)
                                : IM_COL32(28, 30, 34, 255), 4.0f);
                drawList->AddRect(
                    dropMin, dropMax,
                    dropHovered ? IM_COL32(120, 160, 220, 255)
                                : IM_COL32(80, 84, 92, 255), 4.0f);
                if (type.meshPath.empty()) {
                    drawList->AddText(
                        ImVec2(dropMin.x + 10.0f, dropMin.y + 7.0f),
                        IM_COL32(200, 205, 215, 255),
                        "Drop a tree model here");
                    drawList->AddText(
                        ImVec2(dropMin.x + 10.0f, dropMin.y + 25.0f),
                        IM_COL32(130, 135, 145, 255),
                        "glTF / GLB / OBJ / DAE");
                } else {
                    const ImVec4 clipRect(dropMin.x, dropMin.y, dropMax.x,
                                          dropMax.y);
                    drawList->AddText(
                        ImGui::GetFont(), ImGui::GetFontSize(),
                        ImVec2(dropMin.x + 10.0f, dropMin.y + 7.0f),
                        IM_COL32(220, 225, 235, 255), type.meshPath.c_str(),
                        nullptr, dropWidth - 20.0f, &clipRect);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Drag a model from the Content Browser or click to choose.\n"
                        "The model keeps its native size; use Size for scaling.");
                }
                if (ImGui::IsItemClicked()) {
                    m_vegetationMeshTarget = selected[0].Handle();
                    m_vegetationMeshTypeIndex = typeIndex;
                    std::string start = "assets";
                    const std::string projectPath =
                        Galeshi::ProjectManager::GetProjectPath();
                    if (!projectPath.empty()) {
                        start = (std::filesystem::path(projectPath) /
                                 "assets").string();
                    }
                    m_vegetationMeshPicker.Open(
                        start, "Select Tree Model",
                        {".gltf", ".glb", ".obj", ".dae"});
                }
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload =
                            ImGui::AcceptDragDropPayload(
                                "GALESHI_ASSET_PATH")) {
                        const char* path =
                            static_cast<const char*>(payload->Data);
                        const std::string candidate = path ? path : "";
                        if (IsDragDropModelAssetPath(candidate)) {
                            AssignLandscapeTreeModel(
                                selected[0], candidate, typeIndex);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                ImGui::SetNextItemWidth(160.0f);
                if (ImGui::DragFloat("Density", &type.density,
                                     0.01f, 0.0f, 100.0f, "%.2f")) {
                    changed = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Scales this species' generated tree count. With "
                        "multiple species, 1.0 gives each an equal share of "
                        "the base Trees count.");
                }
                ImGui::SetNextItemWidth(160.0f);
                if (ImGui::DragFloat("Size", &type.scale, 0.001f,
                                     0.001f, 100.0f, "%.3f")) {
                    changed = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Multiplier of the model's native size. Dropped models "
                        "start at 0.010.");
                }

                // Each authored tree species owns its own texture row. Leave
                // the Landscape material texture row above untouched; this
                // override is only used when at least one slot is assigned.
                if (ImGui::TreeNodeEx("Whole Tree Textures",
                                      ImGuiTreeNodeFlags_None)) {
                    ImGui::TextDisabled(
                        "Drag textures from the Content Browser. Empty slots "
                        "use the tree model's imported materials.");
                    if (type.material) {
                        DrawTextureSlot("Albedo Map",
                                        type.material->albedoMap, true,
                                        &type.material->albedoSrgb);
                        DrawTextureSlot("Normal Map",
                                        type.material->normalMap, false,
                                        &type.material->normalSrgb);
                        DrawTextureSlot("Metallic/Roughness",
                                        type.material->metallicRoughnessMap,
                                        false,
                                        &type.material->metallicRoughnessSrgb);
                        DrawTextureSlot("Metallic Map",
                                        type.material->metallicMap, false,
                                        &type.material->metallicSrgb);
                        DrawTextureSlot("Roughness Map",
                                        type.material->roughnessMap, false,
                                        &type.material->roughnessSrgb);
                        DrawTextureSlot("Emissive Map",
                                        type.material->emissiveMap, true,
                                        &type.material->emissiveSrgb);
                        DrawTextureSlot("Occlusion Map",
                                        type.material->occlusionMap, false,
                                        &type.material->occlusionSrgb);
                        DrawTextureSlot("Opacity Map",
                                        type.material->opacityMap, false,
                                        &type.material->opacitySrgb);
                        DrawTextureSlot("Specular Map",
                                        type.material->specularMap, false,
                                        &type.material->specularSrgb);
                    }
                    ImGui::TreePop();
                }

                if (ImGui::TreeNodeEx(
                        "Leaf Textures",
                        ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::TextDisabled(
                        "Add one set per leaf mesh. Each set overrides only "
                        "its target mesh; the trunk keeps the model material.");
                    Galeshi::AssetManager::LoadedModel treeModel;
                    if (!type.meshPath.empty()) {
                        treeModel = m_assets->LoadModel(type.meshPath);
                    }
                    if (type.meshPath.empty()) {
                        ImGui::TextDisabled(
                            "Assign a tree model first, then add leaf sets.");
                    } else if (treeModel.meshes.empty()) {
                        ImGui::TextDisabled(
                            "The tree model has no usable meshes.");
                    } else {
                        std::vector<std::string> meshLabels;
                        meshLabels.reserve(treeModel.meshes.size());
                        for (int meshIndex = 0;
                             meshIndex <
                             static_cast<int>(treeModel.meshes.size());
                             ++meshIndex) {
                            meshLabels.push_back(
                                TreeMeshLabel(treeModel, meshIndex));
                        }

                        int removeLeaf = -1;
                        for (int leafIndex = 0;
                             leafIndex <
                             static_cast<int>(type.leafSets.size());
                             ++leafIndex) {
                            auto& leaf = type.leafSets[
                                static_cast<size_t>(leafIndex)];
                            EnsureLeafMaterial(leaf, typeIndex, leafIndex);
                            ImGui::PushID(leafIndex);
                            ImGui::Separator();
                            ImGui::Text("Leaf %d", leafIndex + 1);
                            ImGui::SameLine();
                            if (ImGui::SmallButton("- Remove Leaf")) {
                                removeLeaf = leafIndex;
                            }

                            const char* preview =
                                leaf.meshIndex >= 0 &&
                                    leaf.meshIndex <
                                        static_cast<int>(
                                            meshLabels.size())
                                    ? meshLabels[static_cast<size_t>(
                                          leaf.meshIndex)].c_str()
                                    : "Select leaf mesh";
                            ImGui::SetNextItemWidth(260.0f);
                            if (ImGui::BeginCombo("Leaf Mesh", preview)) {
                                for (int meshIndex = 0;
                                     meshIndex <
                                     static_cast<int>(meshLabels.size());
                                     ++meshIndex) {
                                    const bool isSelected =
                                        leaf.meshIndex == meshIndex;
                                    ImGui::PushID(meshIndex);
                                    if (ImGui::Selectable(
                                            meshLabels[static_cast<size_t>(
                                                meshIndex)].c_str(),
                                            isSelected)) {
                                        PushHistory();
                                        leaf.meshIndex = meshIndex;
                                    }
                                    if (isSelected) {
                                        ImGui::SetItemDefaultFocus();
                                    }
                                    ImGui::PopID();
                                }
                                ImGui::EndCombo();
                            }
                            if (leaf.meshIndex < 0 ||
                                leaf.meshIndex >=
                                    static_cast<int>(meshLabels.size())) {
                                ImGui::TextColored(
                                    ImVec4(1.0f, 0.7f, 0.35f, 1.0f),
                                    "Choose the imported mesh this leaf "
                                    "material replaces.");
                            } else {
                                for (int other = 0;
                                     other <
                                     static_cast<int>(
                                         type.leafSets.size());
                                     ++other) {
                                    if (other == leafIndex) continue;
                                    if (type.leafSets[static_cast<size_t>(
                                            other)].meshIndex ==
                                        leaf.meshIndex) {
                                        ImGui::TextColored(
                                            ImVec4(1.0f, 0.7f, 0.35f, 1.0f),
                                            "Mesh also used by Leaf %d; "
                                            "the first matching set wins.",
                                            other + 1);
                                        break;
                                    }
                                }
                            }

                            if (leaf.material) {
                                DrawTextureSlot(
                                    "Albedo",
                                    leaf.material->albedoMap, true,
                                    &leaf.material->albedoSrgb);
                                DrawTextureSlot(
                                    "ORM",
                                    leaf.material->metallicRoughnessMap,
                                    false,
                                    &leaf.material->
                                        metallicRoughnessSrgb);
                                // ORM is authored as one texture: R=AO,
                                // G=roughness, B=metallic. Bind it to both
                                // material slots the PBR shader reads.
                                leaf.material->occlusionMap =
                                    leaf.material->metallicRoughnessMap;
                                leaf.material->occlusionSrgb =
                                    leaf.material->metallicRoughnessSrgb;
                                if (leaf.material->metallicRoughnessMap) {
                                    leaf.material->metallicFactor = 1.0f;
                                    leaf.material->roughnessFactor = 1.0f;
                                } else {
                                    leaf.material->metallicFactor = 0.0f;
                                    leaf.material->roughnessFactor = 1.0f;
                                }
                                DrawTextureSlot(
                                    "Normal",
                                    leaf.material->normalMap, false,
                                    &leaf.material->normalSrgb);
                                ImGui::TextDisabled(
                                    "ORM: R occlusion, G roughness, "
                                    "B metallic.");

                                bool twoSided =
                                    !leaf.material->backfaceCulling;
                                if (ImGui::Checkbox(
                                        "Two Sided", &twoSided)) {
                                    PushHistory();
                                    leaf.material->twoSided = twoSided;
                                    leaf.material->backfaceCulling =
                                        !twoSided;
                                }
                                bool alphaCutout =
                                    leaf.material->alphaTest;
                                if (ImGui::Checkbox(
                                        "Alpha Cutout",
                                        &alphaCutout)) {
                                    PushHistory();
                                    leaf.material->alphaTest =
                                        alphaCutout;
                                }
                                if (leaf.material->alphaTest) {
                                    ImGui::SetNextItemWidth(160.0f);
                                    ImGui::SliderFloat(
                                        "Alpha Cutoff",
                                        &leaf.material->alphaCutoff,
                                        0.0f, 1.0f);
                                }
                            }
                            ImGui::PopID();
                        }
                        if (removeLeaf >= 0) {
                            PushHistory();
                            type.leafSets.erase(
                                type.leafSets.begin() + removeLeaf);
                        }
                        if (ImGui::Button("+ Add Leaf")) {
                            PushHistory();
                            Galeshi::VegetationLeafSet leaf;
                            leaf.meshIndex = SuggestedLeafMeshIndex(
                                treeModel, type.leafSets);
                            EnsureLeafMaterial(
                                leaf, typeIndex,
                                static_cast<int>(type.leafSets.size()));
                            type.leafSets.push_back(std::move(leaf));
                        }
                        if (type.leafSets.empty()) {
                            ImGui::SameLine();
                            ImGui::TextDisabled(
                                "No leaf mesh overrides.");
                        }
                    }
                    ImGui::TreePop();
                }

                ImGui::TextDisabled("Painted: %zu",
                                    type.paintedInstances.size());
                ImGui::PopID();
            }

            if (ImGui::Button("+ Add Tree Type")) {
                PushHistory();
                veg.treeTypes.push_back(Galeshi::VegetationTreeType{});
                const int newIndex =
                    static_cast<int>(veg.treeTypes.size()) - 1;
                EnsureTreeTypeMaterial(veg.treeTypes.back(), newIndex);
                m_paintTreeType = newIndex;
                changed = true;
            }
            if (veg.treeTypes.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("Empty = procedural trees");
            }

            ImGui::Separator();
            ImGui::Text("Painting");
            {
                std::vector<std::string> paintLabelStorage;
                paintLabelStorage.push_back("Default Procedural");
                for (const auto& type : veg.treeTypes) {
                    paintLabelStorage.push_back(
                        type.meshPath.empty()
                            ? "Tree Type"
                            : std::filesystem::path(type.meshPath)
                                  .stem().string());
                }
                std::vector<const char*> paintLabels;
                paintLabels.reserve(paintLabelStorage.size());
                for (const auto& label : paintLabelStorage)
                    paintLabels.push_back(label.c_str());

                int paintSelection = m_paintTreeType + 1;
                if (paintSelection < 0) paintSelection = 0;
                if (paintSelection >= static_cast<int>(paintLabels.size()))
                    paintSelection = 0;
                ImGui::SetNextItemWidth(220.0f);
                if (ImGui::Combo("Paint Tree Type", &paintSelection,
                                 paintLabels.data(),
                                 static_cast<int>(paintLabels.size()))) {
                    m_paintTreeType = paintSelection - 1;
                }
            }
            ImGui::DragFloat("Paint Density", &veg.paintDensity,
                             0.05f, 0.0f, 10.0f);
            ImGui::DragFloat("Paint Size", &veg.paintSize,
                             0.05f, 0.05f, 20.0f);
            if (ImGui::Button("Paint Trees")) {
                m_editor.toolMode = Galeshi::EditorState::ToolMode::PaintTrees;
                m_editor.Select(selected[0]);
                m_wasSculpting = false;
                m_paintStampValid = false;
                // When a landscape already has authored species, start with
                // the first one instead of silently painting procedural trees.
                if (!veg.treeTypes.empty() &&
                    (m_paintTreeType < 0 ||
                     m_paintTreeType >= static_cast<int>(veg.treeTypes.size()))) {
                    m_paintTreeType = 0;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Paint Grass")) {
                m_editor.toolMode = Galeshi::EditorState::ToolMode::PaintGrass;
                m_editor.Select(selected[0]);
                m_wasSculpting = false;
                m_paintStampValid = false;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("Use Brush Radius in Landscape Sculpting");

            if (changed) veg.dirty = true;
            const bool hasLandscape = selected[0].HasComponent<Galeshi::LandscapeComponent>();
            if (!hasLandscape) ImGui::BeginDisabled();
            if (ImGui::Button("Regenerate")) {
                PushHistory();
                Galeshi::VegetationGenerator::Generate(
                    selected[0].GetComponent<Galeshi::LandscapeComponent>(), veg);
            }
            if (!hasLandscape) ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Clear Instances")) {
                PushHistory();
                Galeshi::VegetationGenerator::Clear(veg);
            }
            ImGui::SameLine();
            size_t treeTotal = veg.treeInstances.size() +
                veg.paintedTreeInstances.size();
            size_t grassTotal = veg.grassInstances.size() +
                veg.paintedGrassInstances.size();
            for (const auto& type : veg.treeTypes) {
                treeTotal += type.generatedInstances.size() +
                             type.paintedInstances.size();
            }
            ImGui::TextDisabled("%zu trees / %zu grass", treeTotal, grassTotal);
        }

        auto materialUserCount = [&](const Galeshi::Material* material) {
            size_t count = 0;
            auto view = m_scene.Registry().view<Galeshi::MaterialComponent>();
            for (auto handle : view) {
                if (view.get<Galeshi::MaterialComponent>(handle).material.get() == material) {
                    ++count;
                }
            }
            return count;
        };

        // Every renderable mesh owns a material slot, even when the imported
        // asset provided an unnamed material without textures. Create a
        // placeholder slot on demand so Details never silently drops it.
        auto ensureMaterialSlot =
            [&](Galeshi::Entity entity) -> Galeshi::MaterialComponent* {
                if (!entity) return nullptr;
                auto& registry = m_scene.Registry();
                Galeshi::MaterialComponent* slot = nullptr;
                if (registry.all_of<Galeshi::MaterialComponent>(entity.Handle())) {
                    slot = &registry.get<Galeshi::MaterialComponent>(entity.Handle());
                } else {
                    if (!registry.all_of<Galeshi::MeshComponent>(entity.Handle()))
                        return nullptr;
                    slot = &registry.emplace<Galeshi::MaterialComponent>(
                        entity.Handle());
                }
                if (!slot->material) {
                    slot->material = std::make_shared<Galeshi::Material>();
                }
                // Do not synthesize Material::name here. The displayed label
                // falls back to the entity/object name, while the serialized
                // material name remains exactly the exported one.
                return slot;
            };

        auto materialStateHash = [](const Galeshi::Material& mat) {
            size_t h = 1469598103934665603ull;
            auto addHash = [&](size_t value) {
                h ^= value + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
                h *= 1099511628211ull;
            };
            auto addFloat = [&](float value) {
                addHash(std::hash<float>{}(value));
            };
            auto addVec3 = [&](const glm::vec3& value) {
                addFloat(value.x); addFloat(value.y); addFloat(value.z);
            };
            auto addVec4 = [&](const glm::vec4& value) {
                addFloat(value.x); addFloat(value.y);
                addFloat(value.z); addFloat(value.w);
            };
            auto addMap = [&](const std::shared_ptr<Galeshi::Texture>& map) {
                addHash(reinterpret_cast<uintptr_t>(map.get()));
            };

            addHash(std::hash<std::string>{}(mat.name));
            addHash(std::hash<std::string>{}(mat.sourcePath));
            addVec4(mat.albedoFactor);
            addHash(mat.albedoSrgb);
            addHash(mat.metallicRoughnessSrgb);
            addHash(mat.metallicSrgb);
            addHash(mat.roughnessSrgb);
            addHash(mat.normalSrgb);
            addHash(mat.emissiveSrgb);
            addHash(mat.occlusionSrgb);
            addHash(mat.opacitySrgb);
            addHash(mat.specularSrgb);
            addHash(mat.landscapeAlbedoSrgb);
            addHash(mat.landscapeOrmSrgb);
            addHash(mat.landscapeNormalSrgb);
            addHash(mat.landscapeBlend);
            addFloat(mat.metallicFactor);
            addFloat(mat.specularFactor);
            addFloat(mat.roughnessFactor);
            addVec3(mat.emissiveFactor);
            addHash(mat.isGlass);
            addHash(mat.twoSided);
            addHash(mat.backfaceCulling);
            addFloat(mat.glassOpacity);
            addFloat(mat.normalStrength);
            addFloat(mat.occlusionStrength);
            addFloat(mat.alphaCutoff);
            addHash(mat.alphaTest);
            addHash(mat.alphaBlend);
            addHash(mat.subsurfaceEnabled);
            addVec3(mat.subsurfaceColor);
            addFloat(mat.subsurfaceStrength);
            addFloat(mat.subsurfaceRadius);
            addHash(mat.translucencyEnabled);
            addVec3(mat.translucencyColor);
            addFloat(mat.translucencyOpacity);
            addFloat(mat.translucencyStrength);
            addFloat(mat.translucencyThickness);
            addFloat(mat.translucencyDistortion);
            addFloat(mat.translucencyShadowDepth);
            addHash(mat.castsShadow);
            addFloat(mat.shadowDepthOffset);
            addMap(mat.albedoMap);
            addMap(mat.metallicRoughnessMap);
            addMap(mat.metallicMap);
            addMap(mat.roughnessMap);
            addMap(mat.normalMap);
            addMap(mat.emissiveMap);
            addMap(mat.occlusionMap);
            addMap(mat.opacityMap);
            addMap(mat.specularMap);
            addMap(mat.landscapeAlbedoMap);
            addMap(mat.landscapeOrmMap);
            addMap(mat.landscapeNormalMap);
            addHash(reinterpret_cast<uintptr_t>(mat.base.get()));
            return h;
        };

        // Helper lambda to draw material UI safely. During a Details search
        // the header stays visible when the material name or any material
        // property matches; inside, only matching parameter groups remain.
        auto DrawMaterialUI = [&](const char* label, Galeshi::MaterialComponent& matc, uint32_t eid) {
            if (!matc.material) return;
            auto& m = *matc.material;
            const size_t materialStateBefore = materialStateHash(m);
            ImGui::PushID(eid);
            const std::string materialKeywords =
                std::string("material instance base used "
                            "albedo factor albedo sRGB color "
                            "space metallic factor roughness factor normal "
                            "strength occlusion strength "
                            "alpha cutoff alpha test albedo alpha mask alpha blend transparency hair "
                            "glass transparent opacity two sided "
                            "emissive color subsurface scattering sss enable "
                            "subsurface subsurface color subsurface strength "
                            "subsurface radius translucency enable translucency "
                            "translucency color translucency opacity "
                            "translucency strength translucency thickness "
                            "translucency distortion shadows cast shadows shadow "
                            "sharpness shadow depth offset texture maps albedo "
                            "map normal map metallic roughness map metallic map "
                            "roughness map emissive map occlusion map opacity map "
                            "specular map material name") +
                " " + (label ? label : "") + " " + m.name +
                (m.base ? " " + m.base->name : std::string{});
            bool materialChanged = false;
            if (beginDetailsSection(label, ImGuiTreeNodeFlags_DefaultOpen,
                                    materialKeywords.c_str())) {
                const bool showAllMaterialFields =
                    !searchActive || searchContains(label) ||
                    searchContains(m.name.c_str()) ||
                    searchContains("material");
                auto showMaterialField = [&](const char* field,
                                             const char* aliases = "") {
                    if (showAllMaterialFields) return true;
                    return lowerAscii(std::string(field ? field : "") + " " +
                                      (aliases ? aliases : ""))
                               .find(searchLower) != std::string::npos;
                };
                auto showMaterialGroup = [&](const char* title,
                                             const char* members) {
                    return showAllMaterialFields || showMaterialField(title, members);
                };

                // Two Sided and Backface Culling are independent per-material
                // settings; editing one must never change the other.
                ImGui::Checkbox("Two Sided", &m.twoSided);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Flip normals on back-facing fragments for two-sided "
                        "shading.");
                }
                ImGui::SameLine();
                ImGui::Checkbox("Backface Culling", &m.backfaceCulling);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Cull faces pointing away from the camera. This is "
                        "independent of Two Sided.");
                }
                ImGui::Separator();

                const size_t userCount = materialUserCount(&m);
                if (showMaterialGroup(
                        "Material Identity",
                        "name instance base used reset create instance")) {
                if (m.base) {
                    ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f),
                        "Instance of: %s", m.base->name.c_str());
                    if (ImGui::Button("Reset to Base")) {
                        const auto base = m.base;
                        const std::string instanceName = m.name;
                        m = *base;
                        m.base = base;
                        m.name = instanceName;
                        m.sceneOverride = true;
                        matc.isGlass = m.isGlass;
                        PushHistory();
                        ImGui::PopID();
                        return;
                    }
                } else {
                    ImGui::TextDisabled("Base material");
                }
                ImGui::SameLine();
                ImGui::TextDisabled("| Used by %zu object%s", userCount, userCount == 1 ? "" : "s");
                if (userCount > 1) {
                    if (ImGui::Button("Create Instance")) {
                        auto base = matc.material;
                        auto instance = std::make_shared<Galeshi::Material>(*base);
                        instance->base = base;
                        instance->name = base->name.empty()
                            ? std::string("Material Instance")
                            : base->name + " Instance";
                        matc.material = instance;
                        matc.material->sceneOverride = true;
                        matc.isGlass = instance->isGlass;
                        PushHistory();
                        ImGui::PopID();
                        return;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Create an independent material instance.\n"
                                          "Texture maps are shared, parameters can be edited separately.");
                    }
                }

                char nameBuffer[128];
                std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", m.name.c_str());
                if (ImGui::InputText("Material Name", nameBuffer, sizeof(nameBuffer))) {
                    m.name = nameBuffer;
                }
                }

                if (showMaterialGroup(
                        "Main Parameters",
                        "albedo color metallic specular reflection roughness "
                        "normal occlusion alpha cutoff glass transparent opacity "
                        "two sided emissive")) {
                ImGui::Separator();

                if (showMaterialField("Albedo Factor", "albedo color base"))
                ImGui::ColorEdit4("Albedo Factor", glm::value_ptr(m.albedoFactor));
                if (showMaterialField("Metallic Factor", "metal metallic"))
                ImGui::SliderFloat("Metallic Factor",  &m.metallicFactor,  0.0f, 1.0f);
                if (showMaterialField("Specular Factor", "specular reflection"))
                ImGui::SliderFloat("Specular Factor", &m.specularFactor, 0.0f, 1.0f);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Scales the dielectric specular response. The optional "
                        "specular map multiplies this value.");
                }
                if (showMaterialField("Roughness Factor", "rough"))
                ImGui::SliderFloat("Roughness Factor", &m.roughnessFactor, 0.04f, 1.0f);
                if (showMaterialField("Normal Strength", "normal bump"))
                ImGui::SliderFloat("Normal Strength",  &m.normalStrength,  0.0f, 2.0f);
                if (showMaterialField("Occlusion Strength", "occlusion ao"))
                ImGui::SliderFloat("Occlusion Strength", &m.occlusionStrength, 0.0f, 1.0f);
                if (showMaterialField("Alpha Test", "alpha test cutoff mask albedo")) {
                    ImGui::Checkbox("Alpha Test (albedo alpha)", &m.alphaTest);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "Discard fragments using the albedo texture alpha.\n"
                            "Used by glTF MASK materials and nearly opaque cutouts.");
                    }
                }
                if (showMaterialField("Alpha Blend", "alpha blend transparency hair")) {
                    ImGui::Checkbox("Alpha Blend (transparent pass)", &m.alphaBlend);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "Render in the sorted alpha-blended forward pass.\n"
                            "Used by glTF BLEND cards such as brows and eyelashes.");
                    }
                }
                if (showMaterialField("Alpha Cutoff", "alpha cutoff mask"))
                ImGui::SliderFloat("Alpha Cutoff",     &m.alphaCutoff,     0.0f, 1.0f);
                if (showMaterialField("Glass (Transparent)", "glass transparent"))
                ImGui::Checkbox("Glass (Transparent)", &m.isGlass);
                if (showMaterialField("Glass Opacity", "glass opacity transparent") && m.isGlass) {
                    ImGui::SliderFloat("Glass Opacity", &m.glassOpacity, 0.0f, 1.0f);
                }
                if (showMaterialField("Emissive Color", "emissive emission"))
                ImGui::ColorEdit3("Emissive Color", glm::value_ptr(m.emissiveFactor));
                }

                if (showMaterialGroup(
                        "Subsurface Scattering",
                        "subsurface scattering sss skin wax leaves enable color "
                        "strength radius")) {
                ImGui::Separator();
                ImGui::Text("Subsurface Scattering");
                ImGui::Checkbox("Enable Subsurface", &m.subsurfaceEnabled);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Wrapped diffuse/back-scatter lighting for skin, wax and leaves.\n"
                                      "Evaluated in the deferred lighting pass.");
                }
                if (m.subsurfaceEnabled) {
                    ImGui::ColorEdit3("Subsurface Color", glm::value_ptr(m.subsurfaceColor));
                    ImGui::SliderFloat("Subsurface Strength", &m.subsurfaceStrength, 0.0f, 2.0f);
                    ImGui::SliderFloat("Subsurface Radius", &m.subsurfaceRadius, 0.01f, 5.0f);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Scattering radius in world units.");
                    }
                }
                }

                if (showMaterialGroup(
                        "Translucency",
                        "translucency translucent transmission enable color opacity "
                        "strength thickness distortion shadow depth")) {
                ImGui::Separator();
                ImGui::Text("Translucency");
                ImGui::Checkbox("Enable Translucency", &m.translucencyEnabled);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Render this material in the lit forward translucency pass.\n"
                                      "Backlight is transmitted through shadow depth.");
                }
                if (m.translucencyEnabled) {
                    ImGui::ColorEdit3("Translucency Color", glm::value_ptr(m.translucencyColor));
                    ImGui::SliderFloat("Translucency Opacity", &m.translucencyOpacity, 0.0f, 1.0f);
                    ImGui::SliderFloat("Translucency Strength", &m.translucencyStrength, 0.0f, 4.0f);
                    ImGui::SliderFloat("Translucency Thickness", &m.translucencyThickness, 0.0f, 1.0f);
                    ImGui::SliderFloat("Translucency Distortion", &m.translucencyDistortion, 0.0f, 1.0f);
                    ImGui::SliderFloat("Shadow Depth Scale", &m.translucencyShadowDepth, 0.0f, 1.0f);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("How strongly shadow-map depth attenuates transmitted light.");
                    }
                }
                }

                if (showMaterialGroup(
                        "Shadows",
                        "shadows shadow sharpness pssm depth offset bias")) {
                ImGui::Separator();
                ImGui::Text("Shadows");
                ImGui::SliderFloat("Shadow Sharpness (Global)",
                                   &m_pssmShadowSharpness,
                                   0.0f, 1.0f, "%.2f");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Global PSSM sharpness: 1.0 = razor-sharp single-tap\n"
                                      "shadows, 0.0 = soft PCF. Affects every cast shadow.");
                }
                ImGui::SliderFloat("Shadow Depth Offset", &m.shadowDepthOffset, -0.25f, 0.25f, "%.3f m");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Offsets the caster along its normal in the shadow-depth pass.\n"
                                      "This changes self-shadow bias, not edge sharpness.");
                }
                }

                if (showMaterialGroup(
                        "Texture Maps",
                        "texture map maps albedo normal metallic roughness emissive "
                        "occlusion opacity specular")) {
                ImGui::Separator();
                ImGui::Text("Texture Maps (drag from Content Browser):");

                if (showMaterialField("Albedo Map", "texture map color"))
                DrawTextureSlot("Albedo Map",           m.albedoMap,           true,
                                &m.albedoSrgb, &materialChanged);
                if (showMaterialField("Normal Map", "texture map normal bump"))
                DrawTextureSlot("Normal Map",           m.normalMap,           false,
                                &m.normalSrgb, &materialChanged);
                if (showMaterialField("Metallic/Roughness/Occlusion",
                                      "texture map orm mr metallic roughness occlusion ao")) {
                    auto* previousOrm = m.metallicRoughnessMap.get();
                    DrawTextureSlot("Metallic/Roughness/Occlusion (ORM)",
                                    m.metallicRoughnessMap, false,
                                    &m.metallicRoughnessSrgb, &materialChanged);
                    if (m.metallicRoughnessMap.get() != previousOrm) {
                        // ORM layout: R = occlusion, G = roughness,
                        // B = metallic. Keep the dedicated AO slot in sync
                        // unless the artist had assigned a separate map.
                        if (m.metallicRoughnessMap) {
                            m.occlusionMap = m.metallicRoughnessMap;
                        } else if (m.occlusionMap.get() == previousOrm) {
                            m.occlusionMap.reset();
                        }
                    }
                }
                if (showMaterialField("Metallic Map", "texture map metal"))
                DrawTextureSlot("Metallic Map",         m.metallicMap,         false,
                                &m.metallicSrgb, &materialChanged);
                if (showMaterialField("Roughness Map", "texture map rough"))
                DrawTextureSlot("Roughness Map",        m.roughnessMap,        false,
                                &m.roughnessSrgb, &materialChanged);
                if (showMaterialField("Emissive Map", "texture map emission"))
                DrawTextureSlot("Emissive Map",         m.emissiveMap,         true,
                                &m.emissiveSrgb, &materialChanged);
                if (showMaterialField("Occlusion Map", "texture map ao"))
                DrawTextureSlot("Occlusion Map",        m.occlusionMap,        false,
                                &m.occlusionSrgb, &materialChanged);
                if (showMaterialField("Opacity Map", "texture map alpha"))
                DrawTextureSlot("Opacity Map",          m.opacityMap,          false,
                                &m.opacitySrgb, &materialChanged);
                if (showMaterialField("Specular Map", "texture map specular"))
                DrawTextureSlot("Specular Map",         m.specularMap,         false,
                                &m.specularSrgb, &materialChanged);
                }
            }

            // Compare the complete editable material state. This catches
            // sliders, checkboxes and texture drops even when no ImGui item
            // remains active on the release frame.
            if (materialStateHash(m) != materialStateBefore) {
                m.sceneOverride = true;
                PushHistory();
            } else if (ImGui::IsAnyItemActive()) {
                m.sceneOverride = true;
            }
            ImGui::PopID();
        };

        // 1. Draw material for the selected entity itself (if any). A
        // collision capsule can own a character subtree; in that case its
        // child material slots are still valid Details content.
        if (!selectedIsLandscape &&
            (!selectedIsCollisionShape || selectionHasMaterials) &&
            (selected[0].HasComponent<Galeshi::MaterialComponent>() ||
             selected[0].HasComponent<Galeshi::MeshComponent>())) {
            if (auto* matc = ensureMaterialSlot(selected[0])) {
                std::string label =
                    (matc->material && !matc->material->name.empty())
                        ? matc->material->name
                        : (selected[0].HasComponent<Galeshi::NameComponent>()
                               ? selected[0]
                                     .GetComponent<Galeshi::NameComponent>()
                                     .name
                               : std::string("Material"));
                DrawMaterialUI(label.c_str(), *matc,
                               (uint32_t)selected[0].Handle());
            }
        }

        // 2. Draw materials for all descendants (skeletal model wrappers can
        // sit below a capsule, and their sub-meshes below another model root).
        if (!selectedIsLandscape &&
            (!selectedIsCollisionShape || selectionHasMaterials)) {
            for (auto e : m_scene.AllEntities()) {
                if (e.Handle() == selectedHandle) continue;
                entt::entity cursor = e.Handle();
                bool belongsToSelection = false;
                for (int depth = 0; depth < 128; ++depth) {
                    if (!reg.all_of<Galeshi::ParentComponent>(cursor)) break;
                    cursor = reg.get<Galeshi::ParentComponent>(cursor).parent;
                    if (!reg.valid(cursor)) break;
                    if (cursor == selectedHandle) {
                        belongsToSelection = true;
                        break;
                    }
                }
                if (!belongsToSelection ||
                    (!e.HasComponent<Galeshi::MaterialComponent>() &&
                     !e.HasComponent<Galeshi::MeshComponent>())) {
                    continue;
                }
                auto* matc = ensureMaterialSlot(e);
                if (!matc) continue;
                std::string label = "Material Slot [" +
                    std::to_string((uint32_t)e.Handle()) + "]";
                if (matc->material && !matc->material->name.empty()) {
                    label = matc->material->name;
                } else if (e.HasComponent<Galeshi::NameComponent>()) {
                    label = e.GetComponent<Galeshi::NameComponent>().name;
                }
                DrawMaterialUI(label.c_str(), *matc, (uint32_t)e.Handle());
            }
        }

        if (selected[0].HasComponent<Galeshi::NavAgentComponent>() && beginDetailsSection(
                "AI Agent", ImGuiTreeNodeFlags_DefaultOpen,
                "ai agent navigation nav speed path waypoints target")) {
            auto& agent = selected[0].GetComponent<Galeshi::NavAgentComponent>();
            ImGui::DragFloat("Speed", &agent.speed, 0.1f, 0.1f, 20.0f);
            ImGui::Text("Path Waypoints: %zu", agent.path.size());
            ImGui::Text("Target: (%.1f, %.1f, %.1f)", agent.targetPosition.x, agent.targetPosition.y, agent.targetPosition.z);
        }

        if (selected[0].HasComponent<Galeshi::AIControllerComponent>() && beginDetailsSection(
                "AI Controller", ImGuiTreeNodeFlags_DefaultOpen,
                "ai controller script path lua behavior")) {
            auto& ctrl = selected[0].GetComponent<Galeshi::AIControllerComponent>();
            char buf[256]; std::snprintf(buf, sizeof(buf), "%s", ctrl.scriptPath.c_str());
            if (ImGui::InputText("Script Path", buf, sizeof(buf))) ctrl.scriptPath = buf;

            // Drag & Drop support for scripts
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("GALESHI_ASSET_PATH")) {
                    const char* path = static_cast<const char*>(payload->Data);
                    std::string p = path;
                    if (p.find(".lua") != std::string::npos) {
                        ctrl.scriptPath = p;
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }

        // The AI & Gameplay section is only relevant for AI agents (both the
        // Behavior Tree and Lua Patrol templates add NavAgentComponent).
        const bool isAiAgent =
            selected[0].HasComponent<Galeshi::NavAgentComponent>() ||
            selected[0].HasComponent<Galeshi::AIControllerComponent>();

        if (isAiAgent && beginDetailsSection(
                "AI & Gameplay", 0,
                "ai gameplay perception behavior tree player tag add")) {
            if (!selected[0].HasComponent<Galeshi::PerceptionComponent>()) {
                if (ImGui::Button("+ Add Perception")) {
                    PushHistory();
                    selected[0].AddComponent<Galeshi::PerceptionComponent>();
                }
            }
            if (!selected[0].HasComponent<Galeshi::BehaviorTreeComponent>()) {
                if (ImGui::Button("+ Add Behavior Tree")) {
                    PushHistory();
                    auto& tree = selected[0].AddComponent<Galeshi::BehaviorTreeComponent>();
                    tree = Galeshi::BehaviorTreeSystem::MakeDefaultBehaviorTree();
                    if (!selected[0].HasComponent<Galeshi::PerceptionComponent>()) {
                        selected[0].AddComponent<Galeshi::PerceptionComponent>();
                    }
                    if (!selected[0].HasComponent<Galeshi::NavAgentComponent>()) {
                        selected[0].AddComponent<Galeshi::NavAgentComponent>();
                    }
                }
            }
            if (!selected[0].HasComponent<Galeshi::PlayerTagComponent>()) {
                if (ImGui::Button("+ Add Player Tag")) {
                    PushHistory();
                    selected[0].AddComponent<Galeshi::PlayerTagComponent>();
                }
            }
        }

        if (selected[0].HasComponent<Galeshi::PerceptionComponent>() &&
            beginDetailsSection(
                "Perception", ImGuiTreeNodeFlags_DefaultOpen,
                "perception sight field of view hearing eye height detection "
                "interval memory line of sight player target")) {
            auto& perception = selected[0].GetComponent<Galeshi::PerceptionComponent>();
            ImGui::SliderFloat("Sight Radius", &perception.sightRadius, 0.0f, 200.0f);
            ImGui::SliderFloat("Field of View", &perception.fieldOfViewDegrees, 1.0f, 360.0f);
            ImGui::SliderFloat("Hearing Radius", &perception.hearingRadius, 0.0f, 100.0f);
            ImGui::SliderFloat("Eye Height", &perception.eyeHeight, 0.0f, 4.0f);
            ImGui::SliderFloat("Detection Interval", &perception.detectionInterval, 0.02f, 2.0f);
            ImGui::SliderFloat("Memory Duration", &perception.memoryDuration, 0.0f, 30.0f);
            ImGui::Checkbox("Require Line of Sight", &perception.requireLineOfSight);
            ImGui::SameLine();
            ImGui::Checkbox("Player Only", &perception.detectPlayerOnly);
            char targetName[128];
            std::snprintf(targetName, sizeof(targetName), "%s", perception.targetName.c_str());
            if (ImGui::InputText("Target Name", targetName, sizeof(targetName))) {
                perception.targetName = targetName;
            }
            if (ImGui::Button("Reset Perception")) {
                Galeshi::BehaviorTreeSystem::ResetPerception(perception);
            }
            ImGui::SameLine();
            ImGui::TextColored(perception.canSeeTarget ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f)
                                                       : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                               perception.canSeeTarget ? "Target visible" : "No target");
            ImGui::Text("Last seen: %.2fs ago", perception.timeSinceSeen);
        }

        if (selected[0].HasComponent<Galeshi::BehaviorTreeComponent>() &&
            beginDetailsSection(
                "Behavior Tree", ImGuiTreeNodeFlags_DefaultOpen,
                "behavior tree node selector sequence condition action tick "
                "status reset load default")) {
            auto& tree = selected[0].GetComponent<Galeshi::BehaviorTreeComponent>();
            ImGui::Checkbox("Enabled", &tree.enabled);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100.0f);
            ImGui::DragFloat("Tick Interval", &tree.tickInterval, 0.01f, 0.0f, 2.0f);
            ImGui::Text("Status: %s (node %d)", tree.statusText.c_str(), tree.runningNode);
            if (ImGui::Button("Reset Runtime")) {
                Galeshi::BehaviorTreeSystem::ResetRuntime(tree);
            }
            ImGui::SameLine();
            if (ImGui::Button("Load Default Tree")) {
                PushHistory();
                tree = Galeshi::BehaviorTreeSystem::MakeDefaultBehaviorTree();
                if (!selected[0].HasComponent<Galeshi::PerceptionComponent>()) {
                    selected[0].AddComponent<Galeshi::PerceptionComponent>();
                }
                if (!selected[0].HasComponent<Galeshi::NavAgentComponent>()) {
                    selected[0].AddComponent<Galeshi::NavAgentComponent>();
                }
            }

            const char* nodeTypeNames[] = {"Selector", "Sequence", "Condition", "Action"};
            const char* conditionNames[] = {
                "Always True", "Has Target", "Target Visible", "Target In Range",
                "Target Lost", "Parameter True"
            };
            const char* actionNames[] = {
                "Wait", "Set Target To Player", "Move To Target", "Patrol", "Attack",
                "Look At Target", "Set Animation State", "Clear Target", "Set Nav Speed"
            };

            std::function<void(int, int)> drawNode = [&](int index, int depth) {
                if (depth > 32 || index < 0 ||
                    index >= static_cast<int>(tree.nodes.size())) return;
                auto& node = tree.nodes[static_cast<size_t>(index)];
                ImGui::PushID(index);
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                           ImGuiTreeNodeFlags_DefaultOpen;
                if (node.children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
                char label[256];
                std::snprintf(label, sizeof(label), "[%d] %s", index, node.name.c_str());
                const bool open = ImGui::TreeNodeEx("##node", flags, "%s", label);
                ImGui::SameLine();
                ImGui::TextDisabled("(%s)", nodeTypeNames[static_cast<int>(node.type)]);

                char nodeName[128];
                std::snprintf(nodeName, sizeof(nodeName), "%s", node.name.c_str());
                if (ImGui::InputText("Name", nodeName, sizeof(nodeName))) {
                    node.name = nodeName;
                }
                int type = static_cast<int>(node.type);
                if (ImGui::Combo("Type", &type, nodeTypeNames, IM_ARRAYSIZE(nodeTypeNames))) {
                    node.type = static_cast<Galeshi::BehaviorNodeType>(type);
                }

                if (node.type == Galeshi::BehaviorNodeType::Condition) {
                    int condition = static_cast<int>(node.condition);
                    if (ImGui::Combo("Condition", &condition, conditionNames,
                                     IM_ARRAYSIZE(conditionNames))) {
                        node.condition = static_cast<Galeshi::BehaviorCondition>(condition);
                    }
                    if (node.condition == Galeshi::BehaviorCondition::TargetInRange) {
                        ImGui::DragFloat("Range", &node.range, 0.1f, 0.0f, 100.0f);
                    }
                    if (node.condition == Galeshi::BehaviorCondition::ParameterTrue) {
                        char parameter[128];
                        std::snprintf(parameter, sizeof(parameter), "%s", node.parameter.c_str());
                        if (ImGui::InputText("Parameter", parameter, sizeof(parameter))) {
                            node.parameter = parameter;
                        }
                    }
                } else if (node.type == Galeshi::BehaviorNodeType::Action) {
                    int action = static_cast<int>(node.action);
                    if (ImGui::Combo("Action", &action, actionNames,
                                     IM_ARRAYSIZE(actionNames))) {
                        node.action = static_cast<Galeshi::BehaviorAction>(action);
                    }
                    switch (node.action) {
                        case Galeshi::BehaviorAction::Wait:
                            ImGui::DragFloat("Duration", &node.duration, 0.05f, 0.0f, 60.0f);
                            break;
                        case Galeshi::BehaviorAction::MoveToTarget:
                            ImGui::DragFloat("Arrive Range", &node.range, 0.05f, 0.0f, 20.0f);
                            break;
                        case Galeshi::BehaviorAction::Patrol:
                            ImGui::DragFloat("Patrol Radius", &node.radius, 0.25f, 0.5f, 200.0f);
                            ImGui::DragFloat("Wait Time", &node.duration, 0.05f, 0.0f, 60.0f);
                            break;
                        case Galeshi::BehaviorAction::Attack:
                            ImGui::DragFloat("Attack Range", &node.range, 0.05f, 0.0f, 20.0f);
                            ImGui::DragFloat("Cooldown", &node.duration, 0.05f, 0.1f, 10.0f);
                            break;
                        case Galeshi::BehaviorAction::SetAnimationState: {
                            char state[128];
                            std::snprintf(state, sizeof(state), "%s", node.value.c_str());
                            if (ImGui::InputText("State", state, sizeof(state))) node.value = state;
                            break;
                        }
                        case Galeshi::BehaviorAction::SetNavSpeed: {
                            char speed[128];
                            std::snprintf(speed, sizeof(speed), "%s", node.value.c_str());
                            if (ImGui::InputText("Speed", speed, sizeof(speed))) node.value = speed;
                            break;
                        }
                        default:
                            break;
                    }
                } else {
                    ImGui::TextDisabled("Children: %zu", node.children.size());
                }

                if (open) {
                    std::vector<int> children = node.children;
                    for (int child : children) drawNode(child, depth + 1);
                    ImGui::TreePop();
                }
                ImGui::PopID();
            };
            if (!tree.nodes.empty()) drawNode(tree.root, 0);
        }

        if (selected[0].HasComponent<Galeshi::PlayerTagComponent>()) {
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Player Tag");
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove##playerTag")) {
                selected[0].RemoveComponent<Galeshi::PlayerTagComponent>();
            }
        }

        if (selected[0].HasComponent<Galeshi::SkeletalComponent>() && beginDetailsSection(
                "Skeletal Animation", ImGuiTreeNodeFlags_DefaultOpen,
                "skeletal animation playing speed source active clip time "
                "bone skeleton")) {
            auto& skel = selected[0].GetComponent<Galeshi::SkeletalComponent>();
            ImGui::Checkbox("Playing", &skel.isAnimated);
            ImGui::DragFloat("Speed", &skel.animationSpeed, 0.1f, 0.0f, 10.0f);
            ImGui::TextDisabled("Source: %s", skel.sourcePath.empty() ? "(none)" : skel.sourcePath.c_str());
            ImGui::Text("Active Clip: %d  Time: %.2fs", skel.activeAnimation, skel.currentTime);
        }

        if (selected[0].HasComponent<Galeshi::SkeletalComponent>() ||
            selected[0].HasComponent<Galeshi::AnimationStateMachineComponent>()) {
            if (beginDetailsSection(
                    "Animation State Machine", ImGuiTreeNodeFlags_DefaultOpen,
                    "animation state machine states transitions parameters "
                    "clip loop speed blend playback animation scale")) {
                DrawAnimationStateMachineContents(selected[0]);
            }
        }

        if (selected[0].HasComponent<Galeshi::DirectionalLightComponent>() && beginDetailsSection(
                "Light", ImGuiTreeNodeFlags_DefaultOpen,
                "directional light sun direction color intensity shadow")) {
            auto& dl = selected[0].GetComponent<Galeshi::DirectionalLightComponent>();
            ImGui::DragFloat3("Direction", glm::value_ptr(dl.direction), 0.05f, -1.0f, 1.0f);
            ImGui::ColorEdit3("Color", glm::value_ptr(dl.color));
            ImGui::SliderFloat("Intensity", &dl.intensity, 0.0f, 10.0f);
        }

        if (selected[0].HasComponent<Galeshi::PointLightComponent>() && beginDetailsSection(
                "Point Light", ImGuiTreeNodeFlags_DefaultOpen,
                "point light color intensity radius shadow")) {
            auto& pl = m_scene.Registry().get<Galeshi::PointLightComponent>(selected[0].Handle());
            ImGui::ColorEdit3("Point Color", glm::value_ptr(pl.color));
            {
                float oldInt = pl.intensity;
                if (ImGui::DragFloat("Point Intensity", &pl.intensity, 0.1f, 0.0f, 100.0f)) {
                }
            }
            ImGui::DragFloat("Point Radius", &pl.radius, 0.1f, 0.1f, 100.0f);
        }

        if (selected[0].HasComponent<Galeshi::SpotLightComponent>() && beginDetailsSection(
                "Spot Light", ImGuiTreeNodeFlags_DefaultOpen,
                "spot light direction color intensity radius inner outer cone")) {
            auto& sl = m_scene.Registry().get<Galeshi::SpotLightComponent>(selected[0].Handle());
            ImGui::DragFloat3("Spot Direction", glm::value_ptr(sl.direction), 0.05f, -1.0f, 1.0f);
            ImGui::ColorEdit3("Spot Color", glm::value_ptr(sl.color));
            ImGui::SliderFloat("Spot Intensity", &sl.intensity, 0.0f, 20.0f);
            ImGui::DragFloat("Spot Radius", &sl.radius, 0.1f, 0.1f, 100.0f);
            ImGui::SliderFloat("Inner Cone", &sl.innerConeAngle, 0.0f, 90.0f);
            ImGui::SliderFloat("Outer Cone", &sl.outerConeAngle, 0.0f, 90.0f);
        }

        if (selected[0].HasComponent<Galeshi::ParticleEmitterComponent>() &&
            beginDetailsSection(
                "Particle Emitter", ImGuiTreeNodeFlags_DefaultOpen,
                "particle emitter enabled emit rate lifetime start end size "
                "color velocity gravity drag max particles texture")) {
            auto& pe = m_scene.Registry().get<Galeshi::ParticleEmitterComponent>(selected[0].Handle());
            ImGui::Checkbox("Enabled", &pe.enabled);
            ImGui::DragFloat("Emit Rate", &pe.emitRate, 0.5f, 0.0f, 500.0f);
            ImGui::DragFloat("Lifetime", &pe.lifetime, 0.01f, 0.05f, 30.0f);
            ImGui::DragFloat("Lifetime Variance", &pe.lifetimeVariance, 0.01f, 0.0f, 10.0f);
            ImGui::DragFloat("Start Size", &pe.startSize, 0.01f, 0.0f, 10.0f);
            ImGui::DragFloat("End Size", &pe.endSize, 0.01f, 0.0f, 10.0f);
            ImGui::ColorEdit4("Start Color", glm::value_ptr(pe.startColor));
            ImGui::ColorEdit4("End Color", glm::value_ptr(pe.endColor));
            ImGui::DragFloat3("Velocity", glm::value_ptr(pe.velocity), 0.05f, -50.0f, 50.0f);
            ImGui::DragFloat("Velocity Variance", &pe.velocityVariance, 0.05f, 0.0f, 50.0f);
            ImGui::DragFloat("Spawn Radius", &pe.spawnRadius, 0.01f, 0.0f, 10.0f);
            ImGui::DragFloat3("Spawn Box Size", glm::value_ptr(pe.spawnBoxSize), 0.05f, 0.0f, 100.0f);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Half extents of a box-shaped spawn volume.\n"
                                  "Any value > 0 switches from the sphere to the box.\n"
                                  "Use it for rain/snow areas.");
            }
            ImGui::DragFloat3("Gravity", glm::value_ptr(pe.gravity), 0.05f, -50.0f, 50.0f);
            ImGui::DragFloat("Drag", &pe.drag, 0.01f, 0.0f, 10.0f);
            int maxParticles = static_cast<int>(pe.maxParticles);
            if (ImGui::DragInt("Max Particles", &maxParticles, 1.0f, 1, 20000)) {
                pe.maxParticles = static_cast<uint32_t>(std::max(maxParticles, 1));
            }

            ImGui::Separator();
            ImGui::Text("Particle Texture (drag from Content Browser):");
            DrawTextureSlot("Particle Texture", pe.texture, true);

            if (m_particles) {
                ImGui::TextDisabled("Live particles: %zu",
                                    m_particles->ParticleCount(selected[0].Handle()));
            }
        }

        if (selected[0].HasComponent<Galeshi::UICanvasComponent>()) {
            auto& uiCanvas = selected[0].GetComponent<Galeshi::UICanvasComponent>().canvas;
            if (m_uiCanvasEntity != selected[0].Handle()) {
                m_uiCanvasEntity = selected[0].Handle();
                m_uiWidgetIndex = -1;
            }
            DrawUICanvasProperties(uiCanvas);
            if (m_uiWidgetIndex >= 0 &&
                m_uiWidgetIndex < static_cast<int>(uiCanvas.widgets.size())) {
                DrawUIWidgetProperties(
                    uiCanvas, uiCanvas.widgets[static_cast<size_t>(m_uiWidgetIndex)]);
            }
        }

        if (searchActive && !detailsAnyMatch) {
            ImGui::TextDisabled(
                "No properties or materials match \"%s\".",
                m_detailsSearch.c_str());
        }

        ImGui::EndChild();
        ImGui::End();
    }

