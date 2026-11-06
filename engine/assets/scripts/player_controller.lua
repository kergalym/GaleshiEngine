-- Player Controller
-- Assign this file in Edit -> Scripts, or use it as the main script.
local playerName = "Player"
local moveSpeed = 8.0
local turnSpeed = 90.0
local jumpImpulse = 8.0
local player = nil
local animationEntity = nil

local function findAnimationEntity(entity)
    if not entity or not entity:valid() then return nil end
    if entity:has_component("animation_state_machine") then
        return entity
    end
    local children = entity:children()
    for i = 1, #children do
        local found = findAnimationEntity(children[i])
        if found then return found end
    end
    return nil
end

local function acquirePlayer()
    player = Galeshi.world.find(playerName)
    animationEntity = findAnimationEntity(player)
end

function RunOnce()
    acquirePlayer()
end

function RunAlways(dt)
    if not player or not player:valid() then
        acquirePlayer()
        if not player then return end
    end
    if not animationEntity or not animationEntity:valid() then
        animationEntity = findAnimationEntity(player)
    end

    local velocity = Galeshi.physics.get_velocity(player)
    local horizontal = Galeshi.input.axis("Horizontal")
    local vertical = Galeshi.input.axis("Vertical")
    local strafing = Galeshi.player.strafing()
    local forward = Galeshi.camera.forward()
    local vx, vz = 0.0, 0.0

    if math.abs(vertical) > 0.001 then
        -- W/S face the character along the camera direction.
        player:set_euler(
            0.0, math.deg(math.atan(forward.x, forward.z)), 0.0)
        vx = forward.x * vertical * moveSpeed
        vz = forward.z * vertical * moveSpeed
    elseif not strafing and math.abs(horizontal) > 0.001 then
        local rotation = player:rotation()
        local yaw = math.deg(2.0 * math.atan(rotation.y, rotation.w))
        player:set_euler(
            0.0, yaw - horizontal * turnSpeed * dt, 0.0)
    end

    if strafing then
        -- A/D move sideways instead of turning.
        local move = Galeshi.camera.movement(horizontal, vertical)
        vx = move.x * moveSpeed
        vz = move.z * moveSpeed
    end

    Galeshi.physics.set_velocity(player, vx, velocity.y, vz)

    if Galeshi.input.axis("Jump") > 0.5 and math.abs(velocity.y) < 0.5 then
        Galeshi.physics.add_impulse(player, 0.0, jumpImpulse, 0.0)
    end

    if animationEntity then
        local speed = math.sqrt(vx * vx + vz * vz) / moveSpeed
        Galeshi.animation.set_parameter(
            animationEntity, "Speed", math.min(speed, 1.0))
    end
end
