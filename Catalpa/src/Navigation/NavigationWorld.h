#pragma once
#include "Navigation/NavMeshAsset.h"
#include "Core/Jobs/CancellationToken.h"

namespace Catalpa {
	class CATALPA_API NavigationWorld final {
	public:
		/**
		 * @brief 初始化 NavigationWorld。
		 */
		NavigationWorld();
		~NavigationWorld();
		NavigationWorld(const NavigationWorld&) = delete;
		NavigationWorld& operator=(const NavigationWorld&) = delete;
		/**
		 * @brief 校验并加载指定版本的导航网格，供 Detour 路径查询。
		 *
		 * @param[in] asset 待安装的导航网格资产。
		 * @param[in] version 与本次加载绑定的资产版本号。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Load(std::shared_ptr<const NavMeshAsset>, uint64_t version, std::string& error);
		void Unload() noexcept;
		/**
		 * @brief 查找路径。
		 *
		 * @param[in] request 描述本次请求的 NavigationPathRequest。
		 * @param[in] cancellation 供轮询调用方取消请求的令牌。
		 * @param[out] output 接收 NavigationPath 结果。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool FindPath(
		    const NavigationPathRequest&, const CancellationToken&, NavigationPath&, std::string& error) const;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
} /* 命名空间 Catalpa */
