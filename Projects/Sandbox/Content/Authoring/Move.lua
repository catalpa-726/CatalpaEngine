--[[
更新间隔 dt 的单位为秒，平移量的单位为米。
每个运行实例各自持有 Lua 状态和属性。
]]
return {
    update = function(dt)
        local speed = properties.speed or 0.5
        scene.translate(speed * dt, 0, 0)
    end
}
