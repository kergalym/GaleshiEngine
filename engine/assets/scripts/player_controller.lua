-- Player Controller
-- Assign this file in Edit -> Scripts, or use it as the main script.
local playerName = "Player"
local moveSpeed = 8.0
local turnSpeed = 90.0
local jumpImpulse = 8.0
local player = nil
local animationEntity = nil
local visualEntity = nil
local bodyEntity = nil

local function findVisualEntity(root)
    if not root or not root:valid() then return nil end
    -- A mesh root already has the character pivot. Otherwise the imported
    -- character usually lives on a descendant (for example under a capsule).
    if root:has_component("mesh") then return root end
    local function walk(entity)
        local children = entity:children()
        for i = 1, #children do
            local child = children[i]
            if child:has_component("mesh") or
               child:has_component("skeletal") or
               child:has_component("model") then
                return child
            end
            local found = walk(child)
            if found then return found end
        end
        return nil
    end
    return walk(root) or root
end

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

local function findBodyEntity(entity)
    if not entity or not entity:valid() then return nil end
    if entity:has_component("rigid_body") then return entity end
    local children = entity:children()
    for i = 1, #children do
        local found = findBodyEntity(children[i])
        if found then return found end
    end
    return nil
end

-- Assign a world-space yaw while keeping the entity's local transform valid
-- under its current parent.
local function setWorldYaw(entity, yaw)
    local parentYaw = 0.0
    local parent = entity:parent()
    while parent and parent:valid() do
        local rotation = parent:rotation()
        parentYaw = parentYaw + 2.0 * math.atan(rotation.y, rotation.w)
        parent = parent:parent()
    end
    entity:set_euler(0.0, math.deg(yaw - parentYaw), 0.0)
end

local function findPlayer()
    -- Prefer the Player that owns the visible character; a scene can contain
    -- more than one entity named Player.
    local candidates = Galeshi.world.find_all(playerName)
    for i = 1, #candidates do
        local candidate = candidates[i]
        if candidate and candidate:valid() and #candidate:children() > 0 then
            return candidate
        end
    end
    if #candidates > 0 then return candidates[1] end
    return nil
end

local function acquirePlayer()
    player = findPlayer()
    animationEntity = findAnimationEntity(player)
    visualEntity = findVisualEntity(player)
    bodyEntity = findBodyEntity(player) or player
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
    if not visualEntity or not visualEntity:valid() then
        visualEntity = findVisualEntity(player)
    end
    if not bodyEntity or not bodyEntity:valid() then
        bodyEntity = findBodyEntity(player) or player
    end

    local body = bodyEntity or player
    local velocity = Galeshi.physics.get_velocity(body)
    local horizontal = Galeshi.input.axis("Horizontal")
    local vertical = Galeshi.input.axis("Vertical")
    local strafing = Galeshi.player.strafing()
    local forward = Galeshi.camera.forward()
    local move = Galeshi.camera.movement(horizontal, vertical)
    local vx, vz = 0.0, 0.0

    if math.abs(vertical) > 0.001 then
        if not strafing and vertical > 0.0 and
           math.abs(horizontal) > 0.001 then
            -- W+A/W+D rotate the Player itself in world space.
            setWorldYaw(player, math.atan(move.x, move.z))
        elseif not strafing then
            -- W/S without lateral input keep the existing local facing.
            local facing = visualEntity or player
            local direction = vertical > 0.0 and move or forward
            facing:set_euler(
                0.0, math.deg(math.atan(direction.x, direction.z)), 0.0)
        end
        vx = move.x * moveSpeed
        vz = move.z * moveSpeed
    elseif not strafing and math.abs(horizontal) > 0.001 then
        -- Without strafing, A/D keep their original local turn-in-place.
        local facing = visualEntity or player
        local rotation = facing:rotation()
        local yaw = math.deg(2.0 * math.atan(rotation.y, rotation.w))
        facing:set_euler(
            0.0, yaw - horizontal * turnSpeed * dt, 0.0)
    elseif strafing then
        -- With strafing enabled, A/D move sideways without world/local
        -- rotation.
        vx = move.x * moveSpeed
        vz = move.z * moveSpeed
    end

    Galeshi.physics.set_velocity(body, vx, velocity.y, vz)

    if Galeshi.input.axis("Jump") > 0.5 and math.abs(velocity.y) < 0.5 then
        Galeshi.physics.add_impulse(body, 0.0, jumpImpulse, 0.0)
    end

    if animationEntity then
        local speed = math.sqrt(vx * vx + vz * vz) / moveSpeed
        Galeshi.animation.set_parameter(
            animationEntity, "Speed", math.min(speed, 1.0))
    end
end
