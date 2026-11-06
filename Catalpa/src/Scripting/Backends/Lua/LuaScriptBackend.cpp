#include "catpch.h"
#include "Scripting/Backends/Lua/LuaScriptBackend.h"
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
#include <cstdlib>
#include <cfloat>
#include <cstring>

namespace Catalpa {
    struct LuaScriptBackend::State {
        static constexpr size_t MemoryLimit = 8 * 1024 * 1024;
        size_t bytes = 0;
        std::shared_ptr<ScriptMemoryBudget> budget;
        lua_State* vm = nullptr;
        ScriptApi* api = nullptr;
        int module = LUA_NOREF;
        uint32_t instructions = 0;
        std::thread::id owner = std::this_thread::get_id();
        std::string diagnostic;
        std::string scratch, event;
        double seconds = 0;
        std::map<std::string, std::shared_ptr<const ScriptAsset>, std::less<>> modules;
        std::set<std::string, std::less<>> loading;
        // Lua 以 longjmp 退出 C 回调；会分配的 Lua 调用之间不能保留 STL 迭代器等 RAII 局部量。
        struct Property {
            std::string name;
            ScriptValue value;
        };
        std::vector<Property> properties;

        static void* Allocate(void* user, void* old, size_t oldSize, size_t size) {
            auto& state = *static_cast<State*>(user);
            if (std::this_thread::get_id() != state.budget->owner) return nullptr;
            if (!old) oldSize = 0;
            if (!size) {
                std::free(old);
                state.bytes -= oldSize;
                state.budget->used -= oldSize;
                return nullptr;
            }
            if (size > MemoryLimit || state.bytes - oldSize > MemoryLimit - size ||
                size > state.budget->limit || state.budget->used - oldSize > state.budget->limit - size) return nullptr;
            void* next = std::realloc(old, size);
            if (next) {
                state.bytes = state.bytes - oldSize + size;
                state.budget->used = state.budget->used - oldSize + size;
            }
            return next;
        }

        static State& Get(lua_State* vm) { return **static_cast<State**>(lua_getextraspace(vm)); }

        static void Budget(lua_State* vm, lua_Debug*) {
            auto& state = Get(vm);
            if (state.instructions <= 100) luaL_error(vm, "Lua instruction budget exhausted");
            state.instructions -= 100;
        }

        static int Translate(lua_State* vm) {
            auto& state = Get(vm);
            int x = 0, y = 0, z = 0;
            const double a = lua_tonumberx(vm, 1, &x), b = lua_tonumberx(vm, 2, &y), c = lua_tonumberx(vm, 3, &z);
            bool accepted = false;
            if (state.api && x && y && z && std::isfinite(a) && std::isfinite(b) && std::isfinite(c) &&
                std::abs(a) <= FLT_MAX && std::abs(b) <= FLT_MAX && std::abs(c) <= FLT_MAX &&
                (a == 0 || float(a) != 0) && (b == 0 || float(b) != 0) && (c == 0 || float(c) != 0)) {
                try { accepted = state.api->Translate({float(a), float(b), float(c)}, state.diagnostic); }
                catch (...) { state.diagnostic = "Cannot allocate script command"; }
            } else state.diagnostic = "scene.translate requires three finite coordinates";
            if (!accepted) return luaL_error(vm, "%s", state.diagnostic.c_str());
            return 0;
        }

        static int SetTranslation(lua_State* vm) {
            auto& state = Get(vm);
            int x = 0, y = 0, z = 0;
            const double a = lua_tonumberx(vm, 1, &x), b = lua_tonumberx(vm, 2, &y), c = lua_tonumberx(vm, 3, &z);
            bool accepted = false;
            if (state.api && x && y && z && std::isfinite(a) && std::isfinite(b) && std::isfinite(c) &&
                std::abs(a) <= FLT_MAX && std::abs(b) <= FLT_MAX && std::abs(c) <= FLT_MAX &&
                (a == 0 || float(a) != 0) && (b == 0 || float(b) != 0) && (c == 0 || float(c) != 0)) {
                try { accepted = state.api->SetTranslation({float(a), float(b), float(c)}, state.diagnostic); }
                catch (...) { state.diagnostic = "Cannot allocate script command"; }
            } else state.diagnostic = "scene.set_translation requires three finite coordinates";
            if (!accepted) return luaL_error(vm, "%s", state.diagnostic.c_str());
            return 0;
        }

