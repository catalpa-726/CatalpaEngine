#pragma once
#include "catpch.h"

#include "Core/Base.h"

namespace Catalpa {
	/* 平台中立的最终字节码接口；不暴露编译器/反射库对象。 */
	enum class ShaderStage : uint32_t { Vertex = 1, Fragment = 2, Compute = 3 };
	enum class ShaderScalarKind : uint32_t {
		Float = 1,
		SignedInteger = 2,
		UnsignedInteger = 3,
		Boolean = 4,
		Structure = 5
	};
	enum class ShaderResourceKind : uint32_t {
		UniformBuffer = 1,
		StorageBuffer = 2,
		SampledImage = 3,
		StorageImage = 4,
		Sampler = 5,
		CombinedImageSampler = 6
	};
	enum class ShaderImageDimension : uint32_t { None = 0, Texture2D = 1, Cube = 2 };
	struct ShaderTypeShape {
		ShaderScalarKind scalar = ShaderScalarKind::Float;
		uint32_t width = 32;
		/* 标量和向量的行数均为 1，列数等于分量数；矩阵保留反射得到的行列数。 */
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
		ShaderImageDimension imageDimension = ShaderImageDimension::None;
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
		uint32_t schemaVersion = 3; /* v3: StorageBuffer最后一成员允许arrayCount=0的运行期数组。 */
		std::string targetEnvironment = "vulkan1.3";
		uint32_t spirvVersion = 0x00010500;
		std::string bytecodeSha256;
		std::vector<ShaderEntryPointMetadata> entries;
		bool operator==(const ShaderMetadata&) const = default;
	};
	/**
	 * @brief 校验着色器元数据的入口、资源绑定和反射布局约束。
	 * 内置和烘焙的输入都须先具备着色器元数据，完整的 SPIR-V 验证由工具链完成。
	 *
	 * @param[in] m 需要检查入口、资源绑定及数据布局的着色器元数据。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 所声明的元数据内部一致时为 true，否则为 false；SPIR-V 本体验证由工具链负责。
	 */
	CATALPA_API bool ValidateShaderMetadata(const ShaderMetadata&, std::string& error);
	/**
	 * @brief 查找着色器入口。
	 *
	 * @param[in] m 包含着色器入口列表的反射元数据。
	 * @param[in] name 与着色阶段同时匹配的入口名称。
	 * @param[in] stage 着色或执行阶段。
	 *
	 * @return 匹配入口的元数据指针；找不到匹配项时为空。
	 */
	CATALPA_API const ShaderEntryPointMetadata* FindShaderEntry(
	    const ShaderMetadata&, std::string_view name, ShaderStage stage) noexcept;
}
