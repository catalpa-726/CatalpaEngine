#pragma once
#include "Materials/MaterialInstance.h"
#include "Geometry/VertexLayout.h"
#include "Core/Serialization/Archive.h"

namespace Catalpa {
	struct ShaderMetadata;
	/**
	 * @brief 校验材质定义的参数标识、默认值及其渲染状态规则。
	 * 校验定义字段、稳定参数ID、默认值/范围、纹理语义和允许的状态策略。
	 *
	 * @param[in] definition 待校验参数、着色器引用和默认状态的材质定义。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 定义内部约束全部有效时为 true，否则为 false。
	 */
	[[nodiscard]] CATALPA_API bool ValidateMaterialDefinition(const MaterialDefinition& definition, std::string& error);
	/**
	 * @brief 对照固定的定义版本检查实例参数、纹理及渲染状态。
	 * 校验实际参数集合、固定定义版本、数值/纹理及状态约束，不创建GPU资源。
	 *
	 * @param[in] instance 待按固定定义版本校验的材质实例。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 实例完整且满足材质定义约束时为 true，否则为 false。
	 */
	[[nodiscard]] CATALPA_API bool ValidateMaterialInstance(const MaterialInstance& instance, std::string& error);
	/**
	 * @brief 对照最终着色器元数据校验材质绑定和顶点输入布局。
	 * 使用最终着色器元数据检查数值块、纹理绑定和显式顶点属性，避免从 C++ 对象布局推断 GPU ABI。
	 *
	 * @param[in] definition 声明参数、纹理和顶点要求的材质定义。
	 * @param[in] metadata 最终着色器程序的反射元数据。
	 * @param[in] vertices 实际网格使用的顶点属性布局。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 数值块、纹理绑定及顶点接口相容时为 true，否则为 false。
	 */
	[[nodiscard]] CATALPA_API bool ValidateMaterialShaderInterface(const MaterialDefinition& definition,
	    const ShaderMetadata& metadata, const VertexLayout& vertices, std::string& error);
	/**
	 * @brief 校验材质参数的类型、数值范围及资源引用。
	 * 供受检实例更新共用的单参数/状态校验，未知类型和不允许覆盖必须报错。
	 *
	 * @param[in] parameter 提供类型、数值范围和引用规则的参数声明。
	 * @param[in] value 按该声明检查的具体材质值。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 参数值满足声明的类型、边界和引用约束时为 true，否则为 false。
	 */
	[[nodiscard]] CATALPA_API bool ValidateMaterialValue(
	    const MaterialParameter& parameter, const MaterialValue& value, std::string& error);
	/**
	 * @brief 对照材质纹理参数检查已解析图像的维度及采样语义。
	 * 在图像依赖解析后核对Texture2D维度与颜色/法线/数据语义，不修正图像或参数。
	 *
	 * @param[in] parameter 规定采样语义和图像约束的纹理参数。
	 * @param[in] image 将与此参数绑定的图像资产。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 图像可用于指定纹理参数时为 true，否则为 false。
	 */
	[[nodiscard]] CATALPA_API bool ValidateMaterialTexture(
	    const MaterialParameter& parameter, const ImageAsset& image, std::string& error);
	/**
	 * @brief 解析材质渲染状态。
	 *
	 * @param[in] definition 使用的材质定义。
	 * @param[in] base 材质定义提供的基础光栅化、深度和混合状态。
	 * @param[in] overrides 实例中显式指定的渲染状态覆盖项。
	 * @param[out] output 接收 MaterialRenderState 结果。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	[[nodiscard]] CATALPA_API bool ResolveMaterialRenderState(const MaterialDefinition& definition,
	    const MaterialRenderState& base, const MaterialStateOverrides& overrides, MaterialRenderState& output,
	    std::string& error);
	/**
	 * @brief 从受大小限制的 JSON 文档解析不可变材质定义。
	 * 材质payload通过统一有界JSON读入；成功发布const值，未知字段/版本均拒绝。
	 *
	 * @param[in] text 保存材质作者文档的 JSON 文本。
	 * @param[out] output 成功时接收通过模式校验的材质定义。
	 * @param[out] error 失败时接收原因说明。
	 * @param[in] limits 限制文档解析字节数、节点数和嵌套深度的配置。
	 *
	 * @return 文档完整解析且通过定义校验时为 true，否则为 false。
	 */
	[[nodiscard]] CATALPA_API bool ParseMaterialDefinition(std::string_view text,
	    std::shared_ptr<const MaterialDefinition>& output, std::string& error, const ArchiveLimits& limits = {});
	/**
	 * @brief 对照调用方固定的材质定义版本解析材质实例文档。
	 * 调用方提供已保活的确切材质定义版本。文档中的标识和版本须与其一致，解析器不会阻塞等待加载。
	 *
	 * @param[in] text 保存材质作者文档的 JSON 文本。
	 * @param[in] definition 外部已加载的确切材质定义版本，须与文档引用匹配。
	 * @param[out] output 成功时接收绑定该定义的只读材质实例。
	 * @param[out] error 失败时接收原因说明。
	 * @param[in] limits 限制文档解析字节数、节点数和嵌套深度的配置。
	 *
	 * @return 文档的标识、版本及实例值与定义相容时为 true，否则为 false。
	 */
	[[nodiscard]] CATALPA_API bool ParseMaterialInstance(std::string_view text,
	    const AssetVersionRef<MaterialDefinition>& definition, std::shared_ptr<const MaterialInstance>& output,
	    std::string& error, const ArchiveLimits& limits = {});
	/**
	 * @brief 为指定着色模型构造内置材质定义。
	 * 按统一的数据模式构造内置的 Unlit、PBR 和 Error 材质定义。每种着色模型须绑定自身的确切着色器版本。
	 *
	 * @param[in] kind 选择要生成的内置材质类型。
	 * @param[in] shaderId 供该内置材质绑定的着色器资产标识。
	 * @param[out] output 成功时接收生成的内置材质定义。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 生成并通过校验时为 true，否则为 false。
	 */
	[[nodiscard]] CATALPA_API bool CreateBuiltinMaterialDefinition(BuiltinMaterialKind kind, AssetId shaderId,
	    std::shared_ptr<const MaterialDefinition>& output, std::string& error);
	/**
	 * @brief 用固定标识和内容版本构造内置材质实例。
	 * 错误材质采用品红色和 Unlit 着色模型，沿用调用方提供的内置标识。
	 *
	 * @param[in] kind 选择要生成的内置材质类型。
	 * @param[in] definitionId 为内置定义保留的稳定资产标识。
	 * @param[in] definitionVersion 实例所绑定的材质定义内容版本。
	 * @param[in] shaderId 供该内置材质绑定的着色器资产标识。
	 * @param[out] output 成功时接收生成的内置材质实例。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 内置定义及其实例创建成功时为 true，否则为 false。
	 */
	[[nodiscard]] CATALPA_API bool CreateBuiltinMaterialInstance(BuiltinMaterialKind kind, AssetId definitionId,
	    uint64_t definitionVersion, AssetId shaderId, std::shared_ptr<const MaterialInstance>& output,
	    std::string& error);
}
