#pragma once
#include "Core/Serialization/Archive.h"
#include "Assets/AssetId.h"
#include "Assets/AssetLoadResult.h"
#include "Scene/Scene.h"
namespace Catalpa {
	/**
	 * @brief 捕获音频界面组件。
	 *
	 * @param[in] scene 此次操作所针对的场景。
	 * @param[in] entity 操作所针对的实体。
	 * @param[out] out 接收 ArchiveValue::Object 结果。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool CaptureAudioUIComponents(const Scene&, EntityId, ArchiveValue::Object&, std::string& error);
	/**
	 * @brief 校验音频界面组件。
	 *
	 * @param[in] key 标识音频或游戏界面组件种类的序列化键。
	 * @param[in] value 要读取或转换的归档值。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ValidateAudioUIComponent(std::string_view, const ArchiveValue&, std::string& error);
	/**
	 * @brief 列出音频界面组件引用的资产标识。
	 *
	 * @param[in] key 标识音频或游戏界面组件种类的序列化键。
	 * @param[in] value 要读取或转换的归档值。
	 * @param[out] out 接收 std::vector<AssetId> 结果。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool AudioUIComponentDependencies(
	    std::string_view, const ArchiveValue&, std::vector<AssetId>&, std::string& error);
	/**
	 * @brief 恢复音频界面组件。
	 *
	 * @param[in] scene 此次操作所针对的场景。
	 * @param[in] entity 操作所针对的实体。
	 * @param[in] key 标识音频或游戏界面组件种类的序列化键。
	 * @param[in] value 要读取或转换的归档值。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 世界操作的状态，失败时包含对应的错误类别。
	 */
	CATALPA_API WorldStatus RestoreAudioUIComponent(
	    Scene&, EntityId, std::string_view, const ArchiveValue&, std::string& error);
	/**
	 * @brief 恢复音频界面组件。
	 *
	 * @param[in] world 实体及组件所在的 ECS 世界。
	 * @param[in] entity 操作所针对的实体。
	 * @param[in] key 标识音频或游戏界面组件种类的序列化键。
	 * @param[in] value 要读取或转换的归档值。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 世界操作的状态，失败时包含对应的错误类别。
	 */
	CATALPA_API WorldStatus RestoreAudioUIComponent(
	    World&, EntityId, std::string_view, const ArchiveValue&, std::string& error);
	/**
	 * @brief 绑定音频界面组件版本。
	 *
	 * @param[in] world 实体及组件所在的 ECS 世界。
	 * @param[in] entity 操作所针对的实体。
	 * @param[in] versions 用于固定音频片段或界面文档引用的依赖版本。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool BindAudioUIComponentVersions(
	    World&, EntityId, std::span<const AssetDependencyValue>, std::string& error);
	/**
	 * @brief 收集音频界面组件版本。
	 *
	 * @param[in] world 实体及组件所在的 ECS 世界。
	 * @param[in] entity 操作所针对的实体。
	 * @param[out] output 接收 std::vector<AssetDependencyValue> 结果。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool CollectAudioUIComponentVersions(
	    const World&, EntityId, std::vector<AssetDependencyValue>&, std::string& error);
} /* 命名空间 Catalpa */
