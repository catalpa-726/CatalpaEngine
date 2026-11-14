#pragma once
#include "Scripting/ScriptAsset.h"
#include "Scripting/ScriptApi.h"
#include "Scripting/ScriptValue.h"

namespace Catalpa {
	class IScriptBackend {
	public:
		virtual ~IScriptBackend() = default;
		/**
		 * @brief 根据脚本资产创建脚本实例，并用暴露值和宿主 API 完成启动。
		 * @details 第 1 个参数（const ScriptAsset&）为输入。待启动的脚本资产。
		 * @details 第 2 个参数（const ScriptValues&）为输入。传给脚本的组件属性。
		 * @details 第 3 个参数（ScriptApi&）为输入。在此次脚本调用期间提供实体操作、事件和异步请求的宿主接口。
		 *
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 操作成功时为 true，失败时为 false。
		 */
		virtual bool Start(const ScriptAsset&, const ScriptValues&, ScriptApi&, std::string& error) = 0;
		/**
		 * @brief 将指定生命周期回调派发给已启动脚本。
		 * @details 第 3 个参数（ScriptApi&）为输入。在此次脚本调用期间提供实体操作、事件和异步请求的宿主接口。
		 *
		 * @param[in] event 要触发的生命周期回调名。
		 * @param[in] seconds 本次更新的时间量（秒）。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 操作成功时为 true，失败时为 false。
		 */
		virtual bool Call(std::string_view event, double seconds, ScriptApi&, std::string& error) = 0;
		/**
		 * @brief 向脚本派发指定通道及其事件参数。
		 * @details 第 2 个参数（const ScriptValues&）为输入。随事件传给脚本的参数。
		 * @details 第 3 个参数（ScriptApi&）为输入。在此次脚本调用期间提供实体操作、事件和异步请求的宿主接口。
		 *
		 * @param[in] channel 事件通道或处理回调名。
		 * @param[out] error 失败时接收具体诊断。
		 *
		 * @return 操作成功时为 true，失败时为 false。
		 */
		virtual bool Event(std::string_view channel, const ScriptValues&, ScriptApi&, std::string& error) = 0;
		/**
		 * @brief 导出状态。
		 * @details 第 2 个参数（ScriptValues&）为输入。本次操作使用的脚本值。
		 *
		 * @param[in] schema 数据结构或组件的模式定义。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 操作成功时为 true，失败时为 false。
		 */
		virtual bool ExportState(uint32_t& schema, ScriptValues&, std::string& error) = 0;
		/**
		 * @brief 导入状态。
		 * @details 第 2 个参数（const ScriptValues&）为输入。本次操作使用的脚本值。
		 * @details 第 3 个参数（ScriptApi&）为输入。在此次脚本调用期间提供实体操作、事件和异步请求的宿主接口。
		 *
		 * @param[in] schema 数据结构或组件的模式定义。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 操作成功时为 true，失败时为 false。
		 */
		virtual bool ImportState(uint32_t schema, const ScriptValues&, ScriptApi&, std::string& error) = 0;
	};
}
