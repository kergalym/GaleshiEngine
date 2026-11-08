#include "GaleshiEngine/Animation/BoneAttachment.hpp"
#include "GaleshiEngine/Assets/AssetManager.hpp"
#include "GaleshiEngine/Scene/Scene.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <vector>

namespace Galeshi {
namespace {

glm::mat4 EntityWorldMatrix(Scene& scene, entt::entity handle) {
    auto& registry = scene.Registry();
    std::vector<entt::entity> chain;
    entt::entity current = handle;
    for (int depth = 0; current != entt::null && depth < 128; ++depth) {
        chain.push_back(current);
        if (!registry.all_of<ParentComponent>(current)) break;
        current = registry.get<ParentComponent>(current).parent;
    }
    glm::mat4 world(1.0f);
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        if (registry.all_of<TransformComponent>(*it)) {
            world *= registry.get<TransformComponent>(*it).LocalMatrix();
        }
    }
    return world;
}

glm::mat4 LocalMatrix(const BoneAttachmentComponent& attachment) {
    return glm::translate(glm::mat4(1.0f), attachment.localPosition) *
           glm::mat4_cast(attachment.localRotation) *
           glm::scale(glm::mat4(1.0f), attachment.localScale);
}

void SetWorldTransform(TransformComponent& transform, const glm::mat4& world) {
    glm::vec3 scale, translation, skew;
    glm::vec4 perspective;
    glm::quat rotation;
    if (glm::decompose(world, scale, rotation, translation, skew,
                       perspective)) {
        transform.position = translation;
        transform.rotation = glm::normalize(rotation);
        transform.scale = scale;
        return;
    }
    transform.position = glm::vec3(world[3]);
    transform.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    transform.scale = glm::vec3(1.0f);
}

bool MatricesNearlyEqual(const glm::mat4& a, const glm::mat4& b) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (std::abs(a[column][row] - b[column][row]) > 1e-5f) {
                return false;
            }
        }
    }
    return true;
}

void SetLocalFromMatrix(BoneAttachmentComponent& attachment,
                        const glm::mat4& local) {
    glm::vec3 scale, translation, skew;
    glm::vec4 perspective;
    glm::quat rotation;
    if (glm::decompose(local, scale, rotation, translation, skew,
                       perspective)) {
        attachment.localPosition = translation;
        attachment.localRotation = glm::normalize(rotation);
        attachment.localScale = scale;
        return;
    }
    attachment.localPosition = glm::vec3(local[3]);
    attachment.localRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    attachment.localScale = glm::vec3(1.0f);
}

bool IsAncestorOf(Scene& scene, entt::entity ancestor,
                  entt::entity entity) {
    auto& registry = scene.Registry();
    entt::entity current = entity;
    for (int depth = 0; current != entt::null && depth < 128; ++depth) {
        if (current == ancestor) return true;
        if (!registry.all_of<ParentComponent>(current)) break;
        current = registry.get<ParentComponent>(current).parent;
    }
    return false;
}

} // namespace

bool BoneAttachmentSystem::BoneWorldMatrix(Scene& scene, AssetManager& assets,
                                           entt::entity character,
                                           const std::string& bone,
                                           glm::mat4& outWorld) {
    auto& registry = scene.Registry();
    if (character == entt::null || bone.empty() ||
        !registry.valid(character) ||
        !registry.all_of<SkeletalComponent>(character)) {
        return false;
    }
    const auto& skeleton = registry.get<SkeletalComponent>(character);
    if (skeleton.sourcePath.empty()) return false;

    const auto& model = assets.LoadModelRef(skeleton.sourcePath);
    const auto boneIt = model.boneMap.find(bone);
    if (boneIt == model.boneMap.end()) return false;
    const auto offsetIt = model.boneOffsetMatrices.find(bone);
    if (offsetIt == model.boneOffsetMatrices.end()) return false;

    const int boneIndex = boneIt->second;
    glm::mat4 boneModel;
    if (skeleton.isAnimated && boneIndex >= 0 &&
        static_cast<size_t>(boneIndex) < skeleton.boneMatrices.size()) {
        // The palette stores pose * inverseBind. Recover the model-space
        // bone transform before applying the character's world transform.
        boneModel = skeleton.boneMatrices[static_cast<size_t>(boneIndex)] *
                    glm::inverse(offsetIt->second);
    } else {
        const auto restIt = model.nodeRestGlobals.find(bone);
        boneModel = restIt != model.nodeRestGlobals.end()
                        ? restIt->second
                        : glm::inverse(offsetIt->second);
    }
    outWorld = EntityWorldMatrix(scene, character) * boneModel;
    return true;
}

