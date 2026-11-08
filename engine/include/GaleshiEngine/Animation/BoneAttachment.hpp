#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <string>

namespace Galeshi {

class Scene;
class AssetManager;

// Attaches an entity to a bone of a skeletal character. The entity becomes a
// child of the character in the Outliner; its TransformComponent is stored in
// character space and BoneAttachmentSystem::Update refreshes it from the bone.
// The authored bone-local offset lives in BoneAttachmentComponent.
namespace BoneAttachmentSystem {

// Returns the skeletal entity that owns the live animation palette for a
// character. Imported characters often have an empty Player/model wrapper
// around the actual animated skeleton, so callers must not assume the nearest
// SkeletalComponent is the animated one.
entt::entity FindAnimatingSkeleton(Scene& scene, entt::entity character);

bool BoneWorldMatrix(Scene& scene, AssetManager& assets,
                     entt::entity character, const std::string& bone,
                     glm::mat4& outWorld);

bool Attach(Scene& scene, AssetManager& assets, entt::entity entity,
            entt::entity character, const std::string& bone);

bool Detach(Scene& scene, AssetManager& assets, entt::entity entity);

void Update(Scene& scene, AssetManager& assets);

} // namespace BoneAttachmentSystem
} // namespace Galeshi
