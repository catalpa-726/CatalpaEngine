#pragma once
#include "catpch.h"
#include "Shaders/ShaderMetadata.h"
namespace Catalpa::Tools {
    // 仅用于已经通过最终 spirv-val 的字节码；不会承担/绕过 validator。
    class SpirvReflection final {
    public: static bool Reflect(std::span<const uint32_t> validatedWords,ShaderMetadata& output,std::string& error);
    };
}
