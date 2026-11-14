#pragma once
#include "Scripting/Backends/IScriptBackend.h"

namespace Catalpa {
	struct ScriptMemoryBudget {
		size_t used = 0, limit = 64 * 1024 * 1024;
		std::thread::id owner = std::this_thread::get_id();
	};
	/* Lua C API、虚拟机内存和钩子由后端实现管理；外部只移交独占的作者数据。 */
	class CATALPA_API LuaScriptBackend final : public IScriptBackend {
	public:
		LuaScriptBackend();
		explicit LuaScriptBackend(std::shared_ptr<ScriptMemoryBudget>);
		~LuaScriptBackend() override;
		LuaScriptBackend(const LuaScriptBackend&) = delete;
		LuaScriptBackend& operator=(const LuaScriptBackend&) = delete;
		/**
		 * @brief 根据脚本资产创建脚本实例，并用暴露值和宿主 API 完成启动。
		 *
		 * @param[in] asset 待启动的脚本资产。
		 * @param[in] values 传给脚本的组件属性。
		 * @param[in] api 在此次脚本调用期间提供实体操作、事件和异步请求的宿主接口。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Start(const ScriptAsset&, const ScriptValues&, ScriptApi&, std::string&) override;
		/**
		 * @brief 将指定生命周期回调派发给已启动脚本。
		 *
		 * @param[in] event 要触发的生命周期回调名。
		 * @param[in] seconds 本次更新的时间量（秒）。
		 * @param[in] api 在此次脚本调用期间提供实体操作、事件和异步请求的宿主接口。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Call(std::string_view, double, ScriptApi&, std::string&) override;
		/**
		 * @brief 向脚本派发指定通道及其事件参数。
		 *
		 * @param[in] channel 事件通道或处理回调名。
		 * @param[in] values 随事件传给脚本的参数。
		 * @param[in] api 在此次脚本调用期间提供实体操作、事件和异步请求的宿主接口。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Event(std::string_view, const ScriptValues&, ScriptApi&, std::string&) override;
		/**
		 * @brief 导出状态。
		 *
		 * @param[out] schema 接收导出脚本状态的模式版本号。
		 * @param[out] values 接收从 Lua 实例导出的命名状态值。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool ExportState(uint32_t&, ScriptValues&, std::string&) override;
		/**
		 * @brief 导入状态。
		 *
		 * @param[in] schema 数据结构或组件的模式定义。
		 * @param[in] values 将要恢复到 Lua 实例中的命名状态值。
		 * @param[in] api 在此次脚本调用期间提供实体操作、事件和异步请求的宿主接口。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool ImportState(uint32_t, const ScriptValues&, ScriptApi&, std::string&) override;
		/**
		 * @brief 设置模块。
		 *
		 * @param[in] modules 以模块名称为键的脚本资产表，供脚本解析模块引用。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool SetModules(std::map<std::string, std::shared_ptr<const ScriptAsset>, std::less<>>, std::string&);
		/**
		 * @brief 检查 Lua 源文本能否由当前后端接受。
		 *
		 * @param[in] source 待检查的 Lua 源码。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		static bool CheckSource(std::string_view source, std::string& error);

	private:
		struct State;
		std::unique_ptr<State> m_State;
	};
}