        static int Position(lua_State* vm) {
            auto& state = Get(vm);
            Math::Vec3 position;
            if (!state.api || !state.api->commands.ReadTranslation(state.api->scene,state.api->self,position,state.diagnostic))
                return luaL_error(vm, "Script entity expired");
            lua_pushnumber(vm, position.x);
            lua_pushnumber(vm, position.y);
            lua_pushnumber(vm, position.z);
            return 3;
        }

        static int Require(lua_State* vm) {
            auto& state = Get(vm);
            size_t nameSize = 0;
            const char* name = luaL_checklstring(vm, 1, &nameSize);
            if (!nameSize || nameSize > 128 || std::memchr(name, '\0', nameSize))
                return luaL_error(vm, "Invalid declared module name");
            lua_getfield(vm, LUA_REGISTRYINDEX, "Catalpa.modules");
            lua_getfield(vm, -1, name);
            if (!lua_isnil(vm, -1)) return 1;
            lua_pop(vm, 1);
            const ScriptAsset* module = nullptr;
            {
                const auto found = state.modules.find(name);
                if (found != state.modules.end()) module = found->second.get();
            }
            if (!module) return luaL_error(vm, "Undeclared script module: %s", name);
            if (state.loading.contains(name)) return luaL_error(vm, "Script module cycle: %s", name);
            bool reserved = false;
            try { reserved = state.loading.emplace(name).second; } catch (...) {}
            if (!reserved) return luaL_error(vm, "Cannot reserve script module");
            const auto& source = module->source;
            int status = luaL_loadbufferx(vm, source.data(), source.size(), name, "t");
            if (status == LUA_OK) status = lua_pcall(vm, 0, 1, 0);
            state.loading.erase(state.loading.find(name));
            if (status != LUA_OK) return lua_error(vm);
            if (!lua_istable(vm, -1)) return luaL_error(vm, "Module must return a table: %s", name);
            lua_pushvalue(vm, -1);
            lua_setfield(vm, -3, name);
            return 1;
        }

        static int Bootstrap(lua_State* vm) {
            luaL_requiref(vm, "_G", luaopen_base, 1);
            lua_pop(vm, 1);
            const char* restricted[]{"load", "loadfile", "dofile", "collectgarbage", "pcall", "xpcall",
                                     "setmetatable", "getmetatable", "print"};
            for (const char* name : restricted) { lua_pushnil(vm); lua_setglobal(vm, name); }
            luaL_requiref(vm, LUA_MATHLIBNAME, luaopen_math, 1);
            luaL_requiref(vm, LUA_STRLIBNAME, luaopen_string, 1);
            // C 实现的模式回溯不经过 Lua instruction hook；本阶段只开放有界的基础字符串操作。
            for (const char* name : {"find", "match", "gmatch", "gsub", "dump"}) {
                lua_pushnil(vm);
                lua_setfield(vm, -2, name);
            }
            luaL_requiref(vm, LUA_TABLIBNAME, luaopen_table, 1);
            luaL_requiref(vm, LUA_UTF8LIBNAME, luaopen_utf8, 1);
            lua_pop(vm, 4);
            lua_pushcfunction(vm, Require);
            lua_setglobal(vm, "require");
            lua_newtable(vm);
            lua_setfield(vm, LUA_REGISTRYINDEX, "Catalpa.modules");
            lua_newtable(vm);
            lua_pushcfunction(vm, Translate);
            lua_setfield(vm, -2, "translate");
            lua_pushcfunction(vm, SetTranslation);
            lua_setfield(vm, -2, "set_translation");
            lua_pushcfunction(vm, Position);
            lua_setfield(vm, -2, "position");
            lua_setglobal(vm, "scene");
            return 0;
        }

