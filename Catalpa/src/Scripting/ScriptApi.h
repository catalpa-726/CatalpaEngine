#pragma once
#include "Scripting/ScriptCommands.h"
#include "Scene/Scene.h"
#include "Scripting/ScriptValue.h"
#include "Assets/AssetId.h"

namespace Catalpa {
    // 仅在当前 owner 线程调用期间借用，禁止脚本长期保存 Scene 或组件地址。
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
        bool SetTranslation(Math::Vec3 value, std::string& error) { return commands.SetTranslation(scene, self, value, error); }
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
        bool AsyncChecksum(std::string_view text, std::string_view callback, uint64_t& output,
                           std::string& error) {
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
