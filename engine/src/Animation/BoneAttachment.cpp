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

entt::entity BoneAttachmentSystem::FindAnimatingSkeleton(
    Scene& scene, entt::entity character) {
    auto& registry = scene.Registry();
    if (character == entt::null || !registry.valid(character)) {
        return entt::null;
    }
    const auto isAnimating = [&](entt::entity entity) {
        if (!registry.valid(entity) ||
            !registry.all_of<SkeletalComponent>(entity)) {
            return false;
        }
        const auto& skeleton = registry.get<SkeletalComponent>(entity);
        return skeleton.isAnimated && skeleton.boneMatrices.size() >= 256;
    };
    if (isAnimating(character)) return character;

    // Prefer the nearest animated ancestor (Player/model wrapper).
    entt::entity current = character;
    for (int depth = 0; current != entt::null && depth < 128; ++depth) {
        if (current != character && isAnimating(current)) return current;
        if (!registry.all_of<ParentComponent>(current)) break;
        current = registry.get<ParentComponent>(current).parent;
    }

    // Some imported hierarchies put the animated skeleton on a descendant.
    std::vector<entt::entity> queue{character};
    for (size_t head = 0; head < queue.size() && head < 1024; ++head) {
        const entt::entity parent = queue[head];
        for (entt::entity child : registry.view<ParentComponent>()) {
            if (registry.get<ParentComponent>(child).parent != parent) continue;
            if (isAnimating(child)) return child;
            queue.push_back(child);
        }
    }
    return character;
}

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
    const entt::entity boneOwner = FindAnimatingSkeleton(scene, character);
    if (boneOwner == entt::null ||
        !registry.valid(boneOwner) ||
        !registry.all_of<SkeletalComponent>(boneOwner)) {
        return false;
    }
    const auto& skeleton = registry.get<SkeletalComponent>(boneOwner);
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
    outWorld = EntityWorldMatrix(scene, boneOwner) * boneModel;
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
    const glm::mat4 characterWorld = EntityWorldMatrix(scene, character);

    BoneAttachmentComponent attachment;
    attachment.character = character;
    attachment.boneName = bone;
    // Snap to the bone. Any authored offset can be set afterwards in Details
    // (Position/Rotation/Scale are bone-local while attached).
    attachment.localPosition = glm::vec3(0.0f);
    attachment.localRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    attachment.localScale = glm::vec3(1.0f);
    registry.emplace_or_replace<BoneAttachmentComponent>(entity, attachment);

    if (!registry.all_of<TransformComponent>(entity)) {
        registry.emplace<TransformComponent>(entity);
    }
    // The entity is a child of the character in the Outliner. Its transform
    // is stored in character space; bone-space offsets live in the
    // BoneAttachmentComponent.
    registry.emplace_or_replace<ParentComponent>(
        entity, ParentComponent{character});
    auto& transform = registry.get<TransformComponent>(entity);
    SetWorldTransform(transform, glm::inverse(characterWorld) * boneWorld);
    attachment.appliedLocal = transform.LocalMatrix();
    attachment.appliedLocalValid = true;
    registry.get<BoneAttachmentComponent>(entity) = attachment;
    return true;
}

bool BoneAttachmentSystem::Detach(Scene& scene, AssetManager& assets,
                                  entt::entity entity) {
    auto& registry = scene.Registry();
    if (entity == entt::null || !registry.valid(entity) ||
        !registry.all_of<BoneAttachmentComponent>(entity)) {
        return false;
    }
    (void)assets;
    if (auto* transform = registry.try_get<TransformComponent>(entity)) {
        // Preserve the current world pose. Removing the character parent and
        // the attachment turns the entity into a normal world-space root, so
        // a dynamic body can fall from the bone position under gravity.
        const glm::mat4 world = EntityWorldMatrix(scene, entity);
        SetWorldTransform(*transform, world);
    }
    registry.remove<ParentComponent>(entity);
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
        // Keep the entity in the character's Outliner hierarchy. Its local
        // transform is in character space; the bone offset stays in the
        // attachment component.
        if (!registry.all_of<ParentComponent>(entity) ||
            registry.get<ParentComponent>(entity).parent !=
                attachment.character) {
            registry.emplace_or_replace<ParentComponent>(
                entity, ParentComponent{attachment.character});
        }
        auto& transform = view.get<TransformComponent>(entity);
        const glm::mat4 characterWorld =
            EntityWorldMatrix(scene, attachment.character);
        // An external local edit (viewport gizmo, physics, script) wins over
        // the stored bone-local transform.
        if (attachment.appliedLocalValid &&
            !MatricesNearlyEqual(transform.LocalMatrix(),
                                 attachment.appliedLocal)) {
            const glm::mat4 currentWorld =
                characterWorld * transform.LocalMatrix();
            SetLocalFromMatrix(attachment,
                               glm::inverse(boneWorld) * currentWorld);
        }
        const glm::mat4 desiredWorld =
            boneWorld * LocalMatrix(attachment);
        SetWorldTransform(transform,
                          glm::inverse(characterWorld) * desiredWorld);
        attachment.appliedLocal = transform.LocalMatrix();
        attachment.appliedLocalValid = true;
    }
}

} // namespace Galeshi
