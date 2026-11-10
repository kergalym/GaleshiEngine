#pragma once

#include "EditorCommon.hpp"
#include "TextEditor/TextEditor.h"

class GaleshiEditorApp final : public Galeshi::Application {
    Galeshi::AudioSystem m_audio;
    Galeshi::ScriptEngine m_scripts;
    Galeshi::PostProcessManager m_postProcess;
    // AO method used when no post-process volume overrides the settings.
    Galeshi::AmbientOcclusionMode m_globalAoMode =
        Galeshi::AmbientOcclusionMode::SSAO;
    // PSSM tuning. Normal offset and depth bias are expressed in shadow-map
    // texels and converted to per-cascade world/NDC values each frame.
    float m_pssmDepthBiasTexels = 0.75f;
    float m_pssmSlopeBias = 0.00025f;
    float m_pssmNormalOffsetTexels = 1.75f;
    float m_pssmNormalOffsetSlope = 0.5f;
    // 0 = softest PCF, 1 = single-tap hard shadows. The effective PCF
    // radius is derived in PssmPcfRadius().
    float m_pssmShadowSharpness = 0.625f;
    // Viewport render scaling. Dynamic resolution adjusts m_dynamicScale in
    // 5% steps while the manual scale is used when dynamic mode is disabled.
    float m_renderScale = 1.0f;
    bool m_dynamicResolution = false;
    float m_dynamicScale = 1.0f;
    float m_dynamicMinScale = 0.5f;
    float m_dynamicTargetFps = 60.0f;
    float m_smoothedFrameTime = 1.0f / 60.0f;
    int m_dynamicFrameCounter = 0;
    LoadingState m_loadingState;
    float m_displayProgress = 0.0f; // UI thread only, smoothed copy of progress
    std::unique_ptr<Galeshi::Texture> m_logoTexture;
    bool m_iblGenerated = false;
    bool m_projectLoaded = false;
    bool m_resetLayout = false;
    std::string m_gameProjectPath;
    EditorPreferences m_preferences;
    enum class PreferencesSection {
        General,
        Viewport,
        InputAxes,
        About,
    };
    PreferencesSection m_preferencesSection = PreferencesSection::General;
    bool m_showPreferences = false;
    bool m_showHistoryModal = false;
    bool m_showAudioMixer = false;
    bool m_showEnvironmentPalette = false;
    bool m_showStateMachineEditor = false;
    bool m_showBehaviorTreeEditor = false;
    bool m_showOutputLog = true;
    bool m_logAutoScroll = true;
    int m_logLevelFilter = 0;
    std::string m_outputLogBuffer;
    std::string m_outputLogSelection;
    entt::entity m_stateMachineEditorEntity = entt::null;
    entt::entity m_lastStateMachineSelection = entt::null;
    entt::entity m_behaviorTreeEditorEntity = entt::null;
    entt::entity m_boneAttachCharacter = entt::null;
    entt::entity m_boneAttachEntity = entt::null;
    std::string m_boneAttachBone;
    int m_selectedAxis = 0;
    int m_rebindAxis = -1;
    int m_rebindBinding = -1;
    std::vector<std::string> m_recentProjects;
    std::vector<std::string> m_recentScenes;
    bool m_pickingScene = false;
    char m_newProjectName[128] = "MyGame";
    char m_newProjectPath[512] = "";
    Galeshi::FilePickerDialog m_projectPicker;
    glm::vec3 m_lastIblSun{999.0f, 999.0f, 999.0f};

    // --- UI Designer state ---
    Galeshi::UIRuntime m_uiRuntime;
    Galeshi::UIRuntime m_uiPreviewRuntime;
    Galeshi::UICanvas m_uiPreviewCanvas;
    entt::entity m_uiCanvasEntity = entt::null;
    int m_uiWidgetIndex = -1;
    int m_uiTool = 0;               // 0 Select, 1 Move, 2 Rect
    bool m_uiShowDesigner = true;
    bool m_uiPreview = false;
    bool m_uiSafeArea = true;
    bool m_uiZTags = true;
    bool m_uiSnap = true;
    float m_uiSnapPx = 8.0f;
    int m_uiZoomPercent = 68;
    char m_uiSearch[64] = "";
    bool m_uiCanvasOverlay = true;
    bool m_uiDragActive = false;
    bool m_uiResizeMode = false;
    bool m_uiDragDirty = false;
    int m_uiResizeHandle = -1; // 0 TL, 1 TC, 2 TR, 3 MR, 4 BR, 5 BC, 6 BL, 7 ML
    int m_uiDragWidget = -1;
    ImVec2 m_uiDragStartMouse{0.0f, 0.0f};
    glm::vec2 m_uiDragStartPosition{0.0f};
    glm::vec2 m_uiDragStartSize{0.0f};

public:
    bool m_isGameMode = false;

