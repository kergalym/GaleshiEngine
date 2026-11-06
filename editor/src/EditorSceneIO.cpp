#include "GaleshiEditorApp.hpp"



    // -------------------------------------------------------------------
    // Undo / Redo
    // -------------------------------------------------------------------
    void GaleshiEditorApp::PushHistory() {
        if (m_isRestoringHistory) return;
        std::string snap = m_serializer->SaveToString();
        if (m_historyIndex >= 0 && m_history[m_historyIndex] == snap) return;

        m_history.resize(m_historyIndex + 1);
        m_history.push_back(snap);
        m_historyIndex = (int)m_history.size() - 1;

        if (m_history.size() > 50) {
            m_history.erase(m_history.begin());
            m_historyIndex--;
        }
    }



    void GaleshiEditorApp::Undo() {
        // Some actions (delete, replace, create) snapshot the pre-action
        // state; make the current post-action state the redo point first so
        // one Undo returns to that pre-action state instead of skipping it.
        PushHistory();
        if (m_historyIndex <= 0) return;
        m_historyIndex--;
        m_isRestoringHistory = true;
        m_suppressHistory = true;
        m_editor.ClearSelection();
        m_serializer->LoadFromString(m_history[m_historyIndex]);
        // Normalize the restored snapshot so the next comparison does not
        // mistake serialization order for a new user edit.
        m_history[m_historyIndex] = m_serializer->SaveToString();
        if (m_particles) m_particles->Reset();
        ReleaseUnusedSceneAssets();
        m_isRestoringHistory = false;
    }



    void GaleshiEditorApp::Redo() {
        // A change made after Undo invalidates the redo tail.
        PushHistory();
        if (m_historyIndex + 1 >= (int)m_history.size()) return;
        m_historyIndex++;
        m_isRestoringHistory = true;
        m_suppressHistory = true;
        m_editor.ClearSelection();
        m_serializer->LoadFromString(m_history[m_historyIndex]);
        m_history[m_historyIndex] = m_serializer->SaveToString();
        if (m_particles) m_particles->Reset();
        ReleaseUnusedSceneAssets();
        m_isRestoringHistory = false;
    }



    // -------------------------------------------------------------------
    // Save / Load / Delete
    // -------------------------------------------------------------------
    void GaleshiEditorApp::SaveSceneToPath(const std::string& path) {
        if (!m_serializer || path.empty()) return;
        CaptureSceneViewState();
        SyncCodeEditorStateToSerializer();
        if (m_browser) {
            std::error_code ec;
            const std::filesystem::path relative = std::filesystem::relative(
                m_browser->CurrentDirectory(), m_browser->RootDirectory(), ec);
            m_serializer->SetContentBrowserPath(
                ec ? std::string{} : relative.generic_string());
        }
        if (m_serializer->Save(path)) {
            m_currentScenePath =
                std::filesystem::absolute(path).lexically_normal().string();
            AddRecentScene(m_currentScenePath);
            if (m_assets) m_assets->SaveImportCache();
            spdlog::info("[Editor] Scene saved to {}", m_currentScenePath);
        } else {
            spdlog::error("[Editor] Failed to save scene to {}", path);
        }
    }



    void GaleshiEditorApp::SaveScene(bool saveAs) {
        if (!saveAs && !m_currentScenePath.empty()) {
            SaveSceneToPath(m_currentScenePath);
            return;
        }
        const std::string start = SceneStartDirectory();
        const std::string fileName = m_currentScenePath.empty()
            ? "scene.json"
            : std::filesystem::path(m_currentScenePath).filename().string();
        m_filePickerSave.SaveAs(
            start, fileName, saveAs ? "Save Scene As" : "Save Scene",
            { ".json" });
    }



    void GaleshiEditorApp::LoadScene() {
        m_scenePicker.Open(SceneStartDirectory(), "Load Scene", { ".json" });
    }



    void GaleshiEditorApp::StopPlay() {
        if (!m_editor.isPlaying) return;
        EndEditorMouseLook();
        m_lastViewportTexture = BGFX_INVALID_HANDLE;
        m_editor.isPlaying = false;
        m_scripts.ClearTriggerEvents();
        m_audio.StopAll();
        if (m_physics) m_physics->Reset();
        if (m_particles) m_particles->Reset();

        // The Play snapshot recreates every entity, invalidating the ASME
        // selection handle. Remember the character name so the editor can
        // stay on the same character after the scene is restored.
        std::string stateMachineCharacterName;
        if (m_stateMachineEditorEntity != entt::null) {
            auto& registry = m_scene.Registry();
            if (registry.valid(m_stateMachineEditorEntity) &&
                registry.all_of<Galeshi::NameComponent>(
                    m_stateMachineEditorEntity)) {
                stateMachineCharacterName =
                    registry.get<Galeshi::NameComponent>(
                        m_stateMachineEditorEntity).name;
            }
        }

        if (m_serializer && m_historyIndex >= 0 &&
            m_historyIndex < static_cast<int>(m_history.size())) {
            m_serializer->LoadFromString(m_history[m_historyIndex]);
        }

        if (!stateMachineCharacterName.empty()) {
            Galeshi::Entity restored =
                m_scene.FindEntityByName(stateMachineCharacterName);
            if (restored) {
                m_stateMachineEditorEntity = restored.Handle();
            }
        }
        DeactivateGameplayCamera();
    }



    void GaleshiEditorApp::StopPlayForSceneChange() {
        if (!m_editor.isPlaying && !m_usingGameCamera) return;
        EndEditorMouseLook();
        const bool wasPlaying = m_editor.isPlaying;
        m_editor.isPlaying = false;
        if (m_usingGameCamera) DeactivateGameplayCamera();
        if (wasPlaying) {
            m_scripts.ClearTriggerEvents();
            m_audio.StopAll();
            if (m_physics) m_physics->Reset();
            if (m_particles) m_particles->Reset();
        }
        m_lastViewportTexture = BGFX_INVALID_HANDLE;
    }



    void GaleshiEditorApp::LoadSceneFromPath(const std::string& path) {
        if (m_loadingState.isLoading) return;
        const std::string normalized = NormalizeRecentScenePath(path);
        if (normalized.empty()) return;

        std::error_code ec;
        if (!std::filesystem::is_regular_file(normalized, ec)) {
            spdlog::error("[Editor] Scene not found: {}", normalized);
            RemoveRecentScene(normalized);
            return;
        }

        StopPlayForSceneChange();
        m_editor.ClearSelection();
        StartAsyncLoad(normalized);
    }



    void GaleshiEditorApp::NewScene() {
        if (!m_assets || !m_serializer) return;
        spdlog::info("[Editor] New Scene: releasing previous scene and GPU assets");

        // This is the same safe replacement path used by Build Scene and
        // cancelled loads: entities release their shared resources first,
        // then AssetManager::Clear destroys the cached meshes and textures.
        // Skip the intermediate undo snapshot; the empty scene gets a clean
        // snapshot after all scene-level state has been reset below.
        UnloadSceneContent(false);

        m_scene.AudioBuses().fill(Galeshi::AudioBusSettings{});
        m_scene.CameraState() = Galeshi::SceneCameraState{};
        m_scene.GlobalPostProcess() = Galeshi::PostProcessSettings{};
        m_scene.SetHasGlobalPostProcess(false);
        m_postProcess.ResetTaaHistory();
        m_iblGenerated = false;
        m_lastIblSun = {999.0f, 999.0f, 999.0f};

        m_history.clear();
        m_historyIndex = -1;
        m_uiCanvasEntity = entt::null;
        m_uiWidgetIndex = -1;
        m_lastViewportTexture = BGFX_INVALID_HANDLE;
        RestoreEditorCamera();

        PushHistory();
        spdlog::info("[Editor] New empty scene created; previous VRAM resources released");
    }



    void GaleshiEditorApp::CollectEntityHierarchy(entt::entity root,
                                std::unordered_set<entt::entity>& out) {
        auto& reg = m_scene.Registry();
        if (!reg.valid(root) || !out.insert(root).second) return;
        auto childView = reg.view<Galeshi::ParentComponent>();
        for (auto child : childView) {
            if (childView.get<Galeshi::ParentComponent>(child).parent == root) {
                CollectEntityHierarchy(child, out);
            }
        }
    }



    bool GaleshiEditorApp::IsSkeletalCharacter(Galeshi::Entity entity) {
        if (!entity) return false;
        auto& registry = m_scene.Registry();
        entt::entity current = entity.Handle();
        int depth = 0;
        while (registry.valid(current) && depth++ < 128) {
            if (registry.all_of<Galeshi::SkeletalComponent>(current)) {
                return true;
            }
            if (!registry.all_of<Galeshi::ParentComponent>(current)) break;
            current = registry.get<Galeshi::ParentComponent>(current).parent;
        }
        return false;
    }



    void GaleshiEditorApp::ReleaseUnusedSceneAssets() {
        if (!m_assets) return;
        std::vector<std::string> retainedModels;
        auto skeletonView = m_scene.Registry().view<Galeshi::SkeletalComponent>();
        for (auto e : skeletonView) {
            const auto& skeleton = skeletonView.get<Galeshi::SkeletalComponent>(e);
            if (!skeleton.sourcePath.empty()) {
                retainedModels.push_back(skeleton.sourcePath);
            }
        }
        auto vegetationView =
            m_scene.Registry().view<Galeshi::VegetationComponent>();
        for (auto e : vegetationView) {
            const auto& vegetation =
                vegetationView.get<Galeshi::VegetationComponent>(e);
            if (!vegetation.treeMeshPath.empty()) {
                retainedModels.push_back(vegetation.treeMeshPath);
            }
            for (const auto& type : vegetation.treeTypes) {
                if (!type.meshPath.empty()) {
                    retainedModels.push_back(type.meshPath);
                }
            }
        }
        m_assets->ReleaseUnusedAssets(retainedModels);
    }



    void GaleshiEditorApp::DeleteSelected() {
        if (!m_editor.HasSelection()) return;

        std::unordered_set<entt::entity> toDestroy;
        for (auto entity : m_editor.SelectedAll(&m_scene)) {
            // A skeletal mesh owns its imported child hierarchy. Deleting an
            // individual child would corrupt the character model; only the
            // skeletal root itself may be deleted as a whole.
            if (HasSkeletalParent(entity.Handle())) {
                spdlog::warn("[Editor] Cannot delete '{}': parent is a "
                             "skeletal mesh",
                             entity.HasComponent<Galeshi::NameComponent>()
                                 ? entity.GetComponent<Galeshi::NameComponent>().name
                                 : std::to_string(static_cast<uint32_t>(
                                       entity.Handle())));
                continue;
            }
            CollectEntityHierarchy(entity.Handle(), toDestroy);
        }
        if (toDestroy.empty()) {
            m_editor.ClearSelection();
            return;
        }

        PushHistory();

        // Remove non-ECS runtime owners before the components disappear.
        for (entt::entity e : toDestroy) {
            if (!m_scene.Registry().valid(e)) continue;
            m_audio.StopSource(e);

            if (m_physics) {
                if (auto* rb = m_scene.Registry().try_get<Galeshi::RigidBodyComponent>(e)) {
                    m_physics->DestroyBody(rb->bodyId);
                    rb->bodyId = 0xFFFFFFFFu;
                }
                if (auto* soft = m_scene.Registry().try_get<Galeshi::SoftBodyComponent>(e)) {
                    m_physics->DestroyBody(soft->softBodyId);
                    soft->softBodyId = 0xFFFFFFFFu;
                }
            }
            if (m_particles) m_particles->RemoveEmitter(e);
        }

        for (entt::entity e : toDestroy) {
            if (m_scene.Registry().valid(e)) {
                m_scene.DestroyEntity(Galeshi::Entity(e, &m_scene));
            }
        }

        if (m_uiCanvasEntity != entt::null && toDestroy.count(m_uiCanvasEntity)) {
            m_uiCanvasEntity = entt::null;
            m_uiWidgetIndex = -1;
        }

        spdlog::info("[Editor] Deleted {} entities", toDestroy.size());
        m_editor.ClearSelection();
        if (m_debug) m_debug->Begin();
        m_postProcess.ResetTaaHistory();
        ReleaseUnusedSceneAssets();
    }



    // -------------------------------------------------------------------
    // Dockspace
    // -------------------------------------------------------------------
    // -------------------------------------------------------------------
    // UI Designer
    // -------------------------------------------------------------------
    std::string GaleshiEditorApp::ToLowerCopy(std::string value) {
        for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return value;
    }

