# Third-party notices — S03.1 dependency batch

本文件仅登记本批已核实的 GLM 和 EnTT；既有 GLFW、GLAD、ImGui、spdlog、VMA、JSON、Slang 等依赖的完整清单仍由 B01 后续补齐，不能据此认为全项目许可审阅已完成。

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

本批优先尝试完整 Git 克隆，未完成后采用官方固定标签的完整源码 ZIP。解压目录只有包名，版本、归档 SHA-256、逐文件内容汇总及许可校验记录见 [Dependencies.lock.json](Dependencies.lock.json)。校验复制后的完整内容后删除下载 ZIP；源码未经删减或修改。S03.1-B 当时没有创建两库的 Git 子模块。随后用户要求沿用外部依赖方式、暂不将库内源码作为主仓库普通文件加入；现 `.gitignore` 忽略 GLM/EnTT 目录，`.gitmodules` 补齐其路径与官方仓库声明。声明不等于已注册 gitlink，本轮未执行 Git 或更改源码；Dependencies.lock.json 的 archive 获取事实及 submodule=false 保留，真实登记/暂存/提交/推送由用户执行确认。

重新分发相关源码或二进制时保留对应版权和许可文本；分发上游文档/测试/示例时也保留其已有来源与附属许可。引擎并不将两库的内存布局作为稳定插件、GPU 或磁盘 ABI。具体 GLM/EnTT API 的编译和运行验证等待 S03.1 消费者实现。
