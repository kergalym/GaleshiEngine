log("init.lua: starting player controller")

local player = new_physics_cube("Player", 0.0, 5.0, 0.0, 2.0)
log("init.lua: created player eid=" .. tostring(player))

local log_timer = 0

function update(dt)
    local speed = 10.0
    local jump_force = 1000.0
    
    local vel = get_velocity(player)
    local vx = 0
    local vy = vel.y -- сохраняем гравитацию
    local vz = 0
    
    if is_key_down("W") then vz = speed end
    if is_key_down("S") then vz = -speed end
    if is_key_down("A") then vx = -speed end
    if is_key_down("D") then vx = speed end
    
    -- Применяем скорость только если двигаемся по X или Z
    if vx ~= 0 or vz ~= 0 then
        set_velocity(player, vx, vy, vz)
    end
    
    -- Прыжок
    if is_key_down("Space") then
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
        log("Status: W=" .. tostring(is_key_down("W")) .. 
            " Space=" .. tostring(is_key_down("Space")) .. 
            " PosY=" .. string.format("%.2f", pos.y))
    end
end
