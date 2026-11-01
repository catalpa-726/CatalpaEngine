# CatalpaEngine 完整引擎架构、Vulkan 封装与第三方库方案

整理日期：2026-11-01。适用项目：`F:\C\work\shader\Engine\CatalpaEngine`。

本文汇总本轮关于 Vulkan 引擎封装、ECS、完整引擎目录、同步、第三方库与长期健壮性的讨论，并结合当前源码和其他 AI 的草案进行修正。本文是**目标设计与实施依据，不是功能已经实现的声明**。

配套文档：[完整文件树](catalpa_engine_file_manifest.md#file-tree)、[文件清单与逐文件职责](catalpa_engine_file_manifest.md#file-index)。清单逐个列出文件，不用空目录、通配符或“对应 cpp”省略实现文件。本文解释设计理由和跨文件契约，清单解释每个文件的责任。

## 阅读导航

- [1. 已知信息、缺失信息与默认假设](#section-01)
- [2. 当前项目实际情况与需要面对的问题](#section-02)
- [3. 六个公开项目的封装层次与可借鉴之处](#section-03)
- [4. 对其他 AI 草案的保留与修正](#section-04)
- [5. 全引擎层次与依赖边界](#section-05)
- [6. 所有权、身份与生命周期](#section-06)
- [7. Mesh、Material、Camera 的完整数据链](#section-07)
- [8. ECS、调度与跨线程提取](#section-08)
- [9. 其他领域怎样构成完整引擎](#section-09)
- [10. RHI：隔离 Vulkan，但保留真实 GPU 语义](#section-10)
- [11. 同步、帧调度与资源回收](#section-11)
- [12. 第三方库选择与集成边界](#section-12)
- [13. 工程目标、目录与交付物](#section-13)
- [14. 实施阶段与可验证的完成条件](#section-14)
- [15. 本轮交付、验证边界与结论](#section-15)

<a id="section-01"></a>

## 1. 已知信息、缺失信息与默认假设

### 1.1 已知信息

- 用户确认采用 **ECS：Entity–Component–System**；此前的“ESC”按笔误处理。
- 引擎目标超出 Vulkan 封装，需要覆盖 Mesh、Material、Camera、Scene、资产、动画、物理、音频、工具和编辑器。
- Vulkan 必须与上游应用和游戏逻辑隔离；同步及 GPU 生命周期属于基础正确性要求。
- 当前工程采用 Windows、VS2022/MSVC v143、C++20、Premake，保留已有工程布局有现实价值。
- 当前已有 GLFW、GLAD、Dear ImGui、spdlog，不能把“新增架构”理解成全部推倒重来。
- 本轮只新增设计文档，不修改源代码、不安装库、不迁移构建系统、不创建数百个空源文件。

### 1.2 尚未确定的信息

目标游戏类型、最低 GPU、是否支持旧 Vulkan 设备、未来其他操作系统、是否需要 D3D12、目标帧率、场景规模、联网模式、脚本语言承诺、商业音频中间件需求、资源预算与团队规模尚未确定。因此不能承诺一套接口天然覆盖所有平台，也不能把当前建议当成已测性能结论。

### 1.3 本方案默认假设

1. 以桌面 3D 引擎为主，Windows/Vulkan 是首个实现；其他图形后端通过能力模型扩展。
2. 建议将 Vulkan 1.3 与所需 feature 检测作为现代桌面基线候选，查询并显式启用 synchronization2、dynamic rendering，使用 timeline 时也启用对应 feature；**这仍需依据最低设备矩阵决定**。首版单 graphics queue、传统 descriptor sets 即可；bindless、descriptor buffer、BDA、mesh shader、ray tracing 不作为默认必需能力。loader 能加载 Vulkan 1.4 不代表设备支持对应能力。
3. ECS 默认选 EnTT；引擎掌握 World 生命周期、系统阶段、持久化与跨线程提取。Flecs 是整体数据模型的备选，不与 EnTT 同时作为主 ECS。
4. 静态 3D 先闭环，随后接入编辑器、动态世界和音频；可选领域有明确入口，但不因此提前创建空实现。
5. “完整”指本文所定义产品范围内的运行、内容生产、编辑、诊断、测试与发布闭环，不等于立即实现所有 AAA 技术。

<a id="section-02"></a>

## 2. 当前项目实际情况与需要面对的问题

以下结论来自本地静态阅读，不是构建或运行测试。

| 当前文件/模块 | 已确认状态 | 对后续方案的影响 |
|---|---|---|
| `premake5.lua` | C++20；Catalpa 为 SharedLib；CatalpaBox 为应用；使用 GLFW/GLAD/ImGui | 延续 Premake，先建立目标依赖与公共头边界；没有必要仅为目录美观切换 CMake |
| `Catalpa/src/Core/Application.cpp` | 主循环主要驱动窗口更新与事件 | Application 将来负责应用编排，领域服务放 EngineRuntime，不能继续把所有功能堆进 Run |
| `Catalpa/src/Renderer/Renderer.h` | 空文件 | Renderer 是待实现模块，不应在文档中描述成现成渲染器 |
| `Catalpa/src/Platform/Vulkan/Vulkan.h` | 空文件 | Vulkan 后端尚待实现 |
| `Catalpa/src/Platform/OpenGL/OpenGL.h` | 空文件 | 不据此承诺已有双后端；保留占位不等于启用 OpenGL |
| `Catalpa/src/Platform/WindowsWindow/WindowsWindow.cpp` | `glfwWindowHint` 在首次 `glfwInit` 之前；创建窗口后缺少可靠的失败路径 | 首个实施阶段必须纠正初始化顺序和错误处理 |
| 同上 | NO_API 窗口仍使用 `glfwSwapInterval` 表达 VSync | Vulkan VSync 应由 present mode 策略决定，不能沿用 OpenGL swap interval |
| 同上 | 当前窗口尺寸回调不等于 framebuffer 像素尺寸协议 | 高 DPI、最小化、resize 与交换链重建需要单独处理 |
| `Layer/Imgui` | ImGui 平台/渲染后端调用存在注释，部分 viewport 代码仍沿用 OpenGL 思路 | 要重新建立平台输入桥接和 Vulkan 绘制适配，不能仅打开注释就视为可用 |
| `Catalpa/third_party/glad/include/glad/vulkan.h` | glad 2.0.8 生成的 Vulkan 1.4 loader，LOADER=True、MX=False；生成扩展为 debug_utils/surface/swapchain | 当前 GLAD **不是 OpenGL loader**；默认保留，新增扩展需更新生成配置或有依据地替换为 volk |
| 公共头及 include 路径 | 应用可访问整个 `Catalpa/src`；部分公共接口依赖 PCH | 目录隔离必须进一步变成编译目标和 include 规则，单靠约定不够 |
| Dist 配置 | 目前仍使用 Debug runtime、未优化 | 正式发布阶段要单独修正并验证，本文不修改该配置 |

`Core/Application` 是历史目录中的编排层特例，不代表所有 Core 文件都可以依赖 Runtime。保留现有路径的同时，要在构建目标中区分 Foundation 源文件和 Application 源文件。

<a id="section-03"></a>

## 3. 六个公开项目的封装层次与可借鉴之处

以下基于本轮对官方仓库和文档的阅读。它们是有代表性的公开项目，**不是按 star 数量评判技术正确性**；默认分支会变化，后续移植应固定具体 commit。没有将这些项目全部本地构建或做性能横向测试。

| 项目 | 从上到下的主要路径 | 值得借鉴 | 不能直接照搬的部分 |
|---|---|---|---|
| Godot | 场景/RendererRD → RenderingDevice + RenderingDeviceGraph → RenderingDeviceDriver → Vulkan driver；context driver 处理初始化/平台上下文 | 设备 API、图依赖和后端实现分开；Vulkan 不侵入场景逻辑 | Godot 有自身对象、资源和脚本体系；不能直接当作 ECS 框架移植 |
| Filament | Engine/View/Renderer/Material → FrameGraph → DriverApi/CommandStream → VulkanDriver → VulkanCommands/资源缓存 | 前端与驱动命令流分离；命令引用资源直到完成；view/material 是真实领域对象 | CPU CommandStream 不等于 GPU command buffer；其 FrameGraph 不能被简单描述成万能多队列同步编译器 |
| Wicked Engine | RenderPath/wiRenderer → GraphicsDevice → GraphicsDevice_Vulkan | 相对直接的 RHI；资源内部状态隐藏；高层 renderer 与底层设备拆开 | 显式 barrier 和 command list 依赖仍要由调用协议保证，不能假设“用了抽象就自动安全” |
| The Forge | 应用/渲染逻辑 → IGraphics 的 C 风格资源描述与命令 → Vulkan 实现；ResourceLoader 独立 | 创建/销毁/命令/提交边界清楚；加载完成 token 与渲染调度可分开 | 它提供引擎构件，并非完整场景、声音、物理系统；当前平台与仓库维护路线需核实 |
| Diligent Engine | IRenderDevice / IDeviceContext / ISwapChain → 公共基础实现 → Vulkan 实现与工具层 | 资源创建、命令记录、呈现分工；PipelineResourceSignature 与 ShaderResourceBinding 分离 | 多上下文状态转换仍有线程约束；COM 风格接口与成熟框架体量不一定适合当前项目 |
| Granite | Scene/Renderer/RenderQueue → RenderGraph → Vulkan::Device/CommandBuffer；Context 与 WSI 分工 | 图编译、瞬态资源、帧上下文、命令池和资源回收的实现思路 | 更贴近 Vulkan 语义，不是完全 API 无关的 RHI 范本；自己的分配器不能被误写成 VMA |

具体源码入口：

- Godot：[RenderingDevice](https://github.com/godotengine/godot/blob/master/servers/rendering/rendering_device.h)、[RenderingDeviceGraph](https://github.com/godotengine/godot/blob/master/servers/rendering/rendering_device_graph.h)、[Vulkan driver](https://github.com/godotengine/godot/blob/master/drivers/vulkan/rendering_device_driver_vulkan.h)。
- Filament：[FrameGraph](https://github.com/google/filament/blob/main/filament/src/fg/FrameGraph.cpp)、[CommandStream](https://github.com/google/filament/blob/main/filament/backend/include/private/backend/CommandStream.h)、[VulkanCommands](https://github.com/google/filament/blob/main/filament/backend/src/vulkan/VulkanCommands.cpp)。
- Wicked：[GraphicsDevice](https://github.com/turanszkij/WickedEngine/blob/master/WickedEngine/wiGraphicsDevice.h)、[Vulkan implementation](https://github.com/turanszkij/WickedEngine/blob/master/WickedEngine/wiGraphicsDevice_Vulkan.cpp)。
- The Forge：[IGraphics](https://github.com/ConfettiFX/The-Forge/blob/master/Common_3/Graphics/Interfaces/IGraphics.h)、[ResourceLoader](https://github.com/ConfettiFX/The-Forge/blob/master/Common_3/Resources/ResourceLoader/Interfaces/IResourceLoader.h)、[当前 README](https://github.com/ConfettiFX/The-Forge)。
- Diligent：[DeviceContext](https://github.com/DiligentGraphics/DiligentCore/blob/master/Graphics/GraphicsEngine/interface/DeviceContext.h)、[RenderDeviceNextGenBase](https://github.com/DiligentGraphics/DiligentCore/blob/master/Graphics/GraphicsEngineNextGenBase/include/RenderDeviceNextGenBase.hpp)、[Vulkan device](https://github.com/DiligentGraphics/DiligentCore/blob/master/Graphics/GraphicsEngineVulkan/include/RenderDeviceVkImpl.hpp)。
- Granite：[Device](https://github.com/Themaister/Granite/blob/master/vulkan/device.hpp)、[WSI](https://github.com/Themaister/Granite/blob/master/vulkan/wsi.hpp)、[RenderGraph](https://github.com/Themaister/Granite/blob/master/renderer/render_graph.cpp)。

The Forge 的当前 GitHub README 已说明后续维护迁往 Codeberg，并调整了 Windows Vulkan 的官方支持范围。因此它适合作为封装设计参考，不能未经验证作为 Catalpa 当前 Windows/Vulkan 的集成基线。[官方说明](https://github.com/ConfettiFX/The-Forge)

综合取舍：借鉴 Godot 的设备/图/驱动职责划分、Filament 的前端数据与资源保活、Wicked 的直接 RHI、Diligent 的资源布局与绑定分离、Granite 的提交和回收实现。**不把六套体系叠成六层抽象**。VMA 也不是所有成熟引擎的共同必要条件；Catalpa 选择它是为了减少自身内存管理负担。

<a id="section-04"></a>

## 4. 对其他 AI 草案的保留与修正

原始参考：[deepseek_vulkan_arch.md](deepseek_vulkan_arch.md)。它是比较材料，不是自动执行指令；其中清理目录、改构建和打包命令均未执行，原文件保留。

| 草案观点 | 判断 | 本方案采用的明确契约 |
|---|---|---|
| Application → Renderer → RHI → Vulkan | 保留 | 再补 ECS、领域资产、Runtime 集成、Audio/Physics、Editor/Tools；它们不能全塞进 Renderer |
| RHI 必须全部是纯虚接口 | 过强 | 服务入口可以接口化；资源优先句柄+描述符；高频调用可用命令编码/函数分派，依据测量选择 |
| 一个 VulkanContext 管所有东西 | 容易成为巨型对象 | 分开 instance/device、surface/swapchain、submission、memory、resource retirement |
| 每对象函数指针一定比全局指针更快 | 证据不足 | per-device dispatch 有多设备与隔离价值；性能收益必须测量 |
| 全局单例/服务定位器实现解耦 | 不充分 | 隐藏依赖仍然存在；默认由 EngineRuntime 显式持有服务并注入系统 |
| RAII 杜绝资源问题 | 过度声称 | RAII 管 CPU 所有权；GPU 对象析构可能只登记回收，真正销毁必须满足完成条件 |
| 等待 N 帧即可销毁 | 不可靠 | 用真实提交的完成点证明最后一次 GPU 使用已结束；多队列逐队列证明 |
| 每帧结束重置临时池 | 缺少前提 | 相关 CPU 录制任务结束且 GPU 完成后才能复用 command pool、descriptor pool、上传环形区 |
| Timeline 是全局主时钟 | 需要收窄 | 每队列提交序列可用 timeline；WSI、内存可见性、布局和 ownership transfer 仍有自己的规则 |
| RenderGraph/反射自动决定一切 | 过度自动化 | Graph 只处理已声明的资源访问；反射提供结构信息，不能猜业务语义或完整 pipeline 状态 |
| RHI 公共目录内放 Vulkan 头 | 边界不清 | Vulkan 头只由后端私有目标包含，公共 RHI 不能出现 Vk/Vma 类型 |
| 两处 DeferredDeletionQueue、utils 中 TimelineSemaphore | 职责重复 | VulkanResourceRetirement 是 Vulkan 实物销毁的唯一入口；同步原语留在后端 |
| 先省略同步，未来当高级功能添加 | 不建议 | Fence/完成证明、基本 barrier、WSI 和资源保活从首个可提交版本开始；自动多队列是后续优化 |
| 所有目录换为 CMake 范本 | 不适合当前事实 | 当前是 Premake；保留构建入口，按需要拆目标 |

`VK_EXT_descriptor_heap` 不应被武断称为不存在；相关扩展必须按所选 SDK、设备与 feature 验证。descriptor indexing、descriptor buffer、descriptor heap 是可评估的不同能力路线，不是第一版必须同时实现的三个层级。

<a id="section-05"></a>

## 5. 全引擎层次与依赖边界

### 5.1 逻辑层次

| 层次 | 模块 | 承担的责任 |
|---|---|---|
| 基础设施 | Core 基础部分、Math、Jobs、IO、Serialization、Log、Diagnostics | 错误、句柄、内存、时间、CPU 任务、路径/文件与格式基础 |
| 平台 | Window/Input 抽象、Windows 实现、平台文件能力 | OS 事件、像素尺寸、设备输入、路径/文件监视；不决定材质和场景 |
| 实体与场景 | ECS、Scene、标准 Components | 实体生命周期、组件数据、层级、场景持久化、Prefab |
| 资产 | Assets + 各领域资产结构 | 稳定身份、依赖、加载、版本与包读取；领域模块定义实际数据格式 |
| 领域算法/服务 | Geometry、Materials、Camera、Lighting、Animation、Physics、Audio、Navigation、Scripting、UI | 提供具体功能及其运行时状态，不直接承担全引擎主循环 |
| 场景集成 | Runtime/Systems | 从 ECS 数据驱动领域服务，提取渲染/音频快照，接收结果与事件 |
| 渲染前端 | Renderer、Visibility、Passes、RenderGraph、GPU caches | View、可见性、绘制记录、GPU 资源表现和帧计划 |
| 图形接口 | RHI | 设备能力、资源描述与句柄、命令、访问依赖、提交和完成语义 |
| 图形后端 | Platform/Vulkan | Vk 对象、分配、WSI、屏障转换、队列提交、完成查询及实际销毁 |
| 编排 | Application、EngineRuntime | 主循环、服务创建/关闭、阶段执行、场景与表现服务连接 |
| 产品和生产工具 | CatalpaBox、CatalpaEditor、Tools | 游戏逻辑；编辑；导入/烘焙/Shader 编译/打包；彼此拥有独立构建目标 |

层次是依赖图，不是每次调用必须走完的十一级继承链。

### 5.2 必须由构建系统维护的限制

- `World` 不包含 `Renderer`、`VkDevice`、`AudioService`；ECS 仅组织实体与组件。
- `Renderer` 接受 `RenderFramePacket`、资产访问接口和 `RenderView`，不读写整个 ECS registry，不依赖 Editor。
- `Runtime/Systems` 是 ECS 与渲染/声音/物理服务的桥接处；必要的组件存储和算法状态不在多个模块重复维护。
- `Assets` 提供通用加载协议，不 include 所有 Mesh/Audio/Physics 类型；领域 loader 在组合根注册。
- MeshAsset、MaterialDefinition、AudioClipAsset 不持有 Vk/Vma/miniaudio/Jolt 的原生对象。
- Vulkan 源文件、VMA/loader 头、GLFW surface 适配只对后端目标可见；公共头不能依赖 PCH 才编译通过。
- Editor 和 AssetPipeline 可以依赖 runtime；runtime 不能反向依赖编辑器面板或源素材 importer。
- RHI 不以 Vulkan 枚举数值作为持久化格式；其他后端不能通过“假装支持”绕过能力检测。

初期可以继续只分发一个 Catalpa.dll，通过静态模块目标和受控导出头落实边界。目录不必各自对应一个 DLL。SDK 初期明确编译器、CRT 和构建配置一致；若以后要求跨编译器插件 ABI，再引入版本化 C 入口和不透明句柄，不把 C++ STL/EnTT 的内存布局承诺成稳定 ABI。

<a id="section-06"></a>

## 6. 所有权、身份与生命周期

### 6.1 所有权表

| Owner | 持有内容 | 借用/交接内容 |
|---|---|---|
| Application | 平台窗口、LayerStack、EngineRuntime、应用循环 | 读取输入，提交应用事件 |
| EngineRuntime | JobSystem、AssetManager、Renderer、PhysicsService、AudioService、SceneManager | 显式为系统提供服务引用 |
| SceneManager | 场景实例、激活/卸载规则 | 选择参与模拟与表现的 World |
| Scene | World、SystemScheduler、场景级运行状态 | 使用 engine 级服务；场景卸载请求销毁其 PhysicsWorld/播放归属 |
| World | entity generation、组件存储、结构变更队列 | 系统在受控阶段读写 |
| AssetManager | 通用资产记录、加载任务、版本与依赖关系 | 按 handle 返回受生命周期保护的 CPU 资产 |
| Renderer | RenderScene、GPU caches、RenderGraph、帧调度和 RHI device | 消费提取包；保留 GPU 最后使用引用 |
| PhysicsService | SDK 公共资源和各场景 PhysicsWorld 实例 | World 通过 handle/命令访问，退出场景时显式销毁 |
| AudioService | backend、播放 voice、解码/流送状态及资源引用 | 消费音频命令与空间快照，返回完成/错误事件 |
| EditorApplication | 文档、选中集、Undo/Redo、编辑视图 | 经场景和资产 API 修改内容；Play 模式使用隔离的运行 World |

### 6.2 不合并的身份

| 身份 | 有效范围 | 不能混用的原因 |
|---|---|---|
| EntityId | World 标识 + index/generation | entity 删除后索引可以重用；不能直接写为永久场景引用 |
| PersistentEntityId | 场景/Prefab 的稳定序列化身份 | 加载后需要映射回当前 World 的 EntityId |
| AssetId | 逻辑资产身份 | 内容更新时 ID 可稳定，但版本变化；不等于内存地址或文件路径 |
| AssetHandle / AssetVersionRef | 前者为逻辑引用，后者固定并保活已取得的不可变版本 | 快照/cache/voice 使用后者或等价契约；CPU loaded 不代表 GPU/audio ready |
| Buffer/Image/Pipeline handle | RHI device 及 generation | 设备销毁后失效；不能用作 CPU 资产的永久 ID |
| VoiceHandle / PhysicsBodyHandle | 对应服务或 world 的实例生命周期 | 同一个 clip/collider 可以被多个实例复用 |

稳定 ID 解决引用，版本解决更新，generation 解决复用后的陈旧句柄。这三个问题不能用一个不断增长的整数笼统代替。

### 6.3 状态与错误

资产加载至少区分 Requested、Loading、Ready、Failed、Unloading；GPU 表现还要有 UploadPending、Resident、Retiring，音频有准备/播放/停止确认。状态枚举分别属于各模块，不能把整个系统塞进一个全局 ResourceState。

失败采用结构化错误：操作、资产/对象身份、阶段、底层错误码、可恢复性。底层创建失败返回调用者，不能只 log 后继续使用空句柄。异步取消不等于任务立刻消失，回调与资源引用仍需清理。

### 6.4 关闭顺序

先停止新输入/游戏生产与资产新请求，撤销场景系统调度，等待已提交 CPU 工作退出；再停止声音并取得确认，完成或安全放弃设备丢失后的 GPU 工作；释放表现缓存和后端资源，最后释放仍被它们引用的资产及服务。Jobs 不能在仍依赖它的解码/释放任务前停止。

窗口必须晚于其 surface/swapchain 的使用结束。关闭顺序由拥有者协议决定，不靠全局静态对象的析构顺序碰运气。Device lost 首先进入受控错误/退出路径；本文不承诺无缝恢复整个运行场景。

<a id="section-07"></a>

## 7. Mesh、Material、Camera 的完整数据链

### 7.1 Mesh 与 Model

`Geometry/MeshData` 定义 CPU 顶点流、索引、submesh 范围、拓扑与边界；`VertexLayout` 明确 semantic、格式、stride 和字节 offset；`MeshAsset` 组织 LOD 与 skin 数据；`ModelAsset` 则包含节点层级、mesh/material/skeleton/animation 引用。一个模型不等于一个 Mesh。

模型源文件经工具导入并烘焙为 Mesh/Model 资产，Scene 的 MeshRendererComponent 只引用这些资产和材质槽覆盖。Runtime 提取实例变换与可见性选项，Renderer 的 GeometryGpuCache 按资产版本/设备维护 GPU 缓冲，再由 VulkanBuffer 承担实际对象。

必须检查索引和顶点范围、submesh/material slot 一致性、顶点布局与 shader 输入兼容、LOD 和 skin 骨骼索引。多个 entity 复用同一 MeshAsset，不能各复制一份 MeshData。碰撞网格经 Physics cooker 单独生成，不默认将可视网格原封不动当碰撞结构。

工具处理切线、重排、量化、LOD 时保留原素材，记录参数与算法版本；MikkTSpace 输出可能需要拆分切线接缝顶点，不能直接平均回原 index 的顶点。[MikkTSpace 实现说明](https://github.com/mmikk/MikkTSpace)

### 7.2 Material 与 Shader/Pipeline

- MaterialDefinition：材质族、参数 schema、shader family、渲染 domain 和允许的状态策略。
- MaterialInstance：definition 引用、参数值、纹理 AssetHandle、允许的覆盖项与版本。
- MaterialParameter：明确数值/向量/矩阵/纹理等类型、默认值和语义。
- MaterialValidation：校验参数类型、范围、缺失纹理 fallback 与 schema 版本兼容。
- Shaders/ShaderAsset 与 ShaderMetadata：保存编译产物、入口、阶段、资源布局与输入输出结构；不持有 VkShaderModule。
- Renderer/Resources/MaterialGpuCache：将参数与纹理引用转成 GPU 参数区和绑定记录。
- PipelineLibrary：根据 shader 版本、vertex layout、pass role、附件格式、MSAA、材质状态、skinning 等生成完整 key。

**MaterialInstance 不是 VkPipeline。** 一个材质会参与 shadow、depth、forward、picking 等不同 pass；同一个 shader 也会对应多种 pipeline。第一版采用明确的 PBR 和 Unlit 定义；参数更新尽量更新数据，而非一律转成宏并产生 shader variant。可视化节点材质属于后续产品功能。

### 7.3 Camera 与 RenderView

CameraLens 保存透视/正交参数；CameraProjection 构造并验证投影；CameraMatrices 明确 view/projection/inverse 与历史矩阵；Frustum 提供一致的剔除平面。CameraComponent 保存实体的 lens、启用/层过滤配置，CameraSystem 结合世界变换产生相机状态。

RenderView 是“相机状态 + 输出目标 + 像素 viewport + 渲染选项”。aspect 来自该 view 的目标尺寸，不能永远取主窗口。这样才能支持编辑器多个 viewport、多相机、离屏和反射视图。

EditorCameraController 提供 orbit/free-fly，不要求每个编辑器相机都在游戏场景中创建实体。投影约定、反向 Z、jitter、viewport Y 方向只能各有明确的转换责任；不能在 CameraSystem 与 Vulkan 上传时各翻一次 Y。

### 7.4 数学与单位契约

建议内部使用米、秒、弧度，固定一种 handedness 和 up/forward 约定；实现前明确选择，导入时一次转换。矩阵内存列优先不等于数学一定采用列向量，应分别规定存储、乘法方向和 shader ABI。GPU block 的 alignment/packing 按反射与引擎 schema 校验，不将 C++ struct 的 `sizeof` 当成任意 shader 布局证明。

颜色必须区分 sRGB 编码与线性计算；法线/粗糙度等数据贴图不能按颜色贴图转换。曝光、光照强度与音频空间单位的换算集中管理，禁止各 importer 或 shader 各自猜测。

<a id="section-08"></a>

## 8. ECS、调度与跨线程提取

### 8.1 ECS 的责任

Component 主要保存可检查、可序列化的数据与引擎句柄；System 实现状态推进。EnTT registry 是内部实现，应用不应依赖其 entity 数值布局或任意访问所有 component storage。ComponentSchema 维护稳定类型 ID、字段 schema 和版本迁移，不以 C++ RTTI 名称作为永久序列化键。

EntityCommandBuffer 在安全点合并创建/销毁/增删组件；不能在其他 job 遍历 storage 时直接引起结构变更。SystemScheduler 依据阶段、组件读写和显式先后关系调度，JobSystem 执行 CPU 工作。LayerStack 保留应用与 UI 层语义，不承担 ECS 的数据冲突调度。

### 8.2 初始模拟阶段

1. 处理 OS 事件，形成 InputAction 状态。
2. 按固定步长推进 Gameplay/Script、动画时间与 root motion。
3. 更新必要的 transform，将 kinematic/static 目标传入 PhysicsWorld。
4. 执行物理模拟，写回 dynamic body 结果与碰撞事件。
5. 执行 post-physics 系统，必要时增加明确的 IK 阶段。
6. 在安全点提交结构变更，重新校验受影响层级，再更新最终 Transform/Camera/bounds；不能让新建、删除、重挂接使提取的数据失效。
7. 提取音频空间快照与按序播放/停止命令。
8. 按渲染插值系数提取 RenderFramePacket，生成各 RenderView。
9. 生成游戏 UI、编辑器 UI 与 debug draw，进入渲染调度。

第 2–6 步属于每个 fixed tick；第 7–9 步通常按展示帧交接。多个 tick 中产生的动画事件和声音离散命令应按序累积，不能被最新空间快照覆盖。编辑器在没有模拟 tick 的帧里修改场景后，也需先更新对应派生状态再提取。

固定步长的 accumulator、最大补步与暂停/单步策略由 SimulationClock 统一实现。积压时必须有可观测的降级策略，不能悄悄更改物理时间单位。渲染插值结果只作为表现数据，不写回权威 ECS transform。

### 8.3 提取包生命周期

RenderFramePacket 复制稳定的实例记录，或持有明确保活的只读数据块；不能包含下一次 World 修改就失效的 component 指针、iterator 或 view。帧 arena 直到消费者释放后才能复用；其中的资产版本继续被 GPU cache/提交引用保护。

RenderScene 保存绘制代理和历史状态，不成为第二份可独立修改的游戏世界。相机/实例历史需要稳定实例身份和重置规则，实体新建、teleport、资产替换时不能盲用上一帧历史。

### 8.4 四个调度器不能混为一谈

| 对象 | 解决的问题 | 不解决的问题 |
|---|---|---|
| SystemScheduler | ECS 阶段与组件读写冲突 | Vulkan 内存可见性 |
| JobSystem | CPU 任务执行、依赖和完成 | entity 的业务顺序、GPU barrier |
| RenderGraph compiler | 已声明 GPU 资源访问、队列依赖和可执行计划 | 未声明的外部访问、任意业务语义 |
| FrameScheduler | 按计划组织录制、提交和帧上下文复用 | 任意重排已编译队列而仍声称 barrier 正确 |

<a id="section-09"></a>

## 9. 其他领域怎样构成完整引擎

### 9.1 资产与内容管线

原始 Content、可丢弃的 DerivedDataCache、交付用 Cooked 产物是三个不同类别。源文件只读；import/cook 写新产物，不能覆盖原始模型、音频或图像。编辑器可修改自己的场景/材质文档，但保存应先验证，再用事务性替换避免半文件。

AssetRegistry 管 ID、路径映射、类型、依赖与版本；AssetManager 管异步加载、取消和引用；PackageReader 读取有版本的 runtime package。各领域 loader 注册到通用管理器，避免 AssetManager include 全引擎。

资产热重载先产生新版本，再发布可用状态；旧版本直到 CPU snapshot、GPU submit 和 audio voice 都释放后才退休。GPU upload ready、audio decoded ready 与 CPU loaded 分开表示。文件监视事件需要去抖/合并并核对实际内容，不能看到一个 write 事件就消费半写文件。

缓存 key 应含源内容、导入设置、工具/算法版本、目标平台、格式 schema 和相关依赖；package 加载检查大小、offset、数量上限、版本与校验值，不能直接信任资源文件中的长度。

### 9.2 Lighting 与环境

Light 数据声明光源类别、范围、方向与有定义的强度单位；EnvironmentSettings 保存天空、雾、曝光等场景参数；EnvironmentAsset 保存环境贴图/预计算数据。Renderer 将其转为灯光 buffer、阴影分配和 light list；shadow atlas、cluster 等实现不放在 LightComponent。

### 9.3 Animation

SkeletonAsset 管骨骼层级、bind pose 与稳定 bone identity；AnimationClip 管轨道、时间和事件；Pose 管 local/model pose；AnimationPlayer 推进播放时间；Sampler/Blend 实现采样与混合。ozz 可以承担其中的核心算法，仍要有引擎自身资源和错误边界。

GPU skinning/morph 是 Renderer 的表现工作。状态机、IK、retargeting 需要时再增加明确文件与数据格式，不能认为一个 AnimatorComponent 已经包含完整动画系统。

### 9.4 Physics

PhysicsService 管 SDK 与共享资源，PhysicsWorld 管每个场景的碰撞世界、body 与查询。组件持有 BodyHandle/配置；Jolt 原生类型留在 adapter。

dynamic transform 由物理写回；kinematic/static 从 ECS 读取目标；teleport 为显式命令；root motion 在规定阶段转换为运动意图。多方不能无条件改同一 transform。Raycast/Sweep/Overlap 通过引擎查询结果返回，不泄露 Jolt 指针。固定步长并不能自动保证跨平台确定性。

### 9.5 Audio

AudioClipAsset 是源数据/流送信息，VoiceHandle 是一次播放实例。AudioService 管资源和 voice，AudioBackend 接入 miniaudio 或 NullBackend。AudioExtractSystem 将 Listener/SourceComponent 的空间状态转换为快照，Play/Stop/Seek 形成按序命令。

空间位置快照可以合并为最新状态，离散播放命令不能像过期渲染帧一样随意丢弃。有界队列必须规定满队列策略和错误反馈。实时 callback 不遍历 ECS、不等 GPU、不调用阻塞 IO，不在不可控路径分配内存或同步 log；解码/流送在适当 worker 进行。

停止请求不等于播放已终止；clip 缓冲需要等待消费方停止确认。音频结束事件回传主线程处理，不能在 callback 内直接销毁场景实体。默认利用 miniaudio 的 engine/mixer，先不重写混音器。[miniaudio 手册](https://miniaud.io/docs/manual/index.html)

### 9.6 Navigation、Scripting 与游戏 UI

Recast 的 navmesh 烘焙放 Tools，Detour 查询放运行时。导航只解决可行路径与局部移动基础，不替代游戏 AI 决策、世界流送或联网权威。

ScriptRuntime 管 VM、脚本资产和调用边界，ScriptBindings 只暴露受检查的 handle/API。Lua state 有明确线程 owner；coroutine 不等于多线程。热重载要定义状态迁移/失败回退，脚本错误不能让主循环继续使用半初始化组件。

UIContext 管游戏 UI 生命周期、输入与布局适配，FontAsset 管字体资源，UIDrawData 向 Renderer 提供可消费绘制数据。RmlUi 可承担布局；字体 shaping/fallback、DPI 与 IME 路线需结合产品语言需求验证。绘制适配明确 translation/transform、scissor、预乘 alpha、色彩空间与资源寿命；clip mask、layers、filters 按实际实现声明支持范围，不能默认完整支持上游所有效果。Dear ImGui 主要服务编辑器与调试，不等于所有游戏 UI。[RmlUi RenderInterface](https://github.com/mikke89/RmlUi/blob/master/Include/RmlUi/Core/RenderInterface.h)

### 9.7 Editor、Gameplay 与扩展领域

CatalpaBox 保存示例游戏代码；实际玩法不塞进引擎基础层。CatalpaEditor 通过 SceneDocument、资产文档、可撤销 Commands 修改内容；Hierarchy/Inspector/AssetBrowser/Console 只是界面，不能直接操纵 Vulkan cache。EditorWorld 与 PlayWorld 分离，停止播放不会把临时模拟状态自动覆盖回作者场景。

SceneDocument 持有由 SceneManager 管理的 SceneHandle，不再次独占同一个 World。停止播放或关闭文档时，先停止相关调度、收束该 World 的 CPU 工作，再解除 render/audio/physics 关联，确认消费者不再访问 World 后卸载场景。GPU 可继续消费自己已拥有的快照，但不能继续借用 World 内存；声音的停止确认与资源退休仍遵循各自协议。

Particles、Terrain、Networking 归为按需扩展，清单仍给具体入口文件。粒子资产/模拟与 GPU pass 分离；terrain 数据/流送与 renderer 表现分离；network transport 与 replication/snapshot/authority 分离。GameNetworkingSockets 提供传输基础，并不会自动完成预测、回滚、存档或反作弊。

没有产品需求时不默认加入光追、虚拟几何、完整地形编辑器、可视化材质图、多人回滚或自研声学。其必要性应由实际内容决定；扩展时仍遵守已有资产、调度和所有权边界。

<a id="section-10"></a>

## 10. RHI：隔离 Vulkan，但保留真实 GPU 语义

### 10.1 对外应该提供什么

RHI 的公共契约覆盖 device/capabilities、buffer/image/sampler、shader/pipeline、binding layout 与 binding data、command list、rendering attachments、资源访问、队列提交、完成观察以及 presentation。资源用类型明确的 handle；创建参数用引擎 descriptor。纯数据结构不必都有一个虚基类。

Graphics/Compute/Transfer 是逻辑工作类别，不保证设备一定提供三条独立硬件队列。RHI device 根据能力映射实际队列；同一 queue family 内不同 queue 也不等于彼此自动有序。第一版可以把工作集中在 graphics queue，只要同步契约正确。

`dynamic rendering` 可减少 render pass/framebuffer 对象管理，但不消除附件依赖、layout transition 和 pipeline 的 attachment format 约束；不要为了对齐传统 Vulkan 对象清单而在公共 RHI 强制暴露 RenderPass/Framebuffer 两类对象。[Khronos dynamic rendering 示例](https://docs.vulkan.org/samples/latest/samples/extensions/dynamic_rendering/README.html)

### 10.2 新枚举与类型的归属

| 类型/枚举族 | 推荐归属 | 含义与限制 |
|---|---|---|
| PixelFormat | Images 的格式公共定义，RHI 可依赖该轻量头 | 统一 CPU 图像与 GPU 格式语义；查询设备是否支持对应用途，不只检查名称 |
| VertexSemantic/VertexFormat | Geometry/VertexLayout | 顶点输入含义、存储格式与布局；后端转换为具体 API 描述 |
| BufferUsage/ImageUsage/MemoryDomain | RHI 资源描述 | 用途与访问倾向，不能直接暴露 Vulkan memory type index |
| QueueClass/ShaderStage/ResourceAccess | RHI | 执行范围、访问类型与队列工作语义；包含 image 子资源与 buffer 范围 |
| LoadOp/StoreOp/CompareOp/BlendOp/CullMode | RHI 渲染/管线描述 | 属于实际已支持能力；不为每个 Vulkan 枚举机械复制一个一一对应版本 |
| ProjectionType | CameraLens | 透视/正交的领域选择，与 API 后端无关 |
| MaterialDomain/AlphaMode | Materials | 表面/透明等材质语义，不是 pipeline handle |
| LightType、单位约定 | Lighting | 方向/点/聚光及对应强度解释 |
| MotionType | Physics | static/kinematic/dynamic 的写入权威与模拟行为 |
| AudioPlaybackState/AudioBus | Audio | 播放实例状态和混音路由，不能复用 RenderPass/FrameState |
| AssetLoadState | Assets | CPU 加载状态，不表示 GPU 或声音设备可用 |
| Result/ErrorCode | Core | 通用成功/失败载体，保留领域附加错误信息 |

bit flags 仅用于确实可组合的选项，提供受约束的类型运算；互斥状态采用 enum class。公共 header 不能让上层传 `uint32_t vkFlags` 或 `void* nativeResource` 绕过边界。确实需要第三方原生互操作时，建立后端私有 adapter，并显式声明资源所有权、图访问和生命周期。

序列化使用稳定字段 ID/字符串或明确版本化编码，不把 C++ enum 的当前排列顺序直接当永久磁盘 ABI。对于没实现的 capability，要返回 Unsupported 或选明示 fallback，不能悄悄当成功。

### 10.3 图形对象的真正边界

- RHI device 创建资源，GPU cache 持有引擎句柄，后端持有 Vk 对象和内存分配。
- binding layout 说明资源接口，binding data 说明实际绑定；材质参数不直接管理 VkDescriptorPool。
- command list 记录命令并保留资源使用信息；recorded-but-not-submitted 的引用也必须保活。
- submit 接受明确 wait/signal 与命令范围，成功后生成完成凭证；失败不能凭空产生“已完成”的 token。
- presentation 接受渲染结果，但不是普通 buffer/image 操作；frame slot、swapchain image、present completion 是三个不同概念。

<a id="section-11"></a>

## 11. 同步、帧调度与资源回收

### 11.1 同步从第一个可提交版本开始

完成证明不是可以跳过的“高级章节”。首个版本就必须能通过 Fence 或合适的 Timeline 路径证明何时可复用 command buffer、CPU 上传内存、descriptor 和相关资源；并不要求每次提交同时使用两种机制。WSI 同步另外遵守呈现协议。进阶内容是自动多队列、并行录制、瞬态 alias、bindless 更新等优化，而不是基本完成条件。

| 对象 | 核心作用 | 不能替代什么 |
|---|---|---|
| Fence | host 观察一次相关提交工作完成，支持帧资源复用等 | 不自动证明 presentation engine 已释放 present wait semaphore |
| Binary semaphore | GPU/WSI 操作间的单次信号/等待协议 | 不提供可任意查询递增数值的完成序列 |
| Timeline semaphore | 合理设计下表达队列提交序列与 wait/signal 数值 | 不替代所有 image layout、memory barrier 或 WSI binary 协议 |
| Pipeline/resource barrier | 描述执行与内存依赖、image layout；按需处理 ownership | 不等于 CPU 已经可以释放资源 |
| CPU job completion | 证明 CPU 录制/拷贝任务已结束 | 不证明 GPU 已执行这些命令 |

基本依据：[Vulkan 同步概览](https://docs.vulkan.org/guide/latest/synchronization.html)、[同步规范](https://docs.vulkan.org/spec/latest/chapters/synchronization.html)、[命令缓冲规范](https://docs.vulkan.org/spec/latest/chapters/cmdbuffers.html)。

### 11.2 完成观察与 GPU 等待分开

RHICompletionPoint/CompletionSet 表示 host 可查询的完成条件，包含队列/设备身份与实际提交序列。RHIGpuDependency 描述 GPU 提交必须等待的依赖及相应使用范围。Fence 只能做 host 观察，不能被误当成任意 GPU wait 对象；timeline 可以支持两种用途，但接口语义仍需分开。

每条实际 queue 的 completed 值仅与自己的 submitted 序列比较。不同 queue 的数值不能直接取 max 作为全设备时间；如果资源跨队列使用，retirement 必须满足所有最后使用点，或满足经过证明已支配这些使用的汇聚点。host 人工 signal 的值不能被滥用为“对应 GPU 工作已完成”的证明。

`vkQueueSubmit2` 中 semaphore signal 的 `stageMask` 决定其范围。作为整批回收凭证的完成标记必须覆盖相关全部命令，首版采用 `ALL_COMMANDS_BIT` 范围；用于细粒度 GPU 依赖的较早信号不能自动当成整批资源可释放的证据。[VkSemaphoreSubmitInfo](https://docs.vulkan.org/refpages/latest/refpages/source/VkSemaphoreSubmitInfo.html)

### 11.3 一帧中的安全流程

1. 选 frame slot，等待其 CPU 录制任务和 GPU 完成条件，之后才复用 frame arena、命令池、临时描述符与上传区间。
2. 检查 framebuffer 像素尺寸；零尺寸暂停相关呈现，仍可维持需要运行的模拟/音频。
3. Acquire 获得 image index 与可用依赖；处理 OutOfDate/SurfaceLost/超时及失败，不沿用旧 image index。
4. 收集上传与外部资源的初始状态，构建/编译 RenderGraph，确定队列和访问顺序。
5. 按计划录制命令；所有被命令引用的资源与快照保持有效。
6. 确定确实会提交后，才进行该提交所需的 fence reset；submit 成功后记录 submitted 序列和末状态。
7. Present 等待正确的 binary semaphore；单独处理 Suboptimal/OutOfDate 和后续重建。
8. 轮询 completed 值，回收已经满足全部条件的资源；提交失败或 device lost 进入明确错误路径。

Acquire 返回 SUBOPTIMAL 仍已取得图像。获取后若录制/提交失败，需要处理已有 image/semaphore 状态，不能直接复用。提交前预留跟踪记录，成功后的 commit 不应再依赖可能失败的内存分配。多批次中途失败时，保留已被接受批次的状态，只取消未提交部分；无副作用失败与 device lost 分开处理，后者作废设备 epoch，不能标记成正常完成。[vkQueueSubmit2](https://docs.vulkan.org/refpages/latest/refpages/source/vkQueueSubmit2.html)

### 11.4 frame index 不等于 image index

frame slot 管 CPU/GPU 帧上下文；image index 是交换链返回的图像；completion value 是提交序列。交换链图像数量可能与 frames-in-flight 不同。

常规安全方案是按 swapchain image 管理 present wait semaphore，并在再次 acquire 对应 image 且等待其获取同步后，证明上一次相关 presentation 使用已结束。仅等待 submit fence 不足以证明 present wait semaphore 可以复用。若采用 present fence 等扩展，需要相应 capability 与生命周期分支。[Khronos semaphore 复用说明](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html)

交换链重建应处理旧 image view、同步对象和呈现生命周期。没有相应呈现完成机制时，WaitIdle 是常见工程退路，不能宣称它提供完整规范证明；若要求严格证明，应将 swapchain maintenance 的 present fence 等可验证机制纳入支持条件。该限制应写进设备支持矩阵。[Khronos WSI 说明](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html)

Present 返回 OUT_OF_DATE/SURFACE_LOST 时，其 semaphore wait 仍被视为已入队，不能按“失败意味着未消费”处理。[Present 规范](https://docs.vulkan.org/refpages/latest/refpages/source/vkQueuePresentKHR.html) 以非空 oldSwapchain 创建新链后，即使新建失败旧链也已 retired，不能继续 acquire 旧链。[Swapchain 创建规范](https://docs.vulkan.org/refpages/latest/refpages/source/VkSwapchainCreateInfoKHR.html)

### 11.5 RenderGraph 与资源状态的唯一权威

| 责任对象 | 它可以修改的状态 | 不允许做的事 |
|---|---|---|
| RenderGraph/Compiler | 声明访问、依赖、队列映射、计划中的前后状态与 barrier | 修改尚未成功提交的全局事实状态 |
| RenderGraphPlan | 已编译计划、资源生命周期、提交依赖 | 执行时被随意重排而不重新编译 |
| FrameScheduler | 依计划录制/提交，收集 completion | 自行发明另一套资源状态转换策略 |
| VulkanResourceStateTracker | 已接受提交的范围状态、writer/readers frontier、ownership 中间状态 | 把计划状态或 GPU completed 值当作同一件事 |
| VulkanSubmissionTracker | 每 queue 的 submitted/completed 序列及完成查询 | 只按帧号推断所有队列完成 |
| VulkanBarrierCompiler | 把计划的访问和转换语义转换为合法 Vulkan barrier | 猜测未声明的外部读写 |
| VulkanResourceRetirement | 满足最终使用完成条件后销毁实际对象/归还分配 | 仅因 C++ owner 析构就无条件立即 vkDestroy |

状态范围至少考虑 image 的 aspect/mip/layer 和 buffer 的实际范围，初期允许保守合并，但不能错误忽略。exclusive sharing 资源跨 queue family 时还需 ownership transfer；semaphore 排序本身不能被解释成完成了所有 ownership/layout 工作。已提交末状态代表队列顺序中的逻辑状态，不等于此刻硬件已经执行到那里。

单一权威不等于只记一个“最后访问”：多 queue 并行读取后，后续写要等待全部相关 readers；因此保留每范围 last-writer/readers frontier，或使用能证明等价的保守串行化规则。ownership release 已接受而 acquire 尚未接受的状态也必须能表示。

UploadScheduler、ReadbackManager、第三方 UI integration 的访问也要进入图或明确的 external import/export 协议。绕过图提交命令会破坏其推导前提。多图/多线程并行提交时，需要针对同一 state authority 串行化提交事务或建立等价的受控机制。

### 11.6 上传、回读与内存管理

UploadScheduler 负责 staging 区间、拷贝、目标资源可用依赖与 staging 回收；VMA 负责分配/映射/flush/invalidate，不负责“GPU 已用完”。非 coherent 内存需正确刷新/失效并遵守对齐要求。[VMA 映射说明](https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/memory_mapping.html)

ReadbackManager 只有在复制提交完成和 host 可见性满足后才交付结果，适用于截图、picking、统计。CPU 不应为了读取一个 entity 的 picking 结果无条件阻塞整台 GPU；结果应带请求 ID、view/version，避免旧结果选中已销毁实体。

每线程 command pool、每帧 descriptor pool 可以减少锁，但“每帧结束”不是复用条件。多线程时还必须保证池不被多个线程同时进行需外部同步的操作。[Vulkan threading](https://docs.vulkan.org/guide/latest/threading.html)

### 11.7 延迟销毁与缓存

GPU cache 的引用计数归零只表示逻辑不再需要该表现资源。命令已录制但未提交的引用、已提交的使用点、上传/回读、descriptor 中仍可被执行的引用都要处理。最后由唯一 retirement 入口执行 Vk 对象销毁，再释放对应分配。

热重载创建新 shader/material/mesh 版本并切换后续帧，旧版本不能立即清空。PipelineCache 的磁盘 blob 需要绑定设备/驱动及相关兼容标识，读取失败可以丢弃重建；它是性能缓存，不是唯一资产来源。正常路径不依赖“等三帧就肯定没用了”。

<a id="section-12"></a>

## 12. 第三方库选择与集成边界

以下为官方仓库/文档核查后的推荐，不等于已完成 Catalpa 编译、性能或许可证传递依赖审计。默认选择给一个主路径，备选有触发条件；不是把清单中所有库立刻安装。

### 12.1 运行时及基础依赖

| 领域 | 默认选择 | 放置与职责 | 备选/限制 |
|---|---|---|---|
| 窗口输入 | [GLFW](https://github.com/glfw/glfw)，保留 | 平台窗口、输入、Vulkan surface 桥接 | [SDL3](https://wiki.libsdl.org/SDL3/FrontPage) 在更广输入/平台需求出现时评估，不因“完整引擎”无理由替换 |
| Vulkan loader | 当前 GLAD，保留 | 仅 Vulkan backend；保存可复现生成配置 | [volk](https://github.com/zeux/volk) 在多 device dispatch/扩展维护需要时替换；不默认两套并存 |
| Vulkan 初始化辅助 | 暂不作为必需 | 自己的 instance/device 选择流程即可 | [vk-bootstrap](https://github.com/charles-lunarg/vk-bootstrap) 可减少初始化代码，但不替代 RHI/sync；需验证 header/loader 集成 |
| GPU 内存 | [VMA](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) | Vulkan backend 私有分配器 | 不负责 GPU 最后使用和延迟销毁 |
| 数学 | [GLM](https://github.com/g-truc/glm) | Core/Math 统一配置 | 宏必须在首次 include 前统一；GPU packing/磁盘格式由引擎定义 |
| ECS | [EnTT](https://github.com/skypjack/entt) | ECS/World 内部实现 | [Flecs](https://github.com/SanderMertens/flecs) 适合需要其关系/prefab/pipeline 体系时整体评估，二选一 |
| CPU tasks | [enkiTS](https://github.com/dougbinks/enkiTS) | JobSystem adapter | 不替代 ECS 阶段调度；有复杂任务图需求再评估 Taskflow，避免双 scheduler 争用线程 |
| 日志 | [spdlog](https://github.com/gabime/spdlog)，保留 | Log adapter | 热路径和 audio callback 不做不可控同步输出 |
| 配置与作者文档 | [nlohmann/json](https://github.com/nlohmann/json) | 配置/项目元数据/编辑文档 | 大规模 runtime 资产走版本化二进制格式；yaml-cpp 是偏好替代而非必需第二套 |
| 包压缩 | [zstd](https://github.com/meta/zstd) | cooker 写入、PackageReader 解压 | 分块/长度/校验与内存上限由引擎制定，不把整包无限制解压 |
| 物理 | [Jolt](https://github.com/jrouwe/JoltPhysics) | Physics backend；初期 CPU 物理 | 当前已有可选 GPU compute/hair；关闭未规划的 compute 后端，避免另一套设备提交体系；Bullet 可按特殊需求替代 |
| 音频 | [miniaudio](https://github.com/mackron/miniaudio) | Audio backend，先用现有 engine/mixer | 官方不保证版本间 ABI；对象地址稳定，更新需重编译和回归 |
| 动画 | [ozz-animation](https://github.com/guillaumeblanc/ozz-animation) | Animation adapter + 离线转换 | 采样/混合不是完整 Animator graph/IK/editor/GPU skinning |
| 导航 | [Recast/Detour](https://github.com/recastnavigation/recastnavigation) | Recast 在 Tools；Detour 在 Navigation | 按需求选 Crowd/TileCache，不等于游戏 AI |
| 脚本 | [Lua](https://www.lua.org/) + 薄绑定 | Scripting backend | [sol2](https://github.com/ThePhD/sol2) 可选；不能默认最新版 Lua 与其现有 release 完全兼容 |
| 游戏 UI | [RmlUi](https://github.com/mikke89/RmlUi) | UI adapter，渲染经 UIDrawData/RHI | HTML/CSS 类模型不等于完整浏览器；字体/文本 shaping 的构建依赖需记录 |
| 网络 | [GameNetworkingSockets](https://github.com/ValveSoftware/GameNetworkingSockets)，按需 | Networking transport adapter | 可靠/不可靠消息传输不等于 replication、预测和多人产品架构；未启用时可仅做 Loopback 验证 |

EnTT 提供 ECS 数据组织而不接管整个引擎循环；Flecs 自带更丰富的关系与 pipeline 模型，选择它意味着接受更深的数据模型约束。[EnTT 文档](https://skypjack.github.io/entt/md_docs_2md_2entity.html)、[Flecs 文档](https://github.com/SanderMertens/flecs)

GLM 的 `GLM_FORCE_DEPTH_ZERO_TO_ONE` 不能自动解决 viewport/texture 的全部 Y 方向问题；统一 MathConfig 与投影契约比零散开宏重要。[GLM 配置说明](https://github.com/g-truc/glm/blob/master/manual.md)

### 12.2 导入、烘焙与 shader 工具链

| 领域 | 主选 | 产物/边界 | 备选或注意事项 |
|---|---|---|---|
| glTF/GLB | [fastgltf](https://github.com/spnda/fastgltf) | Tools 转成自有 Model/Mesh/Material/Animation 资产 | 当前主线 C++20；[cgltf](https://github.com/jkuhlmann/cgltf) 是轻量 C 替代，不自动构成完整资产系统 |
| 多格式模型 | [Assimp](https://github.com/assimp/assimp)，可选 | 额外 importer adapter | 有 FBX/OBJ 等需求时添加；不把所有 parser 默认塞进 Player |
| Mesh 加工 | [meshoptimizer](https://github.com/zeux/meshoptimizer) | 重排、LOD、量化、meshlet/压缩按需 | 记录参数与误差；runtime 只在需要时保留 decoder |
| 切线 | [MikkTSpace](https://github.com/mmikk/MikkTSpace) | 与法线烘焙一致的切线生成 | 正确处理 per-face-vertex 结果与拆点 |
| 纹理容器 | [KTX-Software](https://github.com/KhronosGroup/KTX-Software) | tools 编码，runtime 读取与必要转码 | KTX2 是容器而非单一压缩格式；不用其 Vulkan upload API 绕过引擎 RHI |
| 可移植纹理编码 | [Basis Universal](https://github.com/BinomialLLC/basis_universal)，按 KTX 路线取用 | 编码器工具端，必要 transcoder 运行时 | 避免与 KTX 所带功能重复集成完整 encoder |
| 普通源图 | [stb_image](https://github.com/nothings/stb) | 受控 PNG/JPEG 等导入 | 不是 mip、色彩管理或正式纹理资源系统 |
| 专业 EXR | [OpenEXR](https://github.com/AcademySoftwareFoundation/openexr) | 工具端 HDR 导入 | [TinyEXR](https://github.com/syoyo/tinyexr) 是可选替代；当前 v3 为 C11，旧单头版本与格式支持不可混谈 |
| GLSL 编译 | [glslang](https://github.com/KhronosGroup/glslang/blob/main/README.md) | 默认 shader compiler 主路径 | 其 HLSL 前端已弃用，不把它推荐为未来 HLSL 主工具 |
| HLSL 编译 | [DXC](https://github.com/microsoft/DirectXShaderCompiler/blob/main/README.md)，替代路线 | 若选择 HLSL/D3D12 共享代码则评估 | Windows SDK 的 DXC 发行不能直接假定带 SPIR-V 支持，需锁定正确构建 |
| 模块化 shader | [Slang](https://github.com/shader-slang/slang)，替代路线 | 有泛型/interface、多目标需求时评估 | 是语言/工具链决策，不同时默认为三套 runtime 依赖 |
| SPIR-V 校验/优化 | [SPIRV-Tools](https://github.com/KhronosGroup/SPIRV-Tools) | 编译产物合法性检查、受控优化 | 与 compiler/SPIRV-Headers 版本配套 |
| SPIR-V 反射 | [SPIRV-Reflect](https://github.com/KhronosGroup/SPIRV-Reflect) | 生成引擎 ShaderMetadata | 不是 validator，也不能决定材质变量的业务意义 |

统一流水线：源码及 include → 编译 → validate → 可选 optimize → 再 validate → 对最终字节码 reflect → 输出 SPIR-V 与引擎 metadata。shader key 包含 compiler/version、target environment、flags、defines、include 内容及 schema 版本。正式 Player 默认消费已编译产物，不依赖开发机 PATH 上未知的编译器。

### 12.3 编辑器、诊断和测试

| 领域 | 推荐 | 放置 |
|---|---|---|
| 工具 UI | [Dear ImGui](https://github.com/ocornut/imgui)，保留 | Editor/调试；platform 与 Vulkan renderer adapter 分开 |
| Gizmo | [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo) | Editor viewport；矩阵方向须与 Math 契约一致 |
| 文件选择 | [nativefiledialog-extended](https://github.com/btzy/nativefiledialog-extended) | Editor OS 适配，不成为 Player 必需依赖 |
| 性能分析 | [Tracy](https://github.com/wolfpld/tracy) | Profiling 构建启用的 CPU/GPU/内存/锁插桩 |
| GPU 抓帧 | [RenderDoc](https://github.com/baldurk/renderdoc) | 外部开发工具；可选捕获 API，不常规链接整套 GUI |
| 测试框架 | [Catch2 v3](https://github.com/catchorg/Catch2) | 独立 Tests 目标；v3 是编译库，不是旧版单头假设 |
| 轻量测试备选 | [doctest](https://github.com/doctest/doctest) | 与 Catch2 二选一，不重复维护两套 |
| 包依赖管理 | [vcpkg manifest](https://learn.microsoft.com/en-us/vcpkg/concepts/manifest-mode) | 锁 baseline、版本、features、triplet；可集成 Premake，不强迫迁移 CMake |

### 12.4 版本、许可和更新策略

`Dependencies.lock.json` 记录所选库的来源、tag/commit、目标、选项与校验信息；已有 vendored 依赖逐步登记，不静默换版本。`ThirdPartyNotices.md` 记录所锁版本的实际许可及必要 notices，工具和传递依赖也包括在内。

许可不能只看仓库首页一个标签。例如 GLM 有选择性许可，miniaudio 可选 Public Domain/MIT-0，KTX/glslang/DXC 有第三方来源；本地 GLAD 生成文件的 SPDX 与 GLAD 生成器仓库许可也不等同。此处不是法律审计结论。

更新要经过对应契约回归：EnTT 组件/跨模块边界、Jolt 配置与 SIMD/精度、miniaudio 对象生命周期、shader compiler 输出、asset cooker 格式与缓存失效。不以“跟随 main 自动最新”代替版本管理。

本轮核查的许可元数据可作为登记起点，最终以锁定版本的 LICENSE、文件头和传递依赖为准：

| 项目集合 | 上游主要许可标注/需要注意的边界 |
|---|---|
| EnTT、Flecs、Jolt、ozz、Lua、sol2、RmlUi、ImGui、spdlog、nlohmann/json | 主要为 MIT；字体和示例资产另查来源 |
| GLFW、SDL、Recast/Detour、MikkTSpace | zlib 或 zlib-style；逐库保存原文 |
| VMA、volk、vk-bootstrap、fastgltf、cgltf、meshoptimizer、doctest、RenderDoc | 主要为 MIT；仍需检查被打包的第三方内容 |
| GLM | MIT 或其 Happy Bunny 许可选项 |
| miniaudio、stb | 分别提供 Public Domain/MIT-0、Public Domain/MIT 选择 |
| SPIRV-Tools、SPIRV-Reflect、Basis Universal | 主要为 Apache-2.0 |
| KTX-Software | 主项目主要为 Apache-2.0，所带第三方部分另有许可 |
| Assimp、OpenEXR、TinyEXR、Tracy | 主要为 BSD-3-Clause 范畴，具体构建内容和依赖单独记录 |
| Catch2 | BSL-1.0 |
| glslang、DXC、Slang | 不能统一写成 MIT；glslang/DXC 有多来源，Slang 核心有 Apache-2.0 WITH LLVM-exception |
| zstd | 官方提供 BSD/GPL 许可选择，记录实际采用路径及其文件 |
| 本地 GLAD 生成文件 | 当前 SPDX 为 `(WTFPL OR CC0-1.0) AND Apache-2.0`；不等同生成器仓库许可 |

这里没有把“上游许可看起来宽松”推导为“整个发布包已经完成许可核查”。enkiTS、ImGuizmo、文件对话框、网络库及字体等实际引入时同样进入锁文件与 notices。

<a id="section-13"></a>

## 13. 工程目标、目录与交付物

### 13.1 构建目标划分

| 逻辑目标 | 主要内容 | 允许的上层使用者 |
|---|---|---|
| CatalpaFoundation | Core 基础、Math、Jobs、IO、日志与通用序列化 | runtime、tools、tests |
| CatalpaAssets / CatalpaDomain | 通用资产协议及领域数据/算法 | Scene、Renderer、Audio/Physics、Tools |
| CatalpaECS / CatalpaScene | World、组件、场景与持久化 | Runtime、Editor、Tests |
| CatalpaRHI | API 无关类型、接口与调度协议 | Renderer、后端与集成目标 |
| CatalpaVulkan | Vulkan backend 私有实现 | 组合根按工厂选择 |
| CatalpaRenderer | RenderView/Scene、缓存、图与 pass | Runtime、Editor |
| CatalpaImGuiIntegration | ImGui 公共桥与所选后端 adapter | Editor/调试目标；避免 Vulkan 核心反向依赖整个 Renderer |
| CatalpaRuntime / Catalpa | 服务编排、系统桥接、Application，最终公开入口 | Player、CatalpaBox、Editor |
| CatalpaAssetPipeline | 可复用 import/cook 流水线 | Editor 与 AssetCompiler CLI |
| ShaderCompilerCore | 可链接的编译/校验/反射核心，不含 main | AssetPipeline、开发期热重载工具 |
| CatalpaShaderCompiler | 独立 shader CLI，链接 ShaderCompilerCore | 开发/资源构建流程 |
| CatalpaAssetCompiler | 独立资产 CLI，链接 CatalpaAssetPipeline | 内容构建流程 |
| CatalpaPackager / CatalpaAssetInspector | 独立打包/只读检查 CLI | 发布流程与资产诊断 |
| CatalpaEditor | 文档/命令/面板/视口 | 独立可执行程序 |
| CatalpaBox / CatalpaPlayer | 示例游戏 / 可打包运行入口 | 终端应用 |
| CatalpaTests | 基础、领域、同步、工具与集成测试 | 开发/CI |

这些是依赖边界，可先由少量静态目标实现；不是要求一次创建同数量 DLL。RHIBackendFactory 的接口属于 RHI，实际后端注册/选择在组合目标，RHI 本身不需要直接 link 所有未来后端。

### 13.2 文件清单的范围

详见 [完整文件清单](catalpa_engine_file_manifest.md)。它包含本方案自有源码、公共契约、平台后端、编辑器、工具、构建脚本、测试、示例、内置资源、配置和文档的具体建议文件，并逐项说明职责、允许依赖和阶段。

第三方项目有自己的大量源文件，应由锁定的版本清单和上游构建管理；本文不会把 GLFW/Jolt 等整个上游仓库逐文件复制成引擎设计清单。`.vs`、`bin`、`bin-int`、生成的 `.vcxproj/.sln`、DDC、Cooked 内容属于生成物类别，**没有稳定且有限的“最终所有文件名”**；清单给管理脚本、schema 与具体示例产物，不伪造未来每份用户内容。

完整文件清单是目标责任划分，现有文件的职责也可能是未来建议而非当前行为。落地时根据真实代码合并小实现文件是允许的，但不能丢失其契约或让单一 Manager 承包所有职责。

### 13.3 分发三种不同产品

- **源码包**：受版本控制的源码、Premake/构建入口、依赖获取/版本说明、必要资源和文档。不是通过删除当前工作目录来“整理”，而是按 manifest 导出。
- **运行包**：Player/游戏程序、必要引擎与第三方动态库、cooked assets、预编译 shaders、默认配置、许可与安装说明；不包含 Editor/源素材 importer/编译器全集。
- **SDK**：经过选择的公共头、链接产物、明确的编译器/CRT/配置约束、示例和版本说明。全量 `Catalpa/src` 不是稳定 SDK。

动态 CRT 部署优先使用官方 Visual C++ Redistributable，并按所锁工具链核对允许分发的内容。不能将“绝对禁止 app-local 分发”作为无条件规则；具体方式取决于许可和微软支持的部署路径。[微软部署说明](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files?view=msvc-170)

用户运行设备需要兼容的 Vulkan loader/驱动和满足 feature 的 GPU，不需要安装开发用 Vulkan SDK。安装/检测策略要按发布平台验证，不能盲目从开发机复制系统 DLL。包应在没有 Vulkan SDK、没有源码目录与开发机 PATH 帮助的环境进行测试。安装包格式属于后续发布工具选择，不要求仅为 CPack 而把 Premake 项目改成 CMake。

<a id="section-14"></a>

## 14. 实施阶段与可验证的完成条件

阶段用于控制依赖顺序，不用于削弱架构正确性；尤其 GPU 同步和资源回收不能推迟到“画面做好以后”。

| 阶段 | 交付闭环 | 完成条件 |
|---|---|---|
| P0 基础与提交正确性 | 平台初始化、错误路径、RHI/Vulkan device、交换链、同步、内存、上传、最小绘制与关闭 | validation/synchronization validation 无已知错误；反复 resize/minimize/恢复/关闭稳定；能证明帧复用和资源回收 |
| P1 静态 3D 世界 | World/Scene/transform、模型与纹理加载、Mesh/Material/Camera/Light、可见性、基本 pass、保存/加载 | 同资产多实例、多材质槽、多 view、资源失败 fallback、场景 round-trip 正确；无 Vk 泄露到应用头 |
| P2 内容编辑与交付 | Editor、导入/cook、shader metadata、热重载、Undo/Redo、package、Player | 原素材不被覆盖；失败导入可诊断；编辑与运行世界隔离；净环境包可运行 |
| P3 动态世界 | 动画、物理、空间音频、脚本与正式游戏 UI 按目标启用 | 固定步与 transform authority 明确；声音停止/卸载无悬空；动态资源热更新和退出可验证 |
| P4 产品扩展与优化 | 导航、粒子、地形、联网、复杂渲染、多队列等按需要 | 每项有实际场景、预算与回归；新队列/访问协议不破坏旧同步契约 |

P0 可以先单 queue、单录制线程、固定 pass 顺序；RenderGraph 在 P0 从完整声明依赖的保守简单计划开始，P1 扩展真实场景 pass。最小 shader/pipeline 路径属于 P0，资产化 shader 管理属于 P1。之后再加入多队列和 alias，不需一开始做最复杂优化，但前期接口必须容纳明确的提交/完成条件。

### 14.1 需要的测试不是一堆空用例

| 类别 | 要验证的真实契约 |
|---|---|
| 句柄/World | generation 防陈旧引用、跨 World 拒绝、结构变更安全点、持久化 ID 映射 |
| 场景/数学 | transform 层级与循环拒绝、projection/frustum 一致、矩阵方向、单位和非均匀缩放边界 |
| Mesh/Material | 越界 index/submesh、材质槽/schema、顶点输入、默认资产与缺失引用 |
| 资产/工具 | 依赖失效、取消、热重载旧版本保活、损坏 package、cook 可追溯、原文件不变 |
| GPU 计划 | read/write hazard、初末状态、子资源、外部导入、multi-queue waits、ownership |
| 回收 | 仅录制未提交、submit 失败、多队列最后使用、descriptor/上传引用、真正完成后回收 |
| WSI | frame/image 不同数量、快速 resize、零尺寸、surface lost、acquire 后失败、present semaphore 复用 |
| Physics/Animation | fixed step、dynamic/kinematic 写入权、teleport、骨骼索引、root motion 时序 |
| Audio | 命令顺序、队列满策略、clip 卸载前 stop ack、无设备 NullBackend、callback 线程约束 |
| 构建/发布 | 公共头独立编译、禁用后端类型泄露、无 Editor 依赖的 Player、锁版本复现与净环境运行 |

单元测试不能替代真实 GPU validation、抓帧和硬件测试。性能以目标场景中的 Tracy/RenderDoc/计数数据评估，不能从“thin wrapper”“inline”“RAII”推导出零开销保证。

<a id="section-15"></a>

## 15. 本轮交付、验证边界与结论

本轮交付两份 Markdown：本文与逐文件清单。其他 AI 原始文档保留；引擎代码、构建脚本、第三方库和原始资源均未修改。

文档验证关注 UTF-8 可读性、文件清单重复项、职责字段完整性、现有源码覆盖、文件存在状态、规划模块覆盖、本地文档链接与原参考文档完整性。没有运行引擎构建、Vulkan 测试、库集成或性能测试；第 14 节是未来实施的验收计划，不能视为本轮通过记录。

本轮已通过的文档静态检查：783 个文件条目与完整文件树的 783 个叶节点集合一致；覆盖当前 33/33 个自有源码文件；重复路径和空职责字段均为 0；62 处本地文档/锚点链接有效；两份文档可按 UTF-8 读取。原参考文档 SHA-256 前后一致：`3A59A98FBFF8CD4F0AC6E2692F2E7178CE92A1DBF54C3231E7D6F888F95C11BD`。

结论判断：**架构方案可行；项目距离完整引擎仍有大量实现和验证工作。** 不建议直接复制任意一个热门项目的目录，也不建议把所有 Vulkan 对象包装一遍就称为完成引擎。长期健壮性取决于明确依赖、稳定身份、唯一所有权、可证明的同步和可复现的内容/构建流程；目录和文件应当为这些契约服务。