    explicit GaleshiEditorApp(bool debugMode, bool isGameMode = false,
                              std::string gameProjectPath = {});

protected:
    // -------------------------------------------------------------------
    // Scene path helpers
    // -------------------------------------------------------------------
    static std::filesystem::path FindProjectRoot();

    std::string SceneStartDirectory() const;

    // -------------------------------------------------------------------
    // LIFECYCLE
    // -------------------------------------------------------------------
    void OnStart() override;

    void LoadGameStartScene(const std::string& projectFile);

    void OnFileDrop(const std::string& filepath, float x, float y) override;

    void LoadRecentProjects();

    void SaveRecentProjects();

    // Take the path by value: callers used to pass a reference to an element
    // of m_recentProjects, and erasing that element below invalidated the
    // reference before the path could be captured by the load lambda.
    void AddRecentProject(std::string path);

    // -------------------------------------------------------------------
    // Recent scenes
    // -------------------------------------------------------------------
    static std::filesystem::path RecentScenesPath();

    static std::string NormalizeRecentScenePath(const std::string& path);

    std::string RecentScenesProjectKey() const;

    void LoadRecentScenes();

    void SaveRecentScenes();

    void AddRecentScene(std::string path);

    void RemoveRecentScene(const std::string& path);

    void ClearRecentScenes();

    std::string RecentSceneLabel(const std::string& path) const;

    // -------------------------------------------------------------------
    // Input bindings
    // -------------------------------------------------------------------
    std::filesystem::path InputBindingsPath() const;

    void LoadInputBindings();

    void SaveInputBindings();

    void UpdateInputRebind();

    static std::filesystem::path PreferencesPath();

    void LoadPreferences();

    void SavePreferences();

    void DrawProjectManager();

    bool PlayerInputActive() const;

    void InitializeEditor();

    void OnUpdate(float dt) override;

    void OnRender() override;

    void OnImGui() override;

    void OnShutdown() override;

private:
    // -------------------------------------------------------------------
    // UI: Menu Bar
    // -------------------------------------------------------------------
    void StartAsyncLoad(const std::string& path,
                        std::function<void()> customLoad = nullptr,
                        bool cancelReturnsToProjectManager = false);

    // Called from OnUpdate every frame: joins the worker and, after a cancel,
    // destroys everything the loader may have already created.
    void UpdateLoadingState();

    void FinishCancelledLoad();

    // Shutdown variant of UnloadSceneContent: release all scene references
    // and runtime state without pushing an undo snapshot or reinitializing
    // navigation. Called while bgfx is still valid.
    void ClearSceneForShutdown();

    // The scene must release its shared mesh/material/texture references
    // before the AssetManager cache is cleared; only then are the bgfx buffers
    // and textures actually destroyed.
    void UnloadSceneContent(bool pushUndoSnapshot = true);

    // Reset scene-level state that survives an ECS registry clear (audio
    // buses, camera state, global post-process, TAA history, IBL, history,
    // UI references). Call after UnloadSceneContent and before building or
    // loading another scene.
    void ResetSceneStateForReplacement();

    void DrawLoadingModal();

    // -------------------------------------------------------------------
    // Preferences
    // -------------------------------------------------------------------
    void DrawHistoryModal();

    void DrawPreferencesWindow();

    // -------------------------------------------------------------------
    // Profiler
    // -------------------------------------------------------------------
    void ConfigureProfilerViewNames();

    void UpdateProfilerDebugState();

    void UpdateGpuProfilerStats();

    void PushProfilerHistory();

    void DrawProfilerWindow();

    void DrawMainMenuBar();

    // -------------------------------------------------------------------
    // Undo / Redo
    // -------------------------------------------------------------------
    void PushHistory();

    void Undo();

    void Redo();

    // -------------------------------------------------------------------
    // Save / Load / Delete
    // -------------------------------------------------------------------
    void SaveSceneToPath(const std::string& path);

    void SaveScene(bool saveAs = false);

    void LoadScene();

    void StopPlay();

    void StopPlayForSceneChange();

    void LoadSceneFromPath(const std::string& path);

    void NewScene();

    void CollectEntityHierarchy(entt::entity root,
                                std::unordered_set<entt::entity>& out);

    bool IsSkeletalCharacter(Galeshi::Entity entity);

    void ReleaseUnusedSceneAssets();

    void DeleteSelected();

    // -------------------------------------------------------------------
    // Dockspace
    // -------------------------------------------------------------------
    // -------------------------------------------------------------------
    // UI Designer
    // -------------------------------------------------------------------
    static std::string ToLowerCopy(std::string value);

    static const char* const* UIWidgetTypeNames();

    static const char* UIWidgetTypeName(Galeshi::UIWidgetType type);

    static const char* UIWidgetIdPrefix(Galeshi::UIWidgetType type);

