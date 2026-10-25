log("init.lua: loaded player controller")

-- Lifecycle callbacks:
--   RunOnce()          runs once after the script is loaded (and after a successful hot reload)
--   RunAlways(dt)      runs every frame
local player = nil
local log_timer = 0

function RunOnce()
    player = new_physics_cube("Player", 0.0, 5.0, 0.0, 2.0)
    log("init.lua: created player eid=" .. tostring(player))
    log_timer = 0
end

function RunAlways(dt)
    if not player then return end

    local speed = 10.0
    local jump_force = 1000.0

    local vel = get_velocity(player)
    local vy = vel.y -- сохраняем гравитацию

    -- Keyboard/mouse axes are configured in Edit -> Preferences.
    local horizontal = get_axis("Horizontal")
    local vertical = get_axis("Vertical")
    local vx = horizontal * speed
    local vz = vertical * speed

    -- Применяем скорость только если двигаемся по X или Z
    if vx ~= 0 or vz ~= 0 then
        set_velocity(player, vx, vy, vz)
    end

    -- Прыжок
    if get_axis("Jump") > 0.5 then
        if math.abs(vy) < 0.5 then
            log("JUMP!")
            apply_impulse(player, 0, jump_force, 0)
        end
    end

    -- Логируем раз в секунду, чтобы увидеть состояние
    log_timer = log_timer + dt
    if log_timer > 1.0 then
        log_timer = 0
        local pos = get_position(player)
        log("Status: Horizontal=" .. string.format("%.2f", horizontal) ..
            " Vertical=" .. string.format("%.2f", vertical) ..
            " Jump=" .. string.format("%.2f", get_axis("Jump")) ..
            " PosY=" .. string.format("%.2f", pos.y))
    end
end
