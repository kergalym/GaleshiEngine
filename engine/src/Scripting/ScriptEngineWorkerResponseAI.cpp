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

bool ScriptEngine::MakeWorkerAIResponse(
    const CppWorkerPacket& request, CppWorkerPacket& response) {
    const auto kind = static_cast<CppWorkerRequest>(request.kind);
    switch (kind) {
    case CppWorkerRequest::FindParticleEmitter:
        if (m_scene) {
            auto view =
                m_scene->Registry().view<ParticleEmitterComponent>();
            if (!view.empty()) {
                response.entity = static_cast<uint32_t>(*view.begin());
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetParticleFlag:
    case CppWorkerRequest::SetParticleFloat:
    case CppWorkerRequest::SetParticleInt:
    case CppWorkerRequest::SetParticleVec3:
    case CppWorkerRequest::SetParticleVec4:
    case CppWorkerRequest::GetParticleFlag:
    case CppWorkerRequest::GetParticleFloat:
    case CppWorkerRequest::GetParticleInt:
    case CppWorkerRequest::GetParticleVec3:
    case CppWorkerRequest::GetParticleVec4:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<ParticleEmitterComponent>(entity)) {
                auto& particle =
                    registry.get<ParticleEmitterComponent>(entity);
                const std::string key = NormalizeKey(request.text);
                switch (kind) {
                case CppWorkerRequest::SetParticleFlag:
                    if (const auto* field =
                            FindPostProcessField(
                                kParticleBools, key)) {
                        particle.*(field->member) =
                            request.valid != 0;
                        response.valid = 1;
                    }
                    break;
                case CppWorkerRequest::GetParticleFlag:
                    if (const auto* field =
                            FindPostProcessField(
                                kParticleBools, key)) {
                        response.entity =
                            particle.*(field->member) ? 1u : 0u;
                        response.valid = 1;
                    }
                    break;
                case CppWorkerRequest::SetParticleFloat:
                    if (const auto* field =
                            FindPostProcessField(
                                kParticleFloats, key)) {
                        particle.*(field->member) = request.x;
                        response.valid = 1;
                    }
                    break;
                case CppWorkerRequest::GetParticleFloat:
                    if (const auto* field =
                            FindPostProcessField(
                                kParticleFloats, key)) {
                        response.x = particle.*(field->member);
                        response.valid = 1;
                    }
                    break;
                case CppWorkerRequest::SetParticleInt:
                    if (const auto* field =
                            FindPostProcessField(
                                kParticleInts, key)) {
                        particle.*(field->member) = request.valid;
                        response.valid = 1;
                    }
                    break;
                case CppWorkerRequest::GetParticleInt:
                    if (const auto* field =
                            FindPostProcessField(
                                kParticleInts, key)) {
                        response.entity = particle.*(field->member);
                        response.valid = 1;
                    }
                    break;
                case CppWorkerRequest::SetParticleVec3:
                    if (const auto* field =
                            FindPostProcessField(
                                kParticleVec3s, key)) {
                        particle.*(field->member) =
                            glm::vec3(request.x, request.y,
                                      request.z);
                        response.valid = 1;
                    }
                    break;
                case CppWorkerRequest::GetParticleVec3:
                    if (const auto* field =
                            FindPostProcessField(
                                kParticleVec3s, key)) {
                        const glm::vec3 value =
                            particle.*(field->member);
                        response.x = value.x;
                        response.y = value.y;
                        response.z = value.z;
                        response.valid = 1;
                    }
                    break;
                case CppWorkerRequest::SetParticleVec4:
                    if (const auto* field =
                            FindPostProcessField(
                                kParticleVec4s, key)) {
                        particle.*(field->member) = glm::vec4(
                            request.x, request.y, request.z,
                            request.w);
                        response.valid = 1;
                    }
                    break;
                case CppWorkerRequest::GetParticleVec4:
                    if (const auto* field =
                            FindPostProcessField(
                                kParticleVec4s, key)) {
                        const glm::vec4 value =
                            particle.*(field->member);
                        response.x = value.x;
                        response.y = value.y;
                        response.z = value.z;
                        response.w = value.w;
                        response.valid = 1;
                    }
                    break;
                default:
                    break;
                }
            }
        }
        break;
    case CppWorkerRequest::FindGroom:
        if (m_scene) {
            auto view = m_scene->Registry().view<GroomComponent>();
            if (!view.empty()) {
                response.entity = static_cast<uint32_t>(*view.begin());
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetGroomFlag:
    case CppWorkerRequest::GetGroomFlag:
    case CppWorkerRequest::SetGroomFloat:
    case CppWorkerRequest::GetGroomFloat:
    case CppWorkerRequest::SetGroomInt:
    case CppWorkerRequest::GetGroomInt:
    case CppWorkerRequest::SetGroomVec3:
    case CppWorkerRequest::GetGroomVec3:
    case CppWorkerRequest::SetGroomString:
    case CppWorkerRequest::GetGroomString:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<GroomComponent>(entity)) {
                auto& groom = registry.get<GroomComponent>(entity);
                const std::string key = NormalizeKey(request.text);
                switch (kind) {
                case CppWorkerRequest::SetGroomFlag:
                    if (const auto* field =
                            FindPostProcessField(kGroomBools, key)) {
                        groom.*(field->member) =
                            request.valid != 0;
                        response.valid = 1;
                    }
                    break;
                case CppWorkerRequest::GetGroomFlag:
                    if (const auto* field =
                            FindPostProcessField(kGroomBools, key)) {
                        response.entity =
                            groom.*(field->member) ? 1u : 0u;
                        response.valid = 1;
                    }
                    break;
                case CppWorkerRequest::SetGroomFloat:
                    if (const auto* field =
                            FindPostProcessField(kGroomFloats, key)) {
                        groom.*(field->member) = request.x;
                        response.valid = 1;
                    }
                    break;
                case CppWorkerRequest::GetGroomFloat:
                    if (const auto* field =
                            FindPostProcessField(kGroomFloats, key)) {
                        response.x = groom.*(field->member);
                        response.valid = 1;
                    }
                    break;
                case CppWorkerRequest::SetGroomInt:
                    if (key == "seed") {
                        groom.seed = request.valid;
                        response.valid = 1;
                    } else if (const auto* field =
                                   FindPostProcessField(
                                       kGroomInts, key)) {
                        groom.*(field->member) =
                            static_cast<int>(request.valid);
                        response.valid = 1;
                    }
                    break;
                case CppWorkerRequest::GetGroomInt:
                    if (key == "seed") {
                        response.entity = groom.seed;
                        response.valid = 1;
                    } else if (const auto* field =
                                   FindPostProcessField(
                                       kGroomInts, key)) {
                        response.entity = static_cast<uint32_t>(
                            groom.*(field->member));
                        response.valid = 1;
                    }
                    break;
                case CppWorkerRequest::SetGroomVec3:
                    if (const auto* field =
                            FindPostProcessField(kGroomVec3s, key)) {
                        groom.*(field->member) = glm::vec3(
                            request.x, request.y, request.z);
                        response.valid = 1;
                    }
                    break;
                case CppWorkerRequest::GetGroomVec3:
                    if (const auto* field =
                            FindPostProcessField(kGroomVec3s, key)) {
                        const glm::vec3 value =
                            groom.*(field->member);
                        response.x = value.x;
                        response.y = value.y;
                        response.z = value.z;
                        response.valid = 1;
                    }
                    break;
                case CppWorkerRequest::SetGroomString:
                    if (const auto* field =
                            FindPostProcessField(kGroomStrings, key)) {
                        groom.*(field->member) = request.text2;
                        response.valid = 1;
                    }
                    break;
                case CppWorkerRequest::GetGroomString:
                    if (const auto* field =
                            FindPostProcessField(kGroomStrings, key)) {
                        std::snprintf(
                            response.text, sizeof(response.text), "%s",
                            (groom.*(field->member)).c_str());
                        response.valid = 1;
                    }
                    break;
                default:
                    break;
                }
            }
        }
        break;
    case CppWorkerRequest::FindNavAgent:
        if (m_scene) {
            auto view = m_scene->Registry().view<NavAgentComponent>();
            if (!view.empty()) {
                response.entity = static_cast<uint32_t>(*view.begin());
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::FindPerception:
        if (m_scene) {
            auto view = m_scene->Registry().view<PerceptionComponent>();
            if (!view.empty()) {
                response.entity = static_cast<uint32_t>(*view.begin());
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::FindBehaviorTree:
        if (m_scene) {
            auto view =
                m_scene->Registry().view<BehaviorTreeComponent>();
            if (!view.empty()) {
                response.entity = static_cast<uint32_t>(*view.begin());
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::FindAIController:
        if (m_scene) {
            auto view =
                m_scene->Registry().view<AIControllerComponent>();
            if (!view.empty()) {
                response.entity = static_cast<uint32_t>(*view.begin());
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetAIFlag:
    case CppWorkerRequest::GetAIFlag:
    case CppWorkerRequest::SetAIFloat:
    case CppWorkerRequest::GetAIFloat:
    case CppWorkerRequest::SetAIInt:
    case CppWorkerRequest::GetAIInt:
    case CppWorkerRequest::SetAIVec3:
    case CppWorkerRequest::GetAIVec3:
    case CppWorkerRequest::SetAIString:
    case CppWorkerRequest::GetAIString:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity)) {
                const std::string key = NormalizeKey(request.text);
                if (kind == CppWorkerRequest::SetAIString ||
                    kind == CppWorkerRequest::GetAIString) {
                    const std::string text2Value = request.text2;
                    std::string component = NormalizeKey(text2Value);
                    std::string value;
                    if (kind == CppWorkerRequest::SetAIString) {
                        const std::size_t split =
                            text2Value.find('\n');
                        component = NormalizeKey(
                            text2Value.substr(0, split));
                        if (split != std::string::npos) {
                            value = text2Value.substr(split + 1);
                        }
                    }
                    const auto setString =
                        [&](std::string& target) {
                            target = value;
                            response.valid = 1;
                        };
                    if (component == "ai_controller" &&
                        registry.all_of<AIControllerComponent>(entity)) {
                        auto& controller =
                            registry.get<AIControllerComponent>(entity);
                        if (const auto* field =
                                FindPostProcessField(
                                    kAIControllerStrings, key)) {
                            if (kind == CppWorkerRequest::SetAIString) {
                                setString(controller.*(field->member));
                            } else {
                                std::snprintf(
                                    response.text,
                                    sizeof(response.text), "%s",
                                    (controller.*(field->member)).c_str());
                                response.valid = 1;
                            }
                        }
                    } else if (component == "perception" &&
                               registry.all_of<PerceptionComponent>(
                                   entity)) {
                        auto& perception =
                            registry.get<PerceptionComponent>(entity);
                        if (const auto* field =
                                FindPostProcessField(
                                    kPerceptionStrings, key)) {
                            if (kind == CppWorkerRequest::SetAIString) {
                                setString(perception.*(field->member));
                            } else {
                                std::snprintf(
                                    response.text,
                                    sizeof(response.text), "%s",
                                    (perception.*(field->member)).c_str());
                                response.valid = 1;
                            }
                        }
                    }
                    break;
                }
                if (request.text2[0] == '\0') break;
                const std::string component = NormalizeKey(request.text2);
                if (component == "nav_agent" &&
                    registry.all_of<NavAgentComponent>(entity)) {
                    auto& agent = registry.get<NavAgentComponent>(entity);
                    switch (kind) {
                    case CppWorkerRequest::SetAIFlag:
                        if (const auto* field =
                                FindPostProcessField(
                                    kNavAgentBools, key)) {
                            agent.*(field->member) =
                                request.valid != 0;
                            response.valid = 1;
                        }
                        break;
                    case CppWorkerRequest::GetAIFlag:
                        if (const auto* field =
                                FindPostProcessField(
                                    kNavAgentBools, key)) {
                            response.entity =
                                agent.*(field->member) ? 1u : 0u;
                            response.valid = 1;
                        }
                        break;
                    case CppWorkerRequest::SetAIFloat:
                        if (const auto* field =
                                FindPostProcessField(
                                    kNavAgentFloats, key)) {
                            agent.*(field->member) = request.x;
                            response.valid = 1;
                        }
                        break;
                    case CppWorkerRequest::GetAIFloat:
                        if (const auto* field =
                                FindPostProcessField(
                                    kNavAgentFloats, key)) {
                            response.x = agent.*(field->member);
                            response.valid = 1;
                        }
                        break;
                    case CppWorkerRequest::SetAIVec3:
                        if (const auto* field =
                                FindPostProcessField(
                                    kNavAgentVec3s, key)) {
                            agent.*(field->member) = glm::vec3(
                                request.x, request.y, request.z);
                            response.valid = 1;
                        }
                        break;
                    case CppWorkerRequest::GetAIVec3:
                        if (const auto* field =
                                FindPostProcessField(
                                    kNavAgentVec3s, key)) {
                            const glm::vec3 value =
                                agent.*(field->member);
                            response.x = value.x;
                            response.y = value.y;
                            response.z = value.z;
                            response.valid = 1;
                        }
                        break;
                    default:
                        break;
                    }
                } else if (component == "perception" &&
                           registry.all_of<PerceptionComponent>(
                               entity)) {
                    auto& perception =
                        registry.get<PerceptionComponent>(entity);
                    switch (kind) {
                    case CppWorkerRequest::SetAIFlag:
                        if (const auto* field =
                                FindPostProcessField(
                                    kPerceptionBools, key)) {
                            perception.*(field->member) =
                                request.valid != 0;
                            response.valid = 1;
                        }
                        break;
                    case CppWorkerRequest::GetAIFlag:
                        if (const auto* field =
                                FindPostProcessField(
                                    kPerceptionBools, key)) {
                            response.entity =
                                perception.*(field->member) ? 1u : 0u;
                            response.valid = 1;
                        }
                        break;
                    case CppWorkerRequest::SetAIFloat:
                        if (const auto* field =
                                FindPostProcessField(
                                    kPerceptionFloats, key)) {
                            perception.*(field->member) = request.x;
                            response.valid = 1;
                        }
                        break;
                    case CppWorkerRequest::GetAIFloat:
                        if (const auto* field =
                                FindPostProcessField(
                                    kPerceptionFloats, key)) {
                            response.x =
                                perception.*(field->member);
                            response.valid = 1;
                        }
                        break;
                    default:
                        break;
                    }
                } else if (component == "behavior_tree" &&
                           registry.all_of<BehaviorTreeComponent>(
                               entity)) {
                    auto& tree =
                        registry.get<BehaviorTreeComponent>(entity);
                    switch (kind) {
                    case CppWorkerRequest::SetAIFlag:
                        if (const auto* field =
                                FindPostProcessField(
                                    kBehaviorTreeBools, key)) {
                            tree.*(field->member) =
                                request.valid != 0;
                            response.valid = 1;
                        }
                        break;
                    case CppWorkerRequest::GetAIFlag:
                        if (const auto* field =
                                FindPostProcessField(
                                    kBehaviorTreeBools, key)) {
                            response.entity =
                                tree.*(field->member) ? 1u : 0u;
                            response.valid = 1;
                        }
                        break;
                    case CppWorkerRequest::SetAIInt:
                        if (const auto* field =
                                FindPostProcessField(
                                    kBehaviorTreeInts, key)) {
                            tree.*(field->member) =
                                static_cast<int>(request.valid);
                            response.valid = 1;
                        }
                        break;
                    case CppWorkerRequest::GetAIInt:
                        if (const auto* field =
                                FindPostProcessField(
                                    kBehaviorTreeInts, key)) {
                            response.entity = static_cast<uint32_t>(
                                tree.*(field->member));
                            response.valid = 1;
                        }
                        break;
                    case CppWorkerRequest::SetAIFloat:
                        if (const auto* field =
                                FindPostProcessField(
                                    kBehaviorTreeFloats, key)) {
                            tree.*(field->member) = request.x;
                            response.valid = 1;
                        }
                        break;
                    case CppWorkerRequest::GetAIFloat:
                        if (const auto* field =
                                FindPostProcessField(
                                    kBehaviorTreeFloats, key)) {
                            response.x = tree.*(field->member);
                            response.valid = 1;
                        }
                        break;
                    default:
                        break;
                    }
                } else if (component == "ai_controller" &&
                           registry.all_of<AIControllerComponent>(
                               entity) &&
                           kind == CppWorkerRequest::SetAIString) {
                    auto& controller =
                        registry.get<AIControllerComponent>(entity);
                    if (const auto* field =
                            FindPostProcessField(
                                kAIControllerStrings, key)) {
                        controller.*(field->member) = request.text2;
                        response.valid = 1;
                    }
                }
            }
        }
        break;
    case CppWorkerRequest::BehaviorTreeCreateDefault:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity)) {
                registry.emplace_or_replace<BehaviorTreeComponent>(
                    entity, BehaviorTreeSystem::MakeDefaultBehaviorTree());
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::BehaviorTreeNodeCount:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<BehaviorTreeComponent>(entity)) {
                response.entity = static_cast<uint32_t>(
                    registry.get<BehaviorTreeComponent>(entity)
                        .nodes.size());
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::BehaviorTreeAddNode:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<BehaviorTreeComponent>(entity)) {
                auto& tree =
                    registry.get<BehaviorTreeComponent>(entity);
                BehaviorTreeNode node;
                node.name = request.text[0] ? request.text : "Node";
                const uint32_t packed = request.valid;
                node.type = static_cast<BehaviorNodeType>(
                    std::clamp(
                        static_cast<int>(packed & 0xFFu), 0, 3));
                node.condition = static_cast<BehaviorCondition>(
                    std::clamp(
                        static_cast<int>((packed >> 8) & 0xFFu), 0, 5));
                node.action = static_cast<BehaviorAction>(
                    std::clamp(
                        static_cast<int>((packed >> 16) & 0xFFu), 0, 8));
                tree.nodes.push_back(std::move(node));
                response.entity = static_cast<uint32_t>(
                    tree.nodes.size() - 1);
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::BehaviorTreeAddChild:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<BehaviorTreeComponent>(entity)) {
                auto& tree =
                    registry.get<BehaviorTreeComponent>(entity);
                const int parent = static_cast<int>(request.valid);
                const int child = static_cast<int>(request.other);
                if (parent >= 0 &&
                    parent < static_cast<int>(tree.nodes.size()) &&
                    child >= 0 &&
                    child < static_cast<int>(tree.nodes.size()) &&
                    parent != child) {
                    tree.nodes[static_cast<size_t>(parent)]
                        .children.push_back(child);
                    response.valid = 1;
                }
            }
        }
        break;
    case CppWorkerRequest::BehaviorTreeSetRoot:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<BehaviorTreeComponent>(entity)) {
                auto& tree =
                    registry.get<BehaviorTreeComponent>(entity);
                const int root = static_cast<int>(request.valid);
                if (root >= 0 &&
                    root < static_cast<int>(tree.nodes.size())) {
                    tree.root = root;
                    response.valid = 1;
                }
            }
        }
        break;
    case CppWorkerRequest::BehaviorTreeSetNodeFloat:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<BehaviorTreeComponent>(entity)) {
                auto& tree =
                    registry.get<BehaviorTreeComponent>(entity);
                const int index = static_cast<int>(request.valid);
                const std::string key = NormalizeKey(request.text);
                if (index >= 0 &&
                    index < static_cast<int>(tree.nodes.size())) {
                    auto& node = tree.nodes[static_cast<size_t>(index)];
                    if (key == "range") node.range = request.x;
                    else if (key == "duration") node.duration = request.x;
                    else if (key == "radius") node.radius = request.x;
                    else break;
                    response.valid = 1;
                }
            }
        }
        break;
    case CppWorkerRequest::BehaviorTreeSetNodeInt:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<BehaviorTreeComponent>(entity)) {
                auto& tree =
                    registry.get<BehaviorTreeComponent>(entity);
                const int index = static_cast<int>(request.other);
                const int value = static_cast<int>(request.valid);
                const std::string key = NormalizeKey(request.text);
                if (index >= 0 &&
                    index < static_cast<int>(tree.nodes.size())) {
                    auto& node = tree.nodes[static_cast<size_t>(index)];
                    if (key == "type") {
                        node.type = static_cast<BehaviorNodeType>(
                            std::clamp(value, 0, 3));
                    } else if (key == "condition") {
                        node.condition =
                            static_cast<BehaviorCondition>(
                                std::clamp(value, 0, 5));
                    } else if (key == "action") {
                        node.action = static_cast<BehaviorAction>(
                            std::clamp(value, 0, 8));
                    } else {
                        break;
                    }
                    response.valid = 1;
                }
            }
        }
        break;
    case CppWorkerRequest::BehaviorTreeSetNodeString:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<BehaviorTreeComponent>(entity)) {
                auto& tree =
                    registry.get<BehaviorTreeComponent>(entity);
                const int index = static_cast<int>(request.valid);
                const std::string key = NormalizeKey(request.text);
                if (index >= 0 &&
                    index < static_cast<int>(tree.nodes.size())) {
                    auto& node = tree.nodes[static_cast<size_t>(index)];
                    if (key == "name") node.name = request.text2;
                    else if (key == "parameter") {
                        node.parameter = request.text2;
                    } else if (key == "value") {
                        node.value = request.text2;
                    } else {
                        break;
                    }
                    response.valid = 1;
                }
            }
        }
        break;
    case CppWorkerRequest::BehaviorTreeStatus:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<BehaviorTreeComponent>(entity)) {
                std::snprintf(
                    response.text, sizeof(response.text), "%s",
                    registry.get<BehaviorTreeComponent>(entity)
                        .statusText.c_str());
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetMaterialFloat:
        if (auto material = FindMaterialByEntity(request.entity)) {
            const std::string key = NormalizeKey(request.text);
            if (const auto* field =
                    FindPostProcessField(kMaterialFloats, key)) {
                (*material).*(field->member) = request.x;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::GetMaterialFloat:
        if (auto material = FindMaterialByEntity(request.entity)) {
            const std::string key = NormalizeKey(request.text);
            if (const auto* field =
                    FindPostProcessField(kMaterialFloats, key)) {
                response.x = (*material).*(field->member);
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetMaterialFlag:
        if (auto material = FindMaterialByEntity(request.entity)) {
            const std::string key = NormalizeKey(request.text);
            if (key == "two_sided") {
                material->twoSided = request.valid != 0;
                material->backfaceCulling = request.valid == 0;
                response.valid = 1;
            } else if (const auto* field =
                           FindPostProcessField(kMaterialBools, key)) {
                (*material).*(field->member) = request.valid != 0;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::GetMaterialFlag:
        if (auto material = FindMaterialByEntity(request.entity)) {
            const std::string key = NormalizeKey(request.text);
            if (key == "two_sided") {
                response.entity = material->twoSided ? 1u : 0u;
                response.valid = 1;
            } else if (const auto* field =
                           FindPostProcessField(kMaterialBools, key)) {
                response.entity =
                    (*material).*(field->member) ? 1u : 0u;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetMaterialColor:
        if (auto material = FindMaterialByEntity(request.entity)) {
            const std::string key = NormalizeKey(request.text);
            if (const auto* field =
                    FindPostProcessField(kMaterialVec4s, key)) {
                (*material).*(field->member) =
                    glm::vec4(request.x, request.y, request.z,
                              request.w);
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::GetMaterialColor:
        if (auto material = FindMaterialByEntity(request.entity)) {
            const std::string key = NormalizeKey(request.text);
            if (const auto* field =
                    FindPostProcessField(kMaterialVec4s, key)) {
                const glm::vec4 value =
                    (*material).*(field->member);
                response.x = value.x;
                response.y = value.y;
                response.z = value.z;
                response.w = value.w;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetMaterialTexture:
        if (auto material = FindMaterialByEntity(request.entity)) {
            const std::string slot = NormalizeKey(request.text);
            const std::string path = request.text2;
            std::shared_ptr<Texture> texture;
            if (!path.empty()) {
                if (!m_assets) break;
                const bool srgb =
                    slot == "albedo" || slot == "emissive" ||
                    slot == "landscape_albedo";
                texture = m_assets->LoadTexture(path, srgb);
                if (!texture) break;
            }
            if (slot == "albedo") material->albedoMap = texture;
            else if (slot == "normal") material->normalMap = texture;
            else if (slot == "metallic_roughness") {
                material->metallicRoughnessMap = texture;
            } else if (slot == "metallic") {
                material->metallicMap = texture;
            } else if (slot == "roughness") {
                material->roughnessMap = texture;
            } else if (slot == "emissive") {
                material->emissiveMap = texture;
            } else if (slot == "occlusion") {
                material->occlusionMap = texture;
            } else if (slot == "opacity") {
                material->opacityMap = texture;
            } else if (slot == "specular") {
                material->specularMap = texture;
            } else if (slot == "landscape_albedo") {
                material->landscapeAlbedoMap = texture;
            } else if (slot == "landscape_orm") {
                material->landscapeOrmMap = texture;
            } else if (slot == "landscape_normal") {
                material->landscapeNormalMap = texture;
            } else {
                break;
            }
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::GetMaterialTexture:
        if (auto material = FindMaterialByEntity(request.entity)) {
            const std::string slot = NormalizeKey(request.text);
            std::shared_ptr<Texture> texture;
            if (slot == "albedo") texture = material->albedoMap;
            else if (slot == "normal") texture = material->normalMap;
            else if (slot == "metallic_roughness") {
                texture = material->metallicRoughnessMap;
            } else if (slot == "metallic") {
                texture = material->metallicMap;
            } else if (slot == "roughness") {
                texture = material->roughnessMap;
            } else if (slot == "emissive") {
                texture = material->emissiveMap;
            } else if (slot == "occlusion") {
                texture = material->occlusionMap;
            } else if (slot == "opacity") {
                texture = material->opacityMap;
            } else if (slot == "specular") {
                texture = material->specularMap;
            } else if (slot == "landscape_albedo") {
                texture = material->landscapeAlbedoMap;
            } else if (slot == "landscape_orm") {
                texture = material->landscapeOrmMap;
            } else if (slot == "landscape_normal") {
                texture = material->landscapeNormalMap;
            } else {
                break;
            }
            std::snprintf(response.text, sizeof(response.text), "%s",
                          texture ? texture->Path().c_str() : "");
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::RendererWidth:
        if (m_rendering.width) {
            response.entity = m_rendering.width();
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::RendererHeight:
        if (m_rendering.height) {
            response.entity = m_rendering.height();
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::RendererDeferred:
        if (m_rendering.getDeferred) {
            response.valid =
                m_rendering.getDeferred() ? 1u : 0u;
        }
        break;
    case CppWorkerRequest::SetRendererDeferred:
        if (m_rendering.setDeferred) {
            m_rendering.setDeferred(request.valid != 0);
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::RendererLod:
        if (m_rendering.getLod) {
            response.valid = m_rendering.getLod() ? 1u : 0u;
        }
        break;
    case CppWorkerRequest::SetRendererLod:
        if (m_rendering.setLod) {
            m_rendering.setLod(request.valid != 0);
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::RendererOcclusion:
        if (m_rendering.getOcclusion) {
            response.valid =
                m_rendering.getOcclusion() ? 1u : 0u;
        }
        break;
    case CppWorkerRequest::SetRendererOcclusion:
        if (m_rendering.setOcclusion) {
            m_rendering.setOcclusion(request.valid != 0);
            response.valid = 1;
        }
        break;
    default:
        return false;
    }
    return true;
}

} // namespace Galeshi
