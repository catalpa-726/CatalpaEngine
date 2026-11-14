#pragma once
#include "AssetPipeline/AssetBuildContext.h"

namespace Catalpa::Tools {
	/* 索引指向不可变构建版本。命中前校验输入、构建清单、来源报告和容器依赖。 */
	class AssetBuildCache final {
	public:
		/**
		 * @brief 按输入依赖、签名和内容哈希验证派生数据缓存。
		 * 验证失败视为未命中，保留调用者的上下文和原结果。
		 *
		 * @param[in,out] c 当前构建上下文，命中后接收已验证的输入记录。
		 * @param[in] signature 本次构建的完整签名文本，必须与缓存索引中的签名相同。
		 * @param[out] out 命中并核验成功后接收缓存中的构建结果。
		 * @param[out] error 失败时接收当前操作的具体原因。
		 *
		 * @return 索引、构建签名、输入依赖和缓存产物均验证通过时为 true，缓存未命中或无法验证时为 false。
		 */
		static bool Load(AssetBuildContext&, std::string_view signature, AssetBuildResult&, std::string& error);
		/**
		 * @brief 在构建产物发布后原子写入对应缓存索引。
		 * 主清单发布后更新索引，索引写入失败只影响下次缓存命中。
		 *
		 * @param[in] c 提供缓存目录和实际读取的构建输入记录。
		 * @param[in] signature 写入索引并供下次构建匹配的签名。
		 * @param[in] result 已发布的构建结果，供索引记录产物及定位信息。
		 * @param[out] error 失败时接收当前操作的具体原因。
		 *
		 * @return 缓存索引成功写入时为 true；写入失败时仍保留先前发布的构建产物。
		 */
		static bool Save(
		    const AssetBuildContext&, std::string_view signature, const AssetBuildResult&, std::string& error);
	};
}
