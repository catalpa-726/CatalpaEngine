#pragma once
#include "Documents/Document.h"

namespace Catalpa::Editor {
	/* 原子保存只写项目内容根；恢复副本写独立目录，不把源素材当作者文件覆盖。 */
	class DocumentRecovery {
	public:
		static constexpr size_t MaxAuthorBytes = 16 * 1024 * 1024;
		static constexpr size_t MaxAnimationBytes = 96 * 1024 * 1024;
		/* 大动画和二进制使用 base64；128 MiB 正文编码另预留包装空间，旧文本恢复仍兼容。 */
		static constexpr size_t MaxRecoveryBytes = 132 * 1024 * 1024;
		static size_t AuthorLimit(std::string_view type) noexcept;
		/**
		 * @brief 返回已知文档类型对应的作者文件后缀。
		 *
		 * @param[in] type 用于查询作者文件后缀的文档类型键。
		 *
		 * @return 不带前导点的作者文件后缀，未知文档类型返回空视图。
		 */
		static std::string_view AuthorSuffix(std::string_view type) noexcept;
		/**
		 * @brief 根据扩展名及必要的源内容识别作者文档类型。
		 *
		 * @param[in] extension 源文件的扩展名，用于判断作者文档类型。
		 * @param[in] source 用于区分材质定义和实例的作者文档内容。
		 *
		 * @return 作者文档的类型键；材质按源文件的 kind 区分，无法识别的扩展名返回空字符串。
		 */
		static std::string AuthorType(std::string_view extension, std::string_view source);
		/**
		 * @brief 校验创作源。
		 *
		 * @param[in] type 指定源文本应按哪种作者文档格式进行校验。
		 * @param[in] source 待按对应作者文档格式校验的源文本。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool ValidateAuthorSource(std::string_view type, std::string_view source, std::string& error);
		/**
		 * @brief 解码检查点。
		 *
		 * @param[in] envelope 包含文档类型和原始作者文本的恢复文件内容。
		 * @param[out] type 接收从恢复文件提取的作者文档类型键。
		 * @param[out] content 接收恢复文件中的原始作者文档文本。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool DecodeCheckpoint(
		    std::string_view envelope, std::string& type, std::string& content, std::string& error);
		/**
		 * @brief 校验内容根与目标路径后保存文档。
		 *
		 * @param[in,out] doc 当前编辑的文档。
		 * @param[in] target 目标文件系统路径。
		 * @param[in] contentRoot 约束作者文件保存位置的项目内容根目录。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool Save(Document&, const std::filesystem::path& target, const std::filesystem::path& contentRoot,
		    std::string& error);
		/**
		 * @brief 在独立恢复目录中记录文档快照。
		 *
		 * @param[in] doc 当前编辑的文档。
		 * @param[in] recoveryRoot 根目录路径。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool Checkpoint(const Document&, const std::filesystem::path& recoveryRoot, std::string& error);
		/**
		 * @brief 在给定字节上限内读取文档内容。
		 *
		 * @param[in] path 文件系统中的目标路径。
		 * @param[out] output 接收按文件字节完整读取的作者文本。
		 * @param[out] error 失败时接收原因说明。
		 * @param[in] maxBytes 此次读取或解析允许接受的最大字节数。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool Read(
		    const std::filesystem::path&, std::string&, std::string& error, size_t maxBytes = MaxAuthorBytes);
	};
}