    static glm::vec2 DefaultUIWidgetSize(Galeshi::UIWidgetType type);

    static const char* DefaultUIWidgetLabel(Galeshi::UIWidgetType type);

    static std::string MakeUIWidgetId(const Galeshi::UICanvas& canvas,
                                      const Galeshi::UIWidget& widget);

    static void SetUIAnchorPreset(Galeshi::UIWidget& widget,
                                  Galeshi::UIAnchorPreset preset);

    static int UIWidgetDepth(const Galeshi::UICanvas& canvas, size_t index);

    static bool UIEventInput(const char* label, std::string& value);

    void DispatchUIEvent(const Galeshi::UIEvent& event);

    entt::entity ActiveUICanvasEntity();

    Galeshi::UICanvas* ActiveUICanvas();

    Galeshi::UIWidget& AddUIWidget(Galeshi::UICanvas& canvas,
                                   Galeshi::UIWidgetType type,
                                   const std::string& label,
                                   glm::vec2 position,
                                   glm::vec2 size);

    void AddDefaultUIWidget(Galeshi::UIWidgetType type);

    void ApplyUITemplate(Galeshi::UICanvas& canvas, int preset);

    void CreateUICanvas(const char* name, int preset = -1);

    void DeleteUICanvas(entt::entity entity);

    void ReorderSelectedUIWidget(int mode);

    void DrawUILayers();

    void DrawUIDesigner();

    void DrawUICanvasProperties(Galeshi::UICanvas& canvas);

    void DrawUIWidgetProperties(Galeshi::UICanvas& canvas,
                                Galeshi::UIWidget& widget);

    bool DrawUICanvasOverlay(Galeshi::UICanvas& canvas, ImVec2 imageMin,
                             ImVec2 imageMax);

    void DrawDockspace();

    // -------------------------------------------------------------------
    // Gameplay camera
    // -------------------------------------------------------------------
    Galeshi::Entity FindGameplayCamera();

    bool IsEntityInHierarchy(entt::entity entity, entt::entity root);

    // Camera blocking is limited to authored colliders. Meshes without an
    // active ColliderComponent and trigger volumes never block the view.
    bool IsCameraCollisionBlocker(entt::entity entity,
                                  entt::entity ignoreRoot);

    bool IsGameplayCameraAttachedToPlayer(entt::entity cameraEntity);

    // Player-tagged entities are the orbit centre for the gameplay camera.
    // Prefer a Player in the camera's own hierarchy so a stray Player elsewhere
    // in the scene cannot make an attached camera jump away from its character.
    entt::entity FindGameplayCameraTarget(entt::entity cameraEntity);

    bool SetGameplayCameraMode(int mode);

    int GetGameplayCameraMode() const;

    glm::vec3 GetGameplayCameraMoveForward() const;

    glm::vec3 GetGameplayCameraMoveRight() const;

    // Cache the bounds centre, eye position and initial camera basis in world
    // space. Keeping the basis independent from the target transform prevents
    // character turning from rotating the player's camera.
    void RefreshGameplayCameraTarget(entt::entity target,
                                     entt::entity cameraEntity);

    void UpdateGameplayCamera();

    void EndEditorMouseLook();

    void EndGameMouseLook();

    void RestoreEditorCamera();

    bool ActivateGameplayCamera();

    void DeactivateGameplayCamera();

    // -------------------------------------------------------------------
    // Post-processing volumes
    // -------------------------------------------------------------------
    void CaptureSceneViewState();

    void ApplySceneViewState();

    float PssmPcfRadius() const;

    Galeshi::PostProcessSettings DefaultPostProcessSettings() const;

    // The active post-process source is the highest-priority enabled volume,
    // or the scene-global settings when no volume is enabled. Both the
    // Post-Process menu and the volume inspector edit this active source, so
    // the most recently changed control always wins instead of the menu being
    // silently shadowed by a volume.
    Galeshi::PostProcessVolumeComponent* ActivePostProcessVolume();

    Galeshi::PostProcessSettings& ActivePostProcessSettings();

    Galeshi::PostProcessSettings ResolvePostProcessSettings();

    glm::vec3 CameraForward() const;

    float ViewSpaceDepth(const glm::vec3& worldPoint) const;

    // Returns the world-space centre of all mesh bounds below a root entity.
    // For skeletal characters the meshes live on child entities, so the root
    // alone usually only has a feet-level transform.
    bool EntityWorldBoundsCenter(entt::entity root, glm::vec3& outCenter,
                                 glm::vec3* outMin = nullptr,
                                 glm::vec3* outMax = nullptr);

    entt::entity FindAutoFocusEntity();

    float AutoFocusDistance();

    float ResolveDofFocusDistance(const Galeshi::PostProcessSettings& s);

