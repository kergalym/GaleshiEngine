// Headless trigger-overlap smoke test.
//
// The project disables the normal CTest suite, so this is compiled and run
// manually against a built engine library. It creates a static sensor box and
// moves a dynamic box through it, then checks that exactly one begin and one
// end callback were delivered.
#include "GaleshiEngine/Scene/Scene.hpp"
#include "GaleshiEngine/Physics/PhysicsSystem.hpp"

#include <cstdio>

int main() {
    Galeshi::EnginePhysicsSystem physics;
    if (!physics.Init()) return 2;

    Galeshi::ColliderComponent trigger{};
    trigger.shapeType = Galeshi::ColliderShapeType::Box;
    trigger.halfExtents = {1.0f, 1.0f, 1.0f};
    trigger.isTrigger = true;
    Galeshi::RigidBodyComponent triggerBody{};
    triggerBody.motionType = Galeshi::RigidBodyComponent::MotionType::Static;
    const uint32_t triggerId = physics.CreateCollider(
        trigger, triggerBody, {0.0f, 2.0f, 0.0f}, glm::quat{1, 0, 0, 0},
        static_cast<entt::entity>(1));

    Galeshi::ColliderComponent mover{};
    mover.shapeType = Galeshi::ColliderShapeType::Box;
    mover.halfExtents = {0.5f, 0.5f, 0.5f};
    Galeshi::RigidBodyComponent moverBody{};
    moverBody.motionType = Galeshi::RigidBodyComponent::MotionType::Dynamic;
    moverBody.useGravity = false;
    const uint32_t moverId = physics.CreateCollider(
        mover, moverBody, {5.0f, 2.0f, 0.0f}, glm::quat{1, 0, 0, 0},
        static_cast<entt::entity>(2));

    int begins = 0;
    int ends = 0;
    physics.SetOnTriggerBeginOverlap(
        [&](entt::entity triggerEntity, entt::entity otherEntity) {
            if (triggerEntity == static_cast<entt::entity>(1) &&
                otherEntity == static_cast<entt::entity>(2)) {
                ++begins;
            }
        });
    physics.SetOnTriggerEndOverlap(
        [&](entt::entity triggerEntity, entt::entity otherEntity) {
            if (triggerEntity == static_cast<entt::entity>(1) &&
                otherEntity == static_cast<entt::entity>(2)) {
                ++ends;
            }
        });

    Galeshi::Scene scene;
    for (int frame = 0; frame < 240; ++frame) {
        physics.SetBodyTransform(
            moverId, {5.0f - 0.05f * static_cast<float>(frame), 2.0f, 0.0f},
            glm::quat{1, 0, 0, 0});
        physics.Update(scene, 1.0f / 60.0f);
    }

    std::printf("trigger=%u mover=%u begins=%d ends=%d\n",
                triggerId, moverId, begins, ends);
    physics.Shutdown();
    return (begins == 1 && ends == 1) ? 0 : 1;
}
