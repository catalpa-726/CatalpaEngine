#pragma once
#include "Scripting/ScriptCommands.h"
#include "Scene/Scene.h"

namespace Catalpa {
    // 仅在当前 owner 线程调用期间借用，禁止脚本长期保存 Scene 或组件地址。
    struct ScriptApi {
        Scene& scene;
        EntityReference self;
        ScriptCommands commands;
        bool Translate(Math::Vec3 value, std::string& error) { return commands.Translate(scene, self, value, error); }
        bool SetTranslation(Math::Vec3 value, std::string& error) { return commands.SetTranslation(scene, self, value, error); }
    };
}
