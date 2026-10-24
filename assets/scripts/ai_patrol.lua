-- AI Patrol Controller
-- Патрулирует между 4 точками, но не уходит далеко от центра.

log("ai_patrol.lua loaded successfully")

local waypoints = {
    { x =  5, y = 0, z =  5 },
    { x = -5, y = 0, z =  5 },
    { x = -5, y = 0, z = -5 },
    { x =  5, y = 0, z = -5 },
}
local currentWaypoint = 1
local waitTimer = 0

function update_ai(eid, dt)
    -- Если путь закончен, ждём и идём к следующей точке
    if is_path_done(eid) then
        waitTimer = waitTimer + dt
        if waitTimer > 1.5 then
            waitTimer = 0
            currentWaypoint = (currentWaypoint % #waypoints) + 1
            local wp = waypoints[currentWaypoint]
            set_ai_target(eid, wp.x, wp.y, wp.z)
            -- log("[AI " .. tostring(eid) .. "] Moving to waypoint " .. currentWaypoint)
        end
    end
end
