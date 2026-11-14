#pragma once
#include "Assets/PackageFormat.h"

namespace Catalpa {
	/* 完整验证后才发布的只读快照，可跨读任务共享；不提取到源目录或临时目录。 */
	class CATALPA_API PackageReader final {
	public:
		/**
		 * @brief 从绝对文件路径打开并验证只读运行包。
		 *
		 * @param[in] absoluteFile 包文件的绝对路径。
		 * @param[out] out 成功时写入共享只读包。
		 * @param[out] error 失败时接收具体诊断。
		 * @param[in] limits 读取与解码允许的包大小及条目上限。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool Open(const std::filesystem::path& absoluteFile, std::shared_ptr<const PackageReader>&,
		    std::string& error, PackageLimits limits = {});
		/**
		 * @brief 在容量限制内校验包字节、目录和入口资产，再发布只读快照。
		 *
		 * @param[in] bytes 待接管的完整包字节。
		 * @param[out] out 接收经过验证的只读包。
		 * @param[out] error 失败时接收具体诊断。
		 * @param[in] limits 包大小、条目与内容限制。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool Decode(std::vector<std::byte>, std::shared_ptr<const PackageReader>&, std::string& error,
		    PackageLimits limits = {});
		/**
		 * @brief 按虚拟路径读取包内条目，并检查调用方的字节限制。
		 *
		 * @param[in] path 要读取的包内虚拟路径。
		 * @param[in] maxBytes 允许交付的最大字节数。
		 * @param[out] out 接收条目内容。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Read(const VirtualPath&, size_t maxBytes, std::vector<std::byte>&, std::string& error) const;
		const AssetRegistry& Catalog() const noexcept { return m_Catalog; }
		bool OwnsMount(std::string_view mount) const noexcept { return m_Mounts.contains(mount); }
		size_t StorageBytes() const noexcept { return m_Bytes.capacity(); }
		AssetId EntryScene() const noexcept { return m_EntryScene; }

	private:
		AssetRegistry m_Catalog;
		AssetId m_EntryScene;
		std::set<std::string, std::less<>> m_Mounts;
		std::vector<std::byte> m_Bytes;
		std::map<std::string, PackageEntry, std::less<>> m_Entries;
	};
} /* 命名空间 Catalpa */
