-- AI Controller
-- Attach this file to an entity with an AI Controller component.
local targetName = "Player"
local moveSpeed = 3.5
local chaseRadius = 20.0
local stopRadius = 1.0
local animationEntity = nil
local lastPosition = nil

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

function update_ai(eid, dt)
    local agent = Galeshi.world.entity(eid)
    if not agent or not agent:valid() then return end
    if not animationEntity or not animationEntity:valid() then
        animationEntity = findAnimationEntity(agent)
    end

    local agentPosition = agent:position()
    local actualSpeed = 0.0
    if lastPosition and dt > 0.0 then
        local dx = agentPosition.x - lastPosition.x
        local dz = agentPosition.z - lastPosition.z
        actualSpeed = math.sqrt(dx * dx + dz * dz) / dt
    end
    lastPosition = {
        x = agentPosition.x,
        y = agentPosition.y,
        z = agentPosition.z,
    }

    Galeshi.ai.set_speed(agent, moveSpeed)
    local target = Galeshi.world.find(targetName)
    if target and target:valid() then
        local position = target:position()
        local dx = position.x - agentPosition.x
        local dz = position.z - agentPosition.z
        local distanceSq = dx * dx + dz * dz
        if distanceSq <= chaseRadius * chaseRadius and
           distanceSq > stopRadius * stopRadius then
            Galeshi.ai.set_target(
                agent, position.x, position.y, position.z)
        else
            Galeshi.ai.clear_target(agent)
        end
    else
        Galeshi.ai.clear_target(agent)
    end

    if animationEntity then
        local speed = math.min(actualSpeed / moveSpeed, 1.0)
        Galeshi.animation.set_parameter(animationEntity, "Speed", speed)
    end
end
