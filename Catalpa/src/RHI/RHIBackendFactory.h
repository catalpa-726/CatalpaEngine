#pragma once

#include "RHIDevice.h"

namespace Catalpa {

	class Window;

	class RHIBackendFactory {
	public:
		using CreateFunction = std::unique_ptr<RHIDevice> (*)(Window&, const RHIBackendDesc&, RHIStatus&);

		RHIBackendFactory();
		~RHIBackendFactory();

		RHIBackendFactory(const RHIBackendFactory&) = delete;
		RHIBackendFactory& operator=(const RHIBackendFactory&) = delete;

		void Register(GraphicsBackend backend, CreateFunction create);
		/**
		 * @brief 根据配置选择已注册的渲染后端并为窗口创建设备。
		 *
		 * @param[in] desc 指定图形后端和诊断选项的启动配置。
		 * @param[in] window 承载设备呈现目标的应用窗口。
		 * @param[out] status 输出后端查找或设备初始化结果。
		 *
		 * @return 成功返回设备的独占所有权；后端未注册或设备创建失败时返回空指针，详情见 status。
		 */
		[[nodiscard]] std::unique_ptr<RHIDevice> Create(
		    const RHIBackendDesc& desc, Window& window, RHIStatus& status) const;

	private:
		struct Registry;
		std::unique_ptr<Registry> m_Registry;
	};

}