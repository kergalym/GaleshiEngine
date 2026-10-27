-- UI events demo for the standalone GaleshiRuntime app.
-- The engine passes (widgetId, value, checked) to every handler.

function on_menu_open()
    log("UI: main menu opened")
end

function on_play_clicked(widgetId)
    log("UI: " .. tostring(widgetId) .. " clicked - starting a new game")
end

function on_volume_changed(widgetId, value)
    log("UI: volume changed to " .. string.format("%.2f", value))
end

function on_fullscreen_toggled(widgetId, value, checked)
    log("UI: fullscreen = " .. tostring(checked))
end
