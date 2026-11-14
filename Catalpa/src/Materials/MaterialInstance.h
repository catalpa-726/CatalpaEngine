#pragma once
#include "Core/Base.h"
#include "Materials/MaterialDefinition.h"

namespace Catalpa {
	/* 材质实例固定其定义版本，保存完整的已解析参数。revision 记录实例的编辑次数，资产内容版本另行维护。 */
	struct MaterialInstance {
		AssetVersionRef<MaterialDefinition> definition;
		uint64_t revision = 1;
		std::vector<MaterialParameterOverride> parameters;
		MaterialRenderState renderState;
		/*
		 * 普通表面纹理固定版本：base/normal/metalrough/emissive/occlusion，不按逻辑ID追新。
		 * 未解析作者草稿可为空；Renderer必须拒绝有handle但没有对应固定版本的使用。
		 */
		std::array<AssetVersionRef<ImageAsset>, 5> textureVersions;
	};
	/**
	 * @brief 以固定定义版本、默认参数和显式覆盖创建不可变材质实例。
	 * 从默认值与显式覆盖构建新的const实例，任何失败均保留旧output。
	 *
	 * @param[in] definition 固定到确切版本的材质定义，提供参数默认值和可覆盖范围。
	 * @param[in] overrides 按稳定参数标识指定的材质覆盖值。
	 * @param[in] states 需要覆盖的渲染状态，未指定项沿用原设置。
	 * @param[in] revision 新实例的修订号。
	 * @param[out] output 成功时接收新建的只读材质实例。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 候选实例创建并验证成功时为 true，否则为 false。
	 */
	[[nodiscard]] CATALPA_API bool CreateMaterialInstance(const AssetVersionRef<MaterialDefinition>& definition,
	    std::span<const MaterialParameterOverride> overrides, const MaterialStateOverrides& states, uint64_t revision,
	    std::shared_ptr<const MaterialInstance>& output, std::string& error);
	/**
	 * @brief 在材质实例副本上应用覆盖，并发布新的修订版本。
	 * 在副本应用允许的覆盖并递增revision；旧定义/实例不被修改，溢出显式失败。
	 *
	 * @param[in] source 用作修改基准的现有材质实例。
	 * @param[in] overrides 按稳定参数标识指定的材质覆盖值。
	 * @param[in] states 需要覆盖的渲染状态，未指定项沿用原设置。
	 * @param[out] output 成功时接收应用覆盖后的新版本实例。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 新修订发布成功时为 true，修订溢出或验证失败时为 false。
	 */
	[[nodiscard]] CATALPA_API bool UpdateMaterialInstance(const MaterialInstance& source,
	    std::span<const MaterialParameterOverride> overrides, const MaterialStateOverrides& states,
	    std::shared_ptr<const MaterialInstance>& output, std::string& error);
	/**
	 * @brief 按参数标识查找材质实例中已解析的值。
	 * 查找完整解析值；返回指针在其所属不可变实例有效期间借用。
	 *
	 * @param[in] instance 提供已解析参数值及定义的材质实例。
	 * @param[in] parameterId 待查参数的稳定标识。
	 *
	 * @return 实例持有的参数值指针，找不到时返回 nullptr。
	 */
	[[nodiscard]] CATALPA_API const MaterialValue* FindMaterialParameter(
	    const MaterialInstance& instance, std::string_view parameterId) noexcept;
}
