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

## 第三方依赖

待更新。。。

| 依赖 | 当前用途 | 上游与迁移基线 |
|---|---|---|
| GLFW | 窗口、输入及 Vulkan surface 创建 | [glfw/glfw](https://github.com/glfw/glfw)，[92dcf4ce](https://github.com/glfw/glfw/commit/92dcf4ce74f2e2554a98fea09be7c705c17daa5a) |
| Dear ImGui | 主窗口平台输入与 RHI 绘制，用户确认编译通过、UI 可操作 | [ocornut/imgui](https://github.com/ocornut/imgui)，docking [d88f238d](https://github.com/ocornut/imgui/commit/d88f238d1d363ef387382b2e29ad12cf96f826ac) |
| spdlog | 日志，当前使用头文件 | [gabime/spdlog](https://github.com/gabime/spdlog)，[6d9805df](https://github.com/gabime/spdlog/commit/6d9805df3f588402b17073dcd13eb569ceda864d) |
