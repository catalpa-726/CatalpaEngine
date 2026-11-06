-- Runtime dt is in seconds; translation is in metres.
-- Each Play instance owns its Lua state and properties.
return {
    update = function(dt)
        local speed = properties.speed or 0.5
        scene.translate(speed * dt, 0, 0)
    end
}
