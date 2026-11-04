#pragma once

// Public C++ API umbrella for native games, plugins and tools.
//
// This header intentionally excludes editor-only classes (ContentBrowser,
// EditorState, FilePickerDialog). Everything else in the engine's public
// `engine/include/GaleshiEngine` tree is available through this include.
//
// Link against the CMake target `GaleshiEngine::GaleshiEngine`.

#include "GaleshiEngine/Api/EngineContext.hpp"
#include "GaleshiEngine/Api/Version.hpp"

#include "GaleshiEngine/AI/AISystem.hpp"
#include "GaleshiEngine/AI/BehaviorTree.hpp"
#include "GaleshiEngine/AI/NavigationSystem.hpp"

#include "GaleshiEngine/Animation/AnimationSystem.hpp"

#include "GaleshiEngine/Assets/AssetManager.hpp"
#include "GaleshiEngine/Assets/LandscapeMesh.hpp"
#include "GaleshiEngine/Assets/Material.hpp"
#include "GaleshiEngine/Assets/Mesh.hpp"
#include "GaleshiEngine/Assets/Texture.hpp"
#include "GaleshiEngine/Assets/VegetationGenerator.hpp"

#include "GaleshiEngine/Audio/AudioSystem.hpp"
#include "GaleshiEngine/Audio/AudioTypes.hpp"

#include "GaleshiEngine/Core/Application.hpp"
#include "GaleshiEngine/Core/JobSystem.hpp"
#include "GaleshiEngine/Core/ProjectManager.hpp"

#include "GaleshiEngine/Input/InputAxisSystem.hpp"

#include "GaleshiEngine/Physics/PhysicsSystem.hpp"

#include "GaleshiEngine/Renderer/Camera.hpp"
#include "GaleshiEngine/Renderer/DebugRenderer.hpp"
#include "GaleshiEngine/Renderer/Framebuffer.hpp"
#include "GaleshiEngine/Renderer/Frustum.hpp"
#include "GaleshiEngine/Renderer/GroomSystem.hpp"
#include "GaleshiEngine/Renderer/ImGuiBgfx.hpp"
#include "GaleshiEngine/Renderer/ParticleSystem.hpp"
#include "GaleshiEngine/Renderer/PostProcessManager.hpp"
#include "GaleshiEngine/Renderer/PostProcessSettings.hpp"
#include "GaleshiEngine/Renderer/RayPick.hpp"
#include "GaleshiEngine/Renderer/Renderer.hpp"
#include "GaleshiEngine/Renderer/RenderSystem.hpp"
#include "GaleshiEngine/Renderer/ShaderManager.hpp"

#include "GaleshiEngine/Scene/Components.hpp"
#include "GaleshiEngine/Scene/Entity.hpp"
#include "GaleshiEngine/Scene/Scene.hpp"
#include "GaleshiEngine/Scene/SceneSerializer.hpp"

#include "GaleshiEngine/Scripting/CppScriptApi.hpp"
#include "GaleshiEngine/Scripting/CppGameplayApi.hpp"
#include "GaleshiEngine/Scripting/ScriptEngine.hpp"

#include "GaleshiEngine/UI/UIRuntime.hpp"
#include "GaleshiEngine/UI/UIWidgets.hpp"
