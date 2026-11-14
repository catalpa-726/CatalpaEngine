#pragma once
#include "Scripting/ScriptCommands.h"
#include "Scene/Scene.h"
#include "Scripting/ScriptValue.h"
#include "Assets/AssetId.h"

namespace Catalpa {
	/* 借用仅在所属线程的本次调用期间有效，脚本不得长期保存场景或组件地址。 */
	struct ScriptApi {
		Scene& scene;
		EntityReference self;
		ScriptCommands commands;
		struct Emission {
			std::string channel;
			ScriptValues values;
		};
		struct AsyncRequest {
			uint64_t ticket = 0;
			std::string text, callback;
		};
		std::vector<Emission> emissions;
		std::vector<AsyncRequest> asyncRequests;
		std::vector<uint64_t> cancelledAsync;
		std::optional<AssetId> requestedScene;
		std::function<uint64_t()> reserveAsync;
		bool reloading = false;
		bool Translate(Math::Vec3 value, std::string& error) { return commands.Translate(scene, self, value, error); }
		bool SetTranslation(Math::Vec3 value, std::string& error) {
			return commands.SetTranslation(scene, self, value, error);
		}
		/**
		 * @brief 校验事件主题与数据后提交发送请求。
		 *
		 * @param[in] channel 用于路由脚本事件的通道名称。
		 * @param[in] value 随该脚本事件传递的值。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Emit(std::string_view channel, std::string_view value, std::string& error) {
			if (channel.empty() || channel.size() > 128 || channel.front() == '@' ||
			    channel.find('\0') != channel.npos || value.size() > 65536 || emissions.size() >= 64) {
				error = "Script event topic/value/queue bound rejected";
				return false;
			}
			ScriptValues values;
			values["value"] = std::string(value);
			emissions.push_back({std::string(channel), std::move(values)});
			return true;
		}
		/**
		 * @brief 为给定文本申请异步校验作业及回调。
		 *
		 * @param[in] text 用于后台计算校验和的输入文本。
		 * @param[in] callback 接收异步校验结果的脚本回调名称。
		 * @param[out] output 接收64 位无符号整数结果。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool AsyncChecksum(std::string_view text, std::string_view callback, uint64_t& output, std::string& error) {
			if (!reserveAsync || text.size() > 65536 || callback.empty() || callback.size() > 128 ||
			    asyncRequests.size() >= 16) {
				error = "Script async continuation requires Jobs and bounded input/callback";
				return false;
			}
			const auto ticket = reserveAsync();
			if (!ticket) {
				error = "Script async ticket budget exhausted";
				return false;
			}
			asyncRequests.push_back({ticket, std::string(text), std::string(callback)});
			output = ticket;
			return true;
		}
	};
}