        static int SetSelf(lua_State* vm) {
            const auto& text = Get(vm).scratch;
            lua_pushlstring(vm, text.data(), text.size());
            lua_setglobal(vm, "self");
            return 0;
        }

        static int SetProperties(lua_State* vm) {
            const auto& values = Get(vm).properties;
            lua_newtable(vm);
            // 使用索引与借用指针；Lua OOM 不会跳过 MSVC Debug 迭代器的析构。
            for (size_t index = 0; index < values.size(); ++index) {
                const auto& property = values[index];
                if (const auto* value = std::get_if<bool>(&property.value)) lua_pushboolean(vm, *value);
                else if (const auto* value = std::get_if<int64_t>(&property.value)) lua_pushinteger(vm, *value);
                else if (const auto* value = std::get_if<double>(&property.value)) lua_pushnumber(vm, *value);
                else if (const auto* value = std::get_if<std::string>(&property.value))
                    lua_pushlstring(vm, value->data(), value->size());
                else if (const auto* value = std::get_if<Math::Vec3>(&property.value)) {
                    lua_createtable(vm, 3, 0);
                    for (int coordinate = 0; coordinate < 3; ++coordinate) {
                        lua_pushnumber(vm, (*value)[coordinate]);
                        lua_rawseti(vm, -2, coordinate + 1);
                    }
                } else return luaL_error(vm, "Unsupported prepared property type");
                lua_setfield(vm, -2, property.name.c_str());
            }
            lua_setglobal(vm, "properties");
            return 0;
        }

        static int RetainModule(lua_State* vm) {
            lua_pushvalue(vm, 1);
            Get(vm).module = luaL_ref(vm, LUA_REGISTRYINDEX);
            return 0;
        }

        static int Dispatch(lua_State* vm) {
            auto& state = Get(vm);
            lua_rawgeti(vm, LUA_REGISTRYINDEX, state.module);
            lua_getfield(vm, -1, state.event.c_str());
            if (lua_isnil(vm, -1)) return 0;
            if (!lua_isfunction(vm, -1)) return luaL_error(vm, "Lifecycle field is not a function: %s", state.event.c_str());
            lua_pushnumber(vm, state.seconds);
            lua_call(vm, 1, 0);
            return 0;
        }

        explicit State(std::shared_ptr<ScriptMemoryBudget> shared = {}) : budget(shared ? std::move(shared) : std::make_shared<ScriptMemoryBudget>()) {
            diagnostic.reserve(1024);
            scratch.reserve(128);
            vm = lua_newstate(Allocate, this, 0);
            if (vm) *static_cast<State**>(lua_getextraspace(vm)) = this;
        }
        ~State() {
            if (std::this_thread::get_id() != owner) std::terminate();
            if (vm) lua_close(vm);
        }

        bool Protected(int args, int results, ScriptApi* borrowed, std::string& error) {
            api = borrowed;
            instructions = 100000;
            lua_sethook(vm, Budget, LUA_MASKCOUNT, 100);
            const int result = lua_pcall(vm, args, results, 0);
            lua_sethook(vm, nullptr, 0, 0);
            api = nullptr;
            if (result != LUA_OK) {
                const char* message = lua_type(vm,-1)==LUA_TSTRING ? lua_tostring(vm, -1) : nullptr;
                error = message ? message : "Lua VM error";
                lua_pop(vm, 1);
                if (borrowed) borrowed->commands.Clear();
                return false;
            }
            return true;
        }
    };

    LuaScriptBackend::LuaScriptBackend() : m_State(std::make_unique<State>()) {}
    LuaScriptBackend::LuaScriptBackend(std::shared_ptr<ScriptMemoryBudget> budget)
        : m_State(std::make_unique<State>(std::move(budget))) {}
    LuaScriptBackend::~LuaScriptBackend() = default;

