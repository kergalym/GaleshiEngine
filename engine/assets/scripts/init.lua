log("init.lua: loaded player controller")

-- Lifecycle callbacks:
--   RunOnce()          runs once after the script is loaded (and after a successful hot reload)
--   RunAlways(dt)      runs every frame
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

local function findPlayer()
    -- Prefer a Player entity that already has a character/mesh attached.
    local candidates = Galeshi.world.find_all("Player")
    for i = 1, #candidates do
        local candidate = candidates[i]
        if candidate and candidate:valid() and #candidate:children() > 0 then
            return candidate
        end
    end
    if #candidates > 0 then return candidates[1] end
    return nil
end

function RunOnce()
    player = findPlayer()
    if not player then
        player = Galeshi.physics.create_box("Player", 0.0, 5.0, 0.0, 2.0)
    end
    if player then
        animationEntity = findAnimationEntity(player)
        log("init.lua: using player eid=" .. tostring(player.id))
    end
end

function RunAlways(dt)
    if not player or not player:valid() then
        player = findPlayer()
        if not player then return end
    end
    if not animationEntity or not animationEntity:valid() then
        animationEntity = findAnimationEntity(player)
    end

    local speed = 10.0
    local turn_speed = 120.0
    local jump_force = 8.0

    local vel = Galeshi.physics.get_velocity(player)
    local vy = vel.y -- preserve gravity while moving horizontally

    local horizontal = Galeshi.input.axis("Horizontal")
    local vertical = Galeshi.input.axis("Vertical")
    local strafing = Galeshi.player.strafing()
    local forward = Galeshi.camera.forward()
    local vx, vz = 0.0, 0.0

    if math.abs(vertical) > 0.001 then
        -- W/S face the character along the camera direction.
        player:set_euler(
            0.0, math.deg(math.atan(forward.x, forward.z)), 0.0)
        vx = forward.x * vertical * speed
        vz = forward.z * vertical * speed
    elseif not strafing and math.abs(horizontal) > 0.001 then
        -- Without strafing, A/D turn the character in place.
        local rotation = player:rotation()
        local yaw = math.deg(2.0 * math.atan(rotation.y, rotation.w))
        player:set_euler(
            0.0, yaw - horizontal * turn_speed * dt, 0.0)
    end

    if strafing then
        local move = Galeshi.camera.movement(horizontal, vertical)
        vx = move.x * speed
        vz = move.z * speed
    end

    -- Always write the horizontal velocity so the player stops when no key is
    -- held. This also drives the script fallback used by body-less Player roots.
    Galeshi.physics.set_velocity(player, vx, vy, vz)

    if Galeshi.input.axis("Jump") > 0.5 and math.abs(vy) < 0.5 then
        log("JUMP!")
        Galeshi.physics.add_impulse(player, 0, jump_force, 0)
    end

    if animationEntity then
        local horizontalSpeed = math.sqrt(vx * vx + vz * vz) / speed
        Galeshi.animation.set_parameter(
            animationEntity, "Speed", math.min(horizontalSpeed, 1.0))
    end
end
