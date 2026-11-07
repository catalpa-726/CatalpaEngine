#pragma once

#include "catpch.h"

#include "ECS/World.h"

namespace Catalpa {

    struct SystemContext;
    class Scene;

    // CPU 阶段顺序，与 GPU barrier、队列或录制顺序无关。
    enum class SystemPhase {
        Gameplay,
        Animation,
        Kinematic,
        Physics,
        PostPhysics,
        Transform,
        Camera,
        AudioExtract,
        UI,
        Extract
    };
    enum class SystemAccessMode { Read, Write };

    // 资源名是进程内调度契约；不使用第三方 storage 地址或 RTTI 作为持久身份。
    struct SystemAccess {
        std::string resource;
        SystemAccessMode mode = SystemAccessMode::Read;
    };

    // 轻量系统适配：所有声明均自有，回调通过受限上下文借用场景和服务。
    struct SystemDesc {
        std::string id;
        SystemPhase phase = SystemPhase::Gameplay;
        std::vector<SystemAccess> accesses;
        std::vector<std::string> after;
        std::function<WorldStatus(SystemContext&)> execute;
        // 原生玩法显式构造独立 Play 实例，禁止把捕获的可变编辑状态直接复制到运行世界。
        std::function<SystemDesc(Scene&)> cloneForPlay;
    };

}
