#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <string>

namespace Galeshi {

class Scene;
class AssetManager;

// Attaches an entity to a bone of a skeletal character. The entity's
// TransformComponent is kept in world space by Update(); the authored local
// transform lives in BoneAttachmentComponent so it can follow the bone.
namespace BoneAttachmentSystem {

bool BoneWorldMatrix(Scene& scene, AssetManager& assets,
                     entt::entity character, const std::string& bone,
                     glm::mat4& outWorld);

bool Attach(Scene& scene, AssetManager& assets, entt::entity entity,
            entt::entity character, const std::string& bone);

bool Detach(Scene& scene, AssetManager& assets, entt::entity entity);

void Update(Scene& scene, AssetManager& assets);

} // namespace BoneAttachmentSystem
} // namespace Galeshi
