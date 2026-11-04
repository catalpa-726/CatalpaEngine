#pragma once

#include "MathConfig.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Catalpa::Math {

    // CPU 数学值类型；不直接作为磁盘格式、GPU block 或跨编译器插件 ABI。
    using Vec3 = glm::vec<3, float, glm::packed_highp>;
    using Vec4 = glm::vec<4, float, glm::packed_highp>;
    using Quat = glm::qua<float, glm::packed_highp>;
    using Mat3 = glm::mat<3, 3, float, glm::packed_highp>;
    using Mat4 = glm::mat<4, 4, float, glm::packed_highp>;

    static_assert(sizeof(Vec3) == 3 * sizeof(float) && alignof(Vec3) == alignof(float));
    static_assert(sizeof(Quat) == 4 * sizeof(float) && alignof(Quat) == alignof(float));
    static_assert(sizeof(Mat4) == 16 * sizeof(float) && alignof(Mat4) == alignof(float));

    // 作者局部变换；rotation 以具名 w/x/y/z 构造和访问，角度使用弧度。
    struct TransformTRS {
        Vec3 translation{ 0.0f };
        Quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
        Vec3 scale{ 1.0f };
    };

}
