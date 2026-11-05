#pragma once

#include "Lighting/EnvironmentAssetLoader.h"

namespace Catalpa::Tools {

    // 环境使用独立typed容器；格式与runtime loader共享，绝不在运行期重新烘焙。
    class EnvironmentCooker final {
    public:
        static bool Encode(const EnvironmentAsset&, std::vector<std::byte>&, std::string& error);
    };

}
