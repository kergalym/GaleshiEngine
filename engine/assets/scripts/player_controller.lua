-- Player Controller
-- Assign this file in Edit -> Scripts, or use it as the main script.
local playerName = "Player"
local moveSpeed = 8.0
local turnSpeed = math.pi * 0.5 -- 90 degrees/second in radians
local jumpImpulse = 8.0
local player = nil
local animationEntity = nil
local visualEntity = nil
local bodyEntity = nil

local function quatIdentity()
    return {x = 0.0, y = 0.0, z = 0.0, w = 1.0}
end

-- x,y,z,w quaternion multiplication.
local function quatMultiply(a, b)
    return {
        x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
    }
end

local function quatNormalize(q)
    local length = math.sqrt(q.x * q.x + q.y * q.y +
                             q.z * q.z + q.w * q.w)
    if length < 1e-8 then return quatIdentity() end
    local inv = 1.0 / length
    return {x = q.x * inv, y = q.y * inv,
            z = q.z * inv, w = q.w * inv}
end

local function quatInverse(q)
    local lengthSq = q.x * q.x + q.y * q.y +
                     q.z * q.z + q.w * q.w
    if lengthSq < 1e-8 then return quatIdentity() end
    local inv = 1.0 / lengthSq
    return {x = -q.x * inv, y = -q.y * inv,
            z = -q.z * inv, w = q.w * inv}
end

-- Rotation around world up; no degree conversion.
local function quatYaw(radians)
    local half = radians * 0.5
    return {x = 0.0, y = math.sin(half),
            z = 0.0, w = math.cos(half)}
end

-- Clean +Z-to-direction quaternion in the horizontal XZ plane.
-- This is the upright equivalent of quatLookAt; no math.atan is used.
local function quatFromDirectionXZ(direction)
    local x = direction.x
    local z = direction.z
    local lengthSq = x * x + z * z
    if lengthSq < 1e-8 then return quatIdentity() end

    local invLength = 1.0 / math.sqrt(lengthSq)
    x = x * invLength
    z = z * invLength

    if z < -0.999999 then
        return {x = 0.0, y = 1.0, z = 0.0, w = 0.0}
    end
    return quatNormalize({x = 0.0, y = x, z = 0.0, w = 1.0 + z})
end

local function parentWorldRotation(entity)
    local chain = {}
    local parent = entity:parent()
    while parent and parent:valid() do
        table.insert(chain, parent)
        parent = parent:parent()
    end

    local world = quatIdentity()
    -- Parents are child -> root; multiply root -> child.
    for i = #chain, 1, -1 do
        world = quatMultiply(world, chain[i]:rotation())
    end
    return quatNormalize(world)
end

local function worldRotation(entity)
    return quatNormalize(
        quatMultiply(parentWorldRotation(entity), entity:rotation()))
end

-- Assign a world-space quaternion while preserving a valid local rotation.
local function setWorldYaw(entity, targetQuat)
    local parentWorld = parentWorldRotation(entity)
    local localQuat = quatMultiply(
        quatInverse(parentWorld), quatNormalize(targetQuat))
    entity:set_rotation(localQuat)
end

local function addWorldYaw(entity, radians)
    setWorldYaw(entity,
        quatMultiply(quatYaw(radians), worldRotation(entity)))
end

local function findVisualEntity(root)
    if not root or not root:valid() then return nil end
    -- A mesh root already has the character pivot. Otherwise the imported
    -- character usually lives on a descendant.
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
            -- W+A/W+D: face the horizontal movement direction in world space.
            setWorldYaw(player,
                quatFromDirectionXZ({x = move.x, z = move.z}))
        elseif not strafing then
            -- W/S: keep the existing forward/backpedal facing.
            local facing = visualEntity or player
            local direction = vertical > 0.0 and move or forward
            setWorldYaw(facing,
                quatFromDirectionXZ({x = direction.x, z = direction.z}))
        end
        vx = move.x * moveSpeed
        vz = move.z * moveSpeed
    elseif not strafing and math.abs(horizontal) > 0.001 then
        -- A/D turn around world up without Euler extraction.
        local facing = visualEntity or player
        addWorldYaw(facing, -horizontal * turnSpeed * dt)
    elseif strafing then
        -- Strafing A/D: move sideways without changing facing.
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