    bool LuaScriptBackend::SetModules(std::map<std::string, std::shared_ptr<const ScriptAsset>, std::less<>> modules,
                                       std::string& error) {
        if (std::this_thread::get_id() != m_State->owner || modules.size() > 256 || m_State->module != LUA_NOREF) {
            error = "Module set must be bounded and installed before starting the script";
            return false;
        }
        for (const auto& [name, module] : modules) {
            if (name.empty() || name.size() > 128 || name.find('\0') != std::string::npos || !module ||
                module->source.size() > 1024 * 1024 || module->source.find('\0') != std::string::npos) {
                error = "Invalid declared Lua module name/source";
                return false;
            }
        }
        m_State->modules = std::move(modules);
        return true;
    }

    bool LuaScriptBackend::CheckSource(std::string_view source, std::string& error) {
        State state;
        if (!state.vm || source.size() > 1024 * 1024) { error = "Script exceeds the compile budget"; return false; }
        if (luaL_loadbufferx(state.vm, source.data(), source.size(), "author script", "t") != LUA_OK) {
            const char* message = lua_type(state.vm,-1)==LUA_TSTRING ? lua_tostring(state.vm, -1) : nullptr;
            error = message ? message : "Lua parse failed";
            return false;
        }
        error.clear();
        return true;
    }

    bool LuaScriptBackend::Start(const ScriptAsset& asset, const ScriptValues& values, ScriptApi& api,
                                   std::string& error) {
        if(asset.source.size()>1024*1024 || asset.source.find('\0')!=asset.source.npos ||
            (!asset.source.empty() && asset.source.front()=='\x1b') || !ValidateScriptValues(values,error)) {
            if(error.empty()) error="Invalid bounded Lua source or properties";return false;
        }
        auto& state = *m_State;
        if (std::this_thread::get_id() != state.owner || !state.vm || state.module != LUA_NOREF) {
            error = "Invalid Lua owner or lifecycle";
            return false;
        }
        lua_pushcfunction(state.vm, State::Bootstrap);
        if (!state.Protected(0, 0, nullptr, error)) return false;
        // C++ 复制/身份格式化先完成，再进入只含平凡局部量的受保护 Lua 回调。
        state.properties.clear();
        state.properties.reserve(values.size());
        for (const auto& [name, value] : values) {
            State::Property property{name, value};
            if (const auto* entity = std::get_if<PersistentEntityId>(&value)) property.value = entity->ToString();
            state.properties.push_back(std::move(property));
        }
        lua_pushcfunction(state.vm, State::SetProperties);
        const bool copied = state.Protected(0, 0, nullptr, error);
        state.properties.clear();
        if (!copied) return false;
        state.scratch = api.self.persistent.ToString();
        lua_pushcfunction(state.vm, State::SetSelf);
        if (!state.Protected(0, 0, nullptr, error)) return false;
        if (luaL_loadbufferx(state.vm, asset.source.data(), asset.source.size(), state.scratch.c_str(), "t") != LUA_OK) {
            const char* message=lua_type(state.vm,-1)==LUA_TSTRING?lua_tostring(state.vm,-1):nullptr;
            error=message?message:"Lua source compilation failed";
            lua_pop(state.vm, 1);
            return false;
        }
        if (!state.Protected(0, 1, &api, error)) return false;
        if (!lua_istable(state.vm, -1)) {
            lua_pop(state.vm, 1);
            api.commands.Clear();
            error = "Script must return a lifecycle table with optional start/update/stop functions";
            return false;
        }
        lua_pushcfunction(state.vm, State::RetainModule);
        lua_insert(state.vm, -2);
        if (!state.Protected(1, 0, &api, error)) return false;
        return Call("start", 0, api, error);
    }

    bool LuaScriptBackend::Call(std::string_view event, double seconds, ScriptApi& api, std::string& error) {
        auto& state = *m_State;
        if (std::this_thread::get_id() != state.owner || !state.vm || state.module == LUA_NOREF ||
            !std::isfinite(seconds) || seconds < 0 || (event!="start" && event!="update" && event!="stop")) {
            error = "Invalid Lua call owner, time or lifecycle";
            return false;
        }
        state.event.assign(event);
        state.seconds = seconds;
        lua_pushcfunction(state.vm, State::Dispatch);
        return state.Protected(0, 0, &api, error);
    }
}