    void ApplyEnvironmentPalettePreset(
        Galeshi::PostProcessSettings& settings,
        const EnvironmentPalettePreset& preset);

    bool DrawEnvironmentPaletteControls(Galeshi::PostProcessSettings& settings);

    void DrawEnvironmentPaletteWindow();

    void DrawPostProcessVolumeSettings(Galeshi::PostProcessVolumeComponent& volume);

    void DrawWaterSettings(Galeshi::WaterComponent& water, entt::entity entity);

    void DrawGroomSettings(Galeshi::GroomComponent& groom, entt::entity entity);

    // Bone attachments for skeletal characters.
    void DrawBoneAttachmentSettings(entt::entity character);

    // -------------------------------------------------------------------
    // Audio
    // -------------------------------------------------------------------
    void UpdateAudio(float dt);

    void PaintVegetationAt(Galeshi::Entity landscapeEntity,
                           const Galeshi::LandscapeComponent& landscape,
                           const glm::vec3& localPosition,
                           bool paintGrass);

    void DrawCollisionShapeGizmos();

    // -------------------------------------------------------------------
    // Viewport & Picking & Sculpting
    // -------------------------------------------------------------------
    void DrawViewport();

    // Play-mode input focus follows the viewport: Play focuses it, Stop
    // releases it, and Shift+F1 can release it while playing.
    void SetViewportInputFocus(bool focused);

    void EnsurePlayerCameraPreviewResources();
    void ShutdownPlayerCameraPreview();
    bool RenderPlayerCameraPreview(entt::entity cameraEntity,
                                   bool renderThisFrame);

    struct LandscapeImage {
        int width = 0;
        int height = 0;
        std::vector<uint8_t> rgba;
        bool Valid() const { return width > 0 && height > 0 && !rgba.empty(); }
    };

    // shortcut: layer sources are decoded with stb (PNG/JPEG/BMP/TGA).
    // DDS/KTX layer maps fall back to the base texture. Upgrade this to the
    // engine's bimg container decoder if compressed layer sources are needed.
    bool LoadLandscapeImage(const std::string& path, LandscapeImage& out);

    static glm::vec4 SampleLandscapeImage(
        const LandscapeImage& image, float u, float v,
        const glm::vec4& fallback);

    static float SampleLandscapeWeight(
        const std::vector<float>& weights, int sizeX, int sizeZ,
        float x, float z);

    static bool SaveLandscapePng(const std::filesystem::path& path,
                                 const LandscapeImage& image);

    static std::string SafeLandscapeTextureName(const std::string& name);

    bool BakeLandscapeTextures(Galeshi::Entity entity);
    // Creates/resizes the per-layer texture-space paint masks and rebuilds
    // the baked maps at the requested resolution.
    bool SetLandscapeLayerTextureSize(Galeshi::Entity entity, int size);
    // High-resolution brush stroke for the active layer mask.
    bool PaintLandscapeLayerMask(Galeshi::Entity entity, int layer,
                                 float localX, float localZ, float radius,
                                 float strength, float falloff, float dt,
                                 bool erase);
    bool EnsureLandscapeLayerMasks(Galeshi::Entity entity);

    // -------------------------------------------------------------------
    // Details
    // -------------------------------------------------------------------
    void DrawTextureSlot(const char* label, std::shared_ptr<Galeshi::Texture>& slot,
                         bool srgb, bool* srgbFlag = nullptr,
                         bool* changed = nullptr);

    bool HasSkeletalParent(entt::entity handle);

    bool HasCollisionShapeParent(entt::entity handle);

    void SetLocalTransformFromWorld(entt::entity handle,
                                    const glm::mat4& world);

    // Detach is a collision-shape feature: a child can leave a collision
    // shape parent, but never a skeletal parent. The world transform is kept.
    void DetachChildFromCollisionShape(entt::entity child);

    void DetachCollisionShapeChildren(entt::entity parent);

    bool IsDescendantOf(entt::entity node, entt::entity ancestor);

    void DetachFromParent(entt::entity child);

    void DetachChildren(entt::entity parent);

    void AttachSelectedTo(entt::entity parent);

    void AttachPrimitiveMesh(entt::entity parent, bool sphere);

    bool MeshEntityWorldBounds(entt::entity handle, glm::vec3& outCenter,
                               glm::vec3& outExtents);

    void AccumulateEntityWorldBounds(entt::entity handle, glm::vec3& outMin,
                                     glm::vec3& outMax, int depth);

    bool EntityWorldBounds(entt::entity handle, glm::vec3& outCenter,
                           glm::vec3& outExtents);

    void FocusCameraOnEntity(entt::entity handle);

    void AttachCollisionShape(entt::entity parent,
                              Galeshi::ColliderShapeType type);

        void DrawOutliner();

    std::string FindCharacterModelPath(entt::entity entity);

