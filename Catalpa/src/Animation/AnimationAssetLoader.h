#pragma once
#include "Animation/Skinning.h"
#include "Animation/BlendTreeAsset.h"

namespace Catalpa {
	class AssetManager;
	/**
	 * @brief 读取骨骼。
	 *
	 * @param[in] text 骨架文档的 JSON 文本，包含关节层级和绑定姿态。
	 * @param[out] output 接收 SkeletonAsset 结果。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ReadSkeleton(std::string_view, SkeletonAsset&, std::string& error);
	/**
	 * @brief 将骨架资产的关节层级和姿态序列化为文本。
	 *
	 * @param[in] value 待写入文档的骨架，关节记录按其在数组中的顺序输出。
	 * @param[out] output 成功时接收包含各关节层级和绑定变换的 JSON 文本。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool WriteSkeleton(const SkeletonAsset&, std::string&, std::string& error);
	/**
	 * @brief 读取动画片段。
	 *
	 * @param[in] text 包含轨道和关键帧的动画片段数据，接受作者 JSON 或以 CANI1 开头的烘焙二进制载荷。
	 * @param[out] output 接收 AnimationClip 结果。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ReadAnimationClip(std::string_view, AnimationClip&, std::string& error);
	/**
	 * @brief 写入动画片段。
	 *
	 * @param[in] value 要序列化的动画片段，包含目标骨架及关节轨道。
	 * @param[out] output 成功时接收包含动画轨道、关键帧及事件的 JSON 文本。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool WriteAnimationClip(const AnimationClip&, std::string&, std::string& error);
	/**
	 * @brief 编码动画片段。
	 *
	 * @param[in] value 要序列化的动画片段，包含目标骨架及关节轨道。
	 * @param[out] output 成功时接收以 CANI1 标记起始的二进制片段载荷，可含零字节。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool EncodeAnimationClip(const AnimationClip&, std::string&, std::string& error);
	/**
	 * @brief 读取网格与骨骼的绑定。
	 *
	 * @param[in] text 网格蒙皮绑定数据，接受作者 JSON 或以 CSKN1、CSKN2 开头的烘焙二进制载荷。
	 * @param[out] output 接收网格与骨骼的绑定结果。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ReadMeshSkinBinding(std::string_view, MeshSkinBinding&, std::string& error);
	/**
	 * @brief 写入网格与骨骼的绑定。
	 *
	 * @param[in] value 要序列化的网格蒙皮绑定，包含关节索引和顶点权重。
	 * @param[out] output 成功时接收包含网格、骨架及逐顶点影响权重的 JSON 文本。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool WriteMeshSkinBinding(const MeshSkinBinding&, std::string&, std::string& error);
	/**
	 * @brief 将网格蒙皮绑定编码为有界二进制载荷。
	 * Cooked 权重用有界二进制流；避免数十万顶点先膨胀为 JSON DOM 再解析。
	 *
	 * @param[in] value 经校验后要写出的骨骼绑定、逐顶点权重及可选逆绑定矩阵。
	 * @param[out] output 成功时写入 CSKN2 二进制载荷，内容可能含零字节。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 编码完成时为 true；数据不合法或编码失败时为 false。
	 */
	CATALPA_API bool EncodeMeshSkinBinding(const MeshSkinBinding&, std::string&, std::string& error);
	/**
	 * @brief 读取混合树。
	 *
	 * @param[in] text 描述混合节点及其依赖关系的混合树文档文本。
	 * @param[out] output 接收 BlendTreeAsset 结果。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ReadBlendTree(std::string_view, BlendTreeAsset&, std::string& error);
	/**
	 * @brief 写入混合树。
	 *
	 * @param[in] value 要写入文档的动画混合树。
	 * @param[out] output 成功时接收包含混合树节点与依赖引用的 JSON 文本。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool WriteBlendTree(const BlendTreeAsset&, std::string&, std::string& error);
	/**
	 * @brief 注册动画资产加载器。
	 *
	 * @param[in] assets 提供资产查找和版本访问的管理器。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool RegisterAnimationAssetLoaders(AssetManager&, std::string& error);
} /* 命名空间 Catalpa */
