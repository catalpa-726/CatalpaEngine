#pragma once

namespace Catalpa {

    enum class RHIShaderStage { Vertex, Fragment };

    // 字节码须经过工具验证；入口名以零结尾。
    // 两者都只借用到同步管线创建调用结束。
    struct RHIShaderDesc {
        std::span<const uint32_t> bytecode{};
        RHIShaderStage stage = RHIShaderStage::Vertex;
        const char* entryPoint = "main";
    };

}