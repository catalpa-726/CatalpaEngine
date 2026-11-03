-- 为当前项目登记 Slang 源文件、生成头文件路径和预构建命令，不创建独立项目。
-- 复用根配置的 outputdir；所有路径以项目根为基准，不依赖本脚本所在目录。
local engineRoot = _MAIN_SCRIPT_DIR
local shaderCompiler = path.join(os.getenv("VULKAN_SDK"), "Bin/slangc.exe")
local shaderPattern = path.join(engineRoot, "Catalpa/Resources/Shaders/**.slang")
local generatedShaders = path.join(engineRoot, "bin-int", outputdir, "Shaders")

-- 工程生成时递归收集 Shader；新增/删除文件后需重新生成工程。
-- 当前每份文件均为 vertexMain + fragmentMain 双入口图形 Shader。
local shaderFiles = os.matchfiles(shaderPattern)
if #shaderFiles == 0 then
    error("No Slang shaders found: " .. shaderPattern)
end
table.sort(shaderFiles)
local shaderStages = {
    { entry = "vertexMain", stage = "vertex", suffix = "VertexSpirv" },
    { entry = "fragmentMain", stage = "fragment", suffix = "FragmentSpirv" }
}
local shaderCommands = { '{MKDIR} "' .. generatedShaders .. '"' }
local shaderNames = {}
for _, shaderFile in ipairs(shaderFiles) do
    local name = path.getbasename(shaderFile)
    -- 头文件及嵌入数组沿用 <文件名><阶段>Spirv；避免非法标识符或 Windows 同名覆盖。
    if not name:match("^[A-Za-z_][A-Za-z0-9_]*$") then
        error("Shader filename must be a C++ identifier: " .. shaderFile)
    end
    local key = name:lower()
    if shaderNames[key] then
        error("Duplicate shader output name: " .. shaderNames[key] .. " and " .. shaderFile)
    end
    shaderNames[key] = shaderFile

    local command = '"' .. shaderCompiler .. '" "' .. shaderFile ..
        '" -target spirv -profile spirv_1_5 -fvk-use-entrypoint-name -source-embed-style u32'
    for _, stage in ipairs(shaderStages) do
        local symbol = name .. stage.suffix
        table.insert(shaderCommands, command .. ' -entry ' .. stage.entry .. ' -stage ' .. stage.stage ..
            ' -source-embed-name ' .. symbol .. ' -o "' .. path.join(generatedShaders, symbol .. ".h") .. '" || exit /b 1')
    end
end

filter {}
    files { shaderPattern }
    includedirs { generatedShaders }

filter "system:windows"
    -- 编译 C++ 前生成头文件，保持与消费端 include 的依赖顺序。
    prebuildcommands(shaderCommands)

filter {}
