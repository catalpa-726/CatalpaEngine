#pragma once

#include "catpch.h"

#include "Core/Base.h"
#include "Core/IO/VirtualPath.h"

namespace Catalpa {
	class PackageReader;
	/*
	 * 由所属线程配置挂载表。只有挂载表不再变动时才允许并发读取。
	 * 根目录须为显式指定的绝对路径，不回退到当前目录或沙盒目录。
	 */
	class CATALPA_API VirtualFileSystem {
	public:
		void Swap(VirtualFileSystem& other) noexcept {
			m_Mounts.swap(other.m_Mounts);
			m_Packages.swap(other.m_Packages);
		}
		/**
		 * @brief 按优先级将绝对目录注册为只读挂载根。
		 * 同一挂载名下优先采用优先级更高的根目录；优先级相同或根目录重复时拒绝注册。
		 *
		 * @param[in] mount 虚拟路径中用于选择这一组只读根目录的挂载名。
		 * @param[in] absoluteRoot 挂载或解析使用的绝对根目录。
		 * @param[in] priority 同一挂载名下的搜索优先级，值较大者优先。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 挂载成功时为 true；根目录、优先级或挂载名无效时为 false。
		 */
		[[nodiscard]] bool MountReadOnly(
		    std::string_view mount, const std::filesystem::path& absoluteRoot, int priority, std::string& error);
		/**
		 * @brief 将资源包加入虚拟文件系统的查找范围。
		 *
		 * @param[in] package 提供挂载内容的资源包读取器。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		[[nodiscard]] bool MountPackage(std::shared_ptr<const PackageReader>, std::string& error);
		/**
		 * @brief 按挂载优先级在字节预算内读取文件，仅在文件缺失时尝试下一候选根。
		 * 仅当文件不存在时尝试下一挂载。访问失败、路径别名、路径越界或大小超限时停止查找。
		 * 分配前检查字节数上限，完整读取后才替换调用者的输出。
		 *
		 * @param[in] path 包含挂载点和相对位置的虚拟路径。
		 * @param[in] maxBytes 此次读取或解析允许接受的最大字节数。
		 * @param[out] output 成功时接收读取的完整文件字节，并由调用者持有。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 文件读取成功时为 true；找不到文件或遇到访问、路径、大小错误时为 false。
		 */
		[[nodiscard]] bool ReadFile(
		    const VirtualPath& path, size_t maxBytes, std::vector<std::byte>& output, std::string& error) const;

	private:
		struct Mount {
			std::string name;
			std::filesystem::path root;
			int priority = 0;
		};
		/* 先构造候选挂载表，发布时交换容器，避免在事务提交后分配。 */
		std::vector<Mount> m_Mounts;
		std::vector<std::shared_ptr<const PackageReader>> m_Packages;
	};
}