    // Resolves a selection, mesh or wrapper to the entity that owns the
    // character's skeleton/state machine. Returns null for a shared parent
    // with several characters below it.
    entt::entity FindAsmeCharacter(entt::entity entity);

    bool AssignStateAnimationAsset(Galeshi::Entity entity, int stateIndex,
                                   const std::string& originalPath);

    std::filesystem::path AsmeSetupPathFor(entt::entity character);
    bool SaveAsmeSetup(entt::entity character);
    bool LoadAsmeSetup(entt::entity character);
    void SetAsmeCharacter(entt::entity character);

    void DrawAnimationStateMachineContents(
        Galeshi::Entity entity,
        const std::string& fallbackModelPath = {});

    void DrawStateMachineEditor();
    void DrawBehaviorTreeEditor();

    std::filesystem::path BehaviorTreeSetupPathFor(entt::entity agent);
    bool SaveBehaviorTreeSetup(entt::entity agent);
    bool LoadBehaviorTreeSetup(entt::entity agent);

void DrawDetails();

    // -------------------------------------------------------------------
    // Content Browser & Assets
    // -------------------------------------------------------------------
    void SetCodeEditorContent(const std::string& path,
                              const std::string& text,
                              bool cppMode);

    void OpenCodeFile(const std::string& path);

    void SyncCodeEditorStateToSerializer();

    void RestoreCodeEditorStateFromSerializer();

    static int DiagnosticLineNumber(const std::string& line);

    std::string FormatSyntaxDiagnostics(const std::string& diagnostics,
                                        bool cpp);

    struct CodeSyntaxResult {
        bool ok = false;
        bool cpp = false;
        std::string source;
        std::string path;
        std::string diagnostics;
    };

    void StartCodeSyntaxCheck();

    void PollCodeSyntaxCheck();

    std::string CodeEditorErrorsText() const;

    static std::string ShellQuote(const std::string& value);

    std::string FindShadercExecutable() const;

    std::filesystem::path ShaderIncludeDirectory() const;

    void DestroyShaderPreviewProgram();

    void ShutdownShaderPreview();

    void EnsureShaderPreviewResources();

    std::filesystem::path ResolveShaderSourcePath(
        const std::string& path) const;

    void CompileShaderPreview(const std::string& path,
                              const std::string& sourceOverride = {});

    void RenderShaderPreview();

    void DrawShaderPreviewPane();

    void DrawCodeEditor();

    void DrawAudioMixer();

    void DrawOutputLog();

    void DrawLuaConsole();

    void DrawScriptComponents();

    void DrawContentBrowser();

    void OpenFileDialog();

    void EnsureTreeTypeMaterial(Galeshi::VegetationTreeType& type,
                                int typeIndex);

    void EnsureTreeMaterialOverrides(
        Galeshi::VegetationTreeType& type,
        const Galeshi::AssetManager::LoadedModel& model);

    void EnsureLeafMaterial(Galeshi::VegetationLeafSet& leaf,
                            int typeIndex, int leafIndex);

    static bool IsLeafMeshName(const std::string& meshName,
                               const std::string& materialName);

    static std::string TreeMeshLabel(
        const Galeshi::AssetManager::LoadedModel& model, int index);

    static int SuggestedLeafMeshIndex(
        const Galeshi::AssetManager::LoadedModel& model,
        const std::vector<Galeshi::VegetationLeafSet>& leafSets);

    // Assign a static tree model to a Landscape's instanced vegetation.
    // The model is validated before the component/path is changed so a
    // broken FBX cannot silently replace a working tree mesh.
    void AssignLandscapeTreeModel(Galeshi::Entity landscapeEntity,
                                  const std::string& originalPath,
                                  int typeIndex = -1);

    // -------------------------------------------------------------------
    // Character model replacement
    // -------------------------------------------------------------------
    static std::string MaterialNameKey(const std::string& name);

    static float ModelMeshHeight(const std::vector<std::shared_ptr<Galeshi::Mesh>>& meshes);

    // Height of the already-authored model in world space. This must be
    // measured per mesh entity rather than through the wrapper's local scale:
    // the wrapper can be the skeleton root, a capsule child, or omitted
    // entirely for older scenes.
    float ModelEntityWorldHeight(
        const std::vector<entt::entity>& meshEntities);

    // Imported objects start with a non-metallic material. Clone the asset
    // material so this default does not mutate the AssetManager cache or
    // another instance the artist already adjusted manually.
    static std::shared_ptr<Galeshi::Material> ImportedMaterial(
        const std::shared_ptr<Galeshi::Material>& source,
        std::unordered_map<const Galeshi::Material*,
                           std::shared_ptr<Galeshi::Material>>& clones);

