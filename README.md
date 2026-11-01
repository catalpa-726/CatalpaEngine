# CatalpaEngine

CatalpaEngine 是一个使用 **C++20** 开发的引擎项目，当前主要面向 **Windows / Visual Studio 2022**，采用 **Premake** 管理工程生成。

## 当前目录概览

以下展示当前主要入口，完整引擎架构仍在逐步实现。

```text
CatalpaEngine/
├── Catalpa/
│   ├── src/
│   │   ├── Core/
│   │   ├── Events/
│   │   ├── Layer/
│   │   ├── Log/
│   │   ├── Platform/
│   │   ├── Renderer/
│   │   └── Catalpa.h、catpch.h、catpch.cpp
│   └── third_party/
│       ├── glad/                  # 本地生成，不提交生成代码
│       ├── glfw/                  # Git submodule
│       ├── imgui/                 # Git submodule，docking 版本
│       └── spdlog/                # Git submodule，当前使用头文件
├── CatalpaBox/
│   ├── assets/
│   └── src/
│       └── catalpaBox.cpp
├── Script/
│   ├── Build/                     # 引擎维护的第三方构建适配
│   │   ├── GLFW.lua
│   │   ├── ImGui.lua
│   │   ├── GLAD.lua
│   │   └── README.md              # 现有目录的一次性迁移步骤
│   └── premake/
│       ├── premake5.exe
│       └── LICENSE.txt
├── .gitmodules                   # 子模块路径与上游地址
├── .gitignore
├── build_project.bat
├── premake5.lua
└── README.md
```

## 构建与运行

以下命令在**项目根目录**执行。

### 准备依赖

需要 Git、Visual Studio 2022 的 C++ 桌面开发工具（MSVC v143、Windows SDK）。首次获取仓库时使用：

```powershell
git clone --recurse-submodules https://github.com/catalpa-726/CatalpaEngine.git
cd CatalpaEngine
```

如果已克隆主仓库，或者拉取了更新依赖的提交，在项目根目录执行：

```powershell
git submodule update --init --recursive
```

该命令恢复主仓库记录的提交，不自动追踪上游最新版本。此仓库目前为私有仓库，克隆者需要访问权限。

GLAD 需要按下文选项单独生成。对于迁移前已经包含第三方源码的旧工作目录，先按 [一次性迁移说明](Script/Build/README.md) 处理。

### 生成 Visual Studio 工程

PowerShell：

```powershell
.\Script\premake\premake5.exe vs2022
```

也可以运行现有批处理：

```powershell
.\build_project.bat
```

批处理只负责调用 Premake 并暂停，不等于已经完成 C++ 编译。缺少依赖或 GLAD 生成文件时，Premake 会报告对应路径。

### 在 Visual Studio 中构建

1. 打开生成的 `CatalpaEngine.sln`。
2. 选择 `Debug` 或 `Release` 配置与 `x64` 平台。
3. 将 `CatalpaBox` 设为启动项目。
4. 生成解决方案，再启动 `CatalpaBox`。

根据当前 Premake 配置，调试程序输出路径为：

```text
bin/windows-x86_64/Debug/CatalpaBox/CatalpaBox.exe
```

构建成功后，可以在 PowerShell 中运行：

```powershell
.\bin\windows-x86_64\Debug\CatalpaBox\CatalpaBox.exe
```

## 第三方依赖

GLFW、Dear ImGui、spdlog 使用正式 **Git submodule**：主仓库的 gitlink 固定提交，`.gitmodules` 记录获取地址。README 中的链接供查阅，不能替代这两部分配置。参见 [Git 官方说明](https://git-scm.com/docs/git-submodule)。

| 依赖 | 当前用途 | 上游与迁移基线 |
|---|---|---|
| GLFW | 窗口、输入和后续 Vulkan surface 桥接 | [glfw/glfw](https://github.com/glfw/glfw)，[92dcf4ce](https://github.com/glfw/glfw/commit/92dcf4ce74f2e2554a98fea09be7c705c17daa5a) |
| Dear ImGui | 编辑与调试 UI，目前集成未完成 | [ocornut/imgui](https://github.com/ocornut/imgui)，docking [d88f238d](https://github.com/ocornut/imgui/commit/d88f238d1d363ef387382b2e29ad12cf96f826ac) |
| spdlog | 日志，当前使用头文件 | [gabime/spdlog](https://github.com/gabime/spdlog)，[6d9805df](https://github.com/gabime/spdlog/commit/6d9805df3f588402b17073dcd13eb569ceda864d) |
| GLAD | Vulkan 1.4 函数加载代码 | [Dav1dde/glad](https://github.com/Dav1dde/glad)，按下列选项本地生成 |

自定义 Premake 脚本由主仓库的 `Script/Build` 管理，不依赖上游仓库提供同名脚本。第三方生成工程放在 `out/projects`，编译结果统一输出到根目录的 `bin`、`bin-int`。许可证随各上游源码保留。

后续引入 ECS、音频、物理等源码依赖时可沿用此方式：锁定提交，构建适配放在主仓库，验证后再更新 gitlink。修改第三方源码时，应提交到可访问的 fork 并调整 `.gitmodules`，不能只保留未提交的本地改动。

### GLAD 生成选项

当前文件头记录的生成器版本为 **glad 2.0.8**。在 [GLAD 生成页面](https://glad.sh/#api=vulkan%3D1.4&extensions=VK_EXT_debug_utils%2CVK_KHR_surface%2CVK_KHR_swapchain&generator=c&options=LOADER) 核对以下设置，再生成并下载：

| 设置 | 值 |
|---|---|
| Generator | C/C++ |
| API | Vulkan 1.4 |
| Extensions | `VK_EXT_debug_utils`、`VK_KHR_surface`、`VK_KHR_swapchain`，共 3 项 |
| Loader | 开启 |
| Alias / Debug / Header Only / MX / On Demand | 全部关闭 |

将压缩包内的 `include`、`src` 放到 `Catalpa/third_party/glad`，保留包内目录结构。当前构建所需文件为：

```text
Catalpa/third_party/glad/
├── include/
│   ├── glad/vulkan.h
│   └── vk_platform.h
└── src/vulkan.c
```
