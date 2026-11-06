# Third-party notices

本文件登记已核实的 GLM、EnTT、enkiTS、SPIRV-Reflect，以及本机现有 Vulkan SDK 中所选工具的来源/许可依据；既有其他依赖和完整发布包的许可清单仍由 B01 后续补齐，不能据此认为全项目许可审阅已完成。

## GLM 1.0.3

- 官方仓库：https://github.com/g-truc/glm
- 固定发布：https://github.com/g-truc/glm/releases/tag/1.0.3
- 本地完整源码：`Catalpa/third_party/glm/`；包含路径为该目录。
- Copyright (c) 2005 - G-Truc Creation。
- 上游提供 Happy Bunny 或 MIT 许可选项；本工程采用 MIT。完整原始版权及许可保留在 [copying.txt](Catalpa/third_party/glm/copying.txt)，没有改写上游文件。
- 只接入数学头文件；完整源码中的文档、测试、示例及其附属版权说明保留，未运行上游构建/安装/测试脚本。

## EnTT v4.0.0

- 官方仓库：https://github.com/skypjack/entt
- 固定发布：https://github.com/skypjack/entt/releases/tag/v4.0.0
- 本地完整源码：`Catalpa/third_party/entt/`；包含路径为其 `src/` 子目录，仅供 Catalpa 内部使用。
- Copyright (c) 2017-2026 Michele Caini, author of EnTT。
- MIT 许可原文保留在 [LICENSE](Catalpa/third_party/entt/LICENSE)。
- 只接入 ECS 头文件；完整源码中的工具、示例、测试与文档保留，不为它们引入附加依赖或构建目标。

## 获取、校验与分发

S03.1-B 历史获取记录：当时优先尝试完整 Git 克隆，未完成后采用官方固定标签的完整源码 ZIP。解压目录只有包名，版本、归档 SHA-256、逐文件内容汇总及许可校验记录见 [Dependencies.lock.json](Dependencies.lock.json)。校验复制后的完整内容后删除下载 ZIP；源码未经删减或修改。S03.1-B 当时没有创建两库的 Git 子模块。随后用户要求沿用外部依赖方式、暂不将库内源码作为主仓库普通文件加入；现 `.gitignore` 忽略 GLM/EnTT 目录，`.gitmodules` 补齐其路径与官方仓库声明。声明不等于已注册 gitlink，本轮未执行 Git 或更改源码；Dependencies.lock.json 的 archive 获取事实及 submodule=false 保留，真实登记/暂存/提交/推送由用户执行确认。

重新分发相关源码或二进制时保留对应版权和许可文本；分发上游文档/测试/示例时也保留其已有来源与附属许可。引擎并不将两库的内存布局作为稳定插件、GPU 或磁盘 ABI。GLM/EnTT 后续消费者的用户验证证据已在 docx/01 记录；这里保留当时获取事实。

## enkiTS v1.12

