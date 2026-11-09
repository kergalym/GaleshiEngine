#include "GaleshiEditorApp.hpp"



    // -------------------------------------------------------------------
    // Details
    // -------------------------------------------------------------------
    void GaleshiEditorApp::DrawTextureSlot(const char* label,  std::shared_ptr<Galeshi::Texture>& slot,
                         bool srgb,  bool* srgbFlag,
                         bool* changed) {
        ImGui::PushID(label);
        ImGui::Text("%s", label);

        const auto acceptTextureDrop = [&]() {
            if (!ImGui::BeginDragDropTarget()) return;
            if (const ImGuiPayload* payload =
                    ImGui::AcceptDragDropPayload("GALESHI_ASSET_PATH")) {
                const char* path = static_cast<const char*>(payload->Data);
                std::string p = path;
                auto dotPos = p.find_last_of('.');
                std::string ext =
                    (dotPos != std::string::npos) ? p.substr(dotPos) : "";
                for (auto& c : ext) {
                    c = static_cast<char>(
                        std::tolower(static_cast<unsigned char>(c)));
                }
                if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
                    ext == ".bmp" || ext == ".tga" || ext == ".dds" ||
                    ext == ".ktx") {
                    if (changed) *changed = true;
                    PushHistory();
                    slot = m_assets->LoadTexture(
                        p, srgbFlag ? *srgbFlag : srgb);
                }
            }
            ImGui::EndDragDropTarget();
        };

        if (slot && bgfx::isValid(slot->Handle())) {
            ImTextureID texId = static_cast<ImTextureID>(slot->Handle().idx);
            // Engine textures are uploaded with a bottom-left V origin (the
            // Blender/UV convention); ImGui displays top-left, so flip only
            // the preview quad.
            ImGui::Image(texId, ImVec2(64, 64),
                         ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
            acceptTextureDrop();
            ImGui::SameLine();
            ImGui::BeginGroup();
            std::string path = slot->Path();
            auto slash = path.find_last_of('/');
            std::string name = (slash != std::string::npos) ? path.substr(slash + 1) : path;
            ImGui::TextWrapped("%s", name.c_str());
            if (ImGui::Button("Clear")) {
                if (changed) *changed = true;
                PushHistory();
                slot.reset();
            }
            ImGui::EndGroup();
        } else {
            ImGui::Button("Empty (drop texture here)", ImVec2(200, 64));
            acceptTextureDrop();
        }

        // Every material map exposes its color space. Albedo starts enabled
        // and data maps start linear; the user can override any of them.
        bool value = srgbFlag ? *srgbFlag : srgb;
        if (!srgbFlag) ImGui::BeginDisabled();
        if (ImGui::Checkbox("sRGB", &value) && srgbFlag) {
            if (changed) *changed = true;
            PushHistory();
            *srgbFlag = value;
            if (slot) {
                const std::string texturePath = slot->Path();
                if (!texturePath.empty()) {
                    auto reloaded = m_assets->LoadTexture(
                        texturePath, value, nullptr,
                        slot->FlippedVertically());
                    // Keep the current upload if the requested variant cannot
                    // be produced (for example a missing external file).
                    if (reloaded && reloaded != m_assets->WhiteTexture()) {
                        slot = reloaded;
                    }
                }
            }
        }
        if (!srgbFlag) ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip(
                "Decode this texture as sRGB. Disable for linear data maps.");
        }

        ImGui::PopID();
    }



    bool GaleshiEditorApp::HasSkeletalParent(entt::entity handle) {
        auto& reg = m_scene.Registry();
        if (!reg.valid(handle) ||
            !reg.all_of<Galeshi::ParentComponent>(handle)) {
            return false;
        }
        const entt::entity parent =
            reg.get<Galeshi::ParentComponent>(handle).parent;
        return reg.valid(parent) &&
               reg.all_of<Galeshi::SkeletalComponent>(parent);
    }



    bool GaleshiEditorApp::HasCollisionShapeParent(entt::entity handle) {
        auto& reg = m_scene.Registry();
        if (!reg.valid(handle) ||
            !reg.all_of<Galeshi::ParentComponent>(handle)) {
            return false;
        }
        const entt::entity parent =
            reg.get<Galeshi::ParentComponent>(handle).parent;
        return reg.valid(parent) &&
               reg.all_of<Galeshi::ColliderComponent>(parent) &&
               !reg.all_of<Galeshi::SkeletalComponent>(parent);
    }



    void GaleshiEditorApp::SetLocalTransformFromWorld(entt::entity handle,
                                    const glm::mat4& world) {
        auto& reg = m_scene.Registry();
        if (!reg.all_of<Galeshi::TransformComponent>(handle)) return;
        auto& tf = reg.get<Galeshi::TransformComponent>(handle);
        glm::vec3 scale, translation, skew;
        glm::vec4 perspective;
        glm::quat rotation;
        if (glm::decompose(world, scale, rotation, translation, skew,
                           perspective)) {
            tf.position = translation;
            tf.rotation = glm::normalize(rotation);
            tf.scale = scale;
        } else {
            tf.position = glm::vec3(world[3]);
        }
    }



    // Detach is a collision-shape feature: a child can leave a collision
    // shape parent, but never a skeletal parent. The world transform is kept.
    void GaleshiEditorApp::DetachChildFromCollisionShape(entt::entity child) {
        auto& reg = m_scene.Registry();
        if (!HasCollisionShapeParent(child)) return;

        PushHistory();
        const glm::mat4 world = EntityWorldMatrix(child);
        reg.remove<Galeshi::ParentComponent>(child);
        SetLocalTransformFromWorld(child, world);
        spdlog::info("[Outliner] Detached child {} from collision shape",
                     static_cast<uint32_t>(child));
    }



    void GaleshiEditorApp::DetachCollisionShapeChildren(entt::entity parent) {
        auto& reg = m_scene.Registry();
        if (!reg.valid(parent) ||
            !reg.all_of<Galeshi::ColliderComponent>(parent) ||
            reg.all_of<Galeshi::SkeletalComponent>(parent)) {
            return;
        }

        std::vector<std::pair<entt::entity, glm::mat4>> children;
        auto childView = reg.view<Galeshi::ParentComponent>();
        for (auto child : childView) {
            if (childView.get<Galeshi::ParentComponent>(child).parent != parent)
                continue;
            glm::mat4 world(1.0f);
            if (reg.all_of<Galeshi::TransformComponent>(child))
                world = EntityWorldMatrix(child);
            children.emplace_back(child, world);
        }
        if (children.empty()) return;

        PushHistory();
        for (const auto& [child, world] : children) {
            if (!reg.valid(child)) continue;
            reg.remove<Galeshi::ParentComponent>(child);
            SetLocalTransformFromWorld(child, world);
        }
        spdlog::info("[Outliner] Detached {} children from collision shape {}",
                     children.size(), static_cast<uint32_t>(parent));
    }



    bool GaleshiEditorApp::IsDescendantOf(entt::entity node,  entt::entity ancestor) {
        auto& reg = m_scene.Registry();
        for (int depth = 0; reg.valid(node) && depth < 128; ++depth) {
            if (node == ancestor) return true;
            if (!reg.all_of<Galeshi::ParentComponent>(node)) break;
            node = reg.get<Galeshi::ParentComponent>(node).parent;
        }
        return false;
    }



    void GaleshiEditorApp::DetachFromParent(entt::entity child) {
        auto& reg = m_scene.Registry();
        if (!reg.valid(child) ||
            !reg.all_of<Galeshi::ParentComponent>(child) ||
            HasSkeletalParent(child)) {
            return;
        }
        PushHistory();
        const glm::mat4 world = EntityWorldMatrix(child);
        reg.remove<Galeshi::ParentComponent>(child);
        SetLocalTransformFromWorld(child, world);
        spdlog::info("[Outliner] Detached entity {} from its parent",
                     static_cast<uint32_t>(child));
    }



    void GaleshiEditorApp::DetachChildren(entt::entity parent) {
        auto& reg = m_scene.Registry();
        if (!reg.valid(parent) ||
            reg.all_of<Galeshi::SkeletalComponent>(parent)) {
            return;
        }
        std::vector<std::pair<entt::entity, glm::mat4>> children;
        auto childView = reg.view<Galeshi::ParentComponent>();
        for (auto child : childView) {
            if (childView.get<Galeshi::ParentComponent>(child).parent != parent)
                continue;
            if (reg.all_of<Galeshi::NameComponent>(child)) {
                const auto& name = reg.get<Galeshi::NameComponent>(child).name;
                if (name.rfind("__material_slot__", 0) == 0) continue;
            }
            glm::mat4 world(1.0f);
            if (reg.all_of<Galeshi::TransformComponent>(child))
                world = EntityWorldMatrix(child);
            children.emplace_back(child, world);
        }
        if (children.empty()) return;

        PushHistory();
        for (const auto& [child, world] : children) {
            if (!reg.valid(child) || HasSkeletalParent(child)) continue;
            reg.remove<Galeshi::ParentComponent>(child);
            SetLocalTransformFromWorld(child, world);
        }
        spdlog::info("[Outliner] Detached {} children from entity {}",
                     children.size(), static_cast<uint32_t>(parent));
    }



    void GaleshiEditorApp::AttachSelectedTo(entt::entity parent) {
        auto& reg = m_scene.Registry();
        auto selected = m_editor.SelectedAll(&m_scene);
        if (!reg.valid(parent) || selected.size() != 1) return;
        const entt::entity child = selected.front().Handle();
        if (!reg.valid(child) || child == parent ||
            HasSkeletalParent(child) || IsDescendantOf(parent, child)) {
            return;
        }

        PushHistory();
        const glm::mat4 world = EntityWorldMatrix(child);
        reg.emplace_or_replace<Galeshi::ParentComponent>(
            child, Galeshi::ParentComponent{parent});
        SetLocalTransformFromWorld(
            child, glm::inverse(EntityWorldMatrix(parent)) * world);
        spdlog::info("[Outliner] Attached entity {} to {}",
                     static_cast<uint32_t>(child),
                     static_cast<uint32_t>(parent));
    }



    void GaleshiEditorApp::AttachPrimitiveMesh(entt::entity parent,  bool sphere) {
        if (!m_scene.Registry().valid(parent)) return;
        PushHistory();
        auto child = sphere
            ? MakeSphere("MeshChild", glm::vec3(0.0f))
            : MakeCube("MeshChild", glm::vec3(0.0f));
        auto& reg = m_scene.Registry();
        reg.emplace_or_replace<Galeshi::ParentComponent>(
            child.Handle(), Galeshi::ParentComponent{parent});
        if (reg.all_of<Galeshi::TransformComponent>(child.Handle())) {
            auto& tf = reg.get<Galeshi::TransformComponent>(child.Handle());
            tf.position = glm::vec3(0.0f);
            tf.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            tf.scale = glm::vec3(1.0f);
        }
        m_editor.Select(child);
    }



    bool GaleshiEditorApp::MeshEntityWorldBounds(entt::entity handle,  glm::vec3& outCenter,
                               glm::vec3& outExtents) {
        auto& reg = m_scene.Registry();
        if (!reg.valid(handle)) return false;
        const auto* meshComponent =
            reg.try_get<Galeshi::MeshComponent>(handle);
        if (!meshComponent || !meshComponent->mesh) return false;

        glm::vec3 localMin, localMax;
        meshComponent->mesh->LocalAABB(localMin, localMax);
        const glm::mat4 world = EntityWorldMatrix(handle);
        glm::vec3 worldMin(std::numeric_limits<float>::max());
        glm::vec3 worldMax(std::numeric_limits<float>::lowest());
        for (int corner = 0; corner < 8; ++corner) {
            const glm::vec3 local{
                (corner & 1) ? localMax.x : localMin.x,
                (corner & 2) ? localMax.y : localMin.y,
                (corner & 4) ? localMax.z : localMin.z};
            const glm::vec3 point = glm::vec3(world * glm::vec4(local, 1.0f));
            worldMin = glm::min(worldMin, point);
            worldMax = glm::max(worldMax, point);
        }
        outCenter = (worldMin + worldMax) * 0.5f;
        outExtents = (worldMax - worldMin) * 0.5f;
        return true;
    }



    void GaleshiEditorApp::AccumulateEntityWorldBounds(entt::entity handle,  glm::vec3& outMin,
                                     glm::vec3& outMax,  int depth) {
        auto& reg = m_scene.Registry();
        if (!reg.valid(handle) || depth > 128) return;

        glm::vec3 center, extents;
        if (MeshEntityWorldBounds(handle, center, extents)) {
            outMin = glm::min(outMin, center - extents);
            outMax = glm::max(outMax, center + extents);
        } else if (const auto* collider =
                       reg.try_get<Galeshi::ColliderComponent>(handle)) {
            glm::vec3 half;
            switch (collider->shapeType) {
                case Galeshi::ColliderShapeType::Sphere:
                    half = glm::vec3(collider->radius);
                    break;
                case Galeshi::ColliderShapeType::Capsule:
                    half = glm::vec3(collider->radius,
                                     collider->halfHeight + collider->radius,
                                     collider->radius);
                    break;
                case Galeshi::ColliderShapeType::Box:
                default:
                    half = collider->halfExtents;
                    break;
            }
            const glm::mat4 world = EntityWorldMatrix(handle);
            const glm::vec3 centerWorld =
                glm::vec3(world * glm::vec4(collider->offset, 1.0f));
            const glm::vec3 scale{
                glm::length(glm::vec3(world[0])),
                glm::length(glm::vec3(world[1])),
                glm::length(glm::vec3(world[2]))};
            const glm::vec3 worldHalf = half * scale;
            outMin = glm::min(outMin, centerWorld - worldHalf);
            outMax = glm::max(outMax, centerWorld + worldHalf);
        }

        auto childView = reg.view<Galeshi::ParentComponent>();
        for (entt::entity child : childView) {
            if (childView.get<Galeshi::ParentComponent>(child).parent == handle) {
                AccumulateEntityWorldBounds(child, outMin, outMax, depth + 1);
            }
        }
    }



    bool GaleshiEditorApp::EntityWorldBounds(entt::entity handle,  glm::vec3& outCenter,
                           glm::vec3& outExtents) {
        glm::vec3 min(std::numeric_limits<float>::max());
        glm::vec3 max(std::numeric_limits<float>::lowest());
        AccumulateEntityWorldBounds(handle, min, max, 0);
        if (min.x > max.x || min.y > max.y || min.z > max.z) {
            auto& reg = m_scene.Registry();
            if (!reg.valid(handle) ||
                !reg.all_of<Galeshi::TransformComponent>(handle)) {
                return false;
            }
            outCenter = glm::vec3(EntityWorldMatrix(handle)[3]);
            outExtents = glm::vec3(0.5f);
            return true;
        }
        outCenter = (min + max) * 0.5f;
        outExtents = (max - min) * 0.5f;
        return true;
    }



    void GaleshiEditorApp::FocusCameraOnEntity(entt::entity handle) {
        glm::vec3 center, extents;
        if (!EntityWorldBounds(handle, center, extents)) return;
        m_editor.camTarget = center;
        m_editor.camRadius =
            glm::clamp(glm::length(extents) * 2.5f, 1.0f, 200.0f);
        m_editor.Select(Galeshi::Entity(handle, &m_scene));
    }



    void GaleshiEditorApp::AttachCollisionShape(entt::entity parent,
                              Galeshi::ColliderShapeType type) {
        if (!m_scene.Registry().valid(parent)) return;
        auto child = SpawnCollisionShape("CollisionShape", type);
        auto& reg = m_scene.Registry();
        reg.emplace_or_replace<Galeshi::ParentComponent>(
            child.Handle(), Galeshi::ParentComponent{parent});
        if (reg.all_of<Galeshi::TransformComponent>(child.Handle())) {
            auto& tf = reg.get<Galeshi::TransformComponent>(child.Handle());
            tf.position = glm::vec3(0.0f);
            tf.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            tf.scale = glm::vec3(1.0f);
        }
        m_editor.Select(child);
    }



        void GaleshiEditorApp::DrawOutliner() {
        ImGui::Begin("World Outliner");
        auto& reg = m_scene.Registry();

        // ── Helper: compute world matrix by walking up the parent chain ──
        auto GetWorldMatrix = [&](entt::entity ent) -> glm::mat4 {
            glm::mat4 world(1.0f);
            std::vector<entt::entity> chain;
            entt::entity curr = ent;
            int depth = 0;
            while (curr != entt::null && depth++ < 128) {
                chain.push_back(curr);
                if (reg.all_of<Galeshi::ParentComponent>(curr))
                    curr = reg.get<Galeshi::ParentComponent>(curr).parent;
                else
                    break;
            }
            for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
                if (reg.all_of<Galeshi::TransformComponent>(*it))
                    world = world * reg.get<Galeshi::TransformComponent>(*it).LocalMatrix();
            }
            return world;
        };

        // ── Helper: decompose matrix into TRS and write to TransformComponent ──
        auto SetLocalFromMatrix = [&](entt::entity ent, const glm::mat4& m) {
            if (!reg.all_of<Galeshi::TransformComponent>(ent)) return;
            auto& tf = reg.get<Galeshi::TransformComponent>(ent);

            glm::vec3 pos(m[3]);
            glm::vec3 c0(m[0]), c1(m[1]), c2(m[2]);
            glm::vec3 scl(glm::length(c0), glm::length(c1), glm::length(c2));

            // Avoid divide-by-zero
            float sx = (scl.x > 1e-6f) ? scl.x : 1.0f;
            float sy = (scl.y > 1e-6f) ? scl.y : 1.0f;
            float sz = (scl.z > 1e-6f) ? scl.z : 1.0f;

            glm::mat3 rotMat(c0 / sx, c1 / sy, c2 / sz);
            glm::quat rot = glm::quat_cast(rotMat);

            tf.position = pos;
            tf.rotation = rot;
            tf.scale    = scl;
        };

        // A missing/corrupt skeletal source leaves a saved child hierarchy
        // with no valid mesh from that source. Detect it so the whole exploded
        // subtree can be hidden instead of leaving stale bone/mesh children.
        auto HasValidSourceMeshDescendant =
            [&](entt::entity root, const std::string& sourcePath) {
                if (sourcePath.empty() || !m_assets) return false;
                const std::string source =
                    m_assets->MakeRelativePath(sourcePath);
                auto meshView = reg.view<Galeshi::MeshComponent>();
                for (entt::entity meshEntity : meshView) {
                    const auto& mesh =
                        meshView.get<Galeshi::MeshComponent>(meshEntity);
                    if (!mesh.mesh || !mesh.mesh->Valid() ||
                        mesh.sourceAssetPath.empty()) {
                        continue;
                    }
                    if (m_assets->MakeRelativePath(mesh.sourceAssetPath) !=
                        source) {
                        continue;
                    }
                    entt::entity current = meshEntity;
                    for (int depth = 0; current != entt::null && depth < 128;
                         ++depth) {
                        if (current == root) return true;
                        if (!reg.all_of<Galeshi::ParentComponent>(current)) {
                            break;
                        }
                        current =
                            reg.get<Galeshi::ParentComponent>(current).parent;
                    }
                }
                return false;
            };

        // Hide stale children while keeping real attachments and any
        // descendant that still owns usable content.
        std::function<bool(entt::entity)> HasUsableOutlinerContent =
            [&](entt::entity entity) -> bool {
                if (!reg.valid(entity)) return false;
                if (reg.all_of<Galeshi::MeshComponent>(entity)) {
                    const auto& mesh =
                        reg.get<Galeshi::MeshComponent>(entity);
                    if (mesh.mesh && mesh.mesh->Valid()) return true;
                }
                if (reg.any_of<
                        Galeshi::ModelComponent,
                        Galeshi::SkeletalComponent,
                        Galeshi::BoneAttachmentComponent,
                        Galeshi::CameraComponent,
                        Galeshi::DirectionalLightComponent,
                        Galeshi::PointLightComponent,
                        Galeshi::SpotLightComponent,
                        Galeshi::RigidBodyComponent,
                        Galeshi::ColliderComponent,
                        Galeshi::LandscapeComponent,
                        Galeshi::VegetationComponent,
                        Galeshi::NavAgentComponent,
                        Galeshi::SoftBodyComponent,
                        Galeshi::AIControllerComponent,
                        Galeshi::ScriptComponent,
                        Galeshi::PlayerTagComponent,
                        Galeshi::PerceptionComponent,
                        Galeshi::BehaviorTreeComponent,
                        Galeshi::AudioSourceComponent,
                        Galeshi::AudioListenerComponent,
                        Galeshi::PostProcessVolumeComponent,
                        Galeshi::ParticleEmitterComponent,
                        Galeshi::WaterComponent,
                        Galeshi::GroomComponent,
                        Galeshi::UICanvasComponent,
                        Galeshi::RagdollComponent,
                        Galeshi::LuaEntityComponent>(entity)) {
                    return true;
                }

                bool usable = false;
                reg.view<Galeshi::ParentComponent>().each(
                    [&](auto child, auto& parentComponent) {
                        if (usable ||
                            parentComponent.parent != entity) {
                            return;
                        }
                        usable = HasUsableOutlinerContent(child);
                    });
                return usable;
            };

        // Imported models are stored as an empty root with one mesh child per
        // material submesh. Those children only add noise to the Outliner, so
        // show the model root by itself. Skeletal roots can own non-mesh
        // children as well, so recognise them explicitly.
        auto IsModelRoot = [&](entt::entity root,
                               const std::vector<entt::entity>& children) {
            if (reg.all_of<Galeshi::SkeletalComponent>(root)) return true;
            // Collision and physics shapes are real hierarchy nodes, not
            // imported-model wrappers. Keep their attached meshes visible so
            // each child can be selected and detached independently.
            if (reg.all_of<Galeshi::ColliderComponent>(root) ||
                reg.all_of<Galeshi::RigidBodyComponent>(root)) {
                return false;
            }
            if (children.empty() || reg.all_of<Galeshi::MeshComponent>(root))
                return false;
            for (entt::entity child : children) {
                if (!reg.all_of<Galeshi::MeshComponent>(child)) return false;
                if (reg.get<Galeshi::MeshComponent>(child).sourceAssetPath.empty())
                    return false;
            }
            return true;
        };

        std::function<void(entt::entity, int)> DrawNode = [&](entt::entity e, int depth) {
            if (!reg.valid(e)) return;

            std::string name = "Entity " + std::to_string((uint32_t)e);
            if (reg.all_of<Galeshi::NameComponent>(e))
                name = reg.get<Galeshi::NameComponent>(e).name;
            if (reg.all_of<Galeshi::SkeletalComponent>(e))
                name = "[Skeletal] " + name;

            ImGui::PushID((uint32_t)e);

            // Snapshot children before drawing (avoids iterator invalidation during drag&drop)
            std::vector<entt::entity> children;
            reg.view<Galeshi::ParentComponent>().each([&](auto child, auto& pc) {
                if (pc.parent != e) return;
                // Material-only placeholders are Details slots, not model
                // hierarchy nodes; keep them out of the Outliner.
                if (reg.all_of<Galeshi::NameComponent>(child)) {
                    const auto& childName =
                        reg.get<Galeshi::NameComponent>(child).name;
                    if (childName.rfind("__material_slot__", 0) == 0) return;
                }
                children.push_back(child);
            });
            if (const auto* skeleton =
                    reg.try_get<Galeshi::SkeletalComponent>(e)) {
                if (!skeleton->sourcePath.empty() &&
                    !HasValidSourceMeshDescendant(e, skeleton->sourcePath)) {
                    // The source skeleton failed to load: no child from it is
                    // meaningful, so collapse the exploded hierarchy.
                    children.clear();
                } else {
                    children.erase(
                        std::remove_if(
                            children.begin(), children.end(),
                            [&](entt::entity child) {
                                return !HasUsableOutlinerContent(child);
                            }),
                        children.end());
                }
            }
            if (IsModelRoot(e, children)) {
                // Imported model roots collapse their own mesh submeshes, but
                // must keep non-mesh attachments visible. In particular, a
                // skeletal character's collision shape must remain a separate
                // Outliner node that can be selected and detached.
                children.erase(
                    std::remove_if(
                        children.begin(), children.end(),
                        [&](entt::entity child) {
                            if (!reg.all_of<Galeshi::MeshComponent>(child))
                                return false;
                            return !reg.get<Galeshi::MeshComponent>(child)
                                        .sourceAssetPath.empty();
                        }),
                    children.end());
            }
            bool hasChildren = !children.empty();

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen;
            if (m_editor.IsSelected(Galeshi::Entity(e, &m_scene))) flags |= ImGuiTreeNodeFlags_Selected;
            if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf;
            // Landscape rows are asset drop targets for static tree models;
            // spanning the row makes that target much easier to hit.
            if (reg.all_of<Galeshi::LandscapeComponent>(e))
                flags |= ImGuiTreeNodeFlags_SpanAvailWidth;

            bool open = ImGui::TreeNodeEx("##node", flags, "%s", name.c_str());

            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                m_editor.Select(Galeshi::Entity(e, &m_scene));
            if (ImGui::IsItemHovered() &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                FocusCameraOnEntity(e);
            }

            // Hierarchy actions for meshes, collision shapes and the other
            // attachment nodes artists use to assemble a character.
            if (ImGui::BeginPopupContextItem("##node_context")) {
                auto selected = m_editor.SelectedAll(&m_scene);
                const bool canAttachSelected =
                    selected.size() == 1 &&
                    selected.front().Handle() != e &&
                    !HasSkeletalParent(selected.front().Handle()) &&
                    !IsDescendantOf(e, selected.front().Handle());

                if (ImGui::MenuItem("Attach Selected Here", nullptr, false,
                                    canAttachSelected)) {
                    AttachSelectedTo(e);
                }
                if (ImGui::BeginMenu("Attach Mesh")) {
                    if (ImGui::MenuItem("Cube")) AttachPrimitiveMesh(e, false);
                    if (ImGui::MenuItem("Sphere")) AttachPrimitiveMesh(e, true);
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Attach Collision Shape")) {
                    if (ImGui::MenuItem("Box")) {
                        AttachCollisionShape(
                            e, Galeshi::ColliderShapeType::Box);
                    }
                    if (ImGui::MenuItem("Sphere")) {
                        AttachCollisionShape(
                            e, Galeshi::ColliderShapeType::Sphere);
                    }
                    if (ImGui::MenuItem("Capsule")) {
                        AttachCollisionShape(
                            e, Galeshi::ColliderShapeType::Capsule);
                    }
                    ImGui::EndMenu();
                }

                ImGui::Separator();
                const bool canDetachParent =
                    reg.all_of<Galeshi::ParentComponent>(e) &&
                    !HasSkeletalParent(e);
                const bool canDetachChildren =
                    !children.empty() &&
                    !reg.all_of<Galeshi::SkeletalComponent>(e);
                if (ImGui::MenuItem("Detach from Parent", nullptr, false,
                                    canDetachParent)) {
                    DetachFromParent(e);
                }
                if (ImGui::MenuItem("Detach Children", nullptr, false,
                                    canDetachChildren)) {
                    DetachChildren(e);
                }
                ImGui::EndPopup();
            }

            if (!HasSkeletalParent(e) && ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload("ENTITY", &e, sizeof(entt::entity));
                ImGui::Text("Move %s", name.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget()) {
                // A model dropped on a Landscape node becomes its instanced
                // static tree mesh. A glTF/GLB dropped on a character or on
                // its capsule swaps the model while preserving the hierarchy;
                // material values come from the new file. Other entities keep
                // accepting only Outliner reparenting drops.
                if (const ImGuiPayload* active = ImGui::GetDragDropPayload();
                    active && active->IsDataType("GALESHI_ASSET_PATH") &&
                    ImGui::IsItemHovered(
                        ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
                    const char* activePath =
                        static_cast<const char*>(active->Data);
                    if (reg.all_of<Galeshi::LandscapeComponent>(e)) {
                        ImGui::SetTooltip(
                            "Drop a model to use it as this Landscape's "
                            "instanced tree mesh.");
                    } else if (activePath &&
                               IsGltfModelAssetPath(activePath) &&
                               FindSkeletalRootForReplacement(e)) {
                        ImGui::SetTooltip(
                            "Drop to replace this character's model with the "
                            "glTF/GLB.\nMaterial values are reloaded from the "
                            "file.");
                    } else if (activePath &&
                               AssetExtensionLower(activePath) == ".fbx") {
                        ImGui::SetTooltip(
                            "FBX drag & drop is disabled. Use glTF/GLB.");
                    }
                }
                if (const ImGuiPayload* payload =
                        ImGui::AcceptDragDropPayload("GALESHI_ASSET_PATH")) {
                    const char* path =
                        static_cast<const char*>(payload->Data);
                    if (path &&
                        reg.all_of<Galeshi::LandscapeComponent>(e)) {
                        if (IsDragDropModelAssetPath(path)) {
                            AssignLandscapeTreeModel(
                                Galeshi::Entity(e, &m_scene), path);
                        }
                    } else if (path && IsGltfModelAssetPath(path) &&
                               FindSkeletalRootForReplacement(e)) {
                        ReplaceCharacterModel(e, path);
                    }
                }
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY")) {
                    entt::entity dragged = *(const entt::entity*)payload->Data;
                    if (HasSkeletalParent(dragged)) {
                        spdlog::warn("[Outliner] Cannot move a child of a "
                                     "skeletal mesh");
                    } else if (dragged != e) {
                        // Cycle detection: check if 'e' is a descendant of 'dragged'
                        bool isCycle = false;
                        entt::entity curr = e;
                        int depth = 0;
                        while (curr != entt::null && depth++ < 128) {
                            if (curr == dragged) { isCycle = true; break; }
                            if (reg.all_of<Galeshi::ParentComponent>(curr))
                                curr = reg.get<Galeshi::ParentComponent>(curr).parent;
                            else
                                break;
                        }

                        if (!isCycle) {
                            PushHistory();

                            // 1) Detach from old parent
                            if (reg.all_of<Galeshi::ParentComponent>(dragged))
                                reg.remove<Galeshi::ParentComponent>(dragged);

                            // 2) Attach to new parent
                            reg.emplace_or_replace<Galeshi::ParentComponent>(
                                dragged, Galeshi::ParentComponent{ e });

                            // 3) Reset the dragged child's local transform.
                            // The attachment is authored relative to the new
                            // parent, not to the old world position.
                            if (reg.all_of<Galeshi::TransformComponent>(dragged)) {
                                auto& tf = reg.get<Galeshi::TransformComponent>(dragged);
                                tf.position = glm::vec3(0.0f);
                                tf.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                                tf.scale = glm::vec3(1.0f);
                            }
                        } else {
                            spdlog::warn("[Outliner] Cannot reparent: would create a cycle.");
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }

            if (open) {
                if (hasChildren) {
                    for (auto child : children) {
                        DrawNode(child, depth + 1);
                    }
                }
                ImGui::TreePop(); // MUST be called if TreeNodeEx returned true
            }
            ImGui::PopID();
        };

        // Snapshot roots
        std::vector<entt::entity> roots;
        for (auto e : m_scene.AllEntities()) {
            if (!reg.all_of<Galeshi::ParentComponent>(e))
                roots.push_back(e);
        }
        for (auto e : roots) {
            DrawNode(e, 0);
        }

        // Background drop zone: unparent
        ImVec2 avail = ImGui::GetContentRegionAvail();
        if (avail.y < 10.0f) avail.y = 10.0f;
        ImGui::InvisibleButton("##outliner_bg", avail);
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY")) {
                entt::entity dragged = *(const entt::entity*)payload->Data;
                if (HasSkeletalParent(dragged)) {
                    spdlog::warn("[Outliner] Cannot detach a child of a "
                                 "skeletal mesh");
                } else if (reg.all_of<Galeshi::ParentComponent>(dragged)) {
                    PushHistory();
                    // 1) Remember world matrix BEFORE unparent
                    glm::mat4 worldA_old = GetWorldMatrix(dragged);
                    // 2) Detach
                    reg.remove<Galeshi::ParentComponent>(dragged);
                    // 3) World matrix becomes local matrix
                    SetLocalFromMatrix(dragged, worldA_old);
                }
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::End();
    }

