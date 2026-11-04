#pragma once

namespace Galeshi {

class Renderer;
class InputAxisSystem;
class Scene;
class AssetManager;
class RenderSystem;
class PostProcessManager;
class EnginePhysicsSystem;
class NavigationSystem;
class AnimationSystem;
class AudioSystem;
class UIRuntime;
class ParticleSystem;
class GroomSystem;
class DebugRenderer;
class ScriptEngine;
class SceneSerializer;
class JobSystem;

} // namespace Galeshi

namespace Galeshi::Api {

// Optional convenience view for native (link-based) games and plugins.
//
// The context does not own or create subsystems; it simply groups the public
// pointers a game normally has. Hosts can fill the fields they use and pass
// the context to their own gameplay layer. This keeps subsystem wiring
// explicit while providing a single place to discover the engine surface.
struct EngineContext {
    Renderer* renderer = nullptr;
    InputAxisSystem* input = nullptr;
    Scene* scene = nullptr;
    AssetManager* assets = nullptr;
    RenderSystem* renderSystem = nullptr;
    PostProcessManager* postProcess = nullptr;
    EnginePhysicsSystem* physics = nullptr;
    NavigationSystem* navigation = nullptr;
    AnimationSystem* animation = nullptr;
    AudioSystem* audio = nullptr;
    UIRuntime* ui = nullptr;
    ParticleSystem* particles = nullptr;
    GroomSystem* groom = nullptr;
    DebugRenderer* debugRenderer = nullptr;
    ScriptEngine* scripting = nullptr;
    SceneSerializer* serializer = nullptr;
    JobSystem* jobs = nullptr;
};

} // namespace Galeshi::Api
