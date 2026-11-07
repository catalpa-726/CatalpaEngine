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
        bool requiredCallback = false;
        std::map<std::string, std::shared_ptr<const ScriptAsset>, std::less<>> modules;
        std::set<std::string, std::less<>> loading;
        std::map<std::string, std::string, std::less<>> subscriptions;
        struct Timer {
            double remaining = 0;
            std::string callback;
        };
        std::map<uint64_t, Timer> timers;
        uint64_t nextTimer = 1;
        uint32_t stateSchema = 1, importSchema = 1;
        ScriptValues exported;
        // Lua 以 longjmp 退出 C 回调；会分配的 Lua 调用之间不能保留 STL 迭代器等 RAII 局部量。
        struct Property {
            std::string name;
            ScriptValue value;
        };
        std::vector<Property> properties;
        static bool CallbackName(std::string_view name) {
            return !name.empty() && name.size() <= 128 && std::all_of(name.begin(), name.end(), [](char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
            });
        }

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

        static int CreateEntity(lua_State* vm) {
            size_t length = 0;
            const char* name = luaL_checklstring(vm, 1, &length);
            const double x = luaL_optnumber(vm, 2, 0), y = luaL_optnumber(vm, 3, 0),
                         z = luaL_optnumber(vm, 4, 0);
            auto& state = Get(vm);
            bool ok = false;
            try {
                Math::TransformTRS pose;
                pose.translation = {float(x), float(y), float(z)};
                PersistentEntityId id;
                if (std::isfinite(x) && std::isfinite(y) && std::isfinite(z) && std::abs(x) <= FLT_MAX &&
                    std::abs(y) <= FLT_MAX && std::abs(z) <= FLT_MAX && state.api)
                    ok = state.api->commands.Create(state.api->scene, std::string_view(name, length), pose,
                                                    id, state.diagnostic);
                if (ok)
                    state.scratch = id.ToString();
                else if (state.diagnostic.empty())
                    state.diagnostic = "Invalid entity creation coordinates";
            } catch (...) {
                state.diagnostic = "Cannot reserve script entity creation";
            }
            if (!ok)
                return luaL_error(vm, "%s", state.diagnostic.c_str());
            lua_pushlstring(vm, state.scratch.data(), state.scratch.size());
            return 1;
        }
        static int DestroyEntity(lua_State* vm) {
            size_t length = 0;
            const char* id = luaL_checklstring(vm, 1, &length);
            auto& state = Get(vm);
            bool ok = false;
            try {
                PersistentEntityId target;
                if (state.api && PersistentEntityId::TryParse(std::string_view(id, length), target))
                    ok = state.api->commands.Destroy(state.api->scene, target, state.diagnostic);
            } catch (...) {
                state.diagnostic = "Cannot reserve script entity deletion";
            }
            if (!ok)
                return luaL_error(vm, "%s",
                                  state.diagnostic.empty() ? "Invalid script entity identity"
                                                           : state.diagnostic.c_str());
            return 0;
        }
        static int Subscribe(lua_State* vm) {
            size_t a = 0, b = 0;
            const char* topic = luaL_checklstring(vm, 1, &a);
            const char* callback = luaL_checklstring(vm, 2, &b);
            auto& state = Get(vm);
            bool ok = false;
            if (a && a <= 128 && topic[0] != '@' && !std::memchr(topic, 0, a) &&
                CallbackName(std::string_view(callback, b)) &&
                (state.subscriptions.size() < 128 ||
                 state.subscriptions.contains(std::string_view(topic, a))))
                try {
                    state.subscriptions.insert_or_assign(std::string(topic, a), std::string(callback, b));
                    ok = true;
                } catch (...) {
                    state.diagnostic = "Cannot reserve script subscription";
                }
            if (!ok)
                return luaL_error(vm, "Invalid or exhausted script event subscription");
            return 0;
        }
        static int Unsubscribe(lua_State* vm) {
            size_t size = 0;
            const char* topic = luaL_checklstring(vm, 1, &size);
            auto& state = Get(vm);
            if (size > 128)
                return luaL_error(vm, "Invalid script event topic");
            try {
                state.subscriptions.erase(std::string(topic, size));
            } catch (...) {
                return luaL_error(vm, "Cannot remove event subscription");
            }
            return 0;
        }
        static int Emit(lua_State* vm) {
            size_t a = 0, b = 0;
            const char* topic = luaL_checklstring(vm, 1, &a);
            const char* value = luaL_optlstring(vm, 2, "", &b);
            auto& state = Get(vm);
            bool ok = false;
            try {
                if (state.api)
                    ok = state.api->Emit(std::string_view(topic, a), std::string_view(value, b),
                                         state.diagnostic);
            } catch (...) {
                state.diagnostic = "Cannot reserve script event";
            }
            if (!ok)
                return luaL_error(vm, "%s", state.diagnostic.c_str());
            return 0;
        }
        static int After(lua_State* vm) {
            const double delay = luaL_checknumber(vm, 1);
            size_t size = 0;
            const char* callback = luaL_checklstring(vm, 2, &size);
            auto& state = Get(vm);
            uint64_t token = 0;
            if (std::isfinite(delay) && delay >= 0 && delay <= 3600 &&
                CallbackName(std::string_view(callback, size)) && state.timers.size() < 64 &&
                state.nextTimer < INT64_MAX)
                try {
                    token = state.nextTimer++;
                    state.timers.emplace(token, Timer{delay, std::string(callback, size)});
                } catch (...) {
                    token = 0;
                }
            if (!token)
                return luaL_error(vm, "Timer delay/callback/capacity rejected");
            lua_pushinteger(vm, lua_Integer(token));
            return 1;
        }
        static int CancelTimer(lua_State* vm) {
            const auto token = luaL_checkinteger(vm, 1);
            if (token <= 0)
                return luaL_error(vm, "Invalid timer token");
            Get(vm).timers.erase(uint64_t(token));
            return 0;
        }
        static int ChecksumAsync(lua_State* vm) {
            size_t a = 0, b = 0;
            const char* text = luaL_checklstring(vm, 1, &a);
            const char* callback = luaL_checklstring(vm, 2, &b);
            auto& state = Get(vm);
            uint64_t token = 0;
            bool ok = false;
            try {
                if (state.api && CallbackName(std::string_view(callback, b)))
                    ok = state.api->AsyncChecksum(std::string_view(text, a), std::string_view(callback, b),
                                                  token, state.diagnostic);
            } catch (...) {
                state.diagnostic = "Cannot reserve asynchronous script continuation";
            }
            if (!ok)
                return luaL_error(vm, "%s", state.diagnostic.c_str());
            lua_pushinteger(vm, lua_Integer(token));
            return 1;
        }
        static int CancelAsync(lua_State* vm) {
            const auto token = luaL_checkinteger(vm, 1);
            auto& state = Get(vm);
            bool ok = false;
            try {
                if (state.api && token > 0 && state.api->cancelledAsync.size() < 16) {
                    state.api->cancelledAsync.push_back(uint64_t(token));
                    ok = true;
                }
            } catch (...) {
                ok = false;
            }
            if (!ok)
                return luaL_error(vm, "Invalid or exhausted async cancellation request");
            return 0;
        }
        static int SwitchScene(lua_State* vm) {
            size_t size = 0;
            const char* text = luaL_checklstring(vm, 1, &size);
            AssetId id;
            auto& state = Get(vm);
            if (!state.api || !AssetId::TryParse(std::string_view(text, size), id))
                return luaL_error(vm, "Scene switch requires a cooked scene AssetId");
            state.api->requestedScene = id;
            return 0;
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
            lua_pushcfunction(vm, CreateEntity);
            lua_setfield(vm, -2, "create");
            lua_pushcfunction(vm, DestroyEntity);
            lua_setfield(vm, -2, "destroy");
            lua_pushcfunction(vm, SwitchScene);
            lua_setfield(vm, -2, "switch");
            lua_setglobal(vm, "scene");
            lua_newtable(vm);
            lua_pushcfunction(vm, Subscribe);
            lua_setfield(vm, -2, "subscribe");
            lua_pushcfunction(vm, Unsubscribe);
            lua_setfield(vm, -2, "unsubscribe");
            lua_pushcfunction(vm, Emit);
            lua_setfield(vm, -2, "emit");
            lua_setglobal(vm, "events");
            lua_newtable(vm);
            lua_pushcfunction(vm, After);
            lua_setfield(vm, -2, "after");
            lua_pushcfunction(vm, CancelTimer);
            lua_setfield(vm, -2, "cancel");
            lua_pushcfunction(vm, ChecksumAsync);
            lua_setfield(vm, -2, "checksum");
            lua_pushcfunction(vm, CancelAsync);
            lua_setfield(vm, -2, "cancel_async");
            lua_setglobal(vm, "tasks");
            return 0;
        }

        static int SetSelf(lua_State* vm) {
            const auto& text = Get(vm).scratch;
            lua_pushlstring(vm, text.data(), text.size());
            lua_setglobal(vm, "self");
            return 0;
        }

        static int PushValues(lua_State* vm) {
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
            return 1;
        }
        static int SetProperties(lua_State* vm) {
            PushValues(vm);
            lua_setglobal(vm, "properties");
            return 0;
        }
        static int SetEventValues(lua_State* vm) {
            PushValues(vm);
            lua_setglobal(vm, "event");
            return 0;
        }
        static int CaptureState(lua_State* vm) {
            auto& state = Get(vm);
            state.exported.clear();
            state.stateSchema = 1;
            lua_rawgeti(vm, LUA_REGISTRYINDEX, state.module);
            lua_getfield(vm, -1, "state_schema");
            if (!lua_isnil(vm, -1)) {
                if (!lua_isinteger(vm, -1) || lua_tointeger(vm, -1) < 1 || lua_tointeger(vm, -1) > 65535)
                    return luaL_error(vm, "state_schema must be an integer in [1,65535]");
                state.stateSchema = uint32_t(lua_tointeger(vm, -1));
            }
            lua_pop(vm, 1);
            lua_getfield(vm, -1, "state");
            if (lua_isnil(vm, -1))
                return 0;
            if (!lua_istable(vm, -1))
                return luaL_error(vm, "Script state must be a flat owned value table");
            lua_pushnil(vm);
            while (lua_next(vm, -2)) {
                if (lua_type(vm, -2) != LUA_TSTRING || state.exported.size() >= 256)
                    return luaL_error(vm, "State keys/count rejected");
                size_t nameSize = 0;
                const char* name = lua_tolstring(vm, -2, &nameSize);
                const int type = lua_type(vm, -1);
                bool ok = false;
                double vector[3]{};
                if (type == LUA_TTABLE) {
                    if (lua_rawlen(vm, -1) != 3)
                        return luaL_error(vm, "State vectors require three numeric coordinates");
                    for (int i = 0; i < 3; ++i) {
                        lua_rawgeti(vm, -1, i + 1);
                        if (lua_type(vm, -1) != LUA_TNUMBER)
                            return luaL_error(vm, "Invalid state vector");
                        vector[i] = lua_tonumber(vm, -1);
                        lua_pop(vm, 1);
                    }
                }
                try {
                    if (type == LUA_TBOOLEAN) {
                        state.exported[std::string(name, nameSize)] = lua_toboolean(vm, -1) != 0;
                        ok = true;
                    } else if (type == LUA_TNUMBER) {
                        if (lua_isinteger(vm, -1))
                            state.exported[std::string(name, nameSize)] = int64_t(lua_tointeger(vm, -1));
                        else
                            state.exported[std::string(name, nameSize)] = double(lua_tonumber(vm, -1));
                        ok = true;
                    } else if (type == LUA_TSTRING) {
                        size_t size = 0;
                        const char* text = lua_tolstring(vm, -1, &size);
                        state.exported[std::string(name, nameSize)] = std::string(text, size);
                        ok = true;
                    } else if (type == LUA_TTABLE &&
                               std::all_of(std::begin(vector), std::end(vector), [](double n) {
                                   return std::isfinite(n) && std::abs(n) <= FLT_MAX &&
                                          (n == 0 || float(n) != 0);
                               })) {
                        state.exported[std::string(name, nameSize)] =
                            Math::Vec3(float(vector[0]), float(vector[1]), float(vector[2]));
                        ok = true;
                    }
                } catch (...) {
                    state.diagnostic = "Cannot reserve script state snapshot";
                }
                if (!ok)
                    return luaL_error(vm, "Unsupported or over-budget state value");
                lua_pop(vm, 1);
            }
            return 0;
        }
        static int ApplyState(lua_State* vm) {
            auto& state = Get(vm);
            lua_rawgeti(vm, LUA_REGISTRYINDEX, state.module);
            lua_getfield(vm, -1, "state_schema");
            const auto schema = lua_isnil(vm, -1) ? 1 : lua_tointeger(vm, -1);
            if (!lua_isnil(vm, -1) && (!lua_isinteger(vm, -1) || schema < 1 || schema > 65535))
                return luaL_error(vm, "Invalid destination state_schema");
            lua_pop(vm, 1);
            if (uint32_t(schema) != state.importSchema) {
                lua_getfield(vm, -1, "migrate");
                if (!lua_isfunction(vm, -1))
                    return luaL_error(
                        vm, "State schema changed without a migrate(old_schema,old_state) function");
                lua_pushinteger(vm, state.importSchema);
                PushValues(vm);
                lua_call(vm, 2, 1);
                if (!lua_istable(vm, -1))
                    return luaL_error(vm, "Migration must return an owned state table");
            } else
                PushValues(vm);
            lua_setfield(vm, -2, "state");
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
            if (lua_isnil(vm, -1)) {
                if (state.requiredCallback)
                    return luaL_error(vm, "Subscribed continuation callback does not exist: %s",
                                      state.event.c_str());
                return 0;
            }
            if (!lua_isfunction(vm, -1)) return luaL_error(vm, "Lifecycle field is not a function: %s", state.event.c_str());
            lua_pushnumber(vm, state.seconds);
            lua_getglobal(vm, "event");
            lua_call(vm, 2, 0);
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
                size_t size = 0;
                const char* message =
                    lua_type(vm, -1) == LUA_TSTRING ? lua_tolstring(vm, -1, &size) : nullptr;
                error = message ? std::string(message, std::min<size_t>(size, 4096)) : "Lua VM error";
                if (size > 4096)
                    error += " [diagnostic truncated]";
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
        return api.reloading ? true : Call("start", 0, api, error);
    }

    bool LuaScriptBackend::Call(std::string_view event, double seconds, ScriptApi& api, std::string& error) {
        auto& state = *m_State;
        if (std::this_thread::get_id() != state.owner || !state.vm || state.module == LUA_NOREF ||
            !std::isfinite(seconds) || seconds < 0 ||
            (event != "start" && event != "update" && event != "stop" && event != "reload")) {
            error = "Invalid Lua call owner, time or lifecycle";
            return false;
        }
        if (event == "update") {
            std::vector<std::pair<uint64_t, std::string>> due;
            for (auto& [token, timer] : state.timers) {
                timer.remaining -= seconds;
                if (timer.remaining <= 0 && due.size() < 16)
                    due.emplace_back(token, timer.callback);
            }
            for (const auto& [token, callback] : due) {
                state.timers.erase(token);
                ScriptValues payload;
                payload["timer"] = int64_t(token);
                if (!Event("@continuation/" + callback, payload, api, error))
                    return false;
            }
        }
        state.properties.clear();
        lua_pushcfunction(state.vm, State::SetEventValues);
        if (!state.Protected(0, 0, &api, error))
            return false;
        state.event.assign(event);
        state.requiredCallback = false;
        state.seconds = seconds;
        lua_pushcfunction(state.vm, State::Dispatch);
        return state.Protected(0, 0, &api, error);
    }

    bool LuaScriptBackend::Event(std::string_view channel, const ScriptValues& values, ScriptApi& api,
                                 std::string& error) {
        auto& state = *m_State;
        if (std::this_thread::get_id() != state.owner || !state.vm || state.module == LUA_NOREF ||
            !ValidateScriptValues(values, error))
            return false;
        std::string callback;
        if (channel.starts_with("@continuation/"))
            callback = channel.substr(14);
        else {
            const auto found = state.subscriptions.find(channel);
            if (found == state.subscriptions.end()) {
                error.clear();
                return true;
            }
            callback = found->second;
        }
        if (!State::CallbackName(callback)) {
            error = "Invalid script event callback name";
            return false;
        }
        state.properties.clear();
        state.properties.reserve(values.size());
        for (const auto& [name, value] : values) {
            State::Property property{name, value};
            if (const auto* id = std::get_if<PersistentEntityId>(&value))
                property.value = id->ToString();
            state.properties.push_back(std::move(property));
        }
        lua_pushcfunction(state.vm, State::SetEventValues);
        if (!state.Protected(0, 0, &api, error))
            return false;
        state.event = std::move(callback);
        state.requiredCallback = true;
        state.seconds = 0;
        lua_pushcfunction(state.vm, State::Dispatch);
        return state.Protected(0, 0, &api, error);
    }

    bool LuaScriptBackend::ExportState(uint32_t& schema, ScriptValues& values, std::string& error) {
        auto& state = *m_State;
        if (std::this_thread::get_id() != state.owner || !state.vm || state.module == LUA_NOREF) {
            error = "Script state export requires a live owner VM";
            return false;
        }
        lua_pushcfunction(state.vm, State::CaptureState);
        if (!state.Protected(0, 0, nullptr, error) || !ValidateScriptValues(state.exported, error))
            return false;
        values = state.exported;
        schema = state.stateSchema;
        error.clear();
        return true;
    }

    bool LuaScriptBackend::ImportState(uint32_t schema, const ScriptValues& values, ScriptApi& api,
                                       std::string& error) {
        auto& state = *m_State;
        if (std::this_thread::get_id() != state.owner || !state.vm || state.module == LUA_NOREF ||
            schema < 1 || schema > 65535 || !ValidateScriptValues(values, error)) {
            if (error.empty())
                error = "Invalid script state import";
            return false;
        }
        state.properties.clear();
        state.properties.reserve(values.size());
        for (const auto& [name, value] : values) {
            State::Property property{name, value};
            if (const auto* id = std::get_if<PersistentEntityId>(&value))
                property.value = id->ToString();
            state.properties.push_back(std::move(property));
        }
        state.importSchema = schema;
        lua_pushcfunction(state.vm, State::ApplyState);
        if (!state.Protected(0, 0, &api, error))
            return false;
        ScriptValues checked;
        uint32_t actual = 0;
        return ExportState(actual, checked, error);
    }
}