- 官方固定标签：[v1.12](https://github.com/dougbinks/enkiTS/releases/tag/v1.12)；完整源码目录 `Catalpa/third_party/enkiTS/`。
- Copyright (c) 2013 Doug Binks；zlib 许可原文保留在 [License.txt](Catalpa/third_party/enkiTS/License.txt)。
- 构建适配只编译 `src/TaskScheduler.cpp`，静态链接到 Catalpa，供本章 Core/Jobs 的真实worker使用；不导出上游类型作为引擎公共 ABI，不额外构建 C 包装层、示例或测试。
- 保持上游默认 3 个任务优先级和静态库 API 宏。v1.12 标签内 `CMakeLists.txt` 仍写 1.11，是上游版本字段未同步；按固定标签及归档校验锁定，不修改上游。

## SPIRV-Reflect vulkan-sdk-1.4.363.0

- 官方固定标签：[vulkan-sdk-1.4.363.0](https://github.com/KhronosGroup/SPIRV-Reflect/tree/vulkan-sdk-1.4.363.0)；完整源码目录 `Catalpa/third_party/SPIRV-Reflect/`。
- Copyright 2017–2022 Google Inc.；Apache-2.0 许可原文保留在 [LICENSE](Catalpa/third_party/SPIRV-Reflect/LICENSE)。随包 [spirv.h](Catalpa/third_party/SPIRV-Reflect/include/spirv/unified1/spirv.h) 自带 Khronos 的 MIT 许可头，两者分别保留。
- 仅将 `spirv_reflect.c` 构建为静态库，由Tools/ShaderCompiler私有使用；Box作为当前工具宿主最终链接所需实现，不在公共接口暴露上游类型，Catalpa Runtime不依赖反射库。新消费者待整章Debug验收。
- Reflect 不替代 SPIR-V validator。本章元数据针对最终实际字节码，先校验再反射；不解析 CLI 的人读文本/YAML来冒充稳定反射协议。

## 现有 Vulkan SDK 工具

本机静态文件记录为 SDK 1.4.363.0，Slang tag version 为 `2026.17-25-gc7954ebd1`。保留现有 `VULKAN_SDK` 路径解析和 `Slang.lua` 调用，不下载/升级 SDK，不复制其 DLL，不新增 Slang 库链接。`slangc.exe`、实际 `slang-compiler.dll` 与本章Tools使用的 `spirv-val.exe` 哈希见锁文件；仅文件观察，未执行版本命令，也不是本轮运行兼容性证明。

- Slang 对应标签：[Apache-2.0 WITH LLVM-exception 原文](https://github.com/shader-slang/slang/blob/vulkan-sdk-1.4.363.0/LICENSE)。
- SPIRV-Tools 对应标签：[Apache-2.0 原文](https://github.com/KhronosGroup/SPIRV-Tools/blob/vulkan-sdk-1.4.363.0/LICENSE)。
- SDK 的 `Licenses/LICENSE.txt` 只是汇总导航，不把它当作以上组件的完整许可证；本批不重新分发 SDK。后续若分发工具，必须随实际组件携带完整许可及相关声明。

## 本次新增依赖的获取与版本控制边界

2026-11-04，按用户明确授权接入 S04 所需依赖并更新 `.gitmodules`。用户禁止所有 Git 命令，因此未尝试 clone，直接下载上述两个官方固定标签完整 ZIP；核对每个解压文件和许可、计算归档与源码树 SHA-256 后删除 ZIP。目录仅保留包名，未删减源码或应用上游补丁，未运行任何上游脚本。

`.gitignore` 忽略两个完整源码目录，`.gitmodules` 声明官方仓库与路径；声明不等于 gitlink 注册。锁文件仍记录 `submodule=false` 和真实 archive 获取方式，本批没有 Git 操作、提交或推送证据。新克隆仅靠这两个声明不能自动恢复 archive；按锁文件固定 URL、目标目录与校验值还原，或由用户以后明确建立真实子模块。

本章消费接线：enkiTS用于Catalpa内部JobSystem的真实worker任务；SPIRV-Reflect仅由Tools/ShaderCompiler链接。Slang.lua递归追踪输入，在Box完成链接后调用共享工具命令执行受锁定工具、校验及反射；正常Runtime只消费cooked资产。以上新消费链待用户整章Debug验收，不扩大依赖准备批通过证据。上游文件、版本、许可及忽略规则未改变。

## ImGuizmo 用户供包（2026-11-05）

- 官方仓库：[CedricGuillemet/ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo)。完整本地目录为 `Catalpa/third_party/ImGuizmo/`，用户已提供；助手未下载、替换或修改上游文件。
- 本地头标记 `v1.92.5 WIP`。仅核对参与编译的 `src/ImGuizmo.cpp`、`src/ImGuizmo.h` 与 `LICENSE`：在内存中统一换行后，三者与官方提交 [105e4fde45e4e74aae8973d98750820ef7aca7e2](https://github.com/CedricGuillemet/ImGuizmo/commit/105e4fde45e4e74aae8973d98750820ef7aca7e2) 相同。原始文件的 SHA-256 见锁文件；保留本地 CRLF，不将此记录扩张为全包校验或发布标签确认。
- MIT 许可与原始版权保留在 [LICENSE](Catalpa/third_party/ImGuizmo/LICENSE)；源码头保留 Cedric Guillemet 及贡献者声明。分发时携带完整许可。
- 复用 `Script/Build/ImGui.lua`，只将 `src/ImGuizmo.cpp` 编入现有 Catalpa.dll；该文件单独关闭 PCH 并设置 Windows 导出宏。CatalpaEditor 使用同一 DLL 的导入接口和现有 ImGui 上下文。上游示例、额外控件与 CMake 不参与构建，不引入第二份 ImGui。
- `.gitignore` 忽略用户供包源码，`.gitmodules` 仅声明路径和官方仓库；助手未执行 Git，也未确认 gitlink、暂存、提交或推送状态。后续联合功能批已实现 TransformGizmo 的矩阵适配、连续预览、释放提交与取消恢复，复用 TransformDrag/CommandHistory；仅完成静态审阅，生成、编译与原生交互仍待用户验证。

## 作者工具与脚本依赖接线（2026-11-06）

用户提供的三个目录由 `Script/Build/Authoring.lua` 直接编译为静态库。实际版本从本地头文件或构建声明读取；构建输入和许可文件摘要记录在锁文件中，尚未核实下载归档或上游提交。

- imgui-node-editor 0.9.4 使用包内 [MIT LICENSE](Catalpa/third_party/imgui-node-editor/LICENSE)。仅编译根目录四个实现文件，调用 Catalpa.dll 的现有 ImGui，后续由 Editor 承载画布与文档交互。
- nativefiledialog-extended 的本地构建声明为 1.4.1，使用 [Zlib LICENSE](Catalpa/third_party/nativefiledialog-extended/LICENSE)。Windows 编译 `src/nfd_win.cpp` 并链接系统库 ole32、uuid、shell32，由 Editor 消费。
- Lua 5.5.1 使用 [发行包 readme 中的 MIT 许可](Catalpa/third_party/lua/doc/readme.html#license)。静态库保留标准库实现，运行时的库开放策略由后续 ScriptRuntime 制定；不编译解释器和编译器入口。Catalpa 与 Tools 可私有调用 Lua，VM 和 Lua 对象不通过公共 API 传递。

新目录沿用外部依赖的忽略规则。`.gitmodules` 声明仓库来源，尚未登记 gitlink。接线只经过静态核对，工程生成、编译、文件对话框和脚本/画布功能等待各阶段验证。

## S14-S17 user-provided source libraries

Local source/license evidence only; exact upstream commits and complete transitive license audit remain unverified. See Dependencies.lock.json. KTX uses its bundled Basis transcoder and ASTC sources; their individual notices under external/ remain applicable. No network transport library was added.

- `meshoptimizer`: local license files: `Catalpa/third_party/meshoptimizer/LICENSE.md`.
- `zstd`: local license files: `Catalpa/third_party/zstd/COPYING`, `Catalpa/third_party/zstd/LICENSE`.
- `KTX-Software`: local license files: `Catalpa/third_party/KTX-Software/LICENSE.md`.
- `freetype`: local license files: `Catalpa/third_party/freetype/LICENSE.TXT`.
- `ozz-animation`: local license files: `Catalpa/third_party/ozz-animation/LICENSE.md`.
- `JoltPhysics`: local license files: `Catalpa/third_party/JoltPhysics/LICENSE`.
- `miniaudio`: local license files: `Catalpa/third_party/miniaudio/LICENSE`.
- `RmlUi`: local license files: `Catalpa/third_party/RmlUi/LICENSE.txt`.
- `recastnavigation`: local license files: `Catalpa/third_party/recastnavigation/License.txt`.
