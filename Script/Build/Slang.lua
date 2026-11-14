--[[
Slang唯一构建入口规则：递归跟踪全部模块，stage/entry由slangc的[shader]属性发现。
Catalpa库不依赖尚未链接的Box；Box链接完成后调用共享Tools命令生成已验证cooked产物。
]]
local engineRoot = _MAIN_SCRIPT_DIR
local shaderRoot = path.join(engineRoot, "Catalpa/Resources/Shaders")
local shaderFiles = os.matchfiles(path.join(shaderRoot, "**.slang"))
if #shaderFiles == 0 then error("No Slang modules found: " .. shaderRoot) end
table.sort(shaderFiles)
local seen = {}
for _, source in ipairs(shaderFiles) do
    local relative = path.getrelative(shaderRoot, source):gsub("\\", "/")
    if seen[relative:lower()] then error("Case-insensitive Slang path collision: " .. relative) end
    seen[relative:lower()] = true
end
filter {}
    files { path.join(shaderRoot, "**.slang") }

CatalpaShaderBuild = {}

function CatalpaShaderBuild.AttachHostCook()
    local stampRoot = path.join(engineRoot, "bin-int", outputdir, "ShaderInputs")
    files { path.join(shaderRoot, "**.slang") }
    for _, source in ipairs(shaderFiles) do
        local relative = path.getrelative(shaderRoot, source)
        local stamp = path.join(stampRoot, relative .. ".stamp")
        --[[
        这里只记录输入改变，不在链接前执行尚不存在的工具宿主。
        任意公共模块变更使生产步骤重新检查真实内容hash，无C++头生成及全引擎重编译。
        ]]
        filter { "system:windows", "files:" .. path.getrelative(os.getcwd(), source) }
            buildmessage ("Tracking Slang " .. relative)
            buildinputs(shaderFiles)
            buildcommands {
                '{MKDIR} "' .. path.getdirectory(stamp) .. '"',
                'if errorlevel 1 exit /b 1',
                'type nul > "' .. stamp .. '"',
                'if errorlevel 1 exit /b 1'
            }
            buildoutputs { stamp }
            compilebuildoutputs(false)
            linkbuildoutputs(false)
        filter {}
    end
    filter "system:windows"
        postbuildmessage "Validating and cooking changed Slang assets"
        -- 普通路径token会被Premake改成工程相对路径；!保留工具协议要求的绝对路径。
        postbuildcommands {
            '"%{!cfg.buildtarget.abspath}" --cook-shaders --source-root "' .. shaderRoot .. '" --output-root "%{!cfg.targetdir}/Resources/Shaders" --sdk-root "' .. os.getenv("VULKAN_SDK") .. '"',
            'if errorlevel 1 exit /b 1'
        }
    filter {}
end
