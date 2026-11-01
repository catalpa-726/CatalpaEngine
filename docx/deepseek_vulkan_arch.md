# Vulkan 引擎封装、项目结构与分发笔记

> 整理自关于 Vulkan 引擎封装抽象、草稿文件清单、VS2022 打包分发的对话。

---

## 目录

1. [Vulkan 引擎一般如何封装与抽象](#一vulkan-引擎一般如何封装与抽象)
2. [草稿性质的项目文件列表清单](#二草稿性质的项目文件列表清单)
3. [VS2022 下如何打包分发给别人](#三vs2022-下如何打包分发给别人)

---

## 一、Vulkan 引擎一般如何封装与抽象

GitHub 上的 Vulkan 引擎项目通常不会直接裸露冗长的 Vulkan API，而是通过**分层架构**和**设计模式**来封装，核心目标是：

- 降低复杂度
- 保证资源安全
- 平衡性能与开发效率

### 1.1 典型分层架构

常见设计为四层架构：

| 层级 | 职责 |
|---|---|
| 应用层 Application | 管理主循环、窗口、输入，不直接接触 Vulkan |
| 高层抽象层 High-Level | 提供 API 无关的渲染器、资源管理、场景管理等 |
| RHI 层 Render Hardware Interface | 核心抽象层，定义纯虚接口，如 `Device`、`Pipeline`、`Buffer`，不包含 Vulkan 特定代码 |
| 后端层 Backends | 具体 Vulkan 实现，如 `VulkanDevice`、`VulkanPipeline`，通过工厂模式创建 |

这种设计使上层逻辑与图形 API 解耦，方便未来扩展 WebGPU、D3D12 等后端。

### 1.2 关键模块的封装与抽象手法

#### 设备与上下文管理

- **对象持有与封装**：将 `VkInstance`、`VkDevice`、`VkPhysicalDevice` 封装在 `VulkanContext` 或 `VulkanEngine` 类中，集中管理初始化和生命周期。
- **函数指针管理**：Vulkan 函数指针与 `Instance` / `Device` 绑定，封装层会在对象内部存储对应函数指针，避免全局函数指针的性能损耗和错误。
- **单例或服务定位器**：部分引擎使用单例模式全局访问 `VulkanEngine`，或使用服务定位器模式解耦系统依赖。

#### 内存管理：集成 VMA

手工管理 `VkDeviceMemory` 极为繁琐，主流项目普遍：

- 集成 **Vulkan Memory Allocator (VMA)**，用 `vmaCreateBuffer`、`vmaCreateImage` 替代底层分配和绑定。
- 在 VMA 之上用 RAII 类包装 `VmaAllocation`，如 `Buffer`、`Texture`。
- 为每帧临时数据实现线性分配器，帧结束后统一重置，避免频繁分配/释放。

#### 资源与生命周期：RAII 与延迟销毁

- **RAII**：为 `VkBuffer`、`VkImage` 等创建包装类，构造时创建资源，析构时销毁，杜绝手动 `vkDestroy*` 导致的泄漏。
- **延迟销毁**：GPU 可能仍在使用资源时，CPU 不能立即销毁。通常实现延迟销毁队列，等待 N 帧或通过时间线信号量确认 GPU 使用完毕后再释放。
- **资源缓存**：对纹理、Mesh、材质等建立缓存，通过 ID 查找，避免重复加载。

#### 渲染管线与渲染图

- **Render Graph**：将每帧渲染过程构建为有向无环图 DAG，节点是渲染通道，边是资源依赖。引擎自动推导并插入管线屏障和图像布局转换。
- **自动管线管理**：部分库能根据渲染图自动推导 `VkRenderPass`、`VkFramebuffer`、`VkPipeline` 创建参数，甚至通过 SPIR-V 反射自动生成描述符布局。

#### 命令缓冲区

- **记录器模式**：封装 `VkCommandBuffer` 录制过程，提供如 `RenderPassCommandRecorder` 等对象。
- **线程安全与池化**：为每个线程或每帧管理独立 `VkCommandPool`，支持多线程并行录制，帧结束后统一重置池。

#### 描述符管理

- 封装 `VkDescriptorSetLayout`、`VkPipelineLayout`、`VkDescriptorSet` 的创建与绑定流程。
- 通常通过 **SPIR-V Reflection** 自动生成布局，减少手动编码。
- 一些引擎探索 Bindless，利用 `VK_EXT_descriptor_buffer` 或 `VK_EXT_descriptor_heap` 高效管理描述符。

#### 同步原语

- 将 `VkSemaphore`、`VkFence` 的复杂用法封装为简单的帧同步或资源可用信号。
- 使用 **Timeline Semaphore** 作为主时钟统一管理 CPU-GPU 和 GPU-GPU 同步。
- 利用 **Synchronization 2** 的 `vk::DependencyInfo`，将旧式散落屏障统一为更内聚的结构。

### 1.3 可复用的抽象方法论

1. **接口隔离**：定义纯净 RHI 接口，将 Vulkan 实现隐藏在后端层之后，是跨平台和可维护性的基石。
2. **RAII 至上**：所有拥有生命周期的 Vulkan 资源用 C++ 对象包装，析构自动清理，并结合延迟销毁队列解决 GPU/CPU 同步问题。
3. **自动化推导**：利用渲染图和着色器反射自动处理管线屏障、布局转换、描述符布局等易错样板代码。
4. **集中式资源管理**：通过资源缓存和内存池，特别是集成 VMA，提升分配效率并便于统一追踪。
5. **性能敏感的薄封装**：对命令缓冲区等高频调用路径，采用 `inline`、`noexcept` 等手法，确保封装层几乎不引入额外运行时开销。

这些模式并非互斥，成熟引擎通常结合使用。可根据项目规模和性能要求选择性地实现。

---

## 二、草稿性质的项目文件列表清单

目标：先搭出“可运行的 Vulkan 封装骨架”，后续逐步填充。命名和目录可按习惯调整，重点是分层和职责边界。

```text
vulkan-engine/
├── CMakeLists.txt
├── README.md
├── docs/
│   └── architecture.md                 # 分层、RHI、资源生命周期、同步策略说明
├── cmake/
│   ├── FindVulkan.cmake
│   └── CompilerOptions.cmake
├── third_party/                        # 草稿阶段可先只放必需依赖
│   ├── vma/                            # Vulkan Memory Allocator
│   ├── volk/                           # Vulkan 函数加载（可选）
│   ├── vk-bootstrap/                   # Instance/Device 初始化（可选）
│   ├── spirv-reflect/                  # 描述符/着色器反射（可选）
│   ├── glm/
│   ├── stb/
│   └── imgui/                          # 调试 UI（可选）
├── include/engine/
│   ├── core/
│   │   ├── Application.h
│   │   ├── Window.h
│   │   ├── Input.h
│   │   ├── Log.h
│   │   ├── Types.h                     # Handle、Result、枚举等基础类型
│   │   └── NonCopyable.h
│   ├── rhi/                            # 纯接口层：不出现 Vk* 类型
│   │   ├── RHIFactory.h
│   │   ├── Device.h
│   │   ├── Swapchain.h
│   │   ├── Buffer.h
│   │   ├── Texture.h
│   │   ├── Sampler.h
│   │   ├── Shader.h
│   │   ├── Pipeline.h
│   │   ├── RenderPass.h
│   │   ├── Framebuffer.h
│   │   ├── CommandBuffer.h
│   │   ├── CommandQueue.h
│   │   ├── DescriptorSet.h
│   │   ├── DescriptorSetLayout.h
│   │   ├── Fence.h
│   │   ├── Semaphore.h
│   │   └── ResourceTypes.h
│   ├── rhi/vulkan/                     # Vulkan 后端：封装 Vk* 对象
│   │   ├── VulkanContext.h
│   │   ├── VulkanDevice.h
│   │   ├── VulkanSwapchain.h
│   │   ├── VulkanBuffer.h
│   │   ├── VulkanTexture.h
│   │   ├── VulkanSampler.h
│   │   ├── VulkanShader.h
│   │   ├── VulkanPipeline.h
│   │   ├── VulkanRenderPass.h
│   │   ├── VulkanFramebuffer.h
│   │   ├── VulkanCommandBuffer.h
│   │   ├── VulkanCommandQueue.h
│   │   ├── VulkanDescriptorSet.h
│   │   ├── VulkanDescriptorSetLayout.h
│   │   ├── VulkanFence.h
│   │   ├── VulkanSemaphore.h
│   │   ├── VulkanMemoryAllocator.h     # VMA 封装
│   │   ├── VulkanDeferredDeleter.h     # 延迟销毁队列
│   │   ├── VulkanDebugUtils.h
│   │   └── VulkanTypes.h               # Vk* 到 RHI 枚举映射
│   ├── renderer/
│   │   ├── Renderer.h
│   │   ├── FrameContext.h
│   │   ├── RenderGraph.h
│   │   ├── RenderGraphBuilder.h
│   │   ├── RenderPassNode.h
│   │   ├── ResourceState.h
│   │   └── passes/
│   │       ├── ClearPass.h
│   │       ├── TrianglePass.h
│   │       └── ImGuiPass.h
│   ├── resource/
│   │   ├── ResourceManager.h
│   │   ├── ImageCache.h
│   │   ├── MeshCache.h
│   │   ├── ShaderCache.h
│   │   ├── Material.h
│   │   └── Mesh.h
│   ├── scene/
│   │   ├── Scene.h
│   │   ├── Camera.h
│   │   └── Transform.h
│   └── utils/
│       ├── RefCounted.h
│       ├── Handle.h
│       ├── DeferredDeletionQueue.h
│       ├── TimelineSemaphore.h
│       ├── ShaderReflection.h
│       └── EnumFlags.h
├── src/
│   ├── core/
│   │   ├── Application.cpp
│   │   ├── Window.cpp
│   │   ├── Input.cpp
│   │   └── Log.cpp
│   ├── rhi/
│   │   └── RHIFactory.cpp
│   ├── rhi/vulkan/
│   │   ├── VulkanContext.cpp
│   │   ├── VulkanDevice.cpp
│   │   ├── VulkanSwapchain.cpp
│   │   ├── VulkanBuffer.cpp
│   │   ├── VulkanTexture.cpp
│   │   ├── VulkanSampler.cpp
│   │   ├── VulkanShader.cpp
│   │   ├── VulkanPipeline.cpp
│   │   ├── VulkanRenderPass.cpp
│   │   ├── VulkanFramebuffer.cpp
│   │   ├── VulkanCommandBuffer.cpp
│   │   ├── VulkanCommandQueue.cpp
│   │   ├── VulkanDescriptorSet.cpp
│   │   ├── VulkanDescriptorSetLayout.cpp
│   │   ├── VulkanFence.cpp
│   │   ├── VulkanSemaphore.cpp
│   │   ├── VulkanMemoryAllocator.cpp
│   │   ├── VulkanDeferredDeleter.cpp
│   │   └── VulkanDebugUtils.cpp
│   ├── renderer/
│   │   ├── Renderer.cpp
│   │   ├── RenderGraph.cpp
│   │   ├── RenderGraphBuilder.cpp
│   │   └── passes/
│   │       ├── ClearPass.cpp
│   │       ├── TrianglePass.cpp
│   │       └── ImGuiPass.cpp
│   ├── resource/
│   │   ├── ResourceManager.cpp
│   │   ├── ImageCache.cpp
│   │   ├── MeshCache.cpp
│   │   ├── ShaderCache.cpp
│   │   └── Material.cpp
│   ├── scene/
│   │   ├── Scene.cpp
│   │   └── Camera.cpp
│   └── utils/
│       ├── DeferredDeletionQueue.cpp
│       ├── TimelineSemaphore.cpp
│       └── ShaderReflection.cpp
├── shaders/
│   ├── triangle.vert
│   ├── triangle.frag
│   └── compile_shaders.sh
├── examples/
│   └── triangle/
│       ├── main.cpp
│       └── CMakeLists.txt
├── tests/
│   ├── test_handle.cpp
│   ├── test_deferred_deleter.cpp
│   └── test_rendergraph.cpp
└── assets/
    ├── models/
    ├── textures/
    └── shaders/
```

### 2.1 草稿阶段的最小可运行子集

如果先做“能跑起来”的版本，建议只保留：

```text
CMakeLists.txt
include/engine/core/Application.h
include/engine/core/Window.h
include/engine/rhi/RHIFactory.h
include/engine/rhi/Device.h
include/engine/rhi/Swapchain.h
include/engine/rhi/Buffer.h
include/engine/rhi/Shader.h
include/engine/rhi/Pipeline.h
include/engine/rhi/CommandBuffer.h
include/engine/rhi/vulkan/VulkanContext.h
include/engine/rhi/vulkan/VulkanDevice.h
include/engine/rhi/vulkan/VulkanSwapchain.h
include/engine/rhi/vulkan/VulkanBuffer.h
include/engine/rhi/vulkan/VulkanShader.h
include/engine/rhi/vulkan/VulkanPipeline.h
include/engine/rhi/vulkan/VulkanCommandBuffer.h
include/engine/rhi/vulkan/VulkanMemoryAllocator.h
include/engine/rhi/vulkan/VulkanDeferredDeleter.h
src/... 对应 .cpp
examples/triangle/main.cpp
shaders/triangle.vert
shaders/triangle.frag
```

### 2.2 清单设计说明

- **`rhi/` 是抽象核心**：只定义接口和通用类型，不出现 `VkDevice`、`VkBuffer` 等 Vulkan 类型。
- **`rhi/vulkan/` 是唯一允许出现 `Vk*` 的层**：集中处理函数指针、VMA、调试层、延迟销毁。
- **`renderer/` 不直接写 Vulkan 调用**：通过 RHI 和 RenderGraph 组织 Pass。
- **资源类优先 RAII**：`Buffer`、`Texture`、`Pipeline` 析构时进入延迟销毁队列。
- **草稿阶段可以合并小文件**：例如 `VulkanTypes.h` 可先并入 `VulkanContext.h`，`TimelineSemaphore` 可先不建。
- **后续可扩展**：`DescriptorBuffer`、`Bindless`、`Synchronization2`、`PipelineCache`、`FrameGraph` 自动屏障推导等，先留接口位置即可。

---

## 三、VS2022 下如何打包分发给别人

用 VS2022 把 Vulkan 引擎分发给别人，方式取决于对方是**开发者（需要源码编译）**还是**最终用户（直接运行）**。

Vulkan 的特殊性：`vulkan-1.dll` 通常由显卡驱动负责安装，**不建议直接塞进压缩包**，处理不当会引发兼容问题。

### 3.1 路线一：分发源码，给其他开发者

这是最干净、最推荐的方式，适合开源或协作场景。

1. **清理构建产物**：删除 `.vs`、`x64/`、`Debug/`、`Release/`、`out/` 等 VS 自动生成目录。
2. **处理第三方库**：`third_party/` 下 VMA、volk 等如果是源码形式，直接保留；如果是预编译 `.lib` / `.dll`，确保保留 `.lib` 和头文件，`.dll` 可以去掉。
3. **确保 CMake 可用**：项目根目录有 `CMakeLists.txt`，对方可直接用 VS2022 “打开本地文件夹”或 `cmake -B build` 构建。
4. **编写 README**：至少说明如何构建，包括 CMake 版本、Vulkan SDK 版本，以及如何运行。
5. **打包**：将整个项目文件夹（不含 `.vs`、`x64` 等）右键 → 压缩为 ZIP。

### 3.2 路线二：便携式 ZIP，给最终用户，解压即用

对方不想安装任何东西，解压后双击 `.exe` 就能运行。需要把以下内容放进一个文件夹再压缩：

- **可执行文件**：`Release/` 下的 `.exe`。
- **所有非系统 DLL**：如 `glfw3.dll`、`assimp.dll` 等第三方动态库。
- **资产目录**：`shaders/`、`assets/`、`textures/`、`models/` 等，**保持和代码中相对路径完全一致**的层级。
- **VC++ 运行时，关键**：VS 默认使用 `/MD` 动态链接，目标机器可能缺少 `vcruntime140.dll`、`msvcp140.dll`。不要手动复制这些 DLL，而是让用户安装 **Microsoft Visual C++ Redistributable (x64)**。可以在压缩包附 `readme.txt` 写明“请先安装 VC_redist.x64.exe”，或把官方 `VC_redist.x64.exe` 放进压缩包。
- **Vulkan 运行时**：**不要直接打包 `vulkan-1.dll`**。正规做法是依赖显卡驱动，驱动安装时会自动部署 loader。如果担心用户驱动太旧，可从 Vulkan SDK 中提取 **Runtime Installer `VulkanRT-x.x.x.x-Installer.exe`** 放进压缩包，让用户手动运行一次；它自带版本检查，不会覆盖更新的驱动版本。

**操作步骤**：在 `Release/` 输出目录整理好文件 → 全选 → 右键 → **发送到 → 压缩(zipped)文件夹**。

### 3.3 路线三：生成安装程序，更正式的分发

如果希望对方获得“安装/卸载”体验，可用 VS2022 的 **Microsoft Visual Studio Installer Projects** 扩展生成 `.msi`：

1. VS 菜单 **扩展 → 管理扩展**，搜索安装 **Microsoft Visual Studio Installer Projects 2022**，重启 VS。
2. 解决方案中新建 **Setup Project**。
3. 右键 **Application Folder** → **Add → 项目输出**，选主项目，添加“主输出”。
4. **手动补全所有依赖**：第三方 DLL、资产文件夹、VC++ 运行时安装包都需要手动添加，插件**不会自动检测**这些。
5. 生成后会得到 `setup.exe` 和 `.msi`，两个都要发给用户。

如果觉得 Installer Projects 太重，也可以用 **CPack**。项目已有 CMake，在 `CMakeLists.txt` 末尾 `include(CPack)` 并设置 `CPACK_GENERATOR "ZIP"` 即可。

### 3.4 Vulkan 引擎特有的检查点

压缩前建议在**没有安装 Vulkan SDK 的干净 Windows 机器**或虚拟机上测试解压后的文件夹能否直接运行。

同时确保：

- `shaders/` 目录中的 SPIR-V 文件 `.spv` 已随包分发，而不是 `.vert` / `.frag` 源码。
- 可用 **Dependencies**（Dependency Walker 的现代替代品）快速扫描 `.exe`，确认没有遗漏任何非系统 DLL。

---

## 附：推荐保存文件名

```text
vulkan-engine-notes.md
```