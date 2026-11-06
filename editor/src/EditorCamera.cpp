#include "GaleshiEditorApp.hpp"



    bool GaleshiEditorApp::IsEntityInHierarchy(entt::entity entity,  entt::entity root) {
        if (root == entt::null) return false;
        auto& reg = m_scene.Registry();
        for (int depth = 0; depth < 128 && entity != entt::null; ++depth) {
            if (entity == root) return true;
            if (!reg.all_of<Galeshi::ParentComponent>(entity)) break;
            entity = reg.get<Galeshi::ParentComponent>(entity).parent;
        }
        return false;
    }



    // Camera blocking is limited to authored colliders. Meshes without an
    // active ColliderComponent and trigger volumes never block the view.
    bool GaleshiEditorApp::IsCameraCollisionBlocker(entt::entity entity,
                                  entt::entity ignoreRoot) {
        auto& reg = m_scene.Registry();
        if (!reg.valid(entity)) return false;
        const auto* collider =
            reg.try_get<Galeshi::ColliderComponent>(entity);
        if (!collider || collider->isTrigger) return false;
        if (ignoreRoot != entt::null &&
            (IsEntityInHierarchy(entity, ignoreRoot) ||
             IsEntityInHierarchy(ignoreRoot, entity))) {
            return false;
        }
        return true;
    }



    bool GaleshiEditorApp::IsGameplayCameraAttachedToPlayer(entt::entity cameraEntity) {
        auto& reg = m_scene.Registry();
        entt::entity current = cameraEntity;
        for (int depth = 0; depth < 128; ++depth) {
            if (!reg.all_of<Galeshi::ParentComponent>(current)) break;
            current = reg.get<Galeshi::ParentComponent>(current).parent;
            if (current == entt::null) break;
            if (reg.all_of<Galeshi::PlayerTagComponent>(current)) {
                return true;
            }
            if (reg.all_of<Galeshi::NameComponent>(current) &&
                reg.get<Galeshi::NameComponent>(current).name == "Player") {
                return true;
            }
        }
        return false;
    }



    // Player-tagged entities are the orbit centre for the gameplay camera.
    // Prefer a Player in the camera's own hierarchy so a stray Player elsewhere
    // in the scene cannot make an attached camera jump away from its character.
    entt::entity GaleshiEditorApp::FindGameplayCameraTarget(entt::entity cameraEntity) {
        auto& reg = m_scene.Registry();
        entt::entity characterAncestor = entt::null;
        entt::entity current = cameraEntity;
        for (int depth = 0; depth < 128; ++depth) {
            if (!reg.all_of<Galeshi::ParentComponent>(current)) break;
            current = reg.get<Galeshi::ParentComponent>(current).parent;
            if (current == entt::null) break;
            if (reg.all_of<Galeshi::TransformComponent>(current) &&
                reg.all_of<Galeshi::PlayerTagComponent>(current)) {
                return current;
            }
            if (reg.all_of<Galeshi::TransformComponent>(current) &&
                reg.all_of<Galeshi::NameComponent>(current) &&
                reg.get<Galeshi::NameComponent>(current).name == "Player") {
                return current;
            }
            if (characterAncestor == entt::null &&
                (reg.all_of<Galeshi::SkeletalComponent>(current) ||
                 reg.all_of<Galeshi::ModelComponent>(current) ||
                 reg.all_of<Galeshi::MeshComponent>(current))) {
                characterAncestor = current;
            }
        }

        auto players = reg.view<Galeshi::PlayerTagComponent,
                                Galeshi::TransformComponent>();
        for (auto entity : players) return entity;

        return characterAncestor;
    }



    bool GaleshiEditorApp::SetGameplayCameraMode(int mode) {
        if (mode != GALESHI_CAMERA_THIRD_PERSON &&
            mode != GALESHI_CAMERA_FIRST_PERSON) {
            return false;
        }
        m_gameCameraMode = static_cast<GaleshiScriptCameraMode>(mode);
        return true;
    }



    int GaleshiEditorApp::GetGameplayCameraMode() const {
        return static_cast<int>(m_gameCameraMode);
    }



    glm::vec3 GaleshiEditorApp::GetGameplayCameraMoveForward() const {
        glm::vec3 forward = CameraForward();
        forward.y = 0.0f;
        const float length = glm::length(forward);
        if (length < 1e-5f) return glm::vec3(0.0f, 0.0f, -1.0f);
        return forward / length;
    }



    glm::vec3 GaleshiEditorApp::GetGameplayCameraMoveRight() const {
        return glm::normalize(glm::cross(
            GetGameplayCameraMoveForward(), glm::vec3(0.0f, 1.0f, 0.0f)));
    }



    glm::mat4 GaleshiEditorApp::GameplayCameraTargetLocal(
        entt::entity target) const {
        auto& reg = m_scene.Registry();
        if (!reg.valid(target) ||
            !reg.all_of<Galeshi::TransformComponent>(target)) {
            return glm::mat4(1.0f);
        }
        const auto& tf = reg.get<Galeshi::TransformComponent>(target);
        return glm::translate(glm::mat4(1.0f), tf.position) *
               glm::mat4_cast(glm::normalize(tf.rotation));
    }



    // Cache the bounds centre, eye position and initial camera basis. The
    // camera offset stays target-local so position follows the Player's local
    // position and rotation, not its scale. The basis stays world-space so
    // character turning does not rotate the view.
    void GaleshiEditorApp::RefreshGameplayCameraTarget(entt::entity target,
                                     entt::entity cameraEntity) {
        m_gameCameraTarget = target;
        m_gameCameraPivotLocal = glm::vec3(0.0f);
        m_gameCameraEyeLocal = glm::vec3(0.0f, 1.6f, 0.0f);
        m_gameCameraLocalOffset = glm::vec3(0.0f);
        m_gameCameraBaseForward = glm::vec3(0.0f, 0.0f, -1.0f);
        m_gameCameraBaseUp = glm::vec3(0.0f, 1.0f, 0.0f);
        m_gameCameraBaseValid = false;
        if (target == entt::null) return;

        const glm::mat4 targetLocal = GameplayCameraTargetLocal(target);
        const glm::mat4 inverseTarget = glm::inverse(targetLocal);
        auto& reg = m_scene.Registry();

        glm::vec3 center;
        glm::vec3 boundsMin;
        glm::vec3 boundsMax;
        const bool haveBounds = EntityWorldBoundsCenter(
            target, center, &boundsMin, &boundsMax);
        m_gameCameraPivotLocal = glm::vec3(
            inverseTarget * glm::vec4(center, 1.0f));

        if (haveBounds) {
            // Eye height comes from the character itself, not from the
            // third-person camera offset.
            const glm::vec3 eyeWorld{
                center.x,
                boundsMin.y + (boundsMax.y - boundsMin.y) * 0.9f,
                center.z,
            };
            m_gameCameraEyeLocal = glm::vec3(
                inverseTarget * glm::vec4(eyeWorld, 1.0f));
        }

        const glm::mat4 cameraWorld = EntityWorldMatrix(cameraEntity);
        const bool attached = IsEntityInHierarchy(cameraEntity, target);
        glm::mat4 cameraOrientationWorld = cameraWorld;
        if (attached) {
            // Compose only the camera's local chain below the target. This
            // keeps the authored camera offset and orientation while ignoring
            // the target's scale.
            glm::mat4 relative(1.0f);
            entt::entity current = cameraEntity;
            int depth = 0;
            while (current != entt::null && current != target &&
                   depth++ < 128) {
                if (!reg.all_of<Galeshi::TransformComponent>(current)) break;
                relative =
                    reg.get<Galeshi::TransformComponent>(current).LocalMatrix() *
                    relative;
                if (!reg.all_of<Galeshi::ParentComponent>(current)) break;
                current = reg.get<Galeshi::ParentComponent>(current).parent;
            }
            if (current == target) {
                m_gameCameraLocalOffset = glm::vec3(relative[3]);
                const glm::mat4 targetFrame = glm::translate(
                    glm::mat4(1.0f), glm::vec3(targetLocal[3]));
                cameraOrientationWorld = targetFrame * relative;
            } else {
                m_gameCameraLocalOffset = glm::vec3(
                    inverseTarget * glm::vec4(glm::vec3(cameraWorld[3]), 1.0f));
            }
        } else {
            m_gameCameraLocalOffset = glm::vec3(
                inverseTarget * glm::vec4(glm::vec3(cameraWorld[3]), 1.0f));
        }

        glm::vec3 forward = glm::vec3(
            cameraOrientationWorld * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f));
        glm::vec3 cameraUp = glm::vec3(
            cameraOrientationWorld * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
        if (glm::dot(forward, forward) < 1e-6f ||
            glm::dot(cameraUp, cameraUp) < 1e-6f) {
            forward = {0.0f, 0.0f, -1.0f};
            cameraUp = {0.0f, 1.0f, 0.0f};
        } else {
            forward = glm::normalize(forward);
            cameraUp = glm::normalize(cameraUp);
        }
        if (std::abs(glm::dot(forward, cameraUp)) > 0.999f) {
            cameraUp = std::abs(forward.y) > 0.9f
                ? glm::vec3(0.0f, 0.0f, 1.0f)
                : glm::vec3(0.0f, 1.0f, 0.0f);
        }
        m_gameCameraBaseForward = forward;
        m_gameCameraBaseUp = cameraUp;
        m_gameCameraBaseValid = true;
    }



    void GaleshiEditorApp::UpdateGameplayCamera() {
        Galeshi::Entity cameraEntity = FindGameplayCamera();
        if (!cameraEntity) {
            if (m_usingGameCamera) {
                m_usingGameCamera = false;
                RestoreEditorCamera();
            }
            return;
        }

        const auto& camera =
            cameraEntity.GetComponent<Galeshi::CameraComponent>();
        const glm::mat4 world = EntityWorldMatrix(cameraEntity.Handle());
        glm::vec3 eye = glm::vec3(world[3]);
        glm::vec3 forward = glm::vec3(world * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f));
        glm::vec3 up = glm::vec3(world * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
        if (glm::dot(forward, forward) < 1e-6f ||
            glm::dot(up, up) < 1e-6f) {
            forward = {0.0f, 0.0f, -1.0f};
            up = {0.0f, 1.0f, 0.0f};
        } else {
            forward = glm::normalize(forward);
            up = glm::normalize(up);
        }
        if (std::abs(glm::dot(forward, up)) > 0.999f) {
            up = std::abs(forward.y) > 0.9f
                ? glm::vec3(0.0f, 0.0f, 1.0f)
                : glm::vec3(0.0f, 1.0f, 0.0f);
        }

        // Mouse-look orbits the gameplay camera around the character without
        // changing the scene entity. This keeps gameplay scripts free to
        // move/rotate the character while the player looks around with RMB.
        const bool gameLookInput =
            (m_editor.isPlaying || m_isGameMode) &&
            (m_gameLooking ||
             ((m_viewportHovered || m_isGameMode) &&
              !m_uiRuntime.IsPointerOverUI())) &&
            !ImGui::GetIO().WantTextInput &&
            !ImGui::GetIO().AppFocusLost &&
            ImGui::IsMouseDown(ImGuiMouseButton_Right);
        if (gameLookInput) {
            if (!m_gameLooking) {
                // Relative mode keeps the hidden cursor centered and prevents
                // it from leaving the window while the player looks around.
                m_gameLooking = true;
                SetMouseCapture(true);
                m_gameLookSkipFrames = 2;
            }
            if (m_gameLookSkipFrames > 0) {
                --m_gameLookSkipFrames;
            } else {
                const glm::vec2 delta = GetInput().MouseDelta();
                const float sensitivity =
                    m_preferences.cameraOrbitSensitivity;
                m_gameCameraYawOffset -= delta.x * sensitivity;
                m_gameCameraPitchOffset -= delta.y * sensitivity;
                m_gameCameraPitchOffset = glm::clamp(
                    m_gameCameraPitchOffset, -1.50f, 1.50f);
            }
        } else {
            EndGameMouseLook();
        }

        const bool hasLookOffset =
            std::abs(m_gameCameraYawOffset) > 1e-6f ||
            std::abs(m_gameCameraPitchOffset) > 1e-6f;
        const bool firstPerson =
            m_gameCameraMode == GALESHI_CAMERA_FIRST_PERSON;
        const float aspect = m_viewportH
            ? static_cast<float>(m_viewportW) / static_cast<float>(m_viewportH)
            : 16.0f / 9.0f;
        m_camera.SetPerspective(
            glm::radians(glm::clamp(camera.fovY, 1.0f, 179.0f)), aspect,
            std::max(camera.nearZ, 1e-4f), std::max(camera.farZ, camera.nearZ + 0.001f));

        entt::entity target = FindGameplayCameraTarget(cameraEntity.Handle());
        if (target != m_gameCameraTarget ||
            cameraEntity.Handle() != m_activeGameCamera) {
            RefreshGameplayCameraTarget(target, cameraEntity.Handle());
        }
        const bool validTarget =
            target != entt::null && m_scene.Registry().valid(target);

        const auto applyInPlaceLook = [&]() {
            if (!hasLookOffset) return;
            const float baseYaw = std::atan2(forward.x, forward.z);
            const float basePitch =
                std::asin(glm::clamp(forward.y, -1.0f, 1.0f));
            const float yaw = baseYaw + m_gameCameraYawOffset;
            const float pitch = glm::clamp(
                basePitch + m_gameCameraPitchOffset, -1.52f, 1.52f);
            const float horizontal = std::cos(pitch);
            forward = {std::sin(yaw) * horizontal, std::sin(pitch),
                       std::cos(yaw) * horizontal};
            up = glm::vec3(0.0f, 1.0f, 0.0f);
        };

        if (firstPerson && validTarget) {
            // Stand at the character's authored eye height and rotate the view
            // in place instead of orbiting around the body.
            if (m_gameCameraBaseValid) {
                forward = m_gameCameraBaseForward;
                up = m_gameCameraBaseUp;
            }
            const glm::mat4 targetLocal = GameplayCameraTargetLocal(target);
            eye = glm::vec3(
                targetLocal * glm::vec4(m_gameCameraEyeLocal, 1.0f));
            applyInPlaceLook();
            m_camera.SetView(eye, eye + forward, up);
        } else {
            bool orbitActive = false;
            glm::vec3 pivot{0.0f};
            if (validTarget) {
                const glm::mat4 targetLocal = GameplayCameraTargetLocal(target);
                pivot = glm::vec3(
                    targetLocal * glm::vec4(m_gameCameraPivotLocal, 1.0f));
                if (m_gameCameraBaseValid) {
                    eye = glm::vec3(
                        targetLocal *
                        glm::vec4(m_gameCameraLocalOffset, 1.0f));
                    forward = m_gameCameraBaseForward;
                    up = m_gameCameraBaseUp;
                }
                if (hasLookOffset) {
                    const glm::vec3 orbitOffset = eye - pivot;
                    const float radius = glm::length(orbitOffset);
                    if (radius > 1e-3f) {
                        // Orbit the authored offset around the character
                        // centre, keeping the current third-person distance.
                        const float baseYaw =
                            std::atan2(orbitOffset.x, orbitOffset.z);
                        const float basePitch = std::asin(glm::clamp(
                            orbitOffset.y / radius, -1.0f, 1.0f));
                        const float yaw =
                            baseYaw + m_gameCameraYawOffset;
                        const float pitch = glm::clamp(
                            basePitch - m_gameCameraPitchOffset,
                            -1.50f, 1.50f);
                        const float horizontal = std::cos(pitch);
                        eye = pivot + glm::vec3(
                            radius * horizontal * std::sin(yaw),
                            radius * std::sin(pitch),
                            radius * horizontal * std::cos(yaw));
                        orbitActive = true;
                    }
                }

                if (m_physics) {
                    const float collisionRadius =
                        std::max(0.2f, camera.nearZ);
                    const auto collisionFilter =
                        [this, target](entt::entity entity) {
                            return IsCameraCollisionBlocker(entity, target);
                        };
                    glm::vec3 collisionPoint;
                    if (m_physics->SphereCast(
                            pivot, eye, collisionRadius,
                            collisionFilter, collisionPoint)) {
                        eye = collisionPoint;
                    }
                }
            }

            if (orbitActive &&
                glm::length(eye - pivot) > 1e-3f) {
                m_camera.SetView(eye, pivot, glm::vec3(0.0f, 1.0f, 0.0f));
            } else {
                applyInPlaceLook();
                m_camera.SetView(eye, eye + forward, up);
            }
        }
        m_usingGameCamera = true;
        m_activeGameCamera = cameraEntity.Handle();
    }



    void GaleshiEditorApp::EndEditorMouseLook() {
        if (m_editorLookDragging) SetMouseCapture(false);
        m_editorLooking = false;
        m_editorLookDragging = false;
        m_editorLookSkipFrames = 0;
    }



    void GaleshiEditorApp::EndGameMouseLook() {
        if (m_gameLooking) SetMouseCapture(false);
        m_gameLooking = false;
        m_gameLookSkipFrames = 0;
    }



    void GaleshiEditorApp::RestoreEditorCamera() {
        EndGameMouseLook();
        const float aspect = m_viewportH
            ? static_cast<float>(m_viewportW) / static_cast<float>(m_viewportH)
            : 16.0f / 9.0f;
        const float orbitDistance = std::max(m_editor.camRadius, 0.5f);
        const float nearZ = glm::clamp(orbitDistance * 0.0025f, 0.1f, 0.75f);
        const float farZ = std::max(1000.0f, orbitDistance * 4.0f + 500.0f);
        m_camera.SetPerspective(glm::radians(m_preferences.cameraFovDegrees),
                                aspect, nearZ, farZ);
        m_camera.Orbit(m_editor.camTarget, m_editor.camYaw,
                       m_editor.camPitch, m_editor.camRadius);
        m_activeGameCamera = entt::null;
    }



    bool GaleshiEditorApp::ActivateGameplayCamera() {
        m_usingGameCamera = true;
        m_gameCameraTarget = entt::null;
        m_gameCameraPivotLocal = glm::vec3(0.0f);
        m_gameCameraEyeLocal = glm::vec3(0.0f, 1.6f, 0.0f);
        m_gameCameraBaseValid = false;
        m_gameCameraYawOffset = 0.0f;
        m_gameCameraPitchOffset = 0.0f;
        UpdateGameplayCamera();
        if (!m_usingGameCamera) {
            RestoreEditorCamera();
            return false;
        }
        spdlog::info("[Camera] Gameplay camera active: entity {}",
                     static_cast<uint32_t>(m_activeGameCamera));
        return true;
    }



    void GaleshiEditorApp::DeactivateGameplayCamera() {
        m_usingGameCamera = false;
        RestoreEditorCamera();
    }



    // -------------------------------------------------------------------
    // Post-processing volumes
    // -------------------------------------------------------------------
    void GaleshiEditorApp::CaptureSceneViewState() {
        Galeshi::SceneCameraState& camera = m_scene.CameraState();
        camera.valid = true;
        camera.position = m_camera.Position();

        // Camera rotation is the inverse of the view-matrix rotation.
        const glm::mat3 cameraToWorld =
            glm::transpose(glm::mat3(m_camera.View()));
        camera.rotation = glm::normalize(glm::quat_cast(cameraToWorld));

        camera.target = m_editor.camTarget;
        camera.yaw = m_editor.camYaw;
        camera.pitch = m_editor.camPitch;
        camera.radius = m_editor.camRadius;

        // Persist the global post-process state.  The manager owns the
        // toggles; the remaining values live in Scene so custom global
        // settings survive save/load without being reset to struct defaults.
        Galeshi::PostProcessSettings& pp = m_scene.GlobalPostProcess();
        pp.bloomEnabled = m_postProcess.BloomEnabled();
        pp.dofEnabled = m_postProcess.DofEnabled();
        pp.motionBlurEnabled = m_postProcess.MotionBlurEnabled();
        pp.ssaoEnabled = m_postProcess.SsaoEnabled();
        pp.aoMode = m_globalAoMode;
        pp.ssrEnabled = m_postProcess.SsrEnabled();
        pp.ssgiEnabled = m_postProcess.SsgiEnabled();
        pp.fxaaEnabled = m_postProcess.FxaaEnabled();
        pp.smaaEnabled = m_postProcess.SmaaEnabled();
        pp.taaEnabled = m_postProcess.TaaEnabled();
        pp.godRaysEnabled = m_postProcess.GodRaysEnabled();
        pp.atmosphereEnabled = m_postProcess.AtmosphereEnabled();
        pp.fogEnabled = m_postProcess.FogEnabled();
        pp.fogVolumetric = m_postProcess.FogVolumetric();
        pp.invertEnabled = m_postProcess.InvertEnabled();
        m_scene.SetHasGlobalPostProcess(true);
    }

