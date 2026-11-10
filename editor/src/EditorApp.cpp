#include "GaleshiEditorApp.hpp"



    GaleshiEditorApp::GaleshiEditorApp(bool debugMode,  bool isGameMode,
                              std::string gameProjectPath)
        : Galeshi::Application({
              .name = isGameMode ? "GaleshiGame" : "GaleshiEditor",
              .width = 0, .height = 0,
              .editor = true,
              .debug = debugMode,
              .splashImage = isGameMode ? "" : "assets/icons/galeshi_engine_logo.png"}) {
        m_isGameMode = isGameMode;
        m_gameProjectPath = std::move(gameProjectPath);
    }



    // -------------------------------------------------------------------
    // LIFECYCLE
    // -------------------------------------------------------------------
    void GaleshiEditorApp::OnStart()  {
        m_logoTexture.reset(
            Galeshi::Texture::LoadFromFile(
                "assets/icons/galeshi_logo_round.png", true));
        if (!m_logoTexture) {
            spdlog::warn("[Editor] Missing logo asset: assets/icons/galeshi_logo_round.png");
        }

        LoadRecentProjects();
        LoadPreferences();

        // Packaged games pass their .gproj on the command line. The editor
        // keeps the historical project.gproj-in-CWD startup behaviour.
        std::string startupProject;
        if (m_isGameMode) {
            startupProject = m_gameProjectPath;
            if (startupProject.empty() &&
                std::filesystem::exists("project.gproj")) {
                startupProject = "project.gproj";
            }
            if (startupProject.empty()) {
                std::error_code ec;
                for (const auto& entry : std::filesystem::directory_iterator(
                         std::filesystem::current_path(), ec)) {
                    if (!entry.is_regular_file(ec)) continue;
                    if (entry.path().extension() == ".gproj") {
                        startupProject = entry.path().string();
                        break;
                    }
                }
            }
        } else if (std::filesystem::exists("project.gproj")) {
            startupProject = "project.gproj";
        }

        if (!startupProject.empty() &&
            Galeshi::ProjectManager::OpenProject(startupProject)) {
            m_projectLoaded = true;
        }
        if (m_projectLoaded) {
            InitializeEditor();
            if (m_isGameMode) LoadGameStartScene(startupProject);
        }
    }



    void GaleshiEditorApp::OnFileDrop(const std::string& filepath,  float x,  float y)  {
        if (!m_projectLoaded || !m_assets) return;
        namespace fs = std::filesystem;
        const std::string ext = AssetExtensionLower(filepath);
        if (ext == ".fbx") {
            spdlog::warn(
                "[Editor] FBX drag & drop is no longer supported. "
                "Export the model as glTF/GLB first: {}",
                filepath);
            return;
        }
        if (!IsDragDropModelAssetPath(filepath) && !IsImageAssetPath(filepath) &&
            !IsAudioAssetPath(filepath) && !IsFontAssetPath(filepath)) {
            return;
        }

        const std::string projectPath = Galeshi::ProjectManager::GetProjectPath();
        if (projectPath.empty()) {
            spdlog::error("[Editor] Cannot import '{}': no project is open", filepath);
            return;
        }

        const fs::path assetsRoot = fs::path(projectPath) / "assets";
        // A file dropped on the Content Browser is imported into the folder
        // currently open there. Drops elsewhere keep the previous behavior
        // and go to the project assets root.
        const bool overContentBrowser =
            m_browser && m_browser->ContainsPoint(x, y);
        const fs::path destinationDir =
            overContentBrowser ? m_browser->CurrentDirectory() : assetsRoot;

        if (m_browser) {
            if (m_browser->ImportFiles({filepath}, destinationDir)) {
                spdlog::info("[Editor] Imported file-manager drop '{}' into '{}'",
                             filepath, destinationDir.string());
            }
            return;
        }

        std::error_code ec;
        fs::create_directories(destinationDir, ec);
        const fs::path destination = destinationDir / fs::path(filepath).filename();
        fs::copy_file(filepath, destination,
                      fs::copy_options::overwrite_existing, ec);
        if (ec) {
            spdlog::error("[Editor] Failed to import '{}' to '{}': {}",
                          filepath, destination.string(), ec.message());
        }
    }



    bool GaleshiEditorApp::PlayerInputActive() const {
        return m_isGameMode ||
               (m_editor.isPlaying && m_viewportInputFocused);
    }



    void GaleshiEditorApp::InitializeEditor() {
        // Scene loading must stay fast: keep BC7 texture compression off by
        // default. It can be enabled from Details -> Texture Settings.
        Galeshi::Texture::SetCompressionEnabled(false);
        spdlog::info("[Editor] Texture compression on load: {}",
                     Galeshi::Texture::CompressionEnabled() ? "on" : "off");

        // --- Auto-inserted subsystem init ---
        spdlog::info("[Subsystem] m_audio.Init() begin");
        if (!m_audio.Init()) spdlog::error("[Subsystem] m_audio.Init() failed");
        else spdlog::info("[Subsystem] m_audio.Init() done");
        // Assets must be available before scripts run so Lua can declare
        // skeletal animation models. The AssetManager is also used by the
        // scene serializer and renderer below.
        m_assets = std::make_unique<Galeshi::AssetManager>();
        m_assets->LoadImportCache();
        m_audio.SetPathResolver([this](const std::string& path) {
            return m_assets ? m_assets->ResolvePath(path) : path;
        });
        m_uiRuntime.SetAssetManager(m_assets.get());
        m_uiPreviewRuntime.SetAssetManager(m_assets.get());
        const auto reloadUiFonts = []() {
            Galeshi::ImGuiBgfx::ReloadFontTexture();
        };
        m_uiRuntime.SetFontAtlasChangedCallback(reloadUiFonts);
        m_uiPreviewRuntime.SetFontAtlasChangedCallback(reloadUiFonts);
        m_scripts.BindAssets(m_assets.get());
        m_physics = std::make_unique<Galeshi::EnginePhysicsSystem>();
        spdlog::info("[Subsystem] m_physics->Init() begin");
        if (!m_physics->Init()) spdlog::error("[Subsystem] m_physics->Init() failed");
        else spdlog::info("[Subsystem] m_physics->Init() done");
        m_scripts.BindFunctions();
        m_scripts.BindECS(m_scene);
        // Gameplay scripts may only read player input while playing. The
        // editor still reads the same devices directly for its own controls.
        m_scripts.BindInput([this](const std::string& key) -> bool {
            return PlayerInputActive() && GetInput().IsKeyDown(key);
        });
        m_scripts.BindInputAxes(
            [this](const std::string& axis) {
                return PlayerInputActive() ? GetInput().GetAxis(axis) : 0.0f;
            },
            [this](const std::string& axis) {
                return PlayerInputActive() ? GetInput().GetAxisRaw(axis) : 0.0f;
            },
            [this]() {
                return PlayerInputActive() ? GetInput().MousePosition()
                                           : glm::vec2(0.0f);
            },
            [this]() {
                return PlayerInputActive() ? GetInput().MouseDelta()
                                           : glm::vec2(0.0f);
            },
            [this]() {
                return PlayerInputActive() ? GetInput().MouseWheel() : 0.0f;
            },
            [this](const std::string& button) {
                return PlayerInputActive() &&
                       GetInput().IsMouseButtonDown(button);
            });
        m_scripts.BindImpulse([this](uint32_t eid, const glm::vec3& impulse) {
            entt::entity e = static_cast<entt::entity>(eid);
            auto& reg = m_scene.Registry();
            if (reg.valid(e) && reg.all_of<Galeshi::RigidBodyComponent>(e)) {
                auto& rb = reg.get<Galeshi::RigidBodyComponent>(e);
                if (rb.bodyId != 0xFFFFFFFF) {
                    m_physics->AddImpulse(rb.bodyId, impulse);
                }
            }
        });
        m_scripts.BindVelocity(
            [this](uint32_t eid) -> glm::vec3 {
                entt::entity e = static_cast<entt::entity>(eid);
                auto& reg = m_scene.Registry();
                if (reg.valid(e) && reg.all_of<Galeshi::RigidBodyComponent>(e)) {
                    const auto& rb = reg.get<Galeshi::RigidBodyComponent>(e);
                    if (rb.bodyId != 0xFFFFFFFF) {
                        return m_physics->GetLinearVelocity(rb.bodyId);
                    }
                }
                return glm::vec3(0.0f);
            },
            [this](uint32_t eid, const glm::vec3& velocity) {
                entt::entity e = static_cast<entt::entity>(eid);
                auto& reg = m_scene.Registry();
                if (reg.valid(e) && reg.all_of<Galeshi::RigidBodyComponent>(e)) {
                    const auto& rb = reg.get<Galeshi::RigidBodyComponent>(e);
                    if (rb.bodyId != 0xFFFFFFFF) {
                        m_physics->SetLinearVelocity(rb.bodyId, velocity);
                    }
                }
            });
        m_scripts.BindAudio(
            [this](const std::string& path, bool loop) {
                m_audio.PlaySound(path, loop);
            },
            [this](const std::string& path) { m_audio.StopSound(path); },
            [this](const std::string& path) {
                return m_audio.IsSoundPlaying(path);
            },
            [this]() { m_audio.StopAll(); },
            [this](const std::string& path) {
                return m_audio.PauseSound(path);
            },
            [this](const std::string& path) {
                return m_audio.ResumeSound(path);
            },
            [this](const std::string& path, float volume) {
                return m_audio.SetSoundVolume(path, volume);
            },
            [this](const std::string& path, float pitch) {
                return m_audio.SetSoundPitch(path, pitch);
            },
            [this](const std::string& path, bool loop) {
                return m_audio.SetSoundLooping(path, loop);
            },
            [this](const std::string& path, float seconds) {
                return m_audio.SeekSound(path, seconds);
            },
            [this](const std::string& path) {
                return m_audio.SoundDuration(path);
            },
            [this](const std::string& path) {
                return m_audio.SoundPosition(path);
            });
        m_scripts.BindAudio3D(
            [this](uint32_t entityId) {
                auto& registry = m_scene.Registry();
                const entt::entity entity =
                    static_cast<entt::entity>(entityId);
                if (!registry.valid(entity) ||
                    !registry.all_of<Galeshi::AudioSourceComponent>(entity)) {
                    return false;
                }
                auto& source =
                    registry.get<Galeshi::AudioSourceComponent>(entity);
                glm::vec3 position(0.0f);
                if (registry.all_of<Galeshi::TransformComponent>(entity)) {
                    const glm::mat4 world = EntityWorldMatrix(entity);
                    glm::vec3 scale, translation, skew;
                    glm::vec4 perspective;
                    glm::quat rotation;
                    glm::decompose(world, scale, rotation, translation,
                                   skew, perspective);
                    position = translation + rotation * source.offset;
                }
                const bool playing = m_audio.PlaySource(
                    entity, source, position);
                source.playing = playing;
                return playing;
            },
            [this](uint32_t entityId) {
                const entt::entity entity =
                    static_cast<entt::entity>(entityId);
                m_audio.StopSource(entity);
                auto& registry = m_scene.Registry();
                if (registry.valid(entity) &&
                    registry.all_of<Galeshi::AudioSourceComponent>(entity)) {
                    registry.get<Galeshi::AudioSourceComponent>(entity)
                        .playing = false;
                }
            },
            [this](uint32_t entityId) {
                return m_audio.IsSourcePlaying(
                    static_cast<entt::entity>(entityId));
            });
        m_scripts.BindAI();
        Galeshi::ScriptEngine::PhysicsBindings scriptPhysics;
        scriptPhysics.createBox =
            [this](const glm::vec3& halfExtents, const glm::vec3& position,
                   entt::entity entity, bool isStatic) {
                return m_physics->CreateBox(halfExtents, position, entity,
                                            isStatic);
            };
        scriptPhysics.createSphere =
            [this](float radius, const glm::vec3& position,
                   entt::entity entity, bool isStatic) {
                return m_physics->CreateSphere(radius, position, entity,
                                               isStatic);
            };
        scriptPhysics.createCapsule =
            [this](float halfHeight, float radius, const glm::vec3& position,
                   entt::entity entity, bool isStatic) {
                return m_physics->CreateCapsule(halfHeight, radius, position,
                                                entity, isStatic);
            };
        scriptPhysics.destroyBody = [this](uint32_t bodyId) {
            if (m_physics) m_physics->DestroyBody(bodyId);
        };
        scriptPhysics.createRagdoll = [this](entt::entity entity) {
            return m_physics && m_assets &&
                m_physics->CreateRagdoll(m_scene, *m_assets, entity);
        };
        scriptPhysics.destroyRagdoll = [this](entt::entity entity) {
            if (m_physics) m_physics->DestroyRagdoll(m_scene, entity);
        };
        scriptPhysics.ragdollActive = [this](entt::entity entity) {
            return m_physics && m_physics->IsRagdollActive(entity);
        };
        scriptPhysics.applyRagdollImpulse =
            [this](entt::entity entity, const glm::vec3& impulse) {
                if (m_physics) m_physics->ApplyRagdollImpulse(entity, impulse);
            };
        m_scripts.BindPhysics(scriptPhysics);
        m_scripts.BindEngine([this]() { RequestExit(); });
        m_scripts.BindCamera(
            [this](int mode) { return SetGameplayCameraMode(mode); },
            [this]() { return GetGameplayCameraMode(); },
            [this]() { return GetGameplayCameraMoveForward(); },
            [this]() { return GetGameplayCameraMoveRight(); });
        m_scripts.BindPlayer(
            [this]() { return m_playerStrafing; },
            [this](bool enabled) { m_playerStrafing = enabled; });
        m_scripts.BindShader(
            [this](const std::string& path, std::string& error) {
                CompileShaderPreview(path);
                error = m_shaderPreviewError;
                return error.empty();
            });
        // Project-local key bindings override the built-in axis defaults
        // before scripts start reading input.
        LoadInputBindings();
        spdlog::info("[Subsystem] m_scripts.RunScript(init.lua) begin");
        if (!m_scripts.RunScript("assets/scripts/init.lua"))
            spdlog::error("[Subsystem] m_scripts.RunScript(init.lua) failed");
        else
            spdlog::info("[Subsystem] m_scripts.RunScript(init.lua) done");
        // --- End auto-inserted ---

        m_uiRuntime.SetEventHandler([this](const Galeshi::UIEvent& event) {
            DispatchUIEvent(event);
        });
        m_scripts.BindUIState([this]() {
            return m_uiRuntime.IsPointerOverUI();
        });
        m_scripts.BindUIText(
            [this](uint32_t canvasEntity, const std::string& canvasId,
                   const std::string& widgetId) {
                return m_uiRuntime.TextValue(
                    static_cast<int>(canvasEntity), canvasId, widgetId);
            });

        m_physics = std::make_unique<Galeshi::EnginePhysicsSystem>();
        m_physics->Init();
        m_physics->SetOnContactCallback([this](entt::entity e1, entt::entity e2) {
            // Simple demo: play beep on any collision involving an AudioSource entity
            auto& reg = m_scene.Registry();
            for (entt::entity e : { e1, e2 }) {
                if (reg.valid(e) && reg.all_of<Galeshi::AudioSourceComponent>(e)) {
                    auto& a = reg.get<Galeshi::AudioSourceComponent>(e);
                    if (!a.soundPath.empty()) {
                        glm::vec3 position(0.0f);
                        if (reg.all_of<Galeshi::TransformComponent>(e)) {
                            const glm::mat4 world = EntityWorldMatrix(e);
                            glm::vec3 scale, translation, skew;
                            glm::vec4 perspective;
                            glm::quat rotation;
                            glm::decompose(world, scale, rotation, translation,
                                           skew, perspective);
                            position = translation + rotation * a.offset;
                        }
                        m_audio.PlaySource(e, a, position);
                        a.playing = m_audio.IsSourcePlaying(e);
                    }
                }
            }
        });
        m_physics->SetOnTriggerBeginOverlap(
            [this](entt::entity trigger, entt::entity other) {
                if (!m_editor.isPlaying) return;
                m_scripts.DispatchTriggerBeginOverlap(
                    static_cast<uint32_t>(trigger),
                    static_cast<uint32_t>(other));
            });
        m_physics->SetOnTriggerEndOverlap(
            [this](entt::entity trigger, entt::entity other) {
                if (!m_editor.isPlaying) return;
                m_scripts.DispatchTriggerEndOverlap(
                    static_cast<uint32_t>(trigger),
                    static_cast<uint32_t>(other));
            });
        m_navSystem = std::make_unique<Galeshi::NavigationSystem>();
        m_navSystem->Init();
        m_scripts.BindNavigation(
            [this](const glm::vec3& start, const glm::vec3& end,
                   std::vector<glm::vec3>& path) {
                return m_navSystem &&
                       m_navSystem->FindPath(start, end, path);
            },
            [this]() {
                return m_navSystem && m_navSystem->BuildFromScene(m_scene);
            });
        m_debug = std::make_unique<Galeshi::DebugRenderer>();
        m_debug->Init();

        m_render  = std::make_unique<Galeshi::RenderSystem>();
        m_render->Init(m_assets.get());
        m_particles = std::make_unique<Galeshi::ParticleSystem>();
        m_particles->Init();
        m_fluid = std::make_unique<Galeshi::FluidSimulation>();
        if (!m_fluid->Init()) {
            spdlog::warn("[Fluid] SPlisHSPlasH splash simulation unavailable");
            m_fluid.reset();
        }
        m_bubbleAccumulator = 0.0f;
        m_postProcess.Init();
        ConfigureProfilerViewNames();
        m_render->SetDeferredEnabled(true);
        {
            Galeshi::ScriptEngine::RenderingBindings rendering;
            rendering.width = [this]() {
                const auto* renderer = GetRenderer();
                return renderer ? renderer->Width() : 0u;
            };
            rendering.height = [this]() {
                const auto* renderer = GetRenderer();
                return renderer ? renderer->Height() : 0u;
            };
            rendering.getDeferred = [this]() {
                return m_render && m_render->DeferredEnabled();
            };
            rendering.setDeferred = [this](bool value) {
                if (m_render) m_render->SetDeferredEnabled(value);
            };
            rendering.getLod = [this]() {
                return m_render && m_render->LodEnabled();
            };
            rendering.setLod = [this](bool value) {
                if (m_render) m_render->SetLodEnabled(value);
            };
            rendering.getOcclusion = [this]() {
                return m_render && m_render->OcclusionCullingEnabled();
            };
            rendering.setOcclusion = [this](bool value) {
                if (m_render) m_render->SetOcclusionCullingEnabled(value);
            };
            m_scripts.BindRendering(rendering);
        }
        m_scripts.BindLandscapeBake(
            [this](uint32_t entityId) {
                Galeshi::Entity entity(
                    static_cast<entt::entity>(entityId), &m_scene);
                if (!entity ||
                    !entity.HasComponent<
                        Galeshi::LandscapeComponent>()) {
                    return false;
                }
                return BakeLandscapeTextures(entity);
            });
        // Enable SSAO/SSR on frame 2 to avoid sampling an uninitialized Vulkan target.
        m_pendingEffectsEnable = true;
        for (int i = 0; i < 3; ++i) {
            m_shadowMaps[i] = bgfx::createTexture2D(2048, 2048, false, 1, bgfx::TextureFormat::D32F, BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
            m_shadowFbos[i] = bgfx::createFrameBuffer(1, &m_shadowMaps[i], false);
        }



        // Content Browser shows the open project's assets folder, not the
        // engine's own assets directory.
        const std::string projectPath = Galeshi::ProjectManager::GetProjectPath();
        const std::string browserRoot = projectPath.empty()
            ? std::string("assets")
            : (std::filesystem::path(projectPath) / "assets").string();
        {
            std::error_code ec;
            std::filesystem::create_directories(browserRoot, ec);
        }
        m_browser    = std::make_unique<Galeshi::ContentBrowser>(
            browserRoot, m_assets.get());
        m_serializer = std::make_unique<Galeshi::SceneSerializer>(m_scene, *m_assets);
        LoadRecentScenes();

        // The editor deliberately starts with an empty scene.  Scenes are
        // loaded explicitly through File -> Load Scene or File -> Recent Scenes.
        spdlog::info("[Editor] Starting with an empty scene");

        m_history.clear();
        m_historyIndex = -1;
        PushHistory();

        // Force layout reset to match the tabbed design
        m_resetLayout = true;

        m_camera.SetPerspective(glm::radians(m_preferences.cameraFovDegrees),
            static_cast<float>(GetRenderer()->Width()) / static_cast<float>(GetRenderer()->Height()),
            0.1f, 1000.0f);
    }



    void GaleshiEditorApp::OnUpdate(float dt)  {
        UpdateLoadingState();
        if (m_loadingState.isLoading) return; // Pause engine during load
        if (!m_projectLoaded) return;

        // ImGui locks its font atlas during NewFrame/EndFrame, so custom UI
        // fonts must be added here, before the frame starts.
        m_uiRuntime.PreloadFonts(m_scene);
        m_uiPreviewRuntime.PreloadFonts(m_scene);

        // This update path is independent of viewport rendering options.
        // Physics, scripting, AI, animation, groom, particles and time
        // continue every frame even when expensive GPU work is skipped.
        const auto updateStart = ProfileClock::now();
        m_cpuProfile = CpuProfileSample{};
        m_cpuProfile.frameMs = dt * 1000.0f;

        spdlog::trace("[Main] OnUpdate");
        // --- Deferred effect enable (frame 2+) ---
        m_renderFrameCounter++;
        if (m_pendingEffectsEnable && m_renderFrameCounter >= 2) {
            // Temporary debug hook: GALESHI_EFFECTS is a bitmask used to
            // bisect Vulkan layout problems. DoF is intentionally off here:
            // post-process volumes enable it for their own settings.
            int effectMask = 0x3EF;
            if (const char* env = std::getenv("GALESHI_EFFECTS")) {
                effectMask = std::atoi(env);
            }
            m_postProcess.SetSsaoEnabled((effectMask & 1) != 0);
            m_postProcess.SetSsrEnabled((effectMask & 2) != 0);
            m_postProcess.SetSmaaEnabled((effectMask & 4) != 0);
            m_postProcess.SetFxaaEnabled((effectMask & 8) != 0);
            m_postProcess.SetDofEnabled((effectMask & 16) != 0);
            m_postProcess.SetMotionBlurEnabled((effectMask & 32) != 0);
            m_postProcess.SetAtmosphereEnabled((effectMask & 64) != 0);
            m_postProcess.SetGodRaysEnabled((effectMask & 128) != 0);
            m_postProcess.SetTaaEnabled((effectMask & 256) != 0);
            m_postProcess.SetBloomEnabled((effectMask & 512) != 0);
            m_pendingEffectsEnable = false;
            spdlog::info("[Effects] post-processing enabled on frame {} mask=0x{:x}",
                         m_renderFrameCounter, effectMask);
        }

        // --- Auto-inserted physics update ---
        {
            const auto physicsStart = ProfileClock::now();
            if (m_physics) {
                m_physics->Update(m_scene, dt);
                m_physics->SyncTransformsToECS(m_scene);
            }
            m_cpuProfile.physicsMs = ProfileElapsedMs(physicsStart, ProfileClock::now());
        }

        // Audio: listener, 3D source positions, autoplay and mixer sync.
        {
            const auto audioStart = ProfileClock::now();
            UpdateAudio(dt);
            m_cpuProfile.audioMs = ProfileElapsedMs(audioStart, ProfileClock::now());
        }
        // --- End auto-inserted ---

        m_time += dt;
        m_frameDelta = dt;

        if (m_dynamicResolution) {
            const float clampedDelta = std::clamp(dt, 1.0f / 240.0f, 1.0f / 15.0f);
            m_smoothedFrameTime = m_smoothedFrameTime * 0.9f + clampedDelta * 0.1f;
            ++m_dynamicFrameCounter;
            if (m_dynamicFrameCounter >= 20) {
                const float target = 1.0f / std::max(m_dynamicTargetFps, 1.0f);
                if (m_smoothedFrameTime > target * 1.10f) {
                    m_dynamicScale -= 0.05f;
                } else if (m_smoothedFrameTime < target * 0.92f) {
                    m_dynamicScale += 0.05f;
                }
                m_dynamicScale = std::clamp(m_dynamicScale,
                                             m_dynamicMinScale, 1.0f);
                m_dynamicFrameCounter = 0;
            }
        }
        {
            const auto scriptsStart = ProfileClock::now();
            m_scripts.RunAIScripts(m_scene, dt);
            m_scripts.RunEntityScripts(m_scene, dt);
            m_scripts.Update(dt);
            m_cpuProfile.scriptsMs = ProfileElapsedMs(scriptsStart, ProfileClock::now());
        }

        // Buoyancy/drag and entry splashes use the velocity the scripts just
        // applied, before the next fixed physics step.
        UpdateWaterInteraction(dt);

        {
            const auto aiStart = ProfileClock::now();
            Galeshi::AISystem::Update(m_scene, dt, m_navSystem.get());
            m_cpuProfile.aiMs = ProfileElapsedMs(aiStart, ProfileClock::now());
        }

        // Skeletal playback and animation state machines. This samples the
        // active clip and blends the previous state when a transition fires.
        {
            const auto animationStart = ProfileClock::now();
            Galeshi::AnimationSystem::Update(m_scene, *m_assets, dt);
            m_cpuProfile.animationMs = ProfileElapsedMs(animationStart, ProfileClock::now());
        }
        // Soft-body meshes are updated after skeletal animation so a cloth
        // painted on a skinned mesh is not immediately overwritten.
        if (m_physics) {
            m_physics->UpdateSoftBodies(m_scene);
        }
        // Hair follows the freshly sampled skeleton, then simulates its own
        // spring chains before the render pass builds camera-facing ribbons.
        {
            const auto groomStart = ProfileClock::now();
            Galeshi::GroomSystem::Update(m_scene, *m_assets, dt);
            m_cpuProfile.groomMs = ProfileElapsedMs(groomStart, ProfileClock::now());
        }

        // Particle simulation (world-space billboards).
        {
            const auto particlesStart = ProfileClock::now();
            if (m_particles) m_particles->Update(m_scene, dt);
            m_cpuProfile.particlesMs = ProfileElapsedMs(particlesStart, ProfileClock::now());
        }

        const auto cameraStart = ProfileClock::now();
        if (m_usingGameCamera) {
            // Play is driven by the primary gameplay camera. The editor orbit
            // state remains untouched so Stop can immediately restore it.
            EndEditorMouseLook();
            UpdateGameplayCamera();
        } else {
            // The viewport camera must remain free to navigate through the
            // scene, including over collision-shape wireframes and their
            // transform gizmos. Only an actively manipulated gizmo may block
            // camera input; merely hovering it must not.
            const bool cameraControllable =
                (m_viewportHovered || m_editorLookDragging) &&
                (!m_uiOverlayHovered || m_editorLookDragging) &&
                !m_editor.GizmoLocked() && !ImGuizmo::IsUsing();

            // Mouse wheel dollies the viewport camera forward/back along its
            // view direction. Moving the orbit target as well keeps the
            // camera orientation unchanged while it travels through the scene.
            if (cameraControllable && !ImGui::GetIO().WantTextInput) {
                const float wheel = GetInput().MouseWheel();
                if (wheel != 0.0f) {
                    const float cp = std::cos(m_editor.camPitch);
                    const float sp = std::sin(m_editor.camPitch);
                    const float sy = std::sin(m_editor.camYaw);
                    const float cy = std::cos(m_editor.camYaw);
                    const glm::vec3 forward{-cp * sy, -sp, -cp * cy};
                    const float step = std::max(m_editor.camRadius, 0.5f) *
                                       m_preferences.cameraWheelSpeed;
                    m_editor.camTarget += forward * (wheel * step);
                }
            }

            // Godot-style RMB look: only start rotating after the mouse
            // actually moves, then capture the pointer and rotate in place.
            const bool lookInput =
                cameraControllable &&
                !ImGui::GetIO().WantTextInput &&
                !ImGui::GetIO().AppFocusLost &&
                ImGui::IsMouseDown(ImGuiMouseButton_Right);
            if (lookInput) {
                if (!m_editorLooking) {
                    // Capture on press, but skip the synthetic relative
                    // motion caused by switching modes. A plain RMB click
                    // therefore does not rotate the camera.
                    m_editorLooking = true;
                    m_editorLookDragging = true;
                    SetMouseCapture(true);
                    m_editorLookSkipFrames = 2;
                }

                if (m_editorLookSkipFrames > 0) {
                    --m_editorLookSkipFrames;
                } else {
                    const glm::vec2 d = GetInput().MouseDelta();
                    const glm::vec3 eye = m_camera.Position();
                    m_editor.camYaw -=
                        d.x * m_preferences.cameraOrbitSensitivity;
                    m_editor.camPitch +=
                        d.y * m_preferences.cameraOrbitSensitivity;
                    m_editor.camPitch =
                        glm::clamp(m_editor.camPitch, -1.5f, 1.5f);

                    const float cp = std::cos(m_editor.camPitch);
                    const float sp = std::sin(m_editor.camPitch);
                    const glm::vec3 offset{
                        m_editor.camRadius * cp * std::sin(m_editor.camYaw),
                        m_editor.camRadius * sp,
                        m_editor.camRadius * cp * std::cos(m_editor.camYaw)};
                    m_editor.camTarget = eye - offset;
                }
            } else {
                EndEditorMouseLook();
            }

            const float aspect = m_viewportH
                ? static_cast<float>(m_viewportW) / m_viewportH
                : 16.0f / 9.0f;
            // Keep depth precision usable when the orbit camera moves far away
            // from its target. A fixed 0.1/1000 range causes z-fighting and
            // flickering on distant, nearly coplanar geometry.
            const float orbitDistance = std::max(m_editor.camRadius, 0.5f);
            const float nearZ = glm::clamp(orbitDistance * 0.0025f,
                                           0.1f, 0.75f);
            const float farZ = std::max(1000.0f, orbitDistance * 4.0f + 500.0f);
            m_camera.SetPerspective(glm::radians(m_preferences.cameraFovDegrees),
                                    aspect, nearZ, farZ);
            m_camera.Orbit(m_editor.camTarget, m_editor.camYaw, m_editor.camPitch, m_editor.camRadius);
        }
        m_cpuProfile.cameraMs = ProfileElapsedMs(cameraStart, ProfileClock::now());
        m_cpuProfile.updateMs = ProfileElapsedMs(updateStart, ProfileClock::now());
        m_cpuProfile.gameLogicMs = m_cpuProfile.physicsMs + m_cpuProfile.audioMs +
            m_cpuProfile.scriptsMs + m_cpuProfile.aiMs +
            m_cpuProfile.animationMs + m_cpuProfile.groomMs +
            m_cpuProfile.particlesMs + m_cpuProfile.cameraMs;
    }



    void GaleshiEditorApp::OnRender()  {}



    void GaleshiEditorApp::OnImGui()  {
        if (!m_projectLoaded) {
            DrawProjectManager();
            return;
        }
        if (m_isGameMode) {
            DrawViewport();
            m_uiRuntime.RenderInRect(m_scene, m_viewportImagePos.x,
                                     m_viewportImagePos.y, m_viewportImageSize.x,
                                     m_viewportImageSize.y);
            return;
        }

        if (m_loadingState.isLoading) {
            DrawLoadingModal();
            return;
        }

        // Escape is the runtime stop shortcut. It takes priority over an
        // active key-rebind capture so pressing it while playing always
        // returns to the editor instead of binding Escape.
        if (!m_showPreferences) {
            m_rebindAxis = -1;
            m_rebindBinding = -1;
        }
        if (!m_isGameMode && m_editor.isPlaying &&
            ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_rebindAxis = -1;
            m_rebindBinding = -1;
            StopPlay();
        } else {
            UpdateInputRebind();
        }

        const auto imguiStart = ProfileClock::now();
        if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F8)) {
            m_showProfiler = !m_showProfiler;
        }
        DrawMainMenuBar();
        UpdateProfilerDebugState();

        // Hotkeys
        // Preferences works even while a text field is active.
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Comma)) {
            m_showPreferences = true;
        }

        // Focus Camera on F (works even if some widget is active)
        if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F)) {
            Galeshi::Entity sel = m_editor.Selected(&m_scene);
            if (sel && sel.HasComponent<Galeshi::TransformComponent>()) {
                m_editor.camTarget = sel.GetComponent<Galeshi::TransformComponent>().position;
                m_editor.camRadius = 10.0f;
                spdlog::info("[Editor] Focused camera on entity at ({}, {}, {})",
                    m_editor.camTarget.x, m_editor.camTarget.y, m_editor.camTarget.z);
            } else {
                spdlog::warn("[Editor] F pressed but nothing selected");
            }
        }

        if (!ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive()) {
            if (ImGui::IsKeyPressed(ImGuiKey_W)) m_editor.SetMode(Galeshi::GizmoMode::Translate);
            if (ImGui::IsKeyPressed(ImGuiKey_E)) m_editor.SetMode(Galeshi::GizmoMode::Rotate);
            if (ImGui::IsKeyPressed(ImGuiKey_R)) m_editor.SetMode(Galeshi::GizmoMode::Scale);
            if (ImGui::IsKeyPressed(ImGuiKey_Delete)) DeleteSelected();
            if (ImGui::IsKeyPressed(ImGuiKey_F5)) {
                spdlog::info("[Script] Hot-reload init.lua and C++ modules");
                m_scripts.HotReload("assets/scripts/init.lua");
                m_scripts.ReloadCppScripts(false);
            }
            // Space is now used by Lua (Jump)

            if (ImGui::GetIO().KeyCtrl) {
                if (ImGui::IsKeyPressed(ImGuiKey_N)) NewScene();
                if (ImGui::IsKeyPressed(ImGuiKey_I)) OpenFileDialog();
                if (ImGui::IsKeyPressed(ImGuiKey_S)) {
                    SaveScene(ImGui::GetIO().KeyShift);
                }
                if (ImGui::IsKeyPressed(ImGuiKey_O)) LoadScene();
                if (ImGui::IsKeyPressed(ImGuiKey_Z)) Undo();
                if (ImGui::IsKeyPressed(ImGuiKey_Y)) Redo();
            }
        }

        DrawDockspace();
        {
            const auto renderStart = ProfileClock::now();
            DrawViewport();
            m_cpuProfile.renderCpuMs = ProfileElapsedMs(renderStart, ProfileClock::now());
        }
        if (m_pendingLandscapeBake != entt::null &&
            !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            Galeshi::Entity pending(m_pendingLandscapeBake, &m_scene);
            if (pending &&
                pending.HasComponent<Galeshi::LandscapeComponent>() &&
                !pending.GetComponent<Galeshi::LandscapeComponent>()
                     .layers.empty()) {
                BakeLandscapeTextures(pending);
            }
            m_pendingLandscapeBake = entt::null;
        }
        DrawOutliner();
        DrawDetails();
        DrawContentBrowser();
        DrawCodeEditor();
        DrawLuaConsole();
        DrawScriptComponents();
        DrawUIDesigner();
        DrawOutputLog();
        DrawAudioMixer();
        DrawEnvironmentPaletteWindow();
        {
            Galeshi::Entity selected = m_editor.Selected(&m_scene);
            const entt::entity selectedHandle =
                selected ? selected.Handle() : entt::null;
            if (selectedHandle != m_lastStateMachineSelection) {
                m_lastStateMachineSelection = selectedHandle;

                const entt::entity characterHandle =
                    selected ? FindAsmeCharacter(selectedHandle) : entt::null;

                const bool isCharacter =
                    m_scene.Registry().valid(characterHandle) &&
                    (m_scene.Registry().all_of<Galeshi::SkeletalComponent>(
                         characterHandle) ||
                     m_scene.Registry().all_of<
                         Galeshi::AnimationStateMachineComponent>(
                         characterHandle));
                if (isCharacter) {
                    // Selecting a character switches ASME to that character
                    // and loads scripts/<name>.asme when it exists.
                    if (!m_showStateMachineEditor ||
                        m_stateMachineEditorEntity != characterHandle) {
                        SetAsmeCharacter(characterHandle);
                    }
                    m_showStateMachineEditor = true;
                }
                if (selected &&
                    selected.HasComponent<
                        Galeshi::BehaviorTreeComponent>()) {
                    m_behaviorTreeEditorEntity = selectedHandle;
                    m_showBehaviorTreeEditor = true;
                }
            }
        }
        DrawStateMachineEditor();
        DrawBehaviorTreeEditor();
        DrawPreferencesWindow();
        DrawHistoryModal();

        // Play mode draws the authored canvases inside the viewport image.
        if (m_editor.isPlaying && !m_uiPreview) {
            m_uiRuntime.RenderInRect(m_scene, m_viewportImagePos.x,
                                     m_viewportImagePos.y, m_viewportImageSize.x,
                                     m_viewportImageSize.y);
        }


        std::string chosenOpen = m_filePicker.Draw();
        if (!chosenOpen.empty()) {
            SpawnFromModel(chosenOpen, m_editor.camTarget);
        }

        std::string chosenSave = m_filePickerSave.Draw();
        if (!chosenSave.empty()) {
            SaveSceneToPath(chosenSave);
        }

        // Load Scene dialog (.json)
        std::string chosenLoad = m_scenePicker.Draw();
        if (!chosenLoad.empty()) {
            LoadSceneFromPath(chosenLoad);
        }

        // Character model replacement picker (glTF/GLB only).
        std::string chosenCharacterModel = m_characterModelPicker.Draw();
        if (!chosenCharacterModel.empty() &&
            m_characterModelTarget != entt::null) {
            ReplaceCharacterModel(m_characterModelTarget, chosenCharacterModel);
            m_characterModelTarget = entt::null;
        }

        // Separate animation picker for the state-machine state field.
        std::string chosenAnimationAsset = m_animationClipPicker.Draw();
        if (!chosenAnimationAsset.empty() &&
            m_animationClipTarget != entt::null &&
            m_animationClipState >= 0) {
            auto& animRegistry = m_scene.Registry();
            if (animRegistry.valid(m_animationClipTarget) &&
                animRegistry.all_of<Galeshi::AnimationStateMachineComponent>(
                    m_animationClipTarget)) {
                auto& animMachine =
                    animRegistry.get<Galeshi::AnimationStateMachineComponent>(
                        m_animationClipTarget);
                if (m_animationClipState <
                    static_cast<int>(animMachine.states.size())) {
                    AssignStateAnimationAsset(
                        Galeshi::Entity(m_animationClipTarget, &m_scene),
                        m_animationClipState, chosenAnimationAsset);
                }
            }
            m_animationClipTarget = entt::null;
            m_animationClipState = -1;
        }

        std::string chosenAudio = m_audioPicker.Draw();
        if (!chosenAudio.empty() && m_audioPickerTarget != entt::null) {
            auto& audioRegistry = m_scene.Registry();
            if (audioRegistry.valid(m_audioPickerTarget) &&
                audioRegistry.all_of<Galeshi::AudioSourceComponent>(
                    m_audioPickerTarget)) {
                PushHistory();
                auto& source =
                    audioRegistry.get<Galeshi::AudioSourceComponent>(
                        m_audioPickerTarget);
                m_audio.StopSource(m_audioPickerTarget);
                source.soundPath = m_assets
                    ? m_assets->MakeRelativePath(chosenAudio)
                    : chosenAudio;
                source.playing = false;
            }
            m_audioPickerTarget = entt::null;
        } else if (!m_audioPicker.IsOpen()) {
            m_audioPickerTarget = entt::null;
        }

        // Static tree-mesh picker for the selected landscape vegetation.
        std::string chosenTreeMesh = m_vegetationMeshPicker.Draw();
        if (!chosenTreeMesh.empty()) {
            auto& registry = m_scene.Registry();
            if (m_vegetationMeshTarget != entt::null &&
                registry.valid(m_vegetationMeshTarget)) {
                AssignLandscapeTreeModel(
                    Galeshi::Entity(m_vegetationMeshTarget, &m_scene),
                    chosenTreeMesh, m_vegetationMeshTypeIndex);
            }
            m_vegetationMeshTarget = entt::null;
            m_vegetationMeshTypeIndex = -1;
        } else if (!m_vegetationMeshPicker.IsOpen()) {
            m_vegetationMeshTarget = entt::null;
            m_vegetationMeshTypeIndex = -1;
        }

        // Capture property edits that do not have explicit PushHistory calls.
        // The active item is observed after the change; the pre-edit state is
        // already the latest snapshot, so Undo still returns one full step.
        if (!m_isGameMode && !m_editor.isPlaying) {
            if (ImGui::IsAnyItemActive()) {
                m_uiItemWasActive = true;
            } else if (m_uiItemWasActive) {
                m_uiItemWasActive = false;
                if (m_suppressHistory) {
                    m_suppressHistory = false;
                } else {
                    PushHistory();
                }
            } else if (m_suppressHistory) {
                // Undo/Redo via a keyboard shortcut has no item to release.
                m_suppressHistory = false;
            }
        }

        if (m_editorLookDragging) {
            // SDL relative mode captures the pointer; hide the visible cursor
            // while the user is actively looking around.
            ImGui::SetMouseCursor(ImGuiMouseCursor_None);
        }

        m_cpuProfile.imguiMs = ProfileElapsedMs(imguiStart, ProfileClock::now());
        if (m_showProfiler) {
            UpdateGpuProfilerStats();
        } else {
            m_gpuFrameMs = 0.0f;
        }
        PushProfilerHistory();
        DrawProfilerWindow();
    }



    void GaleshiEditorApp::OnShutdown()  {
        spdlog::info("[Editor] OnShutdown: begin");

        // Never leave the OS cursor captured when the process exits.
        EndEditorMouseLook();
        EndGameMouseLook();

        // Never let the loader touch bgfx/SDL after they have been shut down.
        if (m_loadingState.loaderThread.joinable()) {
            m_loadingState.cancelRequested = true;
            m_loadingState.loaderThread.join();
        }

        // Autosave is disabled; scenes are saved only through File -> Save Scene (Ctrl+S).
        SavePreferences();

        // Release scene-owned resources while the Vulkan device is still
        // alive. In particular, do not let SDL tear down the window/display
        // before bgfx has waited for its GPU work.
        ClearSceneForShutdown();
        if (m_particles) m_particles->Shutdown();
        if (m_debug) m_debug->Shutdown();
        if (m_render) m_render->Shutdown();
        ShutdownShaderPreview();
        ShutdownPlayerCameraPreview();
        m_postProcess.Shutdown();
        if (m_framebuffer) m_framebuffer->Destroy();
        for (auto& fbo : m_shadowFbos) {
            if (bgfx::isValid(fbo)) bgfx::destroy(fbo);
            fbo = BGFX_INVALID_HANDLE;
        }
        for (auto& map : m_shadowMaps) {
            if (bgfx::isValid(map)) bgfx::destroy(map);
            map = BGFX_INVALID_HANDLE;
        }
        m_audio.Shutdown();
        if (m_navSystem) m_navSystem->Shutdown();

        // ImGui owns bgfx buffers and its font texture; destroy those before
        // bgfx shuts down. Then stop SDL only after the renderer is down.
        Galeshi::ImGuiBgfx::Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        if (auto* renderer = GetRenderer()) renderer->Shutdown();
        SDL_Quit();

        spdlog::info("[Editor] OnShutdown: complete");
        std::fflush(stdout);
        std::fflush(stderr);
        std::_Exit(0);
    }

