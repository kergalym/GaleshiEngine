log("init.lua: loaded player controller")

-- Lifecycle callbacks:
--   RunOnce()          runs once after the script is loaded (and after a successful hot reload)
--   RunAlways(dt)      runs every frame
local player = nil

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
        log("init.lua: using player eid=" .. tostring(player.id))
    end
end

function RunAlways(dt)
    if not player or not player:valid() then
        player = findPlayer()
        if not player then return end
    end

    local speed = 10.0
    local jump_force = 1000.0

    local vel = Galeshi.physics.get_velocity(player)
    local vy = vel.y -- preserve gravity while moving horizontally

    local horizontal = Galeshi.input.axis("Horizontal")
    local vertical = Galeshi.input.axis("Vertical")
    local vx = horizontal * speed
    local vz = vertical * speed

    -- Always write the horizontal velocity so the player stops when no key is
    -- held. This also drives the script fallback used by body-less Player roots.
    Galeshi.physics.set_velocity(player, vx, vy, vz)

    if Galeshi.input.axis("Jump") > 0.5 and math.abs(vy) < 0.5 then
        log("JUMP!")
        Galeshi.physics.add_impulse(player, 0, jump_force, 0)
    end
end
