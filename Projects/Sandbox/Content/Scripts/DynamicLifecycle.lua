-- 只发布值命令；子实体直到受保护调用成功后的结构安全点才可解析。
local M = { state_schema = 1, state = { child = "", pulses = 0 } }

local function subscribe()
    events.subscribe("collision", "contact")
    events.subscribe("ui", "clicked")
end

function M.start()
    M.state.child = scene.create("Script-created entity", 0, 1, 0)
    subscribe()
    tasks.after(1, "pulse")
    tasks.checksum("Catalpa async owner continuation", "checksum_ready")
end

function M.reload()
    -- 热更迁移作者状态，重新建立新 VM 自己拥有的订阅，不重复创建实体。
    subscribe()
    tasks.after(1, "pulse")
end

function M.contact(dt, event)
    M.state.pulses = M.state.pulses + 1
end

function M.clicked(dt, event)
    if event.type == "click" then
        M.state.pulses = M.state.pulses + 1
    end
end

function M.pulse()
    scene.translate(0, 0.05, 0)
    tasks.after(1, "pulse")
end

function M.checksum_ready(dt, event)
    assert(#event.checksum == 64)
end

function M.stop()
    events.unsubscribe("collision")
    events.unsubscribe("ui")
    -- Runtime Stop 不继续发布世界命令；PlayWorld 的实体随会话整体退役。
end

return M
