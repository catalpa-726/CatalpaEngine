#pragma once
#include "catpch.h"
#include "Core/Base.h"

namespace Catalpa {
    // 平台中立的最终字节码接口；不暴露编译器/反射库对象。
    enum class ShaderStage : uint32_t { Vertex = 1, Fragment = 2, Compute = 3 };
    enum class ShaderScalarKind : uint32_t { Float = 1, SignedInteger = 2, UnsignedInteger = 3, Boolean = 4, Structure = 5 };
    enum class ShaderResourceKind : uint32_t { UniformBuffer = 1, StorageBuffer = 2, SampledImage = 3, StorageImage = 4, Sampler = 5, CombinedImageSampler = 6 };
    struct ShaderTypeShape {
        ShaderScalarKind scalar = ShaderScalarKind::Float;
        uint32_t width = 32;
        // Scalar/vector use rows=1, columns=component count; matrices preserve reflected row/column counts.
        uint32_t rows = 1;
        uint32_t columns = 1;
        uint32_t arrayCount = 1;
        bool operator==(const ShaderTypeShape&) const = default;
    };
    struct ShaderBlockMember {
        std::string name;
        uint32_t offset = 0, size = 0, paddedSize = 0;
        uint32_t arrayStride = 0, matrixStride = 0;
        bool rowMajor = false;
        ShaderTypeShape type;
        std::vector<ShaderBlockMember> members;
        bool operator==(const ShaderBlockMember&) const = default;
    };
    struct ShaderBinding {
        uint32_t set = 0, binding = 0, count = 1;
        ShaderResourceKind kind = ShaderResourceKind::UniformBuffer;
        std::string name;
        uint32_t blockSize = 0;
        std::vector<ShaderBlockMember> members;
        bool operator==(const ShaderBinding&) const = default;
    };
    struct ShaderInterfaceVariable {
        std::string name;
        uint32_t location = 0;
        ShaderTypeShape type;
        bool operator==(const ShaderInterfaceVariable&) const = default;
    };
    struct ShaderEntryPointMetadata {
        std::string name;
        ShaderStage stage = ShaderStage::Vertex;
        std::vector<ShaderBinding> bindings;
        std::vector<ShaderInterfaceVariable> inputs, outputs;
        uint32_t pushConstantBytes = 0;
        std::vector<ShaderBlockMember> pushConstantMembers;
        bool operator==(const ShaderEntryPointMetadata&) const = default;
    };
    struct ShaderMetadata {
        uint32_t schemaVersion = 1;
        std::string targetEnvironment = "vulkan1.3";
        uint32_t spirvVersion = 0x00010500;
        std::string bytecodeSha256;
        std::vector<ShaderEntryPointMetadata> entries;
        bool operator==(const ShaderMetadata&) const = default;
    };
    // 内建或烘焙输入都必须先具备正式metadata；这里不冒充SPIR-V validator。
    CATALPA_API bool ValidateShaderMetadata(const ShaderMetadata&, std::string& error);
    CATALPA_API const ShaderEntryPointMetadata* FindShaderEntry(const ShaderMetadata&, std::string_view name, ShaderStage stage) noexcept;
}
