#include "GaleshiEditorApp.hpp"
#include "GaleshiEngine/Assets/LandscapeMesh.hpp"




namespace {

float WaterHash21(glm::vec2 p) {
    p = glm::fract(p * glm::vec2(123.34f, 456.21f));
    p += glm::vec2(glm::dot(p, p + glm::vec2(45.32f)));
    return glm::fract(p.x * p.y);
}

float WaterValueNoise(glm::vec2 p) {
    const glm::vec2 i = glm::floor(p);
    glm::vec2 f = glm::fract(p);
    f = f * f * (3.0f - 2.0f * f);
    const float a = WaterHash21(i);
    const float b = WaterHash21(i + glm::vec2(1.0f, 0.0f));
    const float c = WaterHash21(i + glm::vec2(0.0f, 1.0f));
    const float d = WaterHash21(i + glm::vec2(1.0f, 1.0f));
    return glm::mix(glm::mix(a, b, f.x), glm::mix(c, d, f.x), f.y);
}

// Signed distance from the procedural water edge in water-plane UV space.
// Negative inside the water body, positive on the shore. Matches fs_water.sc.
float WaterEdgeDistance(const Galeshi::WaterComponent& water,
                        glm::vec2 uv, float seed) {
    const glm::vec2 q = uv * 2.0f - 1.0f;
    const float coast = WaterValueNoise(
        uv * 8.0f + glm::vec2(seed * 3.1f, seed * 7.7f));
    if (water.bodyType == Galeshi::WaterBodyType::River) {
        glm::vec2 flow = water.flowDirection;
        if (glm::dot(flow, flow) < 1e-8f) flow = glm::vec2(1.0f, 0.0f);
        flow = glm::normalize(flow);
        const bool alongU = std::abs(flow.x) >= std::abs(flow.y);
        const float along = alongU ? q.x : q.y;
        const float side = alongU ? q.y : q.x;
        const float center =
            std::sin(along * 3.2f + seed * 6.2831f) * 0.22f +
            (WaterValueNoise(glm::vec2(along * 4.0f, seed)) - 0.5f) * 0.10f;
        const float halfWidth = 0.12f * (0.85f + 0.30f * coast);
        return std::abs(side - center) - halfWidth;
    }
    const float radius =
        water.bodyType == Galeshi::WaterBodyType::Lake ? 0.68f :
        (water.bodyType == Galeshi::WaterBodyType::Ocean ? 0.98f : 0.88f);
    const float edge = radius + (coast - 0.5f) * 0.18f;
    return glm::length(q) - edge;
}

} // namespace

    // -------------------------------------------------------------------
    // Scene Helpers
    // -------------------------------------------------------------------
    Galeshi::Entity GaleshiEditorApp::MakeCube(const std::string& name,  glm::vec3 pos) {
        auto e = m_scene.CreateEntity(name);
        auto mesh = m_assets->CreateCubeMesh();
        e.AddComponent<Galeshi::MeshComponent>(Galeshi::MeshComponent{ mesh, "", "cube" });
        auto mat = std::make_shared<Galeshi::Material>();
        e.AddComponent<Galeshi::MaterialComponent>(Galeshi::MaterialComponent{ mat });
        e.GetComponent<Galeshi::TransformComponent>().position = pos;
        return e;
    }



    Galeshi::Entity GaleshiEditorApp::MakeSphere(const std::string& name,  glm::vec3 pos) {
        auto e = m_scene.CreateEntity(name);
        auto mesh = m_assets->CreateSphereMesh();
        e.AddComponent<Galeshi::MeshComponent>(Galeshi::MeshComponent{ mesh, "", "sphere" });
        auto mat = std::make_shared<Galeshi::Material>();
        e.AddComponent<Galeshi::MaterialComponent>(Galeshi::MaterialComponent{ mat });
        e.GetComponent<Galeshi::TransformComponent>().position = pos;
        return e;
    }



    // -------------------------------------------------------------------
    // Collision shapes
    // -------------------------------------------------------------------
    glm::mat4 GaleshiEditorApp::EntityWorldMatrix(entt::entity handle) {
        glm::mat4 world(1.0f);
        std::vector<entt::entity> chain;
        entt::entity cur = handle;
        int depth = 0;
        while (cur != entt::null && depth++ < 128) {
            chain.push_back(cur);
            if (m_scene.Registry().all_of<Galeshi::ParentComponent>(cur))
                cur = m_scene.Registry().get<Galeshi::ParentComponent>(cur).parent;
            else
                break;
        }
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            if (m_scene.Registry().all_of<Galeshi::TransformComponent>(*it))
                world = world * m_scene.Registry()
                    .get<Galeshi::TransformComponent>(*it).LocalMatrix();
        }
        return world;
    }



    Galeshi::Entity GaleshiEditorApp::SpawnCollisionShape(const std::string& name,
                                        Galeshi::ColliderShapeType type) {
        auto e = m_scene.CreateEntity(name);
        auto& collider = e.AddComponent<Galeshi::ColliderComponent>();
        collider.shapeType = type;

        auto& rb = e.AddComponent<Galeshi::RigidBodyComponent>();
        rb.motionType = Galeshi::RigidBodyComponent::MotionType::Static;
        rb.bodyId = 0xFFFFFFFFu;

        auto& tf = e.GetComponent<Galeshi::TransformComponent>();
        tf.position = m_editor.camTarget;
        m_editor.Select(e);
        spdlog::info("[Physics] Created collision shape '{}'", name);
        return e;
    }



    // Recreate the Jolt body after shape or rigid-body properties changed.
    // Bodies are only created when the scene is playing, so entities without
    // a runtime body are left alone.
    void GaleshiEditorApp::RebuildPhysicsBody(Galeshi::Entity entity) {
        if (!m_physics || !entity) return;
        auto& reg = m_scene.Registry();
        const entt::entity handle = entity.Handle();
        if (!reg.all_of<Galeshi::ColliderComponent, Galeshi::RigidBodyComponent,
                        Galeshi::TransformComponent>(handle)) {
            return;
        }
        auto& rb = reg.get<Galeshi::RigidBodyComponent>(handle);
        if (rb.bodyId == 0xFFFFFFFFu) return;

        m_physics->DestroyBody(rb.bodyId);
        rb.bodyId = 0xFFFFFFFFu;

        const auto& collider = reg.get<Galeshi::ColliderComponent>(handle);
        const glm::mat4 world = EntityWorldMatrix(handle);
        glm::vec3 scale, translation, skew;
        glm::vec4 perspective;
        glm::quat rotation;
        glm::decompose(world, scale, rotation, translation, skew, perspective);
        rb.bodyId = m_physics->CreateCollider(collider, rb, translation,
                                              glm::normalize(rotation), handle);
    }



    Galeshi::Entity GaleshiEditorApp::FindClothMeshTarget(
        Galeshi::Entity selected) {
        if (!selected) return {};
        auto& reg = m_scene.Registry();
        if (reg.all_of<Galeshi::MeshComponent>(selected.Handle())) {
            return selected;
        }
        if (!reg.all_of<Galeshi::ModelComponent>(selected.Handle()) &&
            !reg.all_of<Galeshi::SkeletalComponent>(selected.Handle())) {
            return {};
        }

        auto children = reg.view<Galeshi::ParentComponent>();
        std::vector<entt::entity> pending{selected.Handle()};
        std::vector<entt::entity> next;
        for (int depth = 0; depth < 128 && !pending.empty(); ++depth) {
            next.clear();
            for (entt::entity parent : pending) {
                for (entt::entity child : children) {
                    if (children.get<Galeshi::ParentComponent>(child).parent !=
                        parent) {
                        continue;
                    }
                    if (reg.all_of<Galeshi::MeshComponent>(child)) {
                        return Galeshi::Entity{child, &m_scene};
                    }
                    next.push_back(child);
                }
            }
            pending.swap(next);
        }
        return {};
    }



    void GaleshiEditorApp::RebuildCloth(Galeshi::Entity entity) {
        if (!m_physics || !entity || !m_assets) return;
        auto& registry = m_scene.Registry();
        const entt::entity handle = entity.Handle();
        auto* cloth = registry.try_get<Galeshi::SoftBodyComponent>(handle);
        if (!cloth) return;
        if (cloth->softBodyId != 0xFFFFFFFFu) {
            m_physics->DestroyBody(cloth->softBodyId);
            cloth->softBodyId = 0xFFFFFFFFu;
        }
        if (!cloth->meshCloth) {
            cloth->softBodyId = m_physics->CreateCloth(*cloth, handle);
            spdlog::info("[Cloth] Rebuilt grid body for entity {} (0x{:08x})",
                         static_cast<uint32_t>(handle), cloth->softBodyId);
            return;
        }
        auto* meshComponent = registry.try_get<Galeshi::MeshComponent>(handle);
        if (!meshComponent || !meshComponent->mesh) {
            return;
        }
        const auto& meshVertices = meshComponent->mesh->Vertices();
        if (cloth->paintWeights.size() != meshVertices.size()) {
            cloth->paintWeights.resize(meshVertices.size(), 0.0f);
        }

        // Imported meshes split vertices at UV/normal seams. Weld identical
        // positions for simulation so the painted surface stays connected.
        std::vector<glm::vec3> simPositions;
        std::vector<float> simWeights;
        std::vector<uint32_t> simIndices;
        std::map<std::array<int, 3>, uint32_t> weldMap;
        cloth->simVertexMap.assign(meshVertices.size(), 0);
        for (size_t i = 0; i < meshVertices.size(); ++i) {
            const glm::vec3& position = meshVertices[i].position;
            const std::array<int, 3> key = {
                static_cast<int>(std::lround(position.x * 100000.0f)),
                static_cast<int>(std::lround(position.y * 100000.0f)),
                static_cast<int>(std::lround(position.z * 100000.0f))};
            auto [it, inserted] = weldMap.emplace(key,
                                                   static_cast<uint32_t>(simPositions.size()));
            if (inserted) {
                simPositions.push_back(position);
                simWeights.push_back(cloth->paintWeights[i]);
            } else if (i < cloth->paintWeights.size()) {
                simWeights[it->second] = std::max(
                    simWeights[it->second], cloth->paintWeights[i]);
            }
            cloth->simVertexMap[i] = it->second;
        }
        const auto& meshIndices = meshComponent->mesh->Indices();
        simIndices.reserve(meshIndices.size());
        for (size_t i = 0; i + 2 < meshIndices.size(); i += 3) {
            const uint32_t i0 = meshIndices[i];
            const uint32_t i1 = meshIndices[i + 1];
            const uint32_t i2 = meshIndices[i + 2];
            if (i0 >= cloth->simVertexMap.size() ||
                i1 >= cloth->simVertexMap.size() ||
                i2 >= cloth->simVertexMap.size()) {
                continue;
            }
            const uint32_t s0 = cloth->simVertexMap[i0];
            const uint32_t s1 = cloth->simVertexMap[i1];
            const uint32_t s2 = cloth->simVertexMap[i2];
            if (s0 == s1 || s1 == s2 || s0 == s2) continue;
            simIndices.push_back(s0);
            simIndices.push_back(s1);
            simIndices.push_back(s2);
        }

        Galeshi::SoftBodyComponent simCloth = *cloth;
        simCloth.paintWeights = std::move(simWeights);
        const glm::mat4 world = EntityWorldMatrix(handle);
        glm::vec3 scale, translation, skew;
        glm::vec4 perspective;
        glm::quat rotation;
        glm::decompose(world, scale, rotation, translation, skew, perspective);
        cloth->softBodyId = m_physics->CreateClothFromMesh(
            simPositions, simIndices, simCloth, translation,
            glm::normalize(rotation), handle);
        spdlog::info("[Cloth] Rebuilt body for entity {} (0x{:08x})",
                     static_cast<uint32_t>(handle), cloth->softBodyId);
    }

    // Creates runtime bodies for every authored collider that does not have
    // one yet. Called when Play starts.
    void GaleshiEditorApp::EnsurePhysicsBodies() {
        if (!m_physics) return;
        auto& reg = m_scene.Registry();
        auto softView = reg.view<Galeshi::SoftBodyComponent,
                                 Galeshi::MeshComponent>();
        for (auto e : softView) {
            auto& cloth = softView.get<Galeshi::SoftBodyComponent>(e);
            if (cloth.softBodyId == 0xFFFFFFFFu) {
                RebuildCloth(Galeshi::Entity(e, &m_scene));
            }
        }
        auto view = reg.view<Galeshi::ColliderComponent, Galeshi::RigidBodyComponent,
                             Galeshi::TransformComponent>();
        int created = 0;
        for (auto e : view) {
            auto& rb = view.get<Galeshi::RigidBodyComponent>(e);
            if (rb.bodyId != 0xFFFFFFFFu) continue;
            const auto& collider = view.get<Galeshi::ColliderComponent>(e);
            const glm::mat4 world = EntityWorldMatrix(e);
            glm::vec3 scale, translation, skew;
            glm::vec4 perspective;
            glm::quat rotation;
            glm::decompose(world, scale, rotation, translation, skew, perspective);
            rb.bodyId = m_physics->CreateCollider(collider, rb, translation,
                                                  glm::normalize(rotation), e);
            if (rb.bodyId != 0xFFFFFFFFu) ++created;
        }
        if (created > 0) {
            spdlog::info("[Physics] Created {} runtime collider bodies", created);
        }
    }



    // -------------------------------------------------------------------
    // Spawn a cube with a Jolt rigid body attached.
    // Always use this for physics cubes — it wires RigidBodyComponent,
    // motionType, mass, AND the Jolt body (CreateBox), so SyncTransformsToECS
    // will pick it up.
    // -------------------------------------------------------------------
    Galeshi::Entity GaleshiEditorApp::SpawnPhysicsCube(const std::string& name,  glm::vec3 pos,
                                     glm::vec3 scale) {
        auto e = MakeCube(name, pos);
        auto& tf = e.GetComponent<Galeshi::TransformComponent>();
        tf.scale = scale;

        auto& collider = e.AddComponent<Galeshi::ColliderComponent>();
        collider.shapeType = Galeshi::ColliderShapeType::Box;
        collider.halfExtents = scale * 0.5f;

        auto& rb = e.AddComponent<Galeshi::RigidBodyComponent>();
        rb.motionType = Galeshi::RigidBodyComponent::MotionType::Dynamic;
        rb.mass = 1.0f;
        rb.bodyId = m_physics->CreateBox(scale * 0.5f, pos, static_cast<entt::entity>(e));

        spdlog::info("[Physics] Spawned {} bodyId=0x{:08x}", name, rb.bodyId);
        return e;
    }



    Galeshi::Entity GaleshiEditorApp::SpawnPhysicsSphere(const std::string& name,  glm::vec3 pos,  float radius) {
        auto e = m_scene.CreateEntity(name);
        auto mat = std::make_shared<Galeshi::Material>();
        mat->albedoFactor = {0.7f, 0.5f, 0.5f, 1.0f};
        e.AddComponent<Galeshi::MaterialComponent>(Galeshi::MaterialComponent{mat});
        auto mesh = m_assets->CreateSphereMesh();
        e.AddComponent<Galeshi::MeshComponent>(Galeshi::MeshComponent{ mesh, "", "sphere" });
        auto& tf = e.GetComponent<Galeshi::TransformComponent>();
        tf.position = pos;
        tf.scale = glm::vec3(radius * 2.0f);  // unit sphere (r=0.5) * scale = diameter
        auto& collider = e.AddComponent<Galeshi::ColliderComponent>();
        collider.shapeType = Galeshi::ColliderShapeType::Sphere;
        collider.radius = radius;
        auto& rb = e.AddComponent<Galeshi::RigidBodyComponent>();
        rb.motionType = Galeshi::RigidBodyComponent::MotionType::Dynamic;
        rb.mass = 1.0f;
        rb.bodyId = m_physics->CreateSphere(radius, pos, static_cast<entt::entity>(e), false);
        spdlog::info("[Physics] Spawned {} bodyId=0x{:08x}", name, rb.bodyId);
        return e;
    }



    Galeshi::Entity GaleshiEditorApp::SpawnPhysicsCapsule(const std::string& name,  glm::vec3 pos,  float halfHeight,  float radius) {
        auto e = m_scene.CreateEntity(name);
        auto mat = std::make_shared<Galeshi::Material>();
        mat->albedoFactor = {0.5f, 0.7f, 0.5f, 1.0f};
        e.AddComponent<Galeshi::MaterialComponent>(Galeshi::MaterialComponent{mat});
        auto mesh = m_assets->CreateCapsuleMesh(halfHeight, radius);
        e.AddComponent<Galeshi::MeshComponent>(Galeshi::MeshComponent{ mesh, "", "capsule" });
        auto& tf = e.GetComponent<Galeshi::TransformComponent>();
        tf.position = pos;
        tf.scale = glm::vec3(1.0f);  // mesh baked with correct size
        auto& collider = e.AddComponent<Galeshi::ColliderComponent>();
        collider.shapeType = Galeshi::ColliderShapeType::Capsule;
        collider.halfHeight = halfHeight;
        collider.radius = radius;
        auto& rb = e.AddComponent<Galeshi::RigidBodyComponent>();
        rb.motionType = Galeshi::RigidBodyComponent::MotionType::Dynamic;
        rb.mass = 1.0f;
        rb.lockRotation = true;
        rb.bodyId = m_physics->CreateCollider(collider, rb, pos,
                                              glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                                              static_cast<entt::entity>(e));
        spdlog::info("[Physics] Spawned {} bodyId=0x{:08x}", name, rb.bodyId);
        return e;
    }



    void GaleshiEditorApp::BuildTestScene() {
        if (!m_assets) return;
        StopPlayForSceneChange();
        UnloadSceneContent(false);
        ResetSceneStateForReplacement();

        auto sun = m_scene.CreateEntity("Sun");
        auto& dl = sun.HasComponent<Galeshi::DirectionalLightComponent>()
                   ? sun.GetComponent<Galeshi::DirectionalLightComponent>()
                   : sun.AddComponent<Galeshi::DirectionalLightComponent>();
        dl.direction = glm::normalize(glm::vec3(-0.4f, -0.8f, -0.5f));
        dl.color = {1.0f, 0.97f, 0.9f}; dl.intensity = 3.0f;
        // Large ground plane to receive shadows.
        auto ground = MakeCube("Ground", {0.0f, -5.0f, 0.0f});
        ground.GetComponent<Galeshi::TransformComponent>().scale = { 100.0f, 10.0f, 100.0f };
        ground.GetComponent<Galeshi::MaterialComponent>().material->albedoFactor = { 0.3f, 0.3f, 0.3f, 1.0f };
        ground.GetComponent<Galeshi::MaterialComponent>().material->roughnessFactor = 0.8f;
        {
            auto& rb = ground.AddComponent<Galeshi::RigidBodyComponent>();
            rb.motionType = Galeshi::RigidBodyComponent::MotionType::Static;
            rb.bodyId = m_physics->CreateBox(glm::vec3(100.0f, 10.0f, 100.0f) * 0.5f, ground.GetComponent<Galeshi::TransformComponent>().position, static_cast<entt::entity>(ground), true);
            auto& collider = ground.AddComponent<Galeshi::ColliderComponent>();
            collider.shapeType = Galeshi::ColliderShapeType::Box;
            collider.halfExtents = glm::vec3(100.0f, 10.0f, 100.0f) * 0.5f;
        }
        // --- PBR material test grid ---
        // Front row: albedo comparison. Next three rows: metallic sweep
        // (0.0 / 0.5 / 1.0) against roughness sweep (0.05 / 0.35 / 0.80).
        // Every sphere has its own base colour; the material values remain
        // visible in the entity name and the Details panel.
        m_editor.camTarget = {0.0f, 0.8f, 0.0f};
        m_editor.camYaw = 0.30f;
        m_editor.camPitch = 0.55f;
        m_editor.camRadius = 8.0f;

        // Full-effects post-process for the material/physics test scene.
        // The scene is cleared above, so this volume is the only one and all
        // standard effects stay active. Only the stylized palette and debug
        // invert pass are off; tonemap/exposure stay neutral.
        auto ppEntity = m_scene.CreateEntity("PostProcessVolume_Test");
        auto& ppVolume =
            ppEntity.AddComponent<Galeshi::PostProcessVolumeComponent>();
        ppVolume.priority = 100;
        ppVolume.size = {60.0f, 30.0f, 60.0f};
        ppVolume.visibleInEditor = false;
        ppEntity.GetComponent<Galeshi::TransformComponent>().position =
            m_editor.camTarget;
        auto& pp = ppVolume.settings;
        pp.exposure = 1.0f;
        pp.contrast = 1.0f;
        pp.saturation = 1.0f;
        pp.colorExposure = 1.0f;
        pp.colorContrast = 1.0f;
        pp.colorSaturation = 1.0f;
        pp.environmentPaletteEnabled = false;
        pp.environmentPaletteAmount = 0.0f;
        pp.fxaaEnabled = false;

        const float sphereScale = 1.2f;
        const float sphereY = sphereScale * 0.5f;
        const float xSpacing = 2.8f;
        const float zSpacing = 1.8f;
        const float albedoRowZ = 2.7f;
        const float metallicRowZ = 0.9f;

        const float metallicValues[3] = {0.0f, 0.5f, 1.0f};
        const float roughnessValues[3] = {0.05f, 0.35f, 0.8f};
        const char* metallicLabels[3] = {"M00", "M50", "M100"};
        const char* roughnessLabels[3] = {"R05", "R35", "R80"};
        const char* albedoLabels[3] = {"Red", "Green", "Blue"};
        const glm::vec3 albedoTestColors[3] = {
            {0.85f, 0.18f, 0.14f},
            {0.18f, 0.65f, 0.25f},
            {0.15f, 0.35f, 0.85f},
        };
        const glm::vec3 metallicColors[3][3] = {
            {{0.92f, 0.48f, 0.12f}, {0.88f, 0.78f, 0.18f}, {0.10f, 0.62f, 0.58f}},
            {{0.78f, 0.24f, 0.70f}, {0.16f, 0.68f, 0.86f}, {0.56f, 0.82f, 0.22f}},
            {{1.00f, 0.77f, 0.34f}, {0.95f, 0.64f, 0.54f}, {0.82f, 0.84f, 0.88f}},
        };

        auto makePbrSphere = [this, sphereScale](const std::string& name, glm::vec3 pos,
                                                 glm::vec3 albedo, float metallic, float roughness) {
            auto e = MakeSphere(name, pos);
            e.GetComponent<Galeshi::TransformComponent>().scale = glm::vec3(sphereScale);
            auto& mat = *e.GetComponent<Galeshi::MaterialComponent>().material;
            mat.name = name + "_Material";
            mat.albedoFactor = glm::vec4(albedo, 1.0f);
            mat.metallicFactor = metallic;
            mat.roughnessFactor = roughness;
            return e;
        };

        for (int col = 0; col < 3; ++col) {
            const float x = (static_cast<float>(col) - 1.0f) * xSpacing;
            const std::string name = std::string("PBR_Albedo_") + albedoLabels[col];
            makePbrSphere(name, {x, sphereY, albedoRowZ}, albedoTestColors[col], 0.0f, 0.35f);
        }

        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                const float x = (static_cast<float>(col) - 1.0f) * xSpacing;
                const float z = metallicRowZ - static_cast<float>(row) * zSpacing;
                const std::string name = std::string("PBR_") + metallicLabels[row] + "_" + roughnessLabels[col];
                makePbrSphere(name, {x, sphereY, z}, metallicColors[row][col],
                              metallicValues[row], roughnessValues[col]);
            }
        }

        // Keep a small dynamic physics demo off to the side of the PBR grid.
        auto spawnDynamicCube = [this](const std::string& name, glm::vec3 pos, glm::vec3 color,
                                       float metallic = 0.0f, float roughness = 0.5f) {
            auto e = MakeCube(name, pos);
            auto& mat = *e.GetComponent<Galeshi::MaterialComponent>().material;
            mat.name = name + "_Material";
            mat.albedoFactor = glm::vec4(color, 1.0f);
            mat.metallicFactor = metallic;
            mat.roughnessFactor = roughness;
            auto& tf = e.GetComponent<Galeshi::TransformComponent>();
            auto& rb = e.AddComponent<Galeshi::RigidBodyComponent>();
            rb.motionType = Galeshi::RigidBodyComponent::MotionType::Dynamic;
            rb.mass = 1.0f;
            rb.bodyId = m_physics->CreateBox(tf.scale * 0.5f, pos, static_cast<entt::entity>(e), false);
            auto& collider = e.AddComponent<Galeshi::ColliderComponent>();
            collider.shapeType = Galeshi::ColliderShapeType::Box;
            collider.halfExtents = tf.scale * 0.5f;
            return e;
        };
        spawnDynamicCube("Physics_Orange", { 6.0f, 3.0f, -1.0f}, {0.85f, 0.55f, 0.25f}, 0.0f, 0.6f);
        spawnDynamicCube("Physics_White",  { 6.0f, 4.2f, -1.0f}, {0.95f, 0.95f, 0.95f}, 1.0f, 0.15f);
        spawnDynamicCube("Physics_Blue",   { 6.0f, 5.4f, -1.0f}, {0.30f, 0.55f, 0.85f}, 0.0f, 0.8f);


        auto pl = m_scene.CreateEntity("PointLight");
        if (!pl.HasComponent<Galeshi::PointLightComponent>())
            pl.AddComponent<Galeshi::PointLightComponent>();
        pl.GetComponent<Galeshi::TransformComponent>().position = {0.0f, 5.0f, 0.0f};

        // Demo fire emitter next to the material grid.
        auto emitter = m_scene.CreateEntity("FireParticles");
        emitter.AddComponent<Galeshi::ParticleEmitterComponent>();
        emitter.GetComponent<Galeshi::TransformComponent>().position = {4.5f, 0.2f, -1.5f};

        if (m_serializer) PushHistory();
    }

    void GaleshiEditorApp::ApplyWaterErosion(entt::entity waterEntity) {
        if (!m_scene.Registry().valid(waterEntity)) return;
        auto& registry = m_scene.Registry();
        auto* water = registry.try_get<Galeshi::WaterComponent>(waterEntity);
        if (!water) return;

        const glm::mat4 waterWorld = EntityWorldMatrix(waterEntity);
        const glm::mat4 waterModel =
            waterWorld * glm::scale(glm::mat4(1.0f),
                                    glm::vec3(water->width, 1.0f, water->depth));
        const glm::mat4 waterInv = glm::inverse(waterModel);
        const float waterY = waterWorld[3].y;
        const float minDim = std::max(
            std::min(water->width, water->depth), 0.001f);
        const float bankWidthUV = std::clamp(
            water->bankWidth / minDim, 0.02f, 0.30f);
        const float seed = static_cast<float>(
            entt::to_integral(waterEntity) % 1024u) * 0.6180339f;
        const float basinDepth = std::max(water->basinDepth, 0.0f);
        const float bankErosion = std::max(water->bankErosion, 0.0f);

        for (auto lh : registry.view<Galeshi::LandscapeComponent>()) {
            auto& lc = registry.get<Galeshi::LandscapeComponent>(lh);
            auto* meshComponent = registry.try_get<Galeshi::MeshComponent>(lh);
            if (!meshComponent || !meshComponent->mesh) continue;
            if (lc.sizeX < 2 || lc.sizeZ < 2 ||
                lc.heights.size() != static_cast<size_t>(lc.sizeX) * lc.sizeZ) {
                continue;
            }

            if (!lc.hasErosionBase) {
                lc.erosionBaseHeights = lc.heights;
                lc.hasErosionBase = true;
            } else if (lc.erosionBaseHeights.size() == lc.heights.size()) {
                lc.heights = lc.erosionBaseHeights;
            }
            if (!lc.hasErosionPaintBase) {
                lc.erosionBasePaintWeights = lc.paintWeights;
                lc.hasErosionPaintBase = true;
            } else if (lc.erosionBasePaintWeights.size() ==
                       lc.paintWeights.size()) {
                lc.paintWeights = lc.erosionBasePaintWeights;
            }

            // The shore is drawn by the landscape itself. Bind the
            // procedural sand/gravel layer; the paint weights below decide
            // where it replaces the base grass.
            if (m_assets) {
                if (auto* materialComponent =
                        registry.try_get<Galeshi::MaterialComponent>(lh);
                    materialComponent && materialComponent->material) {
                    auto& material = *materialComponent->material;
                    material.landscapeBlend = true;
                    material.landscapeAlbedoSrgb = true;
                    material.landscapeOrmSrgb = false;
                    material.landscapeNormalSrgb = false;
                    auto shoreAlbedo = m_assets->LoadTexture(
                        "assets/textures/T_Water_Shore_Albedo.png", true);
                    auto shoreOrm = m_assets->LoadTexture(
                        "assets/textures/T_Water_Shore_ORM.png", false);
                    auto shoreNormal = m_assets->LoadTexture(
                        "assets/textures/T_Water_Shore_Normal.png", false);
                    if (shoreAlbedo &&
                        shoreAlbedo != m_assets->WhiteTexture()) {
                        material.landscapeAlbedoMap = shoreAlbedo;
                    }
                    if (shoreOrm && shoreOrm != m_assets->WhiteTexture()) {
                        material.landscapeOrmMap = shoreOrm;
                    }
                    if (shoreNormal &&
                        shoreNormal != m_assets->WhiteTexture()) {
                        material.landscapeNormalMap = shoreNormal;
                    }
                }
            }

            const glm::mat4 landscapeWorld = EntityWorldMatrix(lh);
            const glm::mat4 landscapeInv = glm::inverse(landscapeWorld);

            for (int z = 0; z < lc.sizeZ; ++z) {
                for (int x = 0; x < lc.sizeX; ++x) {
                    const size_t index = static_cast<size_t>(z) * lc.sizeX + x;
                    const glm::vec3 local(
                        (x - lc.sizeX / 2.0f) * lc.scale,
                        lc.heights[index],
                        (z - lc.sizeZ / 2.0f) * lc.scale);
                    const glm::vec3 world =
                        glm::vec3(landscapeWorld * glm::vec4(local, 1.0f));
                    const glm::vec3 waterLocal =
                        glm::vec3(waterInv * glm::vec4(world, 1.0f));
                    const glm::vec2 uv(waterLocal.x + 0.5f,
                                       waterLocal.z + 0.5f);
                    if (uv.x < -0.05f || uv.x > 1.05f ||
                        uv.y < -0.05f || uv.y > 1.05f) {
                        continue;
                    }

                    const float edgeDistance =
                        WaterEdgeDistance(*water, uv, seed);
                    if (edgeDistance > bankWidthUV) continue;

                    const float erosionNoise = WaterValueNoise(
                        uv * 17.0f + glm::vec2(seed * 1.7f, seed * 3.1f));
                    const float bedY =
                        waterY - basinDepth * (0.72f + 0.28f * erosionNoise);
                    float targetWorldY = bedY;
                    if (edgeDistance > 0.0f) {
                        float t = std::clamp(
                            edgeDistance / std::max(bankWidthUV, 1e-4f),
                            0.0f, 1.0f);
                        t = std::clamp(
                            t + (erosionNoise - 0.5f) * bankErosion,
                            0.0f, 1.0f);
                        t = t * t * (3.0f - 2.0f * t);
                        targetWorldY = glm::mix(bedY, world.y, t);
                    }

                    const glm::vec3 targetLocal = glm::vec3(
                        landscapeInv * glm::vec4(
                            world.x, targetWorldY, world.z, 1.0f));
                    lc.heights[index] = targetLocal.y;

                    // Paint the eroded shore/bed with the landscape's shore
                    // layer. The water bed is fully sand/gravel; the bank
                    // fades back into grass over its width.
                    float shorePaint = 0.0f;
                    if (edgeDistance <= 0.0f) {
                        shorePaint = 1.0f;
                    } else {
                        float t = std::clamp(
                            edgeDistance / std::max(bankWidthUV, 1e-4f),
                            0.0f, 1.0f);
                        t = std::clamp(
                            t + (erosionNoise - 0.5f) * bankErosion,
                            0.0f, 1.0f);
                        shorePaint = 1.0f - t * t * (3.0f - 2.0f * t);
                    }
                    if (index < lc.paintWeights.size()) {
                        lc.paintWeights[index] = std::max(
                            lc.paintWeights[index], shorePaint);
                    }
                }
            }

            Galeshi::LandscapeMesh::Rebuild(lc, *meshComponent->mesh);
            if (auto* vegetation =
                    registry.try_get<Galeshi::VegetationComponent>(lh)) {
                vegetation->dirty = true;
            }
        }

        water->terrainCarved = true;
    }