    // Applies a .mat master recipe to one material slot or every slot in the
    // selected entity hierarchy. Returns the number of slots updated.
    bool ApplyMaterialScript(Galeshi::MaterialComponent& slot,
                             const std::string& path);
    int ApplyMaterialScriptToHierarchy(entt::entity root,
                                       const std::string& path);
    // Reapplies an edited .mat to every material instance that uses it.
    int ReapplyMaterialScripts(const std::string& path);

    Galeshi::Entity CreateMaterialSlotEntity(
        entt::entity parent, const std::string& name,
        const std::shared_ptr<Galeshi::Material>& sourceMaterial);

    Galeshi::Entity FindSkeletalRootForReplacement(entt::entity target);

    bool PathsReferToSameAsset(const std::string& a, const std::string& b);

    Galeshi::Entity FindModelContainer(entt::entity skeletonRoot,
                                       const std::string& sourcePath,
                                       std::vector<entt::entity>& outMeshes);

    // Replace the skinned/static model below a character root (or the model
    // attached to its capsule) with a glTF/GLB asset. This is intentionally
    // separate from SpawnFromModel: the character root, capsule physics and
    // all hierarchy links stay intact.
    bool ReplaceCharacterModel(entt::entity target,
                               const std::string& originalPath);

    void OnAssetDoubleClicked(const std::string& path);

    // Attaches a .lua asset to an entity as a per-entity ScriptComponent.
    bool AttachScriptToEntity(entt::entity entity, const std::string& path);

    glm::vec3 ViewportDropWorldPosition(const ImVec2& mouse, const ImVec2& imageMin,
                                        uint16_t width, uint16_t height) const;

    void OnAssetDropped(const std::string& path, glm::vec3 pos);

    void SpawnFromModel(const std::string& originalPath, glm::vec3 pos,
                        bool placeAtPosition = false);

    // -------------------------------------------------------------------
    // Scene Helpers
    // -------------------------------------------------------------------
    Galeshi::Entity MakeCube(const std::string& name, glm::vec3 pos);

    Galeshi::Entity MakeSphere(const std::string& name, glm::vec3 pos);

    // -------------------------------------------------------------------
    // Collision shapes
    // -------------------------------------------------------------------
    glm::mat4 EntityWorldMatrix(entt::entity handle);

    Galeshi::Entity SpawnCollisionShape(const std::string& name,
                                        Galeshi::ColliderShapeType type);

    // Recreate the Jolt body after shape or rigid-body properties changed.
    // Bodies are only created when the scene is playing, so entities without
    // a runtime body are left alone.
    void RebuildPhysicsBody(Galeshi::Entity entity);
    // Resolve the mesh that owns cloth data. Model/skeletal roots redirect to
    // their first descendant mesh so cloth tools also work on the root.
    Galeshi::Entity FindClothMeshTarget(Galeshi::Entity selected);
    void RebuildCloth(Galeshi::Entity entity);

    // Creates runtime bodies for every authored collider that does not have
    // one yet. Called when Play starts.
    void EnsurePhysicsBodies();

    // Replaces the Ground box body with a Jolt heightfield sampled from the
    // main landscape. The Ground entity keeps its authored Transform while
    // Jolt drives the actual collision surface.
    void RebuildGroundHeightfield();

    // -------------------------------------------------------------------
    // Spawn a cube with a Jolt rigid body attached.
    // Always use this for physics cubes — it wires RigidBodyComponent,
    // motionType, mass, AND the Jolt body (CreateBox), so SyncTransformsToECS
    // will pick it up.
    // -------------------------------------------------------------------
    Galeshi::Entity SpawnPhysicsCube(const std::string& name, glm::vec3 pos,
                                     glm::vec3 scale = glm::vec3(1.0f));

    Galeshi::Entity SpawnPhysicsSphere(const std::string& name, glm::vec3 pos, float radius);

    Galeshi::Entity SpawnPhysicsCapsule(const std::string& name, glm::vec3 pos, float halfHeight, float radius);

    void BuildTestScene();

    // Carves/erodes the landscape under the selected water body and rebuilds
    // its mesh. Safe to call repeatedly; the pre-erosion heights are cached
    // in the landscape component for the current editor session.
    void ApplyWaterErosion(entt::entity waterEntity);

    // Creates or refreshes the child terrain owned by a water body, including
    // its own vegetation instance set. Does not modify the main landscape.
    void EnsureUnderwaterTerrain(entt::entity waterEntity);

    // Recomputes the auto-vegetation exclusion masks from all carved water
    // bodies so land grass/trees do not spawn on the submerged bed.
    void RebuildWaterVegetationMasks();

    // Pushes the water's shore detail settings onto the carved landscape and
    // water-bed materials. Cheap; safe to call while editing the sliders.
    void ApplyWaterShoreSettings(entt::entity waterEntity);

    // Repaints the sand/gravel layer on an already-carved scene without
    // touching the carved heights. Used on load for scenes saved before the
    // shore paint rules changed.
    void RefreshWaterShorePaint(entt::entity waterEntity);

