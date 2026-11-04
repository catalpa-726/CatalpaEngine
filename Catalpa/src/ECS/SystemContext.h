#pragma once

#include "Core/Time/FrameTime.h"

namespace Catalpa {

    class Scene;
    struct RuntimeServices;

    // 生命周期由调用方保护；不提供 Application 单例、设备或任意服务定位。
    struct SystemContext {
        Scene& scene;
        const FrameTime& time;
        const RuntimeServices& services;
    };

}
