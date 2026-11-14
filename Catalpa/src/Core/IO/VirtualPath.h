#pragma once

#include "catpch.h"

#include "Core/Base.h"

namespace Catalpa {
	/*
	 * 虚拟路径用于定位文件。UTF-8 路径 mount:/relative 保留实际文件名的大小写。
	 * 解析时拒绝点路径段、反斜杠、DOS 设备名、备用数据流，以及末尾为点或空格的别名。
	 * 挂载名、相对路径和完整路径一起更新，分配失败时仍表示同一位置。
	 */
	class CATALPA_API VirtualPath {
	public:
		VirtualPath() = default;
		VirtualPath(const VirtualPath&) = default;
		VirtualPath(VirtualPath&& other) noexcept { Swap(other); }
		VirtualPath& operator=(const VirtualPath& other) {
			if (this != &other) {
				VirtualPath candidate(other);
				Swap(candidate);
			}
			return *this;
		}
		VirtualPath& operator=(VirtualPath&& other) noexcept {
			if (this != &other) {
				VirtualPath candidate(std::move(other));
				Swap(candidate);
			}
			return *this;
		}
		/**
		 * @brief 解析并校验带挂载名的虚拟路径。
		 * 解析带挂载名的虚拟路径，拒绝非法片段或越界形式。
		 *
		 * @param[in] text 待解析的虚拟路径文本。
		 * @param[out] output 成功时写入验证后的虚拟路径。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 成功生成合法虚拟路径时为 true，否则为 false。
		 */
		[[nodiscard]] static bool Parse(std::string_view text, VirtualPath& output, std::string& error);
		[[nodiscard]] bool HasValue() const noexcept { return !m_Text.empty(); }
		[[nodiscard]] const std::string& ToString() const noexcept { return m_Text; }
		[[nodiscard]] const std::string& MountName() const noexcept { return m_Mount; }
		[[nodiscard]] const std::string& RelativePath() const noexcept { return m_Relative; }
		bool operator==(const VirtualPath&) const = default;

	private:
		void Swap(VirtualPath& other) noexcept {
			m_Text.swap(other.m_Text);
			m_Mount.swap(other.m_Mount);
			m_Relative.swap(other.m_Relative);
		}
		std::string m_Text;
		std::string m_Mount;
		std::string m_Relative;
	};
}