    // Buoyancy, drag and entry splashes for dynamic bodies inside a water
    // body. Runs after scripts so jump/movement velocity is already applied.
    void UpdateWaterInteraction(float dt);

    // Resolves the water-selected character bone for bubble emission.
    bool BubbleOriginForWater(const Galeshi::WaterComponent& water,
                              const glm::vec3& fallback,
                              glm::vec3& origin);

    // State.
    Galeshi::Scene          m_scene;
    Galeshi::Camera         m_camera;
    Galeshi::EditorState    m_editor;
    bool                    m_usingGameCamera = false;
    bool                    m_gameLooking = false;
    bool                    m_playerStrafing = false;
    int                     m_gameLookSkipFrames = 0;
    entt::entity            m_activeGameCamera = entt::null;
    entt::entity            m_gameCameraTarget = entt::null;
    glm::vec3               m_gameCameraPivotLocal{0.0f};
    glm::vec3               m_gameCameraEyeLocal{0.0f, 1.6f, 0.0f};
    glm::vec3               m_gameCameraBaseOffsetLocal{0.0f};
    glm::vec3               m_gameCameraBaseForward{0.0f, 0.0f, -1.0f};
    glm::vec3               m_gameCameraBaseUp{0.0f, 1.0f, 0.0f};
    bool                    m_gameCameraBaseValid = false;
    GaleshiScriptCameraMode m_gameCameraMode = GALESHI_CAMERA_THIRD_PERSON;
    float                   m_gameCameraYawOffset = 0.0f;
    float                   m_gameCameraPitchOffset = 0.0f;
    float                   m_time = 0.0f;
    float                   m_frameDelta = 1.0f / 60.0f;
    int                     m_renderFrameCounter = 0;
    bool                    m_pendingEffectsEnable = false;
    // Skip expensive optional GPU calculations while still rendering the
    // viewport. Physics, animation, scripts, AI and camera movement continue.
    bool                    m_performanceMode = false;
    bgfx::TextureHandle     m_lastViewportTexture = BGFX_INVALID_HANDLE;

    // Small offscreen render target for the selected player camera. The
    // preview uses the forward path so it can share the scene's main render
    // resources without disturbing the editor framebuffer or post chain.
    static constexpr bgfx::ViewId m_playerCameraPreviewView = 24;
    static constexpr bgfx::ViewId m_playerCameraPreviewTonemapView = 25;
    static constexpr uint16_t m_playerCameraPreviewW = 320;
    static constexpr uint16_t m_playerCameraPreviewH = 180;
    bgfx::TextureHandle     m_playerCameraPreviewColor = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle     m_playerCameraPreviewDepth = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle     m_playerCameraPreviewOutput = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle m_playerCameraPreviewFbo = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle m_playerCameraPreviewOutputFbo = BGFX_INVALID_HANDLE;

    std::unique_ptr<Galeshi::AssetManager>    m_assets;
    std::unique_ptr<Galeshi::RenderSystem>    m_render;
    std::unique_ptr<Galeshi::Framebuffer>     m_framebuffer;
    std::unique_ptr<Galeshi::ContentBrowser>  m_browser;
    std::unique_ptr<Galeshi::SceneSerializer> m_serializer;
    std::unique_ptr<Galeshi::EnginePhysicsSystem>   m_physics;
    // Runtime body submersion state used for water entry splashes.
    std::unordered_map<entt::entity, bool>          m_waterSubmerged;
    std::unique_ptr<Galeshi::NavigationSystem> m_navSystem;
    std::unique_ptr<Galeshi::DebugRenderer> m_debug;
    std::unique_ptr<Galeshi::ParticleSystem> m_particles;
    std::unique_ptr<Galeshi::FluidSimulation> m_fluid;
    float m_bubbleAccumulator = 0.0f;
    glm::vec3 m_fluidColor{0.55f, 0.80f, 0.95f};
    std::unordered_map<entt::entity, float> m_bodyWaterSurfaceY;
    std::unordered_map<entt::entity, entt::entity> m_bodyWaterEntity;
    std::unordered_map<entt::entity, glm::vec2> m_bodyWaterUV;
    // Body -> water whose trigger begin event has been sent, so the matching
    // end event fires exactly once even when the body changes water bodies.
    std::unordered_map<entt::entity, entt::entity> m_waterTriggerBodies;
    bgfx::TextureHandle m_shadowMaps[3] = { BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE };
    bgfx::FrameBufferHandle m_shadowFbos[3] = { BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE };
    bgfx::FrameBufferHandle m_pointShadowFbos[6] = { BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE };

    enum class ViewportViewMode { Lit, Unlit, Wireframe };

    void SetViewportViewMode(ViewportViewMode mode);

