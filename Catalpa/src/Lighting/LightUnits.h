#pragma once

namespace Catalpa {

    // 定向光使用照度lux；点光/聚光使用轴向发光强度candela，距离统一为米。
    // 不在运行期猜测作者单位；lumen转换只在明确的作者工具入口执行。
    inline constexpr float LightingPi = 3.14159265358979323846f;
    [[nodiscard]] constexpr float LumensToIsotropicCandela(float lumens) noexcept {
        return lumens / (4.0f * LightingPi);
    }

}
