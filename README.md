# CatalpaEngine

CatalpaEngine 是一个使用 **C++20** 开发的引擎项目，当前主要面向 **Windows / Visual Studio 2022**，采用 **Premake** 管理工程生成。

## 构建与运行

以下命令在**项目根目录**执行。

### 准备依赖

需要以下环境：

- Git。
- Visual Studio 2022 （如果不是需手动配置premake5.lua）
- Vulkan SDK， 需支持 Vulkan 1.3 及以上

首次获取仓库时使用：

```cmd
git clone --recurse-submodules https://github.com/catalpa-726/CatalpaEngine.git
cd CatalpaEngine
```

如果已克隆主仓库，或者拉取了更新依赖的提交，在项目根目录执行：

```cmd
git submodule update --init --recursive
```

GLAD （待更新。。。）

### 生成 Visual Studio 工程

```cmd
.\Script\premake\premake5.exe vs2022
```

也可以运行现有批处理：

```cmd
.\build_project.bat
```

### 在 Visual Studio 中构建

1. 打开生成的 `CatalpaEngine.sln`。
2. 选择 `Debug` 或 `Release` 配置与 `x64` 平台。
3. 将 `CatalpaBox` 设为启动项目。
4. 生成解决方案，再启动 `CatalpaBox`。

Slang 预构建规则集中在 `Script/Build/Slang.lua`，递归收集 `Catalpa/Resources/Shaders/**.slang`，在 C++ 编译前生成 SPIR-V 头文件。当前每份 Shader 使用 `vertexMain` / `fragmentMain` 双入口；新增、删除或重命名 Shader 后需重新生成工程，文件名须为唯一的 C++ 标识符。

S01 已接入主窗口的 ImGui 平台输入与 RenderGraph 绘制代码；用户已确认 `443d6a3` 编译通过、UI 可以操作。`UI Controls` 提供文字输入、点击计数、滑块、VSync、滚动和窗口拖动；具体窗口恢复与退出边界不因这条概括反馈而视为全部验证。本批只注册固定英文字体纹理；正式停靠工作区、菜单、主题和设置持久化由 S02 接续，不启用额外原生窗口。

## 第三方依赖

待更新。。。

| 依赖 | 当前用途 | 上游与迁移基线 |
|---|---|---|
| GLFW | 窗口、输入及 Vulkan surface 创建 | [glfw/glfw](https://github.com/glfw/glfw)，[92dcf4ce](https://github.com/glfw/glfw/commit/92dcf4ce74f2e2554a98fea09be7c705c17daa5a) |
| Dear ImGui | 主窗口平台输入与 RHI 绘制，用户确认编译通过、UI 可操作 | [ocornut/imgui](https://github.com/ocornut/imgui)，docking [d88f238d](https://github.com/ocornut/imgui/commit/d88f238d1d363ef387382b2e29ad12cf96f826ac) |
| spdlog | 日志，当前使用头文件 | [gabime/spdlog](https://github.com/gabime/spdlog)，[6d9805df](https://github.com/gabime/spdlog/commit/6d9805df3f588402b17073dcd13eb569ceda864d) |
| GLAD | Vulkan 1.4 函数加载代码 | [Dav1dde/glad](https://github.com/Dav1dde/glad)，按下列选项本地生成 |
| nlohmann/json | S02 设置与语言包的解析依赖，调用功能待接入 | [v3.12.0](https://github.com/nlohmann/json/releases/tag/v3.12.0)，单头文件，MIT |

nlohmann/json 原文件位于 `Catalpa/third_party/json/include/nlohmann/json.hpp`，许可位于 `Catalpa/third_party/json/LICENSE.MIT`。固定来源为 [官方单头文件](https://raw.githubusercontent.com/nlohmann/json/v3.12.0/single_include/nlohmann/json.hpp) 与 [原始许可](https://raw.githubusercontent.com/nlohmann/json/v3.12.0/LICENSE.MIT)，无额外第三方传递依赖、编译目标或 DLL。文件缺失时从上述固定版本恢复到原路径；不替换为浮动版本。

原始下载文件的 SHA-256：`json.hpp` 为 `aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63`；`LICENSE.MIT` 为 `46a65cffd1ea955132d95a8dd921640714a8d6b537d2e4e482d31145ae95b603`。JSON 类型仅用于设置/序列化实现，不向 RHI 或公共 DLL 接口暴露。