    uint16_t    m_viewportW = 0, m_viewportH = 0;
    bool        m_viewportHovered = false;
    bool        m_viewportInputFocused = false;
    ViewportViewMode m_viewportViewMode = ViewportViewMode::Lit;
    Galeshi::RenderSystem::State m_savedRenderState;
    bool        m_viewModeStateSaved = false;
    bool        m_editorLooking = false;
    bool        m_editorLookDragging = false;
    int         m_editorLookSkipFrames = 0;
    bool        m_uiOverlayHovered = false;
    bool        m_uiItemWasActive = false;
    ImVec2      m_viewportImagePos = {0.0f, 0.0f};
    ImVec2      m_viewportImageSize = {0.0f, 0.0f};
    std::string m_currentScenePath;
    Galeshi::FilePickerDialog m_filePicker;
    Galeshi::FilePickerDialog m_filePickerSave;
    Galeshi::FilePickerDialog m_codeFilePicker;
    Galeshi::FilePickerDialog m_scenePicker;
    Galeshi::FilePickerDialog m_vegetationMeshPicker;
    entt::entity m_vegetationMeshTarget = entt::null;
    int m_vegetationMeshTypeIndex = -1;
    // Last export/import result shown in the Details Material Preset panel.
    std::string m_matPresetStatus;
    // Last PP.settings export/import result for post-process volumes.
    std::string m_ppSettingsStatus;
    Galeshi::FilePickerDialog m_characterModelPicker;
    entt::entity m_characterModelTarget = entt::null;
    Galeshi::FilePickerDialog m_animationClipPicker;
    entt::entity m_animationClipTarget = entt::null;
    int m_animationClipState = -1;
    Galeshi::FilePickerDialog m_audioPicker;
    entt::entity m_audioPickerTarget = entt::null;
    int m_paintTreeType = -1;

    // Details search. The scroll flag makes the first matching section jump
    // into view only when the query changes, so the user can still scroll
    // through results after that.
    std::string m_detailsSearch;
    char m_detailsSearchBuffer[256] = {};
    bool m_detailsSearchScrollPending = false;
    TextEditor m_codeEditor;
    std::string m_currentCodeFile;
    bool m_codeEditorInitialized = false;
    bool m_codeEditorCppMode = false;
    int  m_codeEditorTabRequest = -1;
    std::string m_codeEditorSyntaxErrors;
    std::string m_codeEditorLastSyntaxSource;
    std::string m_shaderPreviewPath;
    std::string m_shaderPreviewError;
    static constexpr bgfx::ViewId m_shaderPreviewView = 60;
    bgfx::ProgramHandle m_shaderPreviewProgram = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle m_shaderPreviewFbo = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_shaderPreviewTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_shaderPreviewFallbackTex = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle m_shaderPreviewVB = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle m_shaderPreviewIB = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_shaderPreviewSampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_shaderPreviewTime = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_shaderPreviewResolution = BGFX_INVALID_HANDLE;
    std::string m_codeEditorSyntaxPendingSource;
    std::string m_codeEditorSyntaxPendingPath;
    std::future<CodeSyntaxResult> m_codeEditorSyntaxFuture;
    bool m_codeEditorSyntaxRunning = false;
    double m_codeEditorLastEditTime = 0.0;
    bool m_codeEditorSyntaxDirty = false;
    bool m_focusCodeEditor = false;

    std::vector<std::string> m_history;
    int  m_historyIndex = -1;
    bool m_isRestoringHistory = false;
    bool m_suppressHistory = false;
    bool m_wasUsingGizmo = false;
    bool m_wasSculpting = false;
    entt::entity m_pendingLandscapeBake = entt::null;
    float m_lastSculptHeight = 0.0f;
    bool m_lastBrushHitValid = false;
    glm::vec3 m_lastBrushLocal{0.0f};
    float m_lastBrushRadius = 1.0f;
    glm::vec3 m_lastPaintLocal{0.0f};
    bool m_paintStampValid = false;
    std::mt19937 m_paintRng{0x51ed270bu};
    bool m_showAIDebug = true;

    // Profiler state and rolling frame history.
    bool m_showProfiler = false;
    bool m_profilerDebugEnabled = false;
    CpuProfileSample m_cpuProfile;
    std::vector<GpuPassSample> m_gpuPasses;
    std::array<std::string, 256> m_profilerViewNames{};
    float m_gpuFrameMs = 0.0f;
    std::array<float, kProfileHistorySize> m_frameHistory{};
    std::array<float, kProfileHistorySize> m_gameLogicHistory{};
    std::array<float, kProfileHistorySize> m_gpuHistory{};

    // Screen-space translate drag (LMB-only, no W/E/R arrows)
    bool      m_isDragging = false;
    glm::vec2 m_dragStartMouse{0.0f, 0.0f};
    glm::vec3 m_dragStartObjWorld{0.0f};
};