#pragma once

#include "GaleshiEngine/Scripting/ScriptEngine.hpp"
#include "GaleshiEngine/Scene/Components.hpp"
#include "GaleshiEngine/AI/BehaviorTree.hpp"
#include "GaleshiEngine/Assets/Material.hpp"
#include "GaleshiEngine/Renderer/PostProcessManager.hpp"
#include "GaleshiEngine/UI/UIWidgets.hpp"

namespace Galeshi::ScriptEngineInternal {



struct PostProcessBoolField {
    const char* name;
    bool PostProcessSettings::* member;
};
struct PostProcessFloatField {
    const char* name;
    float PostProcessSettings::* member;
};
struct PostProcessIntField {
    const char* name;
    int PostProcessSettings::* member;
};
struct PostProcessVec3Field {
    const char* name;
    glm::vec3 PostProcessSettings::* member;
};

inline constexpr PostProcessBoolField kPostProcessBools[] = {
    {"bloom", &PostProcessSettings::bloomEnabled},
    {"dof", &PostProcessSettings::dofEnabled},
    {"dof_auto_focus", &PostProcessSettings::dofAutoFocus},
    {"motion_blur", &PostProcessSettings::motionBlurEnabled},
    {"environment_palette",
     &PostProcessSettings::environmentPaletteEnabled},
    {"ssao", &PostProcessSettings::ssaoEnabled},
    {"ssr", &PostProcessSettings::ssrEnabled},
    {"ssgi", &PostProcessSettings::ssgiEnabled},
    {"fxaa", &PostProcessSettings::fxaaEnabled},
    {"smaa", &PostProcessSettings::smaaEnabled},
    {"taa", &PostProcessSettings::taaEnabled},
    {"god_rays", &PostProcessSettings::godRaysEnabled},
    {"atmosphere", &PostProcessSettings::atmosphereEnabled},
    {"fog", &PostProcessSettings::fogEnabled},
    {"fog_volumetric", &PostProcessSettings::fogVolumetric},
    {"invert", &PostProcessSettings::invertEnabled},
};

inline constexpr PostProcessFloatField kPostProcessFloats[] = {
    {"bloom_threshold", &PostProcessSettings::bloomThreshold},
    {"bloom_intensity", &PostProcessSettings::bloomIntensity},
    {"bloom_radius", &PostProcessSettings::bloomRadius},
    {"dof_focus_distance", &PostProcessSettings::dofFocusDistance},
    {"dof_blur", &PostProcessSettings::dofBlur},
    {"motion_blur_amount", &PostProcessSettings::motionBlurAmount},
    {"motion_blur_max_velocity",
     &PostProcessSettings::motionBlurMaxVelocity},
    {"exposure", &PostProcessSettings::exposure},
    {"contrast", &PostProcessSettings::contrast},
    {"saturation", &PostProcessSettings::saturation},
    {"color_exposure", &PostProcessSettings::colorExposure},
    {"color_contrast", &PostProcessSettings::colorContrast},
    {"color_saturation", &PostProcessSettings::colorSaturation},
    {"environment_palette_amount",
     &PostProcessSettings::environmentPaletteAmount},
    {"vignette_intensity", &PostProcessSettings::vignetteIntensity},
    {"vignette_smoothness", &PostProcessSettings::vignetteSmoothness},
    {"chromatic_aberration",
     &PostProcessSettings::chromaticAberration},
    {"ssao_radius", &PostProcessSettings::ssaoRadius},
    {"ssao_bias", &PostProcessSettings::ssaoBias},
    {"ssao_max_distance", &PostProcessSettings::ssaoMaxDistance},
    {"ssao_strength", &PostProcessSettings::ssaoStrength},
    {"ao_resolution_scale",
     &PostProcessSettings::aoResolutionScale},
    {"ao_radius", &PostProcessSettings::aoRadius},
    {"ao_bias", &PostProcessSettings::aoBias},
    {"ao_thickness", &PostProcessSettings::aoThickness},
    {"ao_max_distance", &PostProcessSettings::aoMaxDistance},
    {"ao_strength", &PostProcessSettings::aoStrength},
    {"ssr_intensity", &PostProcessSettings::ssrIntensity},
    {"ssgi_intensity", &PostProcessSettings::ssgiIntensity},
    {"ssgi_radius", &PostProcessSettings::ssgiRadius},
    {"ssgi_thickness", &PostProcessSettings::ssgiThickness},
    {"taa_blend", &PostProcessSettings::taaBlend},
    {"god_rays_intensity",
     &PostProcessSettings::godRaysIntensity},
    {"god_rays_decay", &PostProcessSettings::godRaysDecay},
    {"god_rays_density", &PostProcessSettings::godRaysDensity},
    {"god_rays_weight", &PostProcessSettings::godRaysWeight},
    {"atmosphere_intensity",
     &PostProcessSettings::atmosphereIntensity},
    {"fog_density", &PostProcessSettings::fogDensity},
    {"fog_height_falloff",
     &PostProcessSettings::fogHeightFalloff},
    {"fog_base_height", &PostProcessSettings::fogBaseHeight},
    {"fog_start_distance",
     &PostProcessSettings::fogStartDistance},
    {"fog_max_distance", &PostProcessSettings::fogMaxDistance},
    {"fog_sun_scattering",
     &PostProcessSettings::fogSunScattering},
    {"fog_anisotropy", &PostProcessSettings::fogAnisotropy},
};

inline constexpr PostProcessIntField kPostProcessInts[] = {
    {"ao_directions", &PostProcessSettings::aoDirections},
    {"ao_steps", &PostProcessSettings::aoSteps},
    {"ssgi_rays", &PostProcessSettings::ssgiRays},
    {"ssgi_steps", &PostProcessSettings::ssgiSteps},
    {"fog_steps", &PostProcessSettings::fogSteps},
};

inline constexpr PostProcessVec3Field kPostProcessVec3s[] = {
    {"environment_palette_shadow",
     &PostProcessSettings::environmentPaletteShadow},
    {"environment_palette_mid",
     &PostProcessSettings::environmentPaletteMid},
    {"environment_palette_highlight",
     &PostProcessSettings::environmentPaletteHighlight},
    {"fog_color", &PostProcessSettings::fogColor},
};

template <typename T, std::size_t N>
const T* FindPostProcessField(const T (&fields)[N],
                              const std::string& name) {
    for (const T& field : fields) {
        if (name == field.name) return &field;
    }
    return nullptr;
}

struct UIBoolField {
    const char* name;
    bool UIWidget::* member;
};
struct UIFloatField {
    const char* name;
    float UIWidget::* member;
};
struct UIIntField {
    const char* name;
    int UIWidget::* member;
};
struct UIStringField {
    const char* name;
    std::string UIWidget::* member;
};
struct UIVec2Field {
    const char* name;
    glm::vec2 UIWidget::* member;
};
struct UIVec4Field {
    const char* name;
    glm::vec4 UIWidget::* member;
};

inline constexpr UIBoolField kUIBools[] = {
    {"visible", &UIWidget::visible},
    {"enabled", &UIWidget::enabled},
    {"interactable", &UIWidget::interactable},
    {"block_input", &UIWidget::blockInput},
    {"checked", &UIWidget::checked},
    {"use_color", &UIWidget::useColor},
    {"use_text_color", &UIWidget::useTextColor},
    {"use_border_color", &UIWidget::useBorderColor},
};
inline constexpr UIFloatField kUIFloats[] = {
    {"opacity", &UIWidget::opacity},
    {"button_height", &UIWidget::buttonHeight},
    {"slider_min", &UIWidget::sliderMin},
    {"slider_max", &UIWidget::sliderMax},
    {"value", &UIWidget::sliderValue},
    {"slider_step", &UIWidget::sliderStep},
    {"font_size", &UIWidget::fontSize},
    {"text_wrap_width", &UIWidget::textWrapWidth},
    {"rotation", &UIWidget::rotation},
    {"radius", &UIWidget::radius},
    {"border", &UIWidget::border},
};
inline constexpr UIIntField kUIInts[] = {
    {"parent", &UIWidget::parent},
    {"z_order", &UIWidget::zOrder},
    {"selected_option", &UIWidget::selectedOption},
    {"max_chars", &UIWidget::maxChars},
};
inline constexpr UIStringField kUIStrings[] = {
    {"label", &UIWidget::label},
    {"tooltip", &UIWidget::tooltip},
    {"image", &UIWidget::imagePath},
    {"font", &UIWidget::fontPath},
    {"on_click", &UIWidget::onClick},
    {"on_hover", &UIWidget::onHover},
    {"on_show", &UIWidget::onShow},
};
inline constexpr UIVec2Field kUIVec2s[] = {
    {"position", &UIWidget::position},
    {"size", &UIWidget::size},
    {"pivot", &UIWidget::pivot},
    {"anchor_min", &UIWidget::anchorMin},
    {"anchor_max", &UIWidget::anchorMax},
    {"text_offset", &UIWidget::textOffset},
};
inline constexpr UIVec4Field kUIVec4s[] = {
    {"color", &UIWidget::color},
    {"text_color", &UIWidget::textColor},
    {"border_color", &UIWidget::borderColor},
};

struct CanvasBoolField {
    const char* name;
    bool UICanvas::* member;
};
struct CanvasFloatField {
    const char* name;
    float UICanvas::* member;
};
struct CanvasStringField {
    const char* name;
    std::string UICanvas::* member;
};
struct CanvasVec2Field {
    const char* name;
    glm::vec2 UICanvas::* member;
};
inline constexpr CanvasBoolField kCanvasBools[] = {
    {"visible", &UICanvas::visible},
    {"resizable", &UICanvas::resizable},
    {"movable", &UICanvas::movable},
    {"closeable", &UICanvas::closeable},
};
inline constexpr CanvasFloatField kCanvasFloats[] = {
    {"background_alpha", &UICanvas::backgroundAlpha},
    {"match_width_or_height", &UICanvas::matchWidthOrHeight},
};
inline constexpr CanvasStringField kCanvasStrings[] = {
    {"title", &UICanvas::title},
    {"on_menu_open", &UICanvas::onMenuOpen},
    {"on_menu_hover", &UICanvas::onMenuHover},
    {"on_menu_click", &UICanvas::onMenuClick},
};
inline constexpr CanvasVec2Field kCanvasVec2s[] = {
    {"screen_position", &UICanvas::screenPosition},
    {"screen_size", &UICanvas::screenSize},
};

struct VegetationBoolField {
    const char* name;
    bool VegetationComponent::* member;
};
struct VegetationIntField {
    const char* name;
    int VegetationComponent::* member;
};
struct VegetationFloatField {
    const char* name;
    float VegetationComponent::* member;
};
inline constexpr VegetationBoolField kVegetationBools[] = {
    {"enabled", &VegetationComponent::enabled},
    {"cast_shadows", &VegetationComponent::castShadows},
    {"grass_cast_shadows", &VegetationComponent::grassCastShadows},
    {"hide_default_instances",
     &VegetationComponent::hideDefaultInstances},
};
inline constexpr VegetationIntField kVegetationInts[] = {
    {"tree_count", &VegetationComponent::treeCount},
    {"grass_count", &VegetationComponent::grassCount},
};
inline constexpr VegetationFloatField kVegetationFloats[] = {
    {"tree_min_scale", &VegetationComponent::treeMinScale},
    {"tree_max_scale", &VegetationComponent::treeMaxScale},
    {"grass_min_scale", &VegetationComponent::grassMinScale},
    {"grass_max_scale", &VegetationComponent::grassMaxScale},
    {"slope_limit", &VegetationComponent::slopeLimit},
    {"paint_density", &VegetationComponent::paintDensity},
    {"paint_size", &VegetationComponent::paintSize},
};

struct ParticleBoolField {
    const char* name;
    bool ParticleEmitterComponent::* member;
};
struct ParticleIntField {
    const char* name;
    uint32_t ParticleEmitterComponent::* member;
};
struct ParticleFloatField {
    const char* name;
    float ParticleEmitterComponent::* member;
};
struct ParticleVec3Field {
    const char* name;
    glm::vec3 ParticleEmitterComponent::* member;
};
struct ParticleVec4Field {
    const char* name;
    glm::vec4 ParticleEmitterComponent::* member;
};
inline constexpr ParticleBoolField kParticleBools[] = {
    {"enabled", &ParticleEmitterComponent::enabled},
};
inline constexpr ParticleIntField kParticleInts[] = {
    {"max_particles", &ParticleEmitterComponent::maxParticles},
};
inline constexpr ParticleFloatField kParticleFloats[] = {
    {"emit_rate", &ParticleEmitterComponent::emitRate},
    {"lifetime", &ParticleEmitterComponent::lifetime},
    {"lifetime_variance", &ParticleEmitterComponent::lifetimeVariance},
    {"start_size", &ParticleEmitterComponent::startSize},
    {"end_size", &ParticleEmitterComponent::endSize},
    {"velocity_variance", &ParticleEmitterComponent::velocityVariance},
    {"spawn_radius", &ParticleEmitterComponent::spawnRadius},
    {"drag", &ParticleEmitterComponent::drag},
};
inline constexpr ParticleVec3Field kParticleVec3s[] = {
    {"velocity", &ParticleEmitterComponent::velocity},
    {"spawn_box_size", &ParticleEmitterComponent::spawnBoxSize},
    {"gravity", &ParticleEmitterComponent::gravity},
};
inline constexpr ParticleVec4Field kParticleVec4s[] = {
    {"start_color", &ParticleEmitterComponent::startColor},
    {"end_color", &ParticleEmitterComponent::endColor},
};

struct GroomBoolField {
    const char* name;
    bool GroomComponent::* member;
};
struct GroomIntField {
    const char* name;
    int GroomComponent::* member;
};
struct GroomFloatField {
    const char* name;
    float GroomComponent::* member;
};
struct GroomVec3Field {
    const char* name;
    glm::vec3 GroomComponent::* member;
};
struct GroomStringField {
    const char* name;
    std::string GroomComponent::* member;
};
inline constexpr GroomBoolField kGroomBools[] = {
    {"enabled", &GroomComponent::enabled},
    {"simulate", &GroomComponent::simulate},
    {"visible_in_editor", &GroomComponent::visibleInEditor},
};
inline constexpr GroomIntField kGroomInts[] = {
    {"guide_count", &GroomComponent::guideCount},
    {"strands_per_guide", &GroomComponent::strandsPerGuide},
    {"segments", &GroomComponent::segments},
};
inline constexpr GroomFloatField kGroomFloats[] = {
    {"length", &GroomComponent::length},
    {"width", &GroomComponent::width},
    {"root_width", &GroomComponent::rootWidth},
    {"tip_width", &GroomComponent::tipWidth},
    {"clump_radius", &GroomComponent::clumpRadius},
    {"clumping", &GroomComponent::clumping},
    {"frizz", &GroomComponent::frizz},
    {"curl", &GroomComponent::curl},
    {"volume", &GroomComponent::volume},
    {"scalp_start", &GroomComponent::scalpStart},
    {"scalp_end", &GroomComponent::scalpEnd},
    {"normal_threshold", &GroomComponent::normalThreshold},
    {"gravity", &GroomComponent::gravity},
    {"stiffness", &GroomComponent::stiffness},
    {"damping", &GroomComponent::damping},
    {"wind_strength", &GroomComponent::windStrength},
    {"wind_frequency", &GroomComponent::windFrequency},
    {"specular", &GroomComponent::specular},
    {"shininess", &GroomComponent::shininess},
    {"ambient", &GroomComponent::ambient},
    {"opacity", &GroomComponent::opacity},
    {"random_tint", &GroomComponent::randomTint},
};
inline constexpr GroomVec3Field kGroomVec3s[] = {
    {"groom_direction", &GroomComponent::groomDirection},
    {"wind_direction", &GroomComponent::windDirection},
    {"root_color", &GroomComponent::rootColor},
    {"tip_color", &GroomComponent::tipColor},
};
inline constexpr GroomStringField kGroomStrings[] = {
    {"target_mesh_name", &GroomComponent::targetMeshName},
};

struct NavAgentBoolField {
    const char* name;
    bool NavAgentComponent::* member;
};
struct NavAgentFloatField {
    const char* name;
    float NavAgentComponent::* member;
};
struct NavAgentVec3Field {
    const char* name;
    glm::vec3 NavAgentComponent::* member;
};
inline constexpr NavAgentBoolField kNavAgentBools[] = {
    {"has_target", &NavAgentComponent::hasTarget},
};
inline constexpr NavAgentFloatField kNavAgentFloats[] = {
    {"speed", &NavAgentComponent::speed},
    {"stopping_distance", &NavAgentComponent::stoppingDistance},
};
inline constexpr NavAgentVec3Field kNavAgentVec3s[] = {
    {"target_position", &NavAgentComponent::targetPosition},
};

struct PerceptionBoolField {
    const char* name;
    bool PerceptionComponent::* member;
};
struct PerceptionFloatField {
    const char* name;
    float PerceptionComponent::* member;
};
struct PerceptionStringField {
    const char* name;
    std::string PerceptionComponent::* member;
};
inline constexpr PerceptionBoolField kPerceptionBools[] = {
    {"require_line_of_sight", &PerceptionComponent::requireLineOfSight},
    {"detect_player_only", &PerceptionComponent::detectPlayerOnly},
    {"can_see_target", &PerceptionComponent::canSeeTarget},
};
inline constexpr PerceptionFloatField kPerceptionFloats[] = {
    {"sight_radius", &PerceptionComponent::sightRadius},
    {"field_of_view_degrees", &PerceptionComponent::fieldOfViewDegrees},
    {"hearing_radius", &PerceptionComponent::hearingRadius},
    {"eye_height", &PerceptionComponent::eyeHeight},
    {"detection_interval", &PerceptionComponent::detectionInterval},
    {"memory_duration", &PerceptionComponent::memoryDuration},
    {"time_since_seen", &PerceptionComponent::timeSinceSeen},
};
inline constexpr PerceptionStringField kPerceptionStrings[] = {
    {"target_name", &PerceptionComponent::targetName},
};

struct BehaviorTreeBoolField {
    const char* name;
    bool BehaviorTreeComponent::* member;
};
struct BehaviorTreeIntField {
    const char* name;
    int BehaviorTreeComponent::* member;
};
struct BehaviorTreeFloatField {
    const char* name;
    float BehaviorTreeComponent::* member;
};
inline constexpr BehaviorTreeBoolField kBehaviorTreeBools[] = {
    {"enabled", &BehaviorTreeComponent::enabled},
};
inline constexpr BehaviorTreeIntField kBehaviorTreeInts[] = {
    {"root", &BehaviorTreeComponent::root},
    {"running_node", &BehaviorTreeComponent::runningNode},
};
inline constexpr BehaviorTreeFloatField kBehaviorTreeFloats[] = {
    {"tick_interval", &BehaviorTreeComponent::tickInterval},
    {"tick_accumulator", &BehaviorTreeComponent::tickAccumulator},
};

struct AIControllerStringField {
    const char* name;
    std::string AIControllerComponent::* member;
};
inline constexpr AIControllerStringField kAIControllerStrings[] = {
    {"script_path", &AIControllerComponent::scriptPath},
};

struct MaterialFloatField {
    const char* name;
    float Material::* member;
};
struct MaterialBoolField {
    const char* name;
    bool Material::* member;
};
struct MaterialVec4Field {
    const char* name;
    glm::vec4 Material::* member;
};
inline constexpr MaterialFloatField kMaterialFloats[] = {
    {"metallic", &Material::metallicFactor},
    {"roughness", &Material::roughnessFactor},
    {"specular", &Material::specularFactor},
    {"normal_strength", &Material::normalStrength},
    {"occlusion_strength", &Material::occlusionStrength},
    {"alpha_cutoff", &Material::alphaCutoff},
    {"glass_opacity", &Material::glassOpacity},
    {"shadow_depth_offset", &Material::shadowDepthOffset},
};
inline constexpr MaterialBoolField kMaterialBools[] = {
    {"backface_culling", &Material::backfaceCulling},
    {"alpha_test", &Material::alphaTest},
    {"alpha_blend", &Material::alphaBlend},
    {"casts_shadow", &Material::castsShadow},
    {"subsurface", &Material::subsurfaceEnabled},
    {"translucency", &Material::translucencyEnabled},
};
inline constexpr MaterialVec4Field kMaterialVec4s[] = {
    {"albedo_factor", &Material::albedoFactor},
};


} // namespace Galeshi::ScriptEngineInternal
