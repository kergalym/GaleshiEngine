#include "GaleshiEditorApp.hpp"



    std::string GaleshiEditorApp::FindCharacterModelPath(entt::entity entity) {
        if (!m_assets) return {};
        auto& registry = m_scene.Registry();
        if (!registry.valid(entity)) return {};

        if (registry.all_of<Galeshi::SkeletalComponent>(entity)) {
            const auto& source =
                registry.get<Galeshi::SkeletalComponent>(entity).sourcePath;
            if (!source.empty()) return source;
        }

        // Find the first descendant mesh belonging to a model with bones or
        // animation clips. The editor does not need Player/NPC tags for this;
        // the imported model type is the source of truth.
        std::vector<entt::entity> pending{entity};
        std::unordered_set<entt::entity> visited;
        while (!pending.empty()) {
            const entt::entity current = pending.back();
            pending.pop_back();
            if (!registry.valid(current) || !visited.insert(current).second) {
                continue;
            }
            if (registry.all_of<Galeshi::MeshComponent>(current)) {
                const auto& mesh =
                    registry.get<Galeshi::MeshComponent>(current);
                if (!mesh.sourceAssetPath.empty()) {
                    auto model = m_assets->LoadModel(mesh.sourceAssetPath);
                    if (model.hasAnimation || model.totalBones > 0) {
                        return mesh.sourceAssetPath;
                    }
                }
            }
            auto childView = registry.view<Galeshi::ParentComponent>();
            for (entt::entity child : childView) {
                if (childView.get<Galeshi::ParentComponent>(child).parent ==
                    current) {
                    pending.push_back(child);
                }
            }
        }
        return {};
    }



    bool GaleshiEditorApp::AssignStateAnimationAsset(Galeshi::Entity entity,  int stateIndex,
                                   const std::string& originalPath) {
        if (!entity || !m_assets || stateIndex < 0) return false;
        if (!IsModelAssetPath(originalPath)) return false;
        auto& registry = m_scene.Registry();
        auto* machine =
            registry.try_get<Galeshi::AnimationStateMachineComponent>(
                entity.Handle());
        if (!machine || stateIndex >= static_cast<int>(machine->states.size())) {
            return false;
        }

        PushHistory();
        auto& state = machine->states[static_cast<size_t>(stateIndex)];
        state.animationPath = m_assets->MakeRelativePath(originalPath);
        state.clip = 0;
        state.animationName.clear();
        auto animationModel = m_assets->LoadModel(state.animationPath);
        if (!animationModel.animationNames.empty()) {
            state.animationName = animationModel.animationNames.front();
        }

        // Dropping a clip is an explicit request to preview it.
        machine->enabled = true;
        machine->currentState = stateIndex;
        machine->previousState = -1;
        machine->stateTime = 0.0f;
        machine->previousTime = 0.0f;
        machine->blendElapsed = machine->blendDuration;
        machine->initialized = true;
        if (auto* skeleton =
                registry.try_get<Galeshi::SkeletalComponent>(entity.Handle())) {
            skeleton->isAnimated = true;
        }
        return true;
    }



    void GaleshiEditorApp::DrawAnimationStateMachineContents(
        Galeshi::Entity entity,
        const std::string& fallbackModelPath) {
        if (!entity) return;
        auto& registry = m_scene.Registry();
        const entt::entity handle = entity.Handle();
        if (!registry.valid(handle)) return;

        auto* skeleton = registry.try_get<Galeshi::SkeletalComponent>(handle);
        std::string modelPath = fallbackModelPath;
        if (skeleton && !skeleton->sourcePath.empty()) {
            modelPath = skeleton->sourcePath;
        }
        if (modelPath.empty()) modelPath = FindCharacterModelPath(handle);

        std::vector<std::string> clipNames;
        bool modelHasSkeleton = false;
        if (!modelPath.empty() && m_assets) {
            auto model = m_assets->LoadModel(modelPath);
            clipNames = model.animationNames;
            modelHasSkeleton = model.totalBones > 0;
        }

        std::vector<std::string> clipLabels;
        clipLabels.reserve(clipNames.size());
        for (size_t i = 0; i < clipNames.size(); ++i) {
            clipLabels.push_back(clipNames[i].empty()
                                     ? "Clip " + std::to_string(i)
                                     : clipNames[i]);
        }
        if (clipLabels.empty()) clipLabels.push_back("No animation clips");

        if (!modelPath.empty()) {
            if (clipNames.empty()) {
                ImGui::TextColored(
                    ImVec4(1.0f, 0.65f, 0.25f, 1.0f),
                    "No embedded animation clips in the character model.");
                ImGui::TextWrapped(
                    "FBX export needs \"All Actions\" (or an active Action) "
                    "to embed clips. glTF/GLB can embed animation clips or "
                    "be used as a separate animation asset.");
            } else {
                ImGui::TextDisabled("Embedded clips: %zu", clipNames.size());
            }
            if (!modelHasSkeleton) {
                ImGui::TextColored(
                    ImVec4(1.0f, 0.65f, 0.25f, 1.0f),
                    "This model has no skin/armature. Separate animation clips "
                    "cannot deform it; export the glTF with the character skin.");
            }
        }
        std::vector<const char*> clipItems;
        clipItems.reserve(clipLabels.size());
        for (const std::string& label : clipLabels) {
            clipItems.push_back(label.c_str());
        }

        auto* machine =
            registry.try_get<Galeshi::AnimationStateMachineComponent>(handle);
        if (!machine) {
            if (!skeleton) {
                if (modelPath.empty()) {
                    ImGui::TextDisabled(
                        "This entity has no skeletal mesh.");
                    return;
                }
                ImGui::TextDisabled("Skeletal model: %s", modelPath.c_str());
                if (ImGui::Button("+ Set Up Skeletal Character")) {
                    PushHistory();
                    auto model = m_assets->LoadModel(modelPath);
                    auto& created =
                        registry.emplace<Galeshi::SkeletalComponent>(handle);
                    created.sourcePath = m_assets->MakeRelativePath(modelPath);
                    created.isAnimated = model.hasAnimation;
                    created.boneMatrices.assign(256, glm::mat4(1.0f));
                    auto& createdMachine =
                        registry.emplace<
                            Galeshi::AnimationStateMachineComponent>(handle);
                    createdMachine =
                        Galeshi::AnimationSystem::CreateDefaultStateMachine(
                            model.animationNames);
                    createdMachine.parameters = {
                        {"Speed", 0.0f},
                        {"PlayerVisible", 0.0f},
                        {"TargetDistance", 100.0f}
                    };
                }
                return;
            }
            if (skeleton->sourcePath.empty() && !modelPath.empty()) {
                skeleton->sourcePath = m_assets->MakeRelativePath(modelPath);
            }
            if (ImGui::Button("+ Add Animation State Machine")) {
                PushHistory();
                auto& created =
                    registry.emplace<Galeshi::AnimationStateMachineComponent>(
                        handle);
                created = Galeshi::AnimationSystem::CreateDefaultStateMachine(
                    clipNames);
                created.parameters = {
                    {"Speed", 0.0f},
                    {"PlayerVisible", 0.0f},
                    {"TargetDistance", 100.0f}
                };
            }
            return;
        }

        ImGui::PushID(static_cast<int>(handle));
        ImGui::Checkbox("Enabled", &machine->enabled);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        ImGui::DragFloat("Playback Speed", &machine->playbackSpeed,
                         0.01f, 0.0f, 4.0f);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        ImGui::DragFloat("Animation Scale", &machine->animationScale,
                         0.001f, 0.001f, 100.0f, "%.4f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Uniform scale for retargeted animation translation.\n"
                "Use 0.01 for centimeter animations on a meter character.");
        }
        ImGui::Text("Current State: %d / %zu", machine->currentState,
                    machine->states.size());
        ImGui::TextDisabled("Clip duration: %.2fs",
                            machine->currentClipDuration);

        if (ImGui::TreeNodeEx("States", ImGuiTreeNodeFlags_DefaultOpen)) {
            int removeState = -1;
            for (int i = 0; i < static_cast<int>(machine->states.size()); ++i) {
                auto& state = machine->states[static_cast<size_t>(i)];
                ImGui::PushID(i);
                char stateName[128];
                std::snprintf(stateName, sizeof(stateName), "%s",
                              state.name.c_str());
                ImGui::SetNextItemWidth(160.0f);
                if (ImGui::InputText("Name", stateName, sizeof(stateName))) {
                    state.name = stateName;
                }
                ImGui::SameLine();

                char animationPath[512];
                std::snprintf(animationPath, sizeof(animationPath), "%s",
                              state.animationPath.c_str());
                ImGui::SetNextItemWidth(260.0f);
                if (ImGui::InputText("Animation Asset", animationPath,
                                     sizeof(animationPath))) {
                    state.animationPath = animationPath;
                    state.clip = 0;
                    state.animationName.clear();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Empty = clips embedded in the character model.\n"
                        "Drag a separate FBX, glTF or GLB animation here to "
                        "retarget its clips by bone name.");
                }
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload =
                            ImGui::AcceptDragDropPayload(
                                "GALESHI_ASSET_PATH")) {
                        const char* path =
                            static_cast<const char*>(payload->Data);
                        const std::string candidate = path ? path : "";
                        AssignStateAnimationAsset(entity, i, candidate);
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Browse##animation_asset")) {
                    m_animationClipTarget = handle;
                    m_animationClipState = i;
                    std::string start = "assets";
                    const std::string projectPath =
                        Galeshi::ProjectManager::GetProjectPath();
                    if (!projectPath.empty()) {
                        start = (std::filesystem::path(projectPath) /
                                 "assets").string();
                    }
                    m_animationClipPicker.Open(
                        start, "Select Animation Asset",
                        {".fbx", ".gltf", ".glb", ".obj", ".dae"});
                }
                if (!state.animationPath.empty()) {
                    ImGui::SameLine();
                    if (clipNames.empty()) {
                        ImGui::BeginDisabled();
                        ImGui::SmallButton("Use Embedded");
                        ImGui::EndDisabled();
                        if (ImGui::IsItemHovered(
                                ImGuiHoveredFlags_AllowWhenDisabled)) {
                            ImGui::SetTooltip(
                                "The character model contains no embedded "
                                "animation clips.");
                        }
                    } else if (ImGui::SmallButton("Use Embedded")) {
                        state.animationPath.clear();
                        state.clip = 0;
                        state.animationName.clear();
                    }
                }

                std::vector<std::string> stateClipNames = clipNames;
                if (!state.animationPath.empty() && m_assets) {
                    auto animationModel =
                        m_assets->LoadModel(state.animationPath);
                    stateClipNames = animationModel.animationNames;
                }
                if (state.animationName.empty() &&
                    state.clip >= 0 &&
                    state.clip < static_cast<int>(stateClipNames.size())) {
                    state.animationName = stateClipNames[state.clip];
                }
                std::vector<std::string> stateClipLabels;
                stateClipLabels.reserve(stateClipNames.size());
                for (size_t clipIndex = 0;
                     clipIndex < stateClipNames.size(); ++clipIndex) {
                    stateClipLabels.push_back(
                        stateClipNames[clipIndex].empty()
                            ? "Clip " + std::to_string(clipIndex)
                            : stateClipNames[clipIndex]);
                }
                if (stateClipLabels.empty()) {
                    stateClipLabels.push_back("No animation clips");
                }
                std::vector<const char*> stateClipItems;
                stateClipItems.reserve(stateClipLabels.size());
                for (const std::string& label : stateClipLabels) {
                    stateClipItems.push_back(label.c_str());
                }
                int clip = std::clamp(
                    state.clip, 0,
                    static_cast<int>(stateClipItems.size()) - 1);
                ImGui::SetNextItemWidth(160.0f);
                if (ImGui::Combo("Clip", &clip, stateClipItems.data(),
                                 static_cast<int>(stateClipItems.size()))) {
                    state.clip = clip;
                    if (clip >= 0 &&
                        clip < static_cast<int>(stateClipNames.size())) {
                        state.animationName = stateClipNames[clip];
                    } else {
                        state.animationName.clear();
                    }
                    // Preview the selected state immediately. Previously a
                    // clip change only edited the state data; the animation
                    // kept sampling machine->currentState, so selecting a
                    // different embedded clip appeared to do nothing until
                    // the separate "Set" button was pressed.
                    machine->enabled = true;
                    machine->currentState = i;
                    machine->previousState = -1;
                    machine->stateTime = 0.0f;
                    machine->blendElapsed = machine->blendDuration;
                    if (skeleton) skeleton->isAnimated = true;
                }
                ImGui::SameLine();
                ImGui::Checkbox("Loop", &state.loop);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(80.0f);
                ImGui::DragFloat("Speed", &state.speed, 0.01f, 0.0f, 4.0f);
                ImGui::SameLine();
                if (ImGui::SmallButton("Set")) {
                    machine->enabled = true;
                    machine->currentState = i;
                    machine->previousState = -1;
                    machine->stateTime = 0.0f;
                    machine->blendElapsed = machine->blendDuration;
                    if (skeleton) skeleton->isAnimated = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove")) removeState = i;
                ImGui::PopID();
            }
            if (removeState >= 0 && machine->states.size() > 1) {
                machine->states.erase(machine->states.begin() + removeState);
                for (auto& transition : machine->transitions) {
                    if (transition.fromState == removeState) {
                        transition.fromState = -1;
                    } else if (transition.fromState > removeState) {
                        --transition.fromState;
                    }
                    if (transition.toState == removeState) {
                        transition.toState = 0;
                    } else if (transition.toState > removeState) {
                        --transition.toState;
                    }
                }
                if (machine->currentState >=
                    static_cast<int>(machine->states.size())) {
                    machine->currentState = 0;
                }
            }
            if (ImGui::Button("+ Add State")) {
                Galeshi::AnimationState state;
                state.name = "State " +
                             std::to_string(machine->states.size());
                state.clip = std::clamp(
                    machine->currentState, 0,
                    std::max(0, static_cast<int>(clipNames.size()) - 1));
                if (state.clip >= 0 &&
                    state.clip < static_cast<int>(clipNames.size())) {
                    state.animationName = clipNames[state.clip];
                }
                machine->states.push_back(state);
            }
            if (!clipNames.empty()) {
                ImGui::SameLine();
                if (ImGui::Button("+ Add All Clips")) {
                    for (size_t clipIndex = 0; clipIndex < clipNames.size();
                         ++clipIndex) {
                        bool alreadyAdded = false;
                        for (const auto& state : machine->states) {
                            if (state.clip == static_cast<int>(clipIndex) &&
                                state.name == clipNames[clipIndex]) {
                                alreadyAdded = true;
                                break;
                            }
                        }
                        if (alreadyAdded) continue;
                        Galeshi::AnimationState state;
                        state.name = clipNames[clipIndex].empty()
                            ? "Clip " + std::to_string(clipIndex)
                            : clipNames[clipIndex];
                        state.clip = static_cast<int>(clipIndex);
                        state.animationName = clipNames[clipIndex];
                        machine->states.push_back(state);
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Create one state for every animation clip in the model.");
                }
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Transitions", ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* conditionNames[] = {
                "Bool True", "Bool False", "Greater", "Less", "Trigger",
                "Always"
            };
            int removeTransition = -1;
            for (int i = 0; i < static_cast<int>(machine->transitions.size());
                 ++i) {
                auto& transition =
                    machine->transitions[static_cast<size_t>(i)];
                ImGui::PushID(1000 + i);
                int from = transition.fromState + 1;
                std::vector<const char*> fromNames;
                fromNames.push_back("Any State");
                for (const auto& state : machine->states) {
                    fromNames.push_back(state.name.c_str());
                }
                ImGui::SetNextItemWidth(150.0f);
                if (ImGui::Combo("From", &from, fromNames.data(),
                                 static_cast<int>(fromNames.size()))) {
                    transition.fromState = from - 1;
                }
                ImGui::SameLine();
                int to = std::clamp(
                    transition.toState, 0,
                    std::max(0, static_cast<int>(machine->states.size()) - 1));
                std::vector<const char*> toNames;
                for (const auto& state : machine->states) {
                    toNames.push_back(state.name.c_str());
                }
                ImGui::SetNextItemWidth(150.0f);
                if (ImGui::Combo("To", &to, toNames.data(),
                                 static_cast<int>(toNames.size()))) {
                    transition.toState = to;
                }
                int condition = static_cast<int>(transition.condition);
                ImGui::SetNextItemWidth(120.0f);
                if (ImGui::Combo("Condition", &condition, conditionNames,
                                 IM_ARRAYSIZE(conditionNames))) {
                    transition.condition =
                        static_cast<Galeshi::AnimationCondition>(condition);
                }
                ImGui::SameLine();
                char parameter[128];
                std::snprintf(parameter, sizeof(parameter), "%s",
                              transition.parameter.c_str());
                ImGui::SetNextItemWidth(120.0f);
                if (ImGui::InputText("Parameter", parameter,
                                     sizeof(parameter))) {
                    transition.parameter = parameter;
                }
                ImGui::SetNextItemWidth(90.0f);
                ImGui::DragFloat("Threshold", &transition.threshold, 0.01f);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(90.0f);
                ImGui::DragFloat("Blend", &transition.duration, 0.01f,
                                 0.0f, 4.0f);
                ImGui::SameLine();
                ImGui::Checkbox("On", &transition.enabled);
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove")) removeTransition = i;
                ImGui::Separator();
                ImGui::PopID();
            }
            if (removeTransition >= 0) {
                machine->transitions.erase(
                    machine->transitions.begin() + removeTransition);
            }
            if (ImGui::Button("+ Add Transition")) {
                Galeshi::AnimationTransition transition;
                transition.fromState = machine->currentState;
                transition.toState =
                    (machine->currentState + 1) %
                    std::max<size_t>(1, machine->states.size());
                machine->transitions.push_back(transition);
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
            int removeParameter = -1;
            for (int i = 0; i < static_cast<int>(machine->parameters.size());
                 ++i) {
                auto& parameter = machine->parameters[static_cast<size_t>(i)];
                ImGui::PushID(2000 + i);
                char parameterName[128];
                std::snprintf(parameterName, sizeof(parameterName), "%s",
                              parameter.name.c_str());
                ImGui::SetNextItemWidth(160.0f);
                if (ImGui::InputText("Name", parameterName,
                                     sizeof(parameterName))) {
                    parameter.name = parameterName;
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(120.0f);
                ImGui::DragFloat("Value", &parameter.value, 0.01f);
                ImGui::SameLine();
                if (ImGui::SmallButton("Trigger")) parameter.value = 1.0f;
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove")) removeParameter = i;
                ImGui::PopID();
            }
            if (removeParameter >= 0) {
                machine->parameters.erase(machine->parameters.begin() +
                                          removeParameter);
            }
            if (ImGui::Button("+ Add Parameter")) {
                machine->parameters.push_back({"Parameter", 0.0f});
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }



    void GaleshiEditorApp::DrawStateMachineEditor() {
        if (!m_showStateMachineEditor) return;

        // Keep this editor in the bottom dock group even when an old
        // imgui.ini remembers it as a floating window.
        if (ImGuiWindow* host = ImGui::FindWindowByName("UI Designer");
            host && host->DockId != 0) {
            ImGui::SetNextWindowDockID(host->DockId, ImGuiCond_Always);
        } else if (ImGuiWindow* output = ImGui::FindWindowByName("Output Log");
                   output && output->DockId != 0) {
            ImGui::SetNextWindowDockID(output->DockId, ImGuiCond_Always);
        } else if (ImGuiWindow* browser =
                       ImGui::FindWindowByName("Content Browser");
                   browser && browser->DockId != 0) {
            ImGui::SetNextWindowDockID(browser->DockId, ImGuiCond_Always);
        }

        if (!ImGui::Begin("Animation State Machine Editor",
                          &m_showStateMachineEditor)) {
            ImGui::End();
            return;
        }

        struct CharacterEntry {
            entt::entity entity = entt::null;
            std::string label;
            std::string sourcePath;
        };
        std::vector<CharacterEntry> characters;
        std::unordered_map<entt::entity, size_t> characterIndices;
        auto& registry = m_scene.Registry();

        auto addCharacter = [&](entt::entity entity,
                                const std::string& sourcePath) {
            if (!registry.valid(entity)) return;
            const auto existing = characterIndices.find(entity);
            if (existing != characterIndices.end()) {
                CharacterEntry& entry =
                    characters[existing->second];
                if (entry.sourcePath.empty() && !sourcePath.empty()) {
                    entry.sourcePath = sourcePath;
                }
                return;
            }
            std::string name = "Entity " + std::to_string((uint32_t)entity);
            if (registry.all_of<Galeshi::NameComponent>(entity)) {
                name = registry.get<Galeshi::NameComponent>(entity).name;
            }
            characterIndices.emplace(entity, characters.size());
            characters.push_back({entity, name, sourcePath});
        };

        // Direct skeletal-character roots.
        auto skeletonView = registry.view<Galeshi::SkeletalComponent>();
        for (entt::entity handle : skeletonView) {
            const auto& skeleton = skeletonView.get<Galeshi::SkeletalComponent>(handle);
            addCharacter(handle, skeleton.sourcePath);
        }

        // Also detect imported skeletal mesh hierarchies that do not yet have
        // a SkeletalComponent on their root. The model's bone/animation data
        // is the type check; Player/NPC tags are intentionally not consulted.
        std::unordered_map<std::string, bool> skeletalModelCache;
        auto isSkeletalModel = [&](const std::string& path) {
            if (path.empty()) return false;
            const auto cached = skeletalModelCache.find(path);
            if (cached != skeletalModelCache.end()) return cached->second;
            auto model = m_assets ? m_assets->LoadModel(path)
                                  : Galeshi::AssetManager::LoadedModel{};
            const bool skeletal = model.hasAnimation || model.totalBones > 0;
            skeletalModelCache.emplace(path, skeletal);
            return skeletal;
        };

        auto modelRootForMesh = [&](entt::entity handle) {
            // Prefer the nearest ancestor that owns a skeleton. Walking all
            // the way to the scene root can select a shared parent: editing
            // its state machine would then animate every character below it.
            entt::entity root = handle;
            for (int depth = 0; depth < 128 &&
                                registry.valid(root);
                 ++depth) {
                if (registry.all_of<Galeshi::SkeletalComponent>(root) ||
                    registry.all_of<Galeshi::AnimationStateMachineComponent>(
                        root)) {
                    return root;
                }
                if (!registry.all_of<Galeshi::ParentComponent>(root)) break;
                root = registry.get<Galeshi::ParentComponent>(root).parent;
            }
            if (registry.all_of<Galeshi::ParentComponent>(handle)) {
                // Imported model meshes normally live under their own root;
                // keep that root instead of the common scene parent.
                return registry.get<Galeshi::ParentComponent>(handle).parent;
            }
            return handle;
        };

        auto meshView = registry.view<Galeshi::MeshComponent>();
        for (entt::entity handle : meshView) {
            const auto& mesh = meshView.get<Galeshi::MeshComponent>(handle);
            if (mesh.sourceAssetPath.empty()) continue;
            // glTF characters can be authored without embedded animation
            // clips (or even without skin data in the file). They must still
            // be selectable here so an external clip or a skeleton can be
            // assigned later. FBX props stay filtered by the skeletal check.
            const bool gltfModel =
                IsGltfModelAssetPath(mesh.sourceAssetPath);
            if (!gltfModel && !isSkeletalModel(mesh.sourceAssetPath)) {
                continue;
            }
            addCharacter(modelRootForMesh(handle), mesh.sourceAssetPath);
        }

        // A model just added to the scene is usually selected. Include the
        // selection explicitly so the state machine editor can target it even
        // if the import does not expose bones or animation metadata.
        for (Galeshi::Entity selected : m_editor.SelectedAll(&m_scene)) {
            const entt::entity handle = selected.Handle();
            if (!registry.valid(handle)) continue;
            if (const auto* skeleton =
                    registry.try_get<Galeshi::SkeletalComponent>(handle)) {
                addCharacter(handle, skeleton->sourcePath);
                continue;
            }
            if (const auto* mesh =
                    registry.try_get<Galeshi::MeshComponent>(handle);
                mesh && !mesh->sourceAssetPath.empty()) {
                addCharacter(modelRootForMesh(handle), mesh->sourceAssetPath);
                continue;
            }

            // Selected model root/container: find the first descendant mesh
            // that belongs to an imported model.
            std::vector<entt::entity> pending{handle};
            std::unordered_set<entt::entity> visited;
            while (!pending.empty()) {
                const entt::entity current = pending.back();
                pending.pop_back();
                if (!registry.valid(current) ||
                    !visited.insert(current).second) {
                    continue;
                }
                if (const auto* mesh =
                        registry.try_get<Galeshi::MeshComponent>(current);
                    mesh && !mesh->sourceAssetPath.empty()) {
                    addCharacter(handle, mesh->sourceAssetPath);
                    break;
                }
                auto childView = registry.view<Galeshi::ParentComponent>();
                for (entt::entity child : childView) {
                    if (childView.get<Galeshi::ParentComponent>(child).parent ==
                        current) {
                        pending.push_back(child);
                    }
                }
            }
        }

        std::sort(characters.begin(), characters.end(),
                  [](const CharacterEntry& a, const CharacterEntry& b) {
                      return a.label < b.label;
                  });

        if (characters.empty()) {
            ImGui::TextDisabled("No skeletal characters in the scene.");
            ImGui::End();
            return;
        }

        bool found = false;
        for (const CharacterEntry& entry : characters) {
            if (entry.entity == m_stateMachineEditorEntity) {
                found = true;
                break;
            }
        }
        if (!found) {
            m_stateMachineEditorEntity = characters.front().entity;
        }

        int current = 0;
        for (int i = 0; i < static_cast<int>(characters.size()); ++i) {
            if (characters[static_cast<size_t>(i)].entity ==
                m_stateMachineEditorEntity) {
                current = i;
                break;
            }
        }
        std::vector<const char*> labels;
        labels.reserve(characters.size());
        for (const CharacterEntry& entry : characters) {
            labels.push_back(entry.label.c_str());
        }
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("##character", &current, labels.data(),
                         static_cast<int>(labels.size()))) {
            m_stateMachineEditorEntity =
                characters[static_cast<size_t>(current)].entity;
        }
        if (ImGui::Button("Select in Scene")) {
            Galeshi::Entity entity(m_stateMachineEditorEntity, &m_scene);
            if (entity) m_editor.Select(entity);
        }
        ImGui::SameLine();
        std::string selectedSourcePath;
        for (const CharacterEntry& entry : characters) {
            if (entry.entity == m_stateMachineEditorEntity) {
                selectedSourcePath = entry.sourcePath;
                break;
            }
        }
        if (registry.all_of<Galeshi::SkeletalComponent>(
                m_stateMachineEditorEntity)) {
            const auto& skeleton = registry.get<Galeshi::SkeletalComponent>(
                m_stateMachineEditorEntity);
            if (!skeleton.sourcePath.empty()) {
                selectedSourcePath = skeleton.sourcePath;
            }
        }
        ImGui::TextDisabled("Source: %s",
                            selectedSourcePath.empty()
                                ? "(skeletal mesh not assigned)"
                                : selectedSourcePath.c_str());
        ImGui::TextDisabled(
            "Drop an animation file on the Animation Asset field below.");
        ImGui::Separator();

        ImGui::BeginChild("##state_machine_scroll");
        DrawAnimationStateMachineContents(
            Galeshi::Entity(m_stateMachineEditorEntity, &m_scene),
            selectedSourcePath);
        ImGui::EndChild();
        ImGui::End();
    }

