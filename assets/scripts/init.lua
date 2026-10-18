-- P1 test script: exercises new_entity / set_position / get_position / translate
log("init.lua: hello from Lua")

local eid = new_entity("LuaCube")
log("init.lua: created LuaCube eid=" .. tostring(eid))

set_position(eid, 0.0, 8.0, 0.0)
local p = get_position(eid)
log("init.lua: position = (" .. p.x .. ", " .. p.y .. ", " .. p.z .. ")")

translate(eid, 1.0, 0.0, 0.0)
p = get_position(eid)
log("init.lua: after translate = (" .. p.x .. ", " .. p.y .. ", " .. p.z .. ")")

log("init.lua: done")
