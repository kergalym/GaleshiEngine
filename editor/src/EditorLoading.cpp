#include "GaleshiEditorApp.hpp"


    // -------------------------------------------------------------------
    // UI: Menu Bar
    // -------------------------------------------------------------------
    void GaleshiEditorApp::StartAsyncLoad(const std::string& path,
                        std::function<void()> customLoad,
                        bool cancelReturnsToProjectManager) {
        auto& s = m_loadingState;
        if (s.isLoading) return;
        if (s.loaderThread.joinable()) s.loaderThread.join();

        s.isLoading = true;
        s.cancelRequested = false;
        s.workerRunning = true;
        s.sceneLoaded = false;
        s.progress = 0.0f;
        s.stage = LoadingState::Stage::Preparing;
        s.scenePath = path;
        s.cancelReturnsToProjectManager = cancelReturnsToProjectManager;
        m_displayProgress = 0.0f;

        s.loaderThread = std::thread([this, path, customLoad]() {
            auto& state = m_loadingState;
            const auto cancelled = [&state] {
                return state.cancelRequested.load(std::memory_order_relaxed);
            };

            // Preparation and parsing are short, cancellable stages. The
            // blocking part (customLoad / SceneSerializer::Load) cannot be
            // interrupted, so the worker only checks the flag around it.
            for (int i = 1; i <= 8 && !cancelled(); ++i) {
                state.progress.store(i * 0.0125f, std::memory_order_relaxed); // 0..10%
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            if (!cancelled()) {
                state.stage.store(LoadingState::Stage::Parsing, std::memory_order_relaxed);
                state.progress.store(0.12f, std::memory_order_relaxed);
                std::this_thread::sleep_for(std::chrono::milliseconds(40));
            }

            if (!cancelled()) {
                state.stage.store(LoadingState::Stage::Loading, std::memory_order_relaxed);
                state.progress.store(0.15f, std::memory_order_relaxed);

                if (customLoad) {
                    customLoad();
                } else if (m_serializer) {
                    if (m_serializer->Load(path, &m_loadingState.cancelRequested)) {
                        m_currentScenePath =
                            std::filesystem::absolute(path).lexically_normal().string();
                        state.sceneLoaded.store(true,
                                                std::memory_order_release);
                    }
                }
            }

            if (cancelled()) {
                // Keep isLoading set: the UI thread joins the worker and
                // releases the partially loaded scene and GPU resources.
                state.stage.store(LoadingState::Stage::Cancelling, std::memory_order_relaxed);
                state.workerRunning = false;
                return;
            }

            state.stage.store(LoadingState::Stage::Finalizing, std::memory_order_relaxed);
            for (int i = 0; i <= 12 && !cancelled(); ++i) {
                state.progress.store(0.85f + 0.15f * (i / 12.0f), std::memory_order_relaxed);
                std::this_thread::sleep_for(std::chrono::milliseconds(12));
            }

            if (cancelled()) {
                state.stage.store(LoadingState::Stage::Cancelling, std::memory_order_relaxed);
                state.workerRunning = false;
                return;
            }

            state.progress.store(1.0f, std::memory_order_relaxed);
            state.workerRunning = false;
            state.isLoading = false;
        });
    }



    // Called from OnUpdate every frame: joins the worker and, after a cancel,
    // destroys everything the loader may have already created.
    void GaleshiEditorApp::UpdateLoadingState() {
        auto& s = m_loadingState;

        if (s.isLoading) {
            if (s.cancelRequested && !s.workerRunning) {
                if (s.loaderThread.joinable()) s.loaderThread.join();
                FinishCancelledLoad();
            }
            return;
        }

        // Successful load: reap the finished thread so the next load starts
        // from a clean state, then release assets left behind by the scene
        // that was replaced.
        if (s.loaderThread.joinable() && !s.workerRunning) {
            s.loaderThread.join();
            ReleaseUnusedSceneAssets();
            if (s.sceneLoaded.exchange(false, std::memory_order_acquire)) {
                ApplySceneViewState();
                RestoreCodeEditorStateFromSerializer();
                if (m_browser && !m_serializer->ContentBrowserPath().empty()) {
                    m_browser->SetDirectory(
                        m_serializer->ContentBrowserPath());
                }
                // A loaded scene is a new history root; snapshots from the
                // previous document must never be reachable through Undo.
                m_history.clear();
                m_historyIndex = -1;
                PushHistory();
                AddRecentScene(m_currentScenePath);
            }
        }
    }



    void GaleshiEditorApp::FinishCancelledLoad() {
        UnloadSceneContent();

        m_loadingState.isLoading = false;
        m_loadingState.cancelRequested = false;
        m_loadingState.stage = LoadingState::Stage::Idle;
        m_loadingState.progress = 0.0f;
        m_displayProgress = 0.0f;

        // A cancelled initial project open returns to the project manager only
        // if InitializeEditor() never ran. Re-running it on an already
        // initialized editor is not supported, so otherwise stay in the editor
        // with an empty scene.
        if (m_loadingState.cancelReturnsToProjectManager && !m_assets) {
            m_projectLoaded = false;
        }
        m_loadingState.cancelReturnsToProjectManager = false;

        spdlog::info("[Loading] Cancelled: scene data and video memory released");
    }



    // Shutdown variant of UnloadSceneContent: release all scene references
    // and runtime state without pushing an undo snapshot or reinitializing
    // navigation. Called while bgfx is still valid.
    void GaleshiEditorApp::ClearSceneForShutdown() {
        m_editor.ClearSelection();
        m_editor.isPlaying = false;
        m_usingGameCamera = false;
        m_activeGameCamera = entt::null;
        m_audio.StopAll();

        if (m_physics) m_physics->Reset();
        if (m_navSystem) m_navSystem->Shutdown();
        if (m_particles) m_particles->Reset();

        m_scene.Registry().clear();
        m_scene.AudioBuses().fill(Galeshi::AudioBusSettings{});
        if (m_assets) {
            m_assets->SaveImportCache();
            m_assets->Clear();
        }
        m_lastViewportTexture = BGFX_INVALID_HANDLE;

        m_currentScenePath.clear();
        m_uiCanvasEntity = entt::null;
        m_uiWidgetIndex = -1;
        m_history.clear();
        m_historyIndex = -1;
    }



    // The scene must release its shared mesh/material/texture references
    // before the AssetManager cache is cleared; only then are the bgfx buffers
    // and textures actually destroyed.
    void GaleshiEditorApp::UnloadSceneContent(bool pushUndoSnapshot) {
        if (!m_assets) return;

        m_editor.ClearSelection();
        m_editor.isPlaying = false;
        m_usingGameCamera = false;
        m_activeGameCamera = entt::null;
        m_pendingLandscapeBake = entt::null;
        m_wasSculpting = false;
        m_boneAttachCharacter = entt::null;
        m_boneAttachEntity = entt::null;
        m_boneAttachBone.clear();
        m_scripts.ClearUIEvents();
        m_scripts.ClearTriggerEvents();
        m_audio.StopAll();

        if (m_physics) m_physics->Reset();
        if (m_navSystem) {
            m_navSystem->Shutdown();
            m_navSystem->Init();
        }

        m_scene.Registry().clear();
        m_assets->Clear(true);
        if (m_render) m_render->ResetSceneState();
        if (m_particles) m_particles->Reset();
        m_postProcess.ResetTaaHistory();
        m_iblGenerated = false;
        m_lastIblSun = {999.0f, 999.0f, 999.0f};
        m_lastViewportTexture = BGFX_INVALID_HANDLE;

        m_history.clear();
        m_historyIndex = -1;
        m_currentScenePath.clear();
        m_uiCanvasEntity = entt::null;
        m_uiWidgetIndex = -1;
        if (pushUndoSnapshot && m_serializer) {
            PushHistory(); // empty-scene snapshot for Undo/Redo
        }
    }



    void GaleshiEditorApp::ResetSceneStateForReplacement() {
        m_scene.AudioBuses().fill(Galeshi::AudioBusSettings{});
        m_scene.CameraState() = Galeshi::SceneCameraState{};
        m_scene.GlobalPostProcess() = Galeshi::PostProcessSettings{};
        m_scene.SetHasGlobalPostProcess(false);
        m_postProcess.ResetTaaHistory();
        m_iblGenerated = false;
        m_lastIblSun = {999.0f, 999.0f, 999.0f};

        m_history.clear();
        m_historyIndex = -1;
        m_currentScenePath.clear();
        m_uiCanvasEntity = entt::null;
        m_uiWidgetIndex = -1;
        m_lastViewportTexture = BGFX_INVALID_HANDLE;
    }



    void GaleshiEditorApp::DrawLoadingModal() {
        auto& s = m_loadingState;
        if (!s.isLoading) return;

        ImGuiViewport* vp = ImGui::GetMainViewport();

        ImGui::GetBackgroundDrawList()->AddRectFilled(
            vp->Pos,
            ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y),
            IM_COL32(0, 0, 0, 180));

        ImGui::SetNextWindowPos(
            ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f),
            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(500, 0), ImGuiCond_Always);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
        ImGui::Begin("Loading Modal", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar);

        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "Loading");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Loading scene %s", s.scenePath.c_str());
        ImGui::Spacing();
        ImGui::Spacing();

        // Smoothly approach the loader milestone. While the blocking asset
        // load runs the bar creeps towards 80% instead of freezing at 15%.
        const float dt = std::min(ImGui::GetIO().DeltaTime, 0.1f);
        const float target = s.progress.load();
        if (s.cancelRequested) {
            // Freeze the bar while the worker is winding down.
        } else if (s.stage.load() == LoadingState::Stage::Loading) {
            m_displayProgress += (0.80f - m_displayProgress) * std::min(1.0f, dt * 0.6f);
            m_displayProgress = std::min(m_displayProgress, 0.80f);
        } else {
            m_displayProgress += (target - m_displayProgress) * std::min(1.0f, dt * 8.0f);
        }

        const float shown = std::clamp(m_displayProgress, 0.0f, 1.0f);
        char overlay[16];
        std::snprintf(overlay, sizeof(overlay), "%d%%",
                      static_cast<int>(shown * 100.0f + 0.5f));

        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImGui::GetStyleColorVec4(ImGuiCol_Button));
        ImGui::ProgressBar(shown, ImVec2(-1, 20), overlay);
        ImGui::PopStyleColor();
        ImGui::Spacing();

        const bool cancelling = s.cancelRequested.load();
        ImGui::TextDisabled("%s", cancelling ? "Cancelling..." : LoadingStageText(s.stage.load()));
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        const float buttonWidth = 120.0f;
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - buttonWidth) * 0.5f);
        ImGui::BeginDisabled(cancelling);
        if (ImGui::Button(cancelling ? "Cancelling..." : "Cancel", ImVec2(buttonWidth, 30))) {
            s.cancelRequested = true;
        }
        ImGui::EndDisabled();

        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }

