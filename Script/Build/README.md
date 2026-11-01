# 现有第三方目录迁移为子模块

使用者只需按根目录 README 初始化子模块、生成 GLAD。

构建适配脚本保留在主仓库的 `Script/Build` 中，由根目录 `premake5.lua` 直接引用。准备好依赖后，在项目根目录执行 `.\Script\premake\premake5.exe vs2022` 生成工程。
