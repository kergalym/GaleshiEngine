#include "GaleshiEditorApp.hpp"

#include <algorithm>
#include <memory>

    void GaleshiEditorApp::EnsureTreeTypeMaterial(Galeshi::VegetationTreeType& type,
                                int typeIndex) {
        if (type.material) return;
        type.material = std::make_shared<Galeshi::Material>();
        type.material->name = "Tree Type " + std::to_string(typeIndex + 1) +
                              " Textures";
        type.material->twoSided = true;
        type.material->backfaceCulling = false;
        type.material->castsShadow = true;
    }



    void GaleshiEditorApp::EnsureTreeMaterialOverrides(
                            Galeshi::VegetationTreeType& type,
                            const Galeshi::AssetManager::LoadedModel& model) {
        for (int materialIndex = 0;
             materialIndex < static_cast<int>(model.materials.size());
             ++materialIndex) {
            const auto& imported = model.materials[
                static_cast<size_t>(materialIndex)];
            if (!imported || imported->name.empty()) continue;

            const bool exists = std::any_of(
                type.materialOverrides.begin(), type.materialOverrides.end(),
                [materialIndex](const Galeshi::VegetationMaterialOverride& m) {
                    return m.materialIndex == materialIndex && m.material;
                });
            if (exists) continue;

            Galeshi::VegetationMaterialOverride overrideMat;
            overrideMat.materialIndex = materialIndex;
            overrideMat.material =
                std::make_shared<Galeshi::Material>(*imported);
            overrideMat.material->sceneOverride = true;
            type.materialOverrides.push_back(std::move(overrideMat));
        }
    }



    void GaleshiEditorApp::EnsureLeafMaterial(Galeshi::VegetationLeafSet& leaf,
                            int typeIndex,  int leafIndex) {
        if (leaf.material) {
            // ORM textures carry their own metallic channel; the material
            // factor must be neutral for that channel to have an effect.
            if (leaf.material->metallicRoughnessMap) {
                leaf.material->metallicFactor = 1.0f;
                leaf.material->roughnessFactor = 1.0f;
            }
            leaf.material->alphaTest = leaf.alphaTest;
            leaf.material->twoSided = leaf.twoSided;
            leaf.material->backfaceCulling = !leaf.twoSided;
            return;
        }
        leaf.material = std::make_shared<Galeshi::Material>();
        leaf.material->name = "Tree " + std::to_string(typeIndex + 1) +
                              " Leaf " + std::to_string(leafIndex + 1);
        leaf.material->twoSided = leaf.twoSided;
        leaf.material->backfaceCulling = !leaf.twoSided;
        leaf.material->castsShadow = true;
        // Leaf textures commonly carry an alpha mask. The shader ignores
        // this when the alpha channel is fully opaque.
        leaf.material->alphaTest = leaf.alphaTest;
        leaf.material->alphaBlend = false;
        leaf.material->alphaCutoff = 0.5f;
        leaf.material->sceneOverride = true;
    }



    bool GaleshiEditorApp::IsLeafMeshName(const std::string& meshName,
                               const std::string& materialName) {
        const std::string haystack =
            ToLowerCopy(meshName + " " + materialName);
        for (const char* token :
             {"leaf", "leaves", "foliage", "frond", "needle", "canopy"}) {
            if (haystack.find(token) != std::string::npos) return true;
        }
        return false;
    }



    std::string GaleshiEditorApp::TreeMeshLabel(
        const Galeshi::AssetManager::LoadedModel& model,  int index) {
        std::string label =
            index >= 0 &&
                index < static_cast<int>(model.meshNames.size()) &&
                !model.meshNames[static_cast<size_t>(index)].empty()
                ? model.meshNames[static_cast<size_t>(index)]
                : "Mesh " + std::to_string(index);
        if (index >= 0 &&
            index < static_cast<int>(model.meshMaterialIndex.size())) {
            const int materialIndex =
                model.meshMaterialIndex[static_cast<size_t>(index)];
            if (materialIndex >= 0 &&
                materialIndex <
                    static_cast<int>(model.materials.size()) &&
                model.materials[static_cast<size_t>(materialIndex)]) {
                const auto& material =
                    model.materials[static_cast<size_t>(materialIndex)];
                if (!material->name.empty()) {
                    label += " (" + material->name + ")";
                }
            }
        }
        return label;
    }



    int GaleshiEditorApp::SuggestedLeafMeshIndex(
        const Galeshi::AssetManager::LoadedModel& model,
        const std::vector<Galeshi::VegetationLeafSet>& leafSets) {
        std::vector<bool> used(model.meshes.size(), false);
        for (const auto& leaf : leafSets) {
            if (leaf.meshIndex >= 0 &&
                leaf.meshIndex < static_cast<int>(used.size())) {
                used[static_cast<size_t>(leaf.meshIndex)] = true;
            }
        }
        for (int pass = 0; pass < 2; ++pass) {
            for (int i = 0; i < static_cast<int>(model.meshes.size()); ++i) {
                if (used[static_cast<size_t>(i)]) continue;
                if (!model.meshes[static_cast<size_t>(i)] ||
                    !model.meshes[static_cast<size_t>(i)]->Valid()) {
                    continue;
                }
                std::string materialName;
                if (i < static_cast<int>(model.meshMaterialIndex.size())) {
                    const int materialIndex =
                        model.meshMaterialIndex[static_cast<size_t>(i)];
                    if (materialIndex >= 0 &&
                        materialIndex <
                            static_cast<int>(model.materials.size()) &&
                        model.materials[static_cast<size_t>(materialIndex)]) {
                        materialName =
                            model.materials[static_cast<size_t>(materialIndex)]
                                ->name;
                    }
                }
                const std::string meshName =
                    i < static_cast<int>(model.meshNames.size())
                        ? model.meshNames[static_cast<size_t>(i)]
                        : std::string{};
                if (pass == 1 || IsLeafMeshName(meshName, materialName)) {
                    return i;
                }
            }
        }
        return -1;
    }



    // Assign a tree model to a Landscape's instanced vegetation.
    // The model is validated before the component/path is changed so a
    // broken FBX cannot silently replace a working tree mesh.
    void GaleshiEditorApp::AssignLandscapeTreeModel(Galeshi::Entity landscapeEntity,
                                  const std::string& originalPath,
                                  int typeIndex) {
        if (!m_assets || !landscapeEntity) return;
        if (!landscapeEntity.HasComponent<Galeshi::LandscapeComponent>()) {
            spdlog::warn("[Editor] Tree model '{}' was dropped on a non-landscape entity",
                         originalPath);
            return;
        }
        if (!IsDragDropModelAssetPath(originalPath)) {
            spdlog::warn("[Editor] Unsupported tree model format: {} "
                         "(FBX drag & drop is disabled)",
                         originalPath);
            return;
        }

        const std::string path = m_assets->MakeRelativePath(originalPath);
        auto model = m_assets->ImportModel(path);
        if (model.meshes.empty()) {
            spdlog::error("[Editor] Cannot use '{}' as a landscape tree: "
                          "the model contains no meshes", path);
            return;
        }

        PushHistory();
        auto& landscape =
            landscapeEntity.GetComponent<Galeshi::LandscapeComponent>();
        auto& vegetation =
            landscapeEntity.HasComponent<Galeshi::VegetationComponent>()
                ? landscapeEntity.GetComponent<Galeshi::VegetationComponent>()
                : landscapeEntity.AddComponent<Galeshi::VegetationComponent>();

        // A dropped model keeps its native size. The per-field Size control
        // remains available for tuning afterwards.
        if (typeIndex >= 0 &&
            typeIndex < static_cast<int>(vegetation.treeTypes.size())) {
            auto& type = vegetation.treeTypes[static_cast<size_t>(typeIndex)];
            if (type.meshPath != path) {
                // Overrides address mesh/material indices in the old model.
                type.leafSets.clear();
                type.materialOverrides.clear();
            }
            type.meshPath = path;
            // Keep the imported model's native size. Asset units are already
            // baked into its vertices, so Size 1.0 is the real file scale.
            type.scale = 1.0f;
            if (type.density <= 0.0f) type.density = 1.0f;
            EnsureTreeTypeMaterial(type, typeIndex);
        } else {
            Galeshi::VegetationTreeType type;
            type.meshPath = path;
            type.scale = 1.0f;
            type.density = 1.0f;
            vegetation.treeTypes.push_back(std::move(type));
            const int newIndex =
                static_cast<int>(vegetation.treeTypes.size()) - 1;
            EnsureTreeTypeMaterial(vegetation.treeTypes.back(), newIndex);
            m_paintTreeType = newIndex;
        }
        vegetation.treeMeshPath.clear();
        vegetation.enabled = true;
        vegetation.dirty = true;
        Galeshi::VegetationGenerator::Generate(landscape, vegetation);
        m_editor.Select(landscapeEntity);

        spdlog::info("[Editor] Landscape tree model: {} ({} meshes, {} materials)",
                     path, model.meshes.size(), model.materials.size());
    }



    // -------------------------------------------------------------------
    // Character model replacement
    // -------------------------------------------------------------------
    std::string GaleshiEditorApp::MaterialNameKey(const std::string& name) {
        std::string key;
        key.reserve(name.size());
        for (unsigned char c : name) {
            if (std::isalnum(c)) {
                key.push_back(
                    static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
        }
        return key;
    }



    float GaleshiEditorApp::ModelMeshHeight(const std::vector<std::shared_ptr<Galeshi::Mesh>>& meshes) {
        float minY = std::numeric_limits<float>::max();
        float maxY = std::numeric_limits<float>::lowest();
        for (const auto& mesh : meshes) {
            if (!mesh) continue;
            for (const auto& vertex : mesh->Vertices()) {
                minY = std::min(minY, vertex.position.y);
                maxY = std::max(maxY, vertex.position.y);
            }
        }
        return maxY > minY ? maxY - minY : 0.0f;
    }



    // Height of the already-authored model in world space. This must be
    // measured per mesh entity rather than through the wrapper's local scale:
    // the wrapper can be the skeleton root, a capsule child, or omitted
    // entirely for older scenes.
    float GaleshiEditorApp::ModelEntityWorldHeight(
        const std::vector<entt::entity>& meshEntities) {
        float minY = std::numeric_limits<float>::max();
        float maxY = std::numeric_limits<float>::lowest();
        auto& reg = m_scene.Registry();
        for (entt::entity entity : meshEntities) {
            if (!reg.valid(entity) ||
                !reg.all_of<Galeshi::MeshComponent>(entity)) {
                continue;
            }
            const auto& mesh = reg.get<Galeshi::MeshComponent>(entity).mesh;
            if (!mesh) continue;
            const glm::mat4 world = EntityWorldMatrix(entity);
            for (const auto& vertex : mesh->Vertices()) {
                const float y =
                    (world * glm::vec4(vertex.position, 1.0f)).y;
                minY = std::min(minY, y);
                maxY = std::max(maxY, y);
            }
        }
        return maxY > minY ? maxY - minY : 0.0f;
    }



    // Imported objects start with a non-metallic material. Clone the asset
    // material so this default does not mutate the AssetManager cache or
    // another instance the artist already adjusted manually.
    std::shared_ptr<Galeshi::Material> GaleshiEditorApp::ImportedMaterial(
        const std::shared_ptr<Galeshi::Material>& source,
        std::unordered_map<const Galeshi::Material*,
                           std::shared_ptr<Galeshi::Material>>& clones) {
        if (!source) return nullptr;
        auto [it, inserted] = clones.try_emplace(source.get());
        if (inserted) {
            it->second = std::make_shared<Galeshi::Material>(*source);
            it->second->metallicFactor = 0.0f;
            it->second->sceneOverride = true;
        }
        return it->second;
    }



    Galeshi::Entity GaleshiEditorApp::CreateMaterialSlotEntity(
        entt::entity parent,  const std::string& name,
        const std::shared_ptr<Galeshi::Material>& sourceMaterial) {
        if (!m_scene.Registry().valid(parent) || !sourceMaterial) return {};
        auto placed = std::make_shared<Galeshi::Material>(*sourceMaterial);
        if (placed->name.empty()) placed->name = name;
        auto slot = m_scene.CreateEntity("__material_slot__" + name);
        slot.AddComponent<Galeshi::ParentComponent>(
            Galeshi::ParentComponent{parent});
        auto& matc = slot.AddComponent<Galeshi::MaterialComponent>(
            Galeshi::MaterialComponent{placed});
        matc.isGlass = placed->isGlass;
        return slot;
    }



    Galeshi::Entity GaleshiEditorApp::FindSkeletalRootForReplacement(entt::entity target) {
        auto& reg = m_scene.Registry();
        if (!reg.valid(target)) return {};
        if (reg.all_of<Galeshi::SkeletalComponent>(target))
            return Galeshi::Entity(target, &m_scene);

        // A capsule/model attachment may be selected instead of the character
        // root. Search both directions so the drop target is forgiving.
        std::vector<entt::entity> queue{target};
        for (size_t head = 0; head < queue.size(); ++head) {
            const entt::entity current = queue[head];
            auto children = reg.view<Galeshi::ParentComponent>();
            for (auto child : children) {
                if (children.get<Galeshi::ParentComponent>(child).parent ==
                    current) {
                    if (reg.all_of<Galeshi::SkeletalComponent>(child))
                        return Galeshi::Entity(child, &m_scene);
                    queue.push_back(child);
                }
            }
        }

        entt::entity current = target;
        for (int depth = 0; depth < 128; ++depth) {
            if (!reg.all_of<Galeshi::ParentComponent>(current)) break;
            current = reg.get<Galeshi::ParentComponent>(current).parent;
            if (!reg.valid(current)) break;
            if (reg.all_of<Galeshi::SkeletalComponent>(current))
                return Galeshi::Entity(current, &m_scene);
        }
        return {};
    }



    bool GaleshiEditorApp::PathsReferToSameAsset(const std::string& a,  const std::string& b) {
        if (a.empty() || b.empty() || !m_assets) return false;
        const std::string ra = m_assets->MakeRelativePath(a);
        const std::string rb = m_assets->MakeRelativePath(b);
        if (ra == rb) return true;
        std::error_code ecA, ecB;
        const auto ca = std::filesystem::weakly_canonical(ra, ecA);
        const auto cb = std::filesystem::weakly_canonical(rb, ecB);
        return !ecA && !ecB && ca == cb;
    }



    Galeshi::Entity GaleshiEditorApp::FindModelContainer(entt::entity skeletonRoot,
                                       const std::string& sourcePath,
                                       std::vector<entt::entity>& outMeshes) {
        auto& reg = m_scene.Registry();
        outMeshes.clear();
        if (!reg.valid(skeletonRoot)) return {};

        std::vector<entt::entity> descendants{skeletonRoot};
        for (size_t head = 0; head < descendants.size(); ++head) {
            const entt::entity current = descendants[head];
            auto children = reg.view<Galeshi::ParentComponent>();
            for (auto child : children) {
                if (children.get<Galeshi::ParentComponent>(child).parent ==
                    current) {
                    descendants.push_back(child);
                }
            }
        }

        for (entt::entity entity : descendants) {
            if (!reg.all_of<Galeshi::MeshComponent>(entity)) continue;
            const auto& mesh = reg.get<Galeshi::MeshComponent>(entity);
            if (!mesh.mesh || mesh.sourceAssetPath.empty()) continue;
            if (!sourcePath.empty() &&
                !PathsReferToSameAsset(mesh.sourceAssetPath, sourcePath)) {
                continue;
            }
            outMeshes.push_back(entity);
        }
        if (outMeshes.empty()) return Galeshi::Entity(skeletonRoot, &m_scene);

        // Climb from a sub-mesh to the outermost imported-model wrapper. Stop
        // at gameplay attachment nodes such as a CollisionCapsule, so the
        // capsule and its parenting are never removed during a swap.
        entt::entity container = skeletonRoot;
        bool containerChosen = false;
        for (entt::entity meshEntity : outMeshes) {
            entt::entity candidate = meshEntity;
            entt::entity parent = entt::null;
            if (reg.all_of<Galeshi::ParentComponent>(meshEntity))
                parent = reg.get<Galeshi::ParentComponent>(meshEntity).parent;

            for (int depth = 0; depth < 128; ++depth) {
                if (!reg.valid(parent) || parent == skeletonRoot) {
                    candidate = skeletonRoot;
                    break;
                }
                if (reg.all_of<Galeshi::ColliderComponent>(parent) ||
                    reg.all_of<Galeshi::RigidBodyComponent>(parent) ||
                    reg.all_of<Galeshi::SkeletalComponent>(parent)) {
                    break;
                }
                candidate = parent;
                if (!reg.all_of<Galeshi::ParentComponent>(parent)) break;
                parent = reg.get<Galeshi::ParentComponent>(parent).parent;
            }

            if (!containerChosen) {
                container = candidate;
                containerChosen = true;
            }
        }
        return reg.valid(container)
            ? Galeshi::Entity(container, &m_scene)
            : Galeshi::Entity(skeletonRoot, &m_scene);
    }



    // Replace the skinned/static model below a character root (or the model
    // attached to its capsule) with a glTF/GLB asset. This is intentionally
    // separate from SpawnFromModel: the character root, capsule physics and
    // all hierarchy links stay intact.
    bool GaleshiEditorApp::ReplaceCharacterModel(entt::entity target,
                               const std::string& originalPath) {
        if (!m_assets) return false;
        if (!IsGltfModelAssetPath(originalPath)) {
            spdlog::warn(
                "[ModelSwap] '{}' is not a glTF/GLB asset. FBX drag & drop "
                "replacement is disabled; export the new character as glTF.",
                originalPath);
            return false;
        }
        Galeshi::Entity skeletonEntity = FindSkeletalRootForReplacement(target);
        if (!skeletonEntity) {
            spdlog::warn(
                "[ModelSwap] No skeletal character found around the drop "
                "target. Drop the glTF on the character or its capsule.");
            return false;
        }

        auto& reg = m_scene.Registry();
        auto& skeleton =
            reg.get<Galeshi::SkeletalComponent>(skeletonEntity.Handle());
        const std::string oldPath = skeleton.sourcePath;

        std::vector<entt::entity> oldMeshEntities;
        Galeshi::Entity modelContainer =
            FindModelContainer(skeletonEntity.Handle(), oldPath,
                               oldMeshEntities);
        if (!modelContainer) modelContainer = skeletonEntity;

        // If the old model has no source path on the skeleton, gather every
        // descendant imported mesh (not primitives) as a fallback.
        if (oldMeshEntities.empty()) {
            modelContainer = FindModelContainer(skeletonEntity.Handle(), {},
                                                oldMeshEntities);
        }

        const std::string newPath = m_assets->MakeRelativePath(originalPath);
        auto newModel = m_assets->ImportModel(newPath);
        if (newModel.meshes.empty()) {
            spdlog::error("[ModelSwap] glTF '{}' contains no meshes", newPath);
            return false;
        }
        if (newModel.totalBones == 0) {
            // Replacing a skinned character with a static glTF destroys the
            // skeleton that external clips retarget onto. Keep the working
            // character instead of silently turning it into a T-pose prop.
            spdlog::error(
                "[ModelSwap] '{}' has no skin/armature; keeping the existing "
                "character model. Export the glTF with the armature and skin.",
                newPath);
            return false;
        }

        // Capture old material values before the old mesh entities disappear.
        struct MaterialSlot {
            std::string key;
            std::string label;
            int meshIndex = -1;
            std::shared_ptr<Galeshi::Material> material;
            bool used = false;
        };
        std::vector<MaterialSlot> oldSlots;
        oldSlots.reserve(oldMeshEntities.size());
        for (entt::entity entity : oldMeshEntities) {
            if (!reg.all_of<Galeshi::MaterialComponent>(entity)) continue;
            const auto& matc = reg.get<Galeshi::MaterialComponent>(entity);
            if (!matc.material) continue;
            MaterialSlot slot;
            slot.key = MaterialNameKey(matc.material->name);
            slot.label = matc.material->name;
            if (slot.label.empty() &&
                reg.all_of<Galeshi::NameComponent>(entity)) {
                // Empty material names keep the Blender object/slot name as a
                // non-persisted display label.
                slot.label =
                    reg.get<Galeshi::NameComponent>(entity).name;
            }
            if (reg.all_of<Galeshi::MeshComponent>(entity))
                slot.meshIndex = reg.get<Galeshi::MeshComponent>(entity).meshIndex;
            slot.material = matc.material;
            oldSlots.push_back(std::move(slot));
        }
        std::sort(oldSlots.begin(), oldSlots.end(),
                  [](const MaterialSlot& a, const MaterialSlot& b) {
                      if (a.meshIndex != b.meshIndex)
                          return a.meshIndex < b.meshIndex;
                      return a.material.get() < b.material.get();
                  });

        float oldHeight = 0.0f;
        {
            std::vector<std::shared_ptr<Galeshi::Mesh>> oldMeshes;
            for (entt::entity entity : oldMeshEntities) {
                if (reg.all_of<Galeshi::MeshComponent>(entity)) {
                    const auto& mesh =
                        reg.get<Galeshi::MeshComponent>(entity);
                    if (mesh.mesh) oldMeshes.push_back(mesh.mesh);
                }
            }
            oldHeight = ModelMeshHeight(oldMeshes);
        }
        const float newHeight = ModelMeshHeight(newModel.meshes);

        const bool containerWasMesh =
            std::find(oldMeshEntities.begin(), oldMeshEntities.end(),
                      modelContainer.Handle()) != oldMeshEntities.end();
        if (containerWasMesh) modelContainer = skeletonEntity;

        glm::vec3 oldContainerScale(1.0f);
        if (modelContainer.HasComponent<Galeshi::TransformComponent>()) {
            oldContainerScale =
                modelContainer.GetComponent<Galeshi::TransformComponent>().scale;
        }

        // Measure the old model in world units. The model wrapper may be
        // parented below the character root and/or a capsule, so its local
        // scale alone is not enough: the old FBX wrapper in the sample scene
        // is 0.01 while its parent chain has another 2x character scale.
        // Using the actual mesh entity world matrices avoids double-counting
        // the wrapper and keeps the new model the same world size even if the
        // wrapper was selected as the skeleton root.
        const float oldWorldHeight =
            ModelEntityWorldHeight(oldMeshEntities);

        entt::entity modelParent = entt::null;
        if (reg.all_of<Galeshi::ParentComponent>(modelContainer.Handle())) {
            modelParent =
                reg.get<Galeshi::ParentComponent>(modelContainer.Handle()).parent;
        }
        float parentWorldScaleY = 1.0f;
        if (reg.valid(modelParent)) {
            const glm::mat4 parentWorld = EntityWorldMatrix(modelParent);
            const float measured = glm::length(glm::vec3(parentWorld[1]));
            if (measured > 1e-6f) parentWorldScaleY = measured;
        }

        PushHistory();

        // Remove only old imported mesh nodes. Capsules, cameras and other
        // gameplay attachments are deliberately left in place.
        for (entt::entity entity : oldMeshEntities) {
            if (reg.valid(entity)) {
                if (m_editor.IsSelected(Galeshi::Entity(entity, &m_scene)))
                    m_editor.RemoveFromSelection(
                        Galeshi::Entity(entity, &m_scene));
                m_scene.DestroyEntity(Galeshi::Entity(entity, &m_scene));
            }
        }

        // Preserve the character's visible world-space size. The local mesh
        // height and the wrapper's world scale are separate quantities:
        // multiplying the old wrapper scale by oldHeight/newHeight
        // double-counts parent/capsule scales and made swaps either gigantic
        // or invisible. Solve for the new local scale from the world-space
        // bind-pose height instead.
        if (modelContainer.HasComponent<Galeshi::TransformComponent>() &&
            oldWorldHeight > 1e-4f && newHeight > 1e-4f &&
            parentWorldScaleY > 1e-6f) {
            const float targetLocalScaleY =
                oldWorldHeight / (newHeight * parentWorldScaleY);
            if (std::isfinite(targetLocalScaleY) &&
                targetLocalScaleY > 1e-5f && targetLocalScaleY < 1e5f) {
                auto& transform =
                    modelContainer.GetComponent<Galeshi::TransformComponent>();
                transform.scale = glm::vec3(targetLocalScaleY);
                spdlog::info(
                    "[ModelSwap] Preserving world height: oldWorld={:.3f} "
                    "newLocal={:.3f} parentScaleY={:.3f} targetScale={:.4f} "
                    "(oldLocal={:.4f})",
                    oldWorldHeight, newHeight, parentWorldScaleY,
                    targetLocalScaleY, oldContainerScale.y);
            } else {
                spdlog::warn(
                    "[ModelSwap] Invalid scale correction "
                    "(oldWorld={}, newLocal={}, parentScaleY={}); "
                    "keeping wrapper transform",
                    oldWorldHeight, newHeight, parentWorldScaleY);
            }
        } else if (modelContainer.HasComponent<Galeshi::TransformComponent>()) {
            // No usable old geometry (or no new geometry): keep the wrapper
            // transform untouched rather than writing a zero/NaN scale.
            auto& transform =
                modelContainer.GetComponent<Galeshi::TransformComponent>();
            if (!std::isfinite(transform.scale.y) ||
                std::abs(transform.scale.y) < 1e-5f) {
                transform.scale = oldContainerScale;
            }
            spdlog::warn(
                "[ModelSwap] Could not measure bind-pose height "
                "(oldLocal={}, newLocal={}); scale left at {:.4f}",
                oldHeight, newHeight, transform.scale.y);
        }

        const std::string stem =
            std::filesystem::path(newPath).stem().string();
        size_t preservedMaterials = 0;
        std::vector<entt::entity> newMeshEntities;
        newMeshEntities.reserve(newModel.meshes.size());
        std::unordered_map<const Galeshi::Material*,
                           std::shared_ptr<Galeshi::Material>> importedMaterials;
        for (size_t i = 0; i < newModel.meshes.size(); ++i) {
            std::shared_ptr<Galeshi::Material> newMaterial;
            if (i < newModel.meshMaterialIndex.size()) {
                const int materialIndex = newModel.meshMaterialIndex[i];
                if (materialIndex >= 0 &&
                    materialIndex < static_cast<int>(newModel.materials.size())) {
                    newMaterial = ImportedMaterial(
                        newModel.materials[static_cast<size_t>(materialIndex)],
                        importedMaterials);
                }
            }
            if (!newMaterial && !newModel.materials.empty())
                newMaterial = ImportedMaterial(
                    newModel.materials.front(), importedMaterials);
            if (!newMaterial)
                newMaterial = std::make_shared<Galeshi::Material>();

            const std::string newKey = MaterialNameKey(newMaterial->name);
            int matchedOld = -1;
            if (!newKey.empty()) {
                for (size_t s = 0; s < oldSlots.size(); ++s) {
                    if (!oldSlots[s].used && oldSlots[s].key == newKey) {
                        matchedOld = static_cast<int>(s);
                        break;
                    }
                }
            }
            if (matchedOld < 0) {
                for (size_t s = 0; s < oldSlots.size(); ++s) {
                    if (!oldSlots[s].used &&
                        oldSlots[s].meshIndex == static_cast<int>(i)) {
                        matchedOld = static_cast<int>(s);
                        break;
                    }
                }
            }

            if (matchedOld >= 0) {
                // Use the material values authored in the replacement glTF.
                // Only the material slot itself is retained; the old FBX
                // factors and texture maps must not override the file.
                oldSlots[static_cast<size_t>(matchedOld)].used = true;
                ++preservedMaterials;
            }

            const std::string meshName =
                (i < newModel.meshNames.size() && !newModel.meshNames[i].empty())
                    ? newModel.meshNames[i]
                    : stem + "_" + std::to_string(i);
            auto meshEntity = m_scene.CreateEntity(meshName);
            meshEntity.AddComponent<Galeshi::ParentComponent>(
                Galeshi::ParentComponent{modelContainer.Handle()});
            meshEntity.AddComponent<Galeshi::MeshComponent>(
                Galeshi::MeshComponent{newModel.meshes[i], newPath, "",
                                       static_cast<int>(i)});
            auto& matc =
                meshEntity.AddComponent<Galeshi::MaterialComponent>(
                    Galeshi::MaterialComponent{newMaterial});
            matc.isGlass = newMaterial->isGlass;
            newMeshEntities.push_back(meshEntity.Handle());
        }

        // Preserve old material slots that have no corresponding new mesh.
        // This keeps custom material slots (for example an extra boot or
        // accessory slot) visible in Details even when the replacement asset
        // has fewer primitives.
        size_t retainedMaterialSlots = 0;
        for (auto& slot : oldSlots) {
            if (slot.used || !slot.material) continue;
            const std::string slotName = slot.label.empty()
                ? "Retained Material"
                : slot.label;
            if (CreateMaterialSlotEntity(modelContainer.Handle(), slotName,
                                         slot.material)) {
                slot.used = true;
                ++retainedMaterialSlots;
            }
        }

        spdlog::info("[ModelSwap] New world height after swap: {:.3f}",
                     ModelEntityWorldHeight(newMeshEntities));

        skeleton.sourcePath = newPath;
        skeleton.currentTime = 0.0f;
        skeleton.activeAnimation = 0;
        skeleton.isAnimated = newModel.hasAnimation;
        skeleton.boneMatrices.assign(256, glm::mat4(1.0f));

        if (auto* machine =
                reg.try_get<Galeshi::AnimationStateMachineComponent>(
                    skeletonEntity.Handle())) {
            auto findClip = [&](const std::string& wanted) {
                if (wanted.empty()) return -1;
                const std::string wantedKey = MaterialNameKey(wanted);
                for (size_t i = 0; i < newModel.animationNames.size(); ++i) {
                    if (newModel.animationNames[i] == wanted) {
                        return static_cast<int>(i);
                    }
                }
                for (size_t i = 0; i < newModel.animationNames.size(); ++i) {
                    if (MaterialNameKey(newModel.animationNames[i]) == wantedKey) {
                        return static_cast<int>(i);
                    }
                }
                return -1;
            };
            if (!newModel.animationNames.empty()) {
                for (auto& state : machine->states) {
                    if (!state.animationPath.empty()) continue;
                    int clip = findClip(state.animationName);
                    if (clip < 0) clip = findClip(state.name);
                    if (clip < 0 && state.clip >= 0 &&
                        state.clip < static_cast<int>(newModel.animationNames.size())) {
                        clip = state.clip;
                    }
                    if (clip < 0) clip = 0;
                    state.clip = clip;
                    state.animationName =
                        newModel.animationNames[static_cast<size_t>(clip)];
                }
                machine->currentState = glm::clamp(
                    machine->currentState, 0,
                    std::max(0, static_cast<int>(machine->states.size()) - 1));
                machine->previousState = -1;
                machine->stateTime = 0.0f;
                machine->previousTime = 0.0f;
                machine->blendElapsed = 0.0f;
                machine->initialized = false;
            }
        }

        if (!reg.all_of<Galeshi::ModelComponent>(skeletonEntity.Handle())) {
            reg.emplace<Galeshi::ModelComponent>(skeletonEntity.Handle());
        }
        m_editor.Select(skeletonEntity);
        m_stateMachineEditorEntity = skeletonEntity.Handle();
        m_showStateMachineEditor = true;
        m_editor.camTarget =
            glm::vec3(EntityWorldMatrix(skeletonEntity.Handle())[3]);
        spdlog::info(
            "[ModelSwap] Replaced '{}' with '{}' ({} meshes, {} matched "
            "material slots, {} retained old slots)",
            oldPath, newPath, newModel.meshes.size(), preservedMaterials,
            retainedMaterialSlots);
        return true;
    }



    void GaleshiEditorApp::OnAssetDoubleClicked(const std::string& path) {
        if (IsCodeAssetPath(path)) {
            OpenCodeFile(path);
            return;
        }
        if (IsModelAssetPath(path)) {
            SpawnFromModel(path, glm::vec3(0.0f));
        } else if (IsImageAssetPath(path)) {
            Galeshi::Entity sel = m_editor.Selected(&m_scene);
            if (sel && sel.HasComponent<Galeshi::MaterialComponent>()) {
                auto& matc = sel.GetComponent<Galeshi::MaterialComponent>();
                if (matc.material) {
                    matc.material->albedoMap = m_assets->LoadTexture(path, true);
                    spdlog::info("[Editor] Applied texture to selected: {}", path);
                }
            }
        }
    }



    glm::vec3 GaleshiEditorApp::ViewportDropWorldPosition(const ImVec2& mouse,  const ImVec2& imageMin,
                                        uint16_t width,  uint16_t height) const {
        if (width == 0 || height == 0) return m_editor.camTarget;

        const float u = (mouse.x - imageMin.x) / static_cast<float>(width);
        const float v = (mouse.y - imageMin.y) / static_cast<float>(height);
        const float ndcX = u * 2.0f - 1.0f;
        const float ndcY = 1.0f - v * 2.0f;

        const glm::mat4 invVP =
            glm::inverse(m_camera.Projection() * m_camera.View());
        glm::vec4 nearPoint = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
        glm::vec4 farPoint  = invVP * glm::vec4(ndcX, ndcY,  1.0f, 1.0f);
        if (std::abs(nearPoint.w) < 1e-6f || std::abs(farPoint.w) < 1e-6f)
            return m_editor.camTarget;

        nearPoint /= nearPoint.w;
        farPoint  /= farPoint.w;
        const glm::vec3 rayOrigin(nearPoint);
        const glm::vec3 rayDir = glm::normalize(glm::vec3(farPoint - nearPoint));

        // Build/empty scenes use the y=0 plane as their placement surface.
        // Falling back to the camera target keeps drops usable even when the
        // camera is looking up or parallel to that plane.
        if (std::abs(rayDir.y) > 1e-4f) {
            const float t = -rayOrigin.y / rayDir.y;
            if (t > 0.0f && t < m_camera.FarPlane()) return rayOrigin + rayDir * t;
        }
        return m_editor.camTarget;
    }



    void GaleshiEditorApp::OnAssetDropped(const std::string& path,  glm::vec3 pos) {
        if (IsCodeAssetPath(path)) {
            OpenCodeFile(path);
            return;
        }
        if (IsModelAssetPath(path)) {
            if (!IsDragDropModelAssetPath(path)) {
                spdlog::warn(
                    "[Editor] FBX cannot be dropped as an asset. "
                    "Use glTF/GLB, OBJ or DAE: {}",
                    path);
                return;
            }
            // Dropping a model onto the selected Landscape assigns it as
            // the instanced tree mesh. A glTF dropped while a character or
            // its capsule is selected replaces the character model in place;
            // otherwise spawn a regular model entity.
            Galeshi::Entity selected = m_editor.Selected(&m_scene);
            if (selected &&
                selected.HasComponent<Galeshi::LandscapeComponent>()) {
                AssignLandscapeTreeModel(selected, path);
            } else if (IsGltfModelAssetPath(path) && selected &&
                       FindSkeletalRootForReplacement(selected.Handle())) {
                ReplaceCharacterModel(selected.Handle(), path);
            } else {
                SpawnFromModel(path, pos, true);
            }
            return;
        }

        // Dropping an image on a material sets its albedo map.
        if (IsImageAssetPath(path)) {
            Galeshi::Entity sel = m_editor.Selected(&m_scene);
            if (sel && sel.HasComponent<Galeshi::MaterialComponent>()) {
                auto& matc = sel.GetComponent<Galeshi::MaterialComponent>();
                if (matc.material) {
                    matc.material->albedoMap = m_assets->LoadTexture(path, true);
                    spdlog::info("[Editor] Set albedo map: {}", path);
                }
            }
        }
    }



    void GaleshiEditorApp::SpawnFromModel(const std::string& originalPath,  glm::vec3 pos,
                        bool placeAtPosition) {
        PushHistory();
        const std::string path = m_assets->MakeRelativePath(originalPath);
        auto model = m_assets->ImportModel(path);
        if (model.meshes.empty()) return;
        namespace fs = std::filesystem;
        const auto stem = fs::path(path).stem().string();

        auto root = m_scene.CreateEntity(stem);
        root.AddComponent<Galeshi::ModelComponent>();
        auto& rootTf = root.GetComponent<Galeshi::TransformComponent>();

        // Model imports are normally created at the origin. A viewport drop
        // additionally positions the new root at the drop point.
        rootTf.position = placeAtPosition ? pos : glm::vec3(0.0f);
        rootTf.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        rootTf.scale = glm::vec3(1.0f);

        // 2. If the model imports flipped (Z-up), uncomment the next line:
        rootTf.rotation = glm::angleAxis(glm::radians(0.0f), glm::vec3(1.0f, 0.0f, 0.0f));

        if (model.hasAnimation || model.totalBones > 0) {
            auto& skel = root.AddComponent<Galeshi::SkeletalComponent>();
            skel.isAnimated = model.hasAnimation;
            skel.sourcePath = path;
            skel.boneMatrices.resize(256, glm::mat4(1.0f));

            if (model.hasAnimation) {
                auto& machine =
                    root.AddComponent<Galeshi::AnimationStateMachineComponent>();
                machine = Galeshi::AnimationSystem::CreateDefaultStateMachine(
                    model.animationNames);
                machine.parameters = {
                    {"Speed", 0.0f},
                    {"PlayerVisible", 0.0f},
                    {"TargetDistance", 100.0f}
                };
            }
        }

        std::unordered_set<int> usedMaterialSlots;
        std::unordered_map<const Galeshi::Material*,
                           std::shared_ptr<Galeshi::Material>> importedMaterials;
        for (size_t i = 0; i < model.meshes.size(); ++i) {
            const std::string meshName =
                (i < model.meshNames.size() && !model.meshNames[i].empty())
                    ? model.meshNames[i]
                    : stem + "_" + std::to_string(i);
            auto e = m_scene.CreateEntity(meshName);

            // NOTE: We do NOT reset child transforms here. Assimp gives them local offsets.

            e.AddComponent<Galeshi::ParentComponent>(Galeshi::ParentComponent{ root.Handle() });
            e.AddComponent<Galeshi::MeshComponent>(Galeshi::MeshComponent{ model.meshes[i], path, "", static_cast<int>(i) });

            std::shared_ptr<Galeshi::Material> mat;
            if (i < model.meshMaterialIndex.size()) {
                int matIdx = model.meshMaterialIndex[i];
                if (matIdx >= 0 && matIdx < static_cast<int>(model.materials.size())) {
                    mat = ImportedMaterial(
                        model.materials[static_cast<size_t>(matIdx)],
                        importedMaterials);
                    usedMaterialSlots.insert(matIdx);
                }
            }
            if (!mat && !model.materials.empty())
                mat = ImportedMaterial(model.materials[0], importedMaterials);
            if (!mat)
                mat = std::make_shared<Galeshi::Material>();
            // Leave the exported material name untouched. Empty names are
            // valid and must not be replaced with a fabricated one.
            e.AddComponent<Galeshi::MaterialComponent>(Galeshi::MaterialComponent{ mat });
        }

        // Some exporters write named materials that are not referenced by a
        // visible primitive. Keep only named slots; a synthetic unnamed
        // material is not a Blender material and must not create a fake entry.
        for (size_t i = 0; i < model.materials.size(); ++i) {
            if (usedMaterialSlots.count(static_cast<int>(i)) != 0 ||
                !model.materials[i] || model.materials[i]->name.empty()) {
                continue;
            }
            CreateMaterialSlotEntity(
                root.Handle(), model.materials[i]->name,
                ImportedMaterial(model.materials[i], importedMaterials));
        }

        m_editor.Select(root);
        m_editor.camTarget = pos;
        m_editor.camRadius = 5.0f;
        if (root.HasComponent<Galeshi::SkeletalComponent>()) {
            m_stateMachineEditorEntity = root.Handle();
            m_showStateMachineEditor = true;
        }
    }

