-- 递归收集内置Slang；Common只提供共享module，其他文件是当前双入口图形shader。
-- 以后stage自动发现归S04.3；这里没有逐文件硬编码编译清单。
local engineRoot = _MAIN_SCRIPT_DIR
local shaderRoot = path.join(engineRoot, "Catalpa/Resources/Shaders")
local shaderCompiler = path.join(os.getenv("VULKAN_SDK"), "Bin/slangc.exe")
local shaderPattern = path.join(shaderRoot, "**.slang")
local generatedShaders = path.join(engineRoot, "bin-int", outputdir, "Shaders")
local shaderFiles = os.matchfiles(shaderPattern)
if #shaderFiles == 0 then error("No Slang shaders found: " .. shaderPattern) end
table.sort(shaderFiles)
local modules, entries = {}, {}
local relativeKeys, symbols = {}, {}
for _, shaderFile in ipairs(shaderFiles) do
    local relative = path.getrelative(shaderRoot, shaderFile):gsub("\\", "/")
    local stem = relative:gsub("%.slang$", "")
    for component in stem:gmatch("[^/]+") do
        if not component:match("^[A-Za-z_][A-Za-z0-9_]*$") then
            error("Shader relative path must contain identifier components: " .. relative)
        end
    end
    local key = relative:lower()
    if relativeKeys[key] then error("Case-insensitive Slang path collision: " .. relative) end
    relativeKeys[key] = true
    if relative:lower():match("^common/") then
        table.insert(modules, shaderFile)
    else
        local symbol = stem:gsub("/", "_")
        if symbols[symbol:lower()] then error("Slang output symbol collision: " .. relative) end
        symbols[symbol:lower()] = true
        table.insert(entries, { file = shaderFile, relative = relative, stem = stem, symbol = symbol })
    end
end
if #entries == 0 then error("No complete Slang entry modules found under " .. shaderRoot) end
local stages = {
    { entry = "vertexMain", stage = "vertex", suffix = "VertexSpirv" },
    { entry = "fragmentMain", stage = "fragment", suffix = "FragmentSpirv" }
}
local prebuild = { '{MKDIR} "' .. generatedShaders .. '"' }
filter {}
    files { shaderPattern }
    includedirs { generatedShaders }
for _, item in ipairs(entries) do
    local directory = path.join(generatedShaders, path.getdirectory(item.relative))
    local commands = { '{MKDIR} "' .. directory .. '"' }
    local outputs = {}
    local inputs = { item.file }
    for _, module in ipairs(modules) do table.insert(inputs, module) end
    local command = '"' .. shaderCompiler .. '" "' .. item.file ..
        '" -I "' .. shaderRoot .. '" -target spirv -profile spirv_1_5 -fvk-use-entrypoint-name -source-embed-style u32'
    for _, stage in ipairs(stages) do
        local symbol = item.symbol .. stage.suffix
        local output = path.join(generatedShaders, item.stem .. stage.suffix .. ".h")
        table.insert(outputs, output)
        table.insert(commands, command .. ' -entry ' .. stage.entry .. ' -stage ' .. stage.stage ..
            ' -source-embed-name ' .. symbol .. ' -o "' .. output .. '" || exit /b 1')
    end
    -- Prebuild确保首次C++编译前头已存在；CustomBuild输入让IDE也跟踪Common变更。
    for _, line in ipairs(commands) do table.insert(prebuild, line) end
    -- filter按当前目录保存basedir，实际文件也会转相对路径；避免绝对pattern漏掉CustomBuild。
    local fileFilter = path.getrelative(os.getcwd(), item.file)
    filter { "system:windows", "files:" .. fileFilter }
        buildmessage ("Compiling Slang " .. item.relative)
        buildinputs (inputs)
        buildcommands (commands)
        buildoutputs (outputs)
        compilebuildoutputs(false)
        linkbuildoutputs(false)
    filter {}
end
filter "system:windows"
    prebuildcommands(prebuild)
filter {}