bool BoneAttachmentSystem::Attach(Scene& scene, AssetManager& assets,
                                  entt::entity entity, entt::entity character,
                                  const std::string& bone) {
    auto& registry = scene.Registry();
    if (entity == entt::null || character == entt::null || entity == character ||
        !registry.valid(entity) || !registry.valid(character) || bone.empty()) {
        return false;
    }
    // Attaching an ancestor would detach the character hierarchy and create
    // a self-referential transform.
    if (IsAncestorOf(scene, entity, character)) return false;

    glm::mat4 boneWorld;
    if (!BoneWorldMatrix(scene, assets, character, bone, boneWorld)) {
        return false;
    }

    const glm::mat4 entityWorld = EntityWorldMatrix(scene, entity);
    glm::mat4 local = glm::inverse(boneWorld) * entityWorld;
    BoneAttachmentComponent attachment;
    attachment.character = character;
    attachment.boneName = bone;
    SetLocalFromMatrix(attachment, local);
    attachment.appliedWorld = entityWorld;
    attachment.appliedWorldValid = true;
    registry.emplace_or_replace<BoneAttachmentComponent>(entity, attachment);

    if (!registry.all_of<TransformComponent>(entity)) {
        registry.emplace<TransformComponent>(entity);
    }
    // Attached entities are world-space roots. Keeping an old hierarchy
    // parent would apply its transform twice.
    if (registry.all_of<ParentComponent>(entity)) {
        registry.remove<ParentComponent>(entity);
    }
    SetWorldTransform(registry.get<TransformComponent>(entity), entityWorld);
    return true;
}

bool BoneAttachmentSystem::Detach(Scene& scene, AssetManager& assets,
                                  entt::entity entity) {
    auto& registry = scene.Registry();
    if (entity == entt::null || !registry.valid(entity) ||
        !registry.all_of<BoneAttachmentComponent>(entity)) {
        return false;
    }
    auto& attachment = registry.get<BoneAttachmentComponent>(entity);
    if (auto* transform = registry.try_get<TransformComponent>(entity)) {
        // Bake the final bone-local transform into world space. Any dynamic
        // body can now fall from the bone position with normal gravity.
        glm::mat4 boneWorld;
        if (BoneWorldMatrix(scene, assets, attachment.character,
                            attachment.boneName, boneWorld)) {
            SetWorldTransform(*transform,
                              boneWorld * LocalMatrix(attachment));
        }
    }
    registry.remove<BoneAttachmentComponent>(entity);
    return true;
}

void BoneAttachmentSystem::Update(Scene& scene, AssetManager& assets) {
    auto& registry = scene.Registry();
    auto view = registry.view<BoneAttachmentComponent, TransformComponent>();
    for (auto entity : view) {
        auto& attachment = view.get<BoneAttachmentComponent>(entity);
        if (!registry.valid(attachment.character) ||
            !registry.all_of<SkeletalComponent>(attachment.character)) {
            continue;
        }
        glm::mat4 boneWorld;
        if (!BoneWorldMatrix(scene, assets, attachment.character,
                             attachment.boneName, boneWorld)) {
            continue;
        }
        if (registry.all_of<ParentComponent>(entity)) {
            registry.remove<ParentComponent>(entity);
        }
        auto& transform = view.get<TransformComponent>(entity);
        // An external world-space edit (viewport gizmo, physics, script) wins
        // over the stored local transform; fold it back into bone space.
        glm::mat4 desired =
            boneWorld * LocalMatrix(attachment);
        if (attachment.appliedWorldValid &&
            !MatricesNearlyEqual(transform.LocalMatrix(),
                                 attachment.appliedWorld)) {
            SetLocalFromMatrix(attachment,
                               glm::inverse(boneWorld) *
                                   transform.LocalMatrix());
            desired = boneWorld * LocalMatrix(attachment);
        }
        SetWorldTransform(transform, desired);
        attachment.appliedWorld = transform.LocalMatrix();
        attachment.appliedWorldValid = true;
    }
}

} // namespace Galeshi
