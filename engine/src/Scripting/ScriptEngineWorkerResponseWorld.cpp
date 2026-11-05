#include "ScriptEngineInternal.hpp"
#include "GaleshiEngine/Core/ProjectManager.hpp"
#include "GaleshiEngine/Scene/Scene.hpp"
#include "GaleshiEngine/Scene/Components.hpp"
#include "GaleshiEngine/AI/BehaviorTree.hpp"
#include "GaleshiEngine/Animation/AnimationSystem.hpp"
#include "GaleshiEngine/Assets/AssetManager.hpp"
#include "GaleshiEngine/Assets/LandscapeMesh.hpp"
#include "GaleshiEngine/Assets/Material.hpp"
#include "GaleshiEngine/Assets/Mesh.hpp"
#include "GaleshiEngine/Assets/VegetationGenerator.hpp"
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace Galeshi {

using namespace ScriptEngineInternal;

bool ScriptEngine::MakeWorkerWorldResponse(
    const CppWorkerPacket& request, CppWorkerPacket& response) {
    const auto kind = static_cast<CppWorkerRequest>(request.kind);
    switch (kind) {
    case CppWorkerRequest::FindLandscape:
        if (m_scene) {
            auto view = m_scene->Registry().view<LandscapeComponent>();
            if (!view.empty()) {
                response.entity = static_cast<uint32_t>(*view.begin());
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::LandscapeSampleHeight:
        if (m_scene) {
            auto view = m_scene->Registry().view<LandscapeComponent>();
            if (!view.empty()) {
                const auto& landscape =
                    view.get<LandscapeComponent>(*view.begin());
                response.x = LandscapeMesh::SampleHeight(
                    landscape, request.x, request.y);
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::LandscapeSetHeight:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            auto view = registry.view<LandscapeComponent>();
            if (!view.empty()) {
                const entt::entity entity = *view.begin();
                auto& landscape = view.get<LandscapeComponent>(entity);
                if (landscape.sizeX >= 2 && landscape.sizeZ >= 2) {
                    const float gx =
                        request.x / std::max(0.001f, landscape.scale) +
                        landscape.sizeX * 0.5f;
                    const float gz =
                        request.y / std::max(0.001f, landscape.scale) +
                        landscape.sizeZ * 0.5f;
                    const int x = std::clamp(
                        static_cast<int>(std::lround(gx)), 0,
                        landscape.sizeX - 1);
                    const int z = std::clamp(
                        static_cast<int>(std::lround(gz)), 0,
                        landscape.sizeZ - 1);
                    const size_t index =
                        static_cast<size_t>(z) * landscape.sizeX + x;
                    if (index < landscape.heights.size()) {
                        landscape.heights[index] = request.z;
                        if (registry.all_of<MeshComponent>(entity)) {
                            auto& mesh =
                                registry.get<MeshComponent>(entity);
                            if (mesh.mesh) {
                                LandscapeMesh::UpdateMesh(
                                    landscape, *mesh.mesh);
                            }
                        }
                        if (registry.all_of<VegetationComponent>(
                                entity)) {
                            registry.get<VegetationComponent>(entity)
                                .dirty = true;
                        }
                        response.valid = 1;
                    }
                }
            }
        }
        break;
    case CppWorkerRequest::LandscapeApplyBrush:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            auto view = registry.view<LandscapeComponent>();
            if (!view.empty()) {
                const entt::entity entity = *view.begin();
                auto& landscape = view.get<LandscapeComponent>(entity);
                LandscapeBrushSettings settings;
                const int tool = static_cast<int>(request.valid);
                settings.tool = static_cast<LandscapeBrush>(
                    std::clamp(tool, 0, 6));
                settings.radius = request.z;
                settings.strength = request.w;
                settings.falloff = request.x2;
                settings.flattenHeight = 0.0f;
                settings.noiseFrequency = 0.15f;
                settings.terraceStep = 1.0f;
                const int changed = LandscapeMesh::ApplyBrush(
                    landscape, settings, request.x, request.y,
                    request.y2);
                if (changed > 0) {
                    if (registry.all_of<MeshComponent>(entity)) {
                        auto& mesh =
                            registry.get<MeshComponent>(entity);
                        if (mesh.mesh) {
                            LandscapeMesh::UpdateMesh(
                                landscape, *mesh.mesh);
                        }
                    }
                    if (registry.all_of<VegetationComponent>(entity)) {
                        registry.get<VegetationComponent>(entity)
                            .dirty = true;
                    }
                    response.valid = 1;
                }
            }
        }
        break;
    case CppWorkerRequest::LandscapeResetHeights:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            auto view = registry.view<LandscapeComponent>();
            if (!view.empty()) {
                const entt::entity entity = *view.begin();
                auto& landscape = view.get<LandscapeComponent>(entity);
                LandscapeMesh::ResetHeights(landscape);
                if (registry.all_of<MeshComponent>(entity)) {
                    auto& mesh = registry.get<MeshComponent>(entity);
                    if (mesh.mesh) {
                        LandscapeMesh::UpdateMesh(landscape, *mesh.mesh);
                    }
                }
                if (registry.all_of<VegetationComponent>(entity)) {
                    registry.get<VegetationComponent>(entity).dirty =
                        true;
                }
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::LandscapeLayerCount:
        if (m_scene) {
            auto view = m_scene->Registry().view<LandscapeComponent>();
            if (!view.empty()) {
                response.entity = static_cast<uint32_t>(
                    view.get<LandscapeComponent>(*view.begin())
                        .layers.size());
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::LandscapeAddLayer:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            auto view = registry.view<LandscapeComponent>();
            if (!view.empty()) {
                const entt::entity entity = *view.begin();
                auto& landscape = view.get<LandscapeComponent>(entity);
                landscape.layers.push_back(LandscapeLayer{});
                const size_t expected =
                    static_cast<size_t>(landscape.sizeX) *
                    static_cast<size_t>(landscape.sizeZ);
                landscape.layerWeights.emplace_back(expected, 0.0f);
                landscape.activeLayer = static_cast<int>(
                    landscape.layers.size() - 1);
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::LandscapeRemoveLayer:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            auto view = registry.view<LandscapeComponent>();
            if (!view.empty()) {
                const entt::entity entity = *view.begin();
                auto& landscape = view.get<LandscapeComponent>(entity);
                const int layer = static_cast<int>(request.valid);
                if (layer >= 0 &&
                    layer < static_cast<int>(landscape.layers.size())) {
                    landscape.layers.erase(
                        landscape.layers.begin() + layer);
                    if (landscape.layerWeights.size() >
                        static_cast<size_t>(layer)) {
                        landscape.layerWeights.erase(
                            landscape.layerWeights.begin() + layer);
                    }
                    if (landscape.layers.empty()) {
                        landscape.activeLayer = 0;
                    } else {
                        landscape.activeLayer = std::clamp(
                            landscape.activeLayer, 0,
                            static_cast<int>(
                                landscape.layers.size()) - 1);
                    }
                    response.valid = 1;
                }
            }
        }
        break;
    case CppWorkerRequest::LandscapeActiveLayer:
        if (m_scene) {
            auto view = m_scene->Registry().view<LandscapeComponent>();
            if (!view.empty()) {
                response.entity = static_cast<uint32_t>(
                    view.get<LandscapeComponent>(*view.begin())
                        .activeLayer);
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::LandscapeSetActiveLayer:
        if (m_scene) {
            auto view = m_scene->Registry().view<LandscapeComponent>();
            if (!view.empty()) {
                auto& landscape =
                    view.get<LandscapeComponent>(*view.begin());
                const int layer = static_cast<int>(request.valid);
                if (layer >= 0 &&
                    layer < static_cast<int>(landscape.layers.size())) {
                    landscape.activeLayer = layer;
                    response.valid = 1;
                }
            }
        }
        break;
    case CppWorkerRequest::LandscapeSetLayerTexture:
        if (m_scene && m_assets) {
            auto view = m_scene->Registry().view<LandscapeComponent>();
            if (!view.empty()) {
                auto& landscape =
                    view.get<LandscapeComponent>(*view.begin());
                const int layer = static_cast<int>(request.valid);
                if (layer >= 0 &&
                    layer < static_cast<int>(landscape.layers.size())) {
                    const std::string slot = NormalizeKey(request.text);
                    const std::string path = request.text2;
                    std::shared_ptr<Texture> texture;
                    if (!path.empty()) {
                        const bool srgb = slot == "albedo";
                        texture = m_assets->LoadTexture(path, srgb);
                        if (!texture) break;
                    }
                    auto& entry =
                        landscape.layers[static_cast<size_t>(layer)];
                    if (slot == "albedo") entry.albedoMap = texture;
                    else if (slot == "orm") entry.ormMap = texture;
                    else if (slot == "normal") entry.normalMap = texture;
                    else break;
                    response.valid = 1;
                }
            }
        }
        break;
    case CppWorkerRequest::LandscapeGetLayerTexture:
        if (m_scene) {
            auto view = m_scene->Registry().view<LandscapeComponent>();
            if (!view.empty()) {
                auto& landscape =
                    view.get<LandscapeComponent>(*view.begin());
                const int layer = static_cast<int>(request.valid);
                if (layer >= 0 &&
                    layer < static_cast<int>(landscape.layers.size())) {
                    const std::string slot = NormalizeKey(request.text);
                    auto& entry =
                        landscape.layers[static_cast<size_t>(layer)];
                    std::shared_ptr<Texture> texture;
                    if (slot == "albedo") texture = entry.albedoMap;
                    else if (slot == "orm") texture = entry.ormMap;
                    else if (slot == "normal") texture = entry.normalMap;
                    else break;
                    std::snprintf(
                        response.text, sizeof(response.text), "%s",
                        texture ? texture->Path().c_str() : "");
                    response.valid = 1;
                }
            }
        }
        break;
    case CppWorkerRequest::LandscapePaintLayer:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            auto view = registry.view<LandscapeComponent>();
            if (!view.empty()) {
                const entt::entity entity = *view.begin();
                auto& landscape = view.get<LandscapeComponent>(entity);
                const int layer = static_cast<int>(request.valid);
                const bool erase = request.other != 0;
                const int changed = LandscapeMesh::ApplyPaintBrush(
                    landscape, layer, request.x, request.y, request.z,
                    request.w, request.x2, request.y2, erase);
                if (changed > 0) {
                    if (m_landscapeBake) {
                        m_landscapeBake(static_cast<uint32_t>(entity));
                    }
                    response.valid = 1;
                }
            }
        }
        break;
    case CppWorkerRequest::LandscapeBake:
        if (m_scene) {
            auto view = m_scene->Registry().view<LandscapeComponent>();
            if (!view.empty()) {
                response.valid =
                    m_landscapeBake &&
                    m_landscapeBake(
                        static_cast<uint32_t>(*view.begin()))
                        ? 1u : 0u;
            }
        }
        break;
    case CppWorkerRequest::SpawnSkeletalModel:
        if (m_scene && m_assets) {
            const auto loaded = m_assets->LoadModel(request.text);
            if (!loaded.meshes.empty()) {
                const std::string source =
                    m_assets->MakeRelativePath(request.text);
                const std::string stem =
                    std::filesystem::path(request.text).stem().string();
                const std::string rootName =
                    request.text2[0] ? request.text2
                    : stem.empty() ? std::string("SkeletalModel")
                                   : stem;
                Entity root = m_scene->CreateEntity(rootName);
                auto& transform =
                    root.GetComponent<TransformComponent>();
                transform.position =
                    glm::vec3(request.x, request.y, request.z);
                if (loaded.hasAnimation || loaded.totalBones > 0) {
                    auto& skeleton =
                        root.AddComponent<SkeletalComponent>();
                    skeleton.isAnimated = loaded.hasAnimation;
                    skeleton.sourcePath = source;
                    skeleton.boneMatrices.assign(
                        256, glm::mat4(1.0f));
                    if (loaded.hasAnimation) {
                        auto& machine = root.AddComponent<
                            AnimationStateMachineComponent>();
                        machine =
                            AnimationSystem::CreateDefaultStateMachine(
                                loaded.animationNames);
                    }
                }
                for (size_t i = 0; i < loaded.meshes.size(); ++i) {
                    Entity meshEntity = m_scene->CreateEntity(
                        rootName + "_" + std::to_string(i));
                    meshEntity.AddComponent<ParentComponent>(
                        ParentComponent{
                            static_cast<entt::entity>(root)});
                    meshEntity.AddComponent<MeshComponent>(
                        MeshComponent{loaded.meshes[i], source, "",
                                      static_cast<int>(i)});
                    std::shared_ptr<Material> material;
                    if (i < loaded.meshMaterialIndex.size()) {
                        const int materialIndex =
                            loaded.meshMaterialIndex[i];
                        if (materialIndex >= 0 &&
                            materialIndex < static_cast<int>(
                                                loaded.materials.size())) {
                            material = loaded.materials[
                                static_cast<size_t>(materialIndex)];
                        }
                    }
                    if (!material && !loaded.materials.empty()) {
                        material = loaded.materials.front();
                    }
                    if (!material) {
                        material = std::make_shared<Material>();
                    }
                    meshEntity.AddComponent<MaterialComponent>(
                        MaterialComponent{material});
                }
                response.entity =
                    static_cast<uint32_t>(root.Handle());
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::ApplySkeletalModel:
        if (m_scene && m_assets) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity)) {
                const auto loaded = m_assets->LoadModel(request.text);
                if (!loaded.meshes.empty() ||
                    loaded.hasAnimation) {
                    SkeletalComponent* skeleton = nullptr;
                    if (registry.all_of<SkeletalComponent>(entity)) {
                        skeleton =
                            &registry.get<SkeletalComponent>(entity);
                    } else {
                        skeleton =
                            &registry.emplace<SkeletalComponent>(entity);
                    }
                    skeleton->isAnimated = loaded.hasAnimation;
                    skeleton->sourcePath =
                        m_assets->MakeRelativePath(request.text);
                    skeleton->currentTime = 0.0f;
                    skeleton->activeAnimation = 0;
                    skeleton->boneMatrices.assign(
                        256, glm::mat4(1.0f));
                    if (loaded.hasAnimation) {
                        if (!registry.all_of<
                                AnimationStateMachineComponent>(entity)) {
                            auto& machine = registry.emplace<
                                AnimationStateMachineComponent>(entity);
                            machine = AnimationSystem::
                                CreateDefaultStateMachine(
                                    loaded.animationNames);
                        } else {
                            auto& machine = registry.get<
                                AnimationStateMachineComponent>(entity);
                            if (machine.states.empty()) {
                                machine = AnimationSystem::
                                    CreateDefaultStateMachine(
                                        loaded.animationNames);
                            }
                        }
                    }
                    response.valid = 1;
                }
            }
        }
        break;
    case CppWorkerRequest::FindVegetation:
        if (m_scene) {
            auto view = m_scene->Registry().view<VegetationComponent>();
            if (!view.empty()) {
                response.entity = static_cast<uint32_t>(*view.begin());
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::VegetationRegenerate:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<LandscapeComponent,
                                VegetationComponent>(entity)) {
                VegetationGenerator::Generate(
                    registry.get<LandscapeComponent>(entity),
                    registry.get<VegetationComponent>(entity));
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::VegetationClear:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<VegetationComponent>(entity)) {
                VegetationGenerator::Clear(
                    registry.get<VegetationComponent>(entity));
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::VegetationSetBool:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<VegetationComponent>(entity)) {
                const std::string key = NormalizeKey(request.text);
                auto& vegetation =
                    registry.get<VegetationComponent>(entity);
                if (const auto* field = FindPostProcessField(
                        kVegetationBools, key)) {
                    vegetation.*(field->member) =
                        request.valid != 0;
                    vegetation.dirty = true;
                    response.valid = 1;
                }
            }
        }
        break;
    case CppWorkerRequest::VegetationGetBool:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<VegetationComponent>(entity)) {
                const std::string key = NormalizeKey(request.text);
                auto& vegetation =
                    registry.get<VegetationComponent>(entity);
                if (const auto* field = FindPostProcessField(
                        kVegetationBools, key)) {
                    response.entity =
                        vegetation.*(field->member) ? 1u : 0u;
                    response.valid = 1;
                }
            }
        }
        break;
    case CppWorkerRequest::VegetationSetInt:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<VegetationComponent>(entity)) {
                const std::string key = NormalizeKey(request.text);
                auto& vegetation =
                    registry.get<VegetationComponent>(entity);
                if (key == "seed") {
                    vegetation.seed =
                        static_cast<uint32_t>(request.valid);
                    vegetation.dirty = true;
                    response.valid = 1;
                } else if (const auto* field =
                               FindPostProcessField(
                                   kVegetationInts, key)) {
                    vegetation.*(field->member) =
                        static_cast<int>(request.valid);
                    vegetation.dirty = true;
                    response.valid = 1;
                }
            }
        }
        break;
    case CppWorkerRequest::VegetationGetInt:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<VegetationComponent>(entity)) {
                const std::string key = NormalizeKey(request.text);
                auto& vegetation =
                    registry.get<VegetationComponent>(entity);
                if (key == "seed") {
                    response.entity = vegetation.seed;
                    response.valid = 1;
                } else if (const auto* field =
                               FindPostProcessField(
                                   kVegetationInts, key)) {
                    response.entity = static_cast<uint32_t>(
                        vegetation.*(field->member));
                    response.valid = 1;
                }
            }
        }
        break;
    case CppWorkerRequest::VegetationSetFloat:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<VegetationComponent>(entity)) {
                const std::string key = NormalizeKey(request.text);
                auto& vegetation =
                    registry.get<VegetationComponent>(entity);
                if (const auto* field = FindPostProcessField(
                        kVegetationFloats, key)) {
                    vegetation.*(field->member) = request.x;
                    vegetation.dirty = true;
                    response.valid = 1;
                }
            }
        }
        break;
    case CppWorkerRequest::VegetationGetFloat:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<VegetationComponent>(entity)) {
                const std::string key = NormalizeKey(request.text);
                auto& vegetation =
                    registry.get<VegetationComponent>(entity);
                if (const auto* field = FindPostProcessField(
                        kVegetationFloats, key)) {
                    response.x = vegetation.*(field->member);
                    response.valid = 1;
                }
            }
        }
        break;
    case CppWorkerRequest::VegetationInstanceCount:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<VegetationComponent>(entity)) {
                auto& vegetation =
                    registry.get<VegetationComponent>(entity);
                const bool grass = (request.valid & 1u) != 0;
                const bool painted = (request.valid & 2u) != 0;
                size_t count = 0;
                if (grass) {
                    count = painted
                        ? vegetation.paintedGrassInstances.size()
                        : vegetation.grassInstances.size();
                } else {
                    count = painted
                        ? vegetation.paintedTreeInstances.size()
                        : vegetation.treeInstances.size();
                    for (const auto& type : vegetation.treeTypes) {
                        count += painted
                            ? type.paintedInstances.size()
                            : type.generatedInstances.size();
                    }
                }
                response.entity = static_cast<uint32_t>(
                    std::min<size_t>(count, UINT32_MAX));
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetAnimationState:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<AnimationStateMachineComponent>(
                    entity)) {
                response.valid = AnimationSystem::SetState(
                    registry.get<AnimationStateMachineComponent>(entity),
                    request.text) ? 1u : 0u;
            }
        }
        break;
    case CppWorkerRequest::SetAnimationParameter:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<AnimationStateMachineComponent>(
                    entity)) {
                auto& machine =
                    registry.get<AnimationStateMachineComponent>(entity);
                AnimationSystem::SetParameter(
                    machine, request.text, request.x);
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::GetAnimationParameter:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<AnimationStateMachineComponent>(
                    entity)) {
                auto& machine =
                    registry.get<AnimationStateMachineComponent>(entity);
                response.x = AnimationSystem::GetParameter(
                    machine, request.text);
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::TriggerAnimationParameter:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<AnimationStateMachineComponent>(
                    entity)) {
                auto& machine =
                    registry.get<AnimationStateMachineComponent>(entity);
                AnimationSystem::Trigger(machine, request.text);
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::PlayAnimationClip:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<SkeletalComponent>(entity)) {
                auto& skeletal =
                    registry.get<SkeletalComponent>(entity);
                skeletal.activeAnimation =
                    static_cast<int>(request.valid);
                skeletal.currentTime = 0.0f;
                skeletal.isAnimated = true;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::StopAnimation:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<SkeletalComponent>(entity)) {
                auto& skeletal =
                    registry.get<SkeletalComponent>(entity);
                skeletal.isAnimated = false;
                skeletal.currentTime = 0.0f;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetAnimationSpeed:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity)) {
                bool applied = false;
                if (registry.all_of<SkeletalComponent>(entity)) {
                    registry.get<SkeletalComponent>(entity)
                        .animationSpeed = request.x;
                    applied = true;
                }
                if (registry.all_of<AnimationStateMachineComponent>(
                        entity)) {
                    registry.get<AnimationStateMachineComponent>(entity)
                        .playbackSpeed = request.x;
                    applied = true;
                }
                response.valid = applied ? 1u : 0u;
            }
        }
        break;
    case CppWorkerRequest::GetAnimationSpeed:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<SkeletalComponent>(entity)) {
                response.x =
                    registry.get<SkeletalComponent>(entity)
                        .animationSpeed;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::AnimationClipCount:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity)) {
                size_t count = 0;
                if (m_assets &&
                    registry.all_of<SkeletalComponent>(entity)) {
                    const auto& skeletal =
                        registry.get<SkeletalComponent>(entity);
                    if (!skeletal.sourcePath.empty()) {
                        const auto model =
                            m_assets->LoadModel(
                                skeletal.sourcePath);
                        count = model.animationNames.size();
                    }
                }
                if (count == 0 &&
                    registry.all_of<AnimationStateMachineComponent>(
                        entity)) {
                    count = registry
                                .get<AnimationStateMachineComponent>(
                                    entity)
                                .states.size();
                }
                response.entity = static_cast<uint32_t>(
                    std::min<size_t>(count, UINT32_MAX));
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::AnimationClipName:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity)) {
                const uint32_t index = request.valid;
                std::string name;
                if (m_assets &&
                    registry.all_of<SkeletalComponent>(entity)) {
                    const auto& skeletal =
                        registry.get<SkeletalComponent>(entity);
                    if (!skeletal.sourcePath.empty()) {
                        const auto model =
                            m_assets->LoadModel(
                                skeletal.sourcePath);
                        if (index < model.animationNames.size()) {
                            name = model.animationNames[index];
                        }
                    }
                }
                if (name.empty() &&
                    registry.all_of<AnimationStateMachineComponent>(
                        entity)) {
                    const auto& machine =
                        registry.get<AnimationStateMachineComponent>(
                            entity);
                    if (index < machine.states.size()) {
                        name = machine.states[index].name;
                    }
                }
                if (!name.empty()) {
                    std::snprintf(
                        response.text, sizeof(response.text), "%s",
                        name.c_str());
                    response.valid = 1;
                }
            }
        }
        break;
    case CppWorkerRequest::AnimationClipDuration:
        if (m_scene && m_assets) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<SkeletalComponent>(entity)) {
                const auto& skeletal =
                    registry.get<SkeletalComponent>(entity);
                if (!skeletal.sourcePath.empty()) {
                    const auto model =
                        m_assets->LoadModel(skeletal.sourcePath);
                    const uint32_t index = request.valid;
                    if (index < model.animationDurations.size()) {
                        response.x = model.animationDurations[index];
                        response.valid = 1;
                    }
                }
            }
        }
        break;
    case CppWorkerRequest::AssetExists:
        if (m_assets) {
            const std::string path =
                m_assets->ResolvePath(request.text);
            std::error_code ec;
            response.valid =
                std::filesystem::exists(path, ec) ? 1u : 0u;
        }
        break;
    case CppWorkerRequest::AssetResolvePath:
        if (m_assets) {
            const std::string path =
                m_assets->ResolvePath(request.text);
            std::snprintf(response.text, sizeof(response.text), "%s",
                          path.c_str());
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::AssetLoadModel:
        if (m_assets) {
            const auto model = m_assets->LoadModel(request.text);
            response.valid =
                !model.meshes.empty() ? 1u : 0u;
        }
        break;
    default:
        return false;
    }
    return true;
}

} // namespace Galeshi
