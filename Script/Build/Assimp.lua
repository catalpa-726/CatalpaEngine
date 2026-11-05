-- 直接用Premake编译用户提供的Assimp及随包zlib，保留默认格式/处理器。
-- 本文件只配置静态库和生成头，不执行外部构建器、下载或Git。
local engineRoot = _MAIN_SCRIPT_DIR
local assimpRoot = path.join(engineRoot, "Catalpa/third_party/assimp")
local generatedRoot = path.join(engineRoot, "out/generated/Assimp")

local function readText(filename)
    local file, message = io.open(filename, "rb")
    if not file then error(message) end
    local text = file:read("*a")
    assert(file:close())
    return text
end

-- 只生成自己out目录的派生头；内容不变时不重写，避免无谓重编译。
local function writeHeader(relative, content)
    local filename = path.join(generatedRoot, relative)
    if os.isfile(filename) and readText(filename) == content then return end
    os.mkdir(path.getdirectory(filename))
    local file, message = io.open(filename, "wb")
    if not file then error(message) end
    assert(file:write(content))
    assert(file:close())
end

local function configureHeader(template, values)
    local text = readText(path.join(assimpRoot, template))
    text = text:gsub("#cmakedefine%s+([%w_]+)[^\r\n]*", "/* #undef %1 */")
    return (text:gsub("@([%w_]+)@", function(key)
        if values[key] == nil then error("Unconfigured Assimp header token: " .. key) end
        return values[key]
    end))
end

-- 只取源码构建声明的资源版本，不读取Git信息或伪造提交号。
local declaration = readText(path.join(assimpRoot, "CMakeLists.txt"))
local major, minor, patch = declaration:match("PROJECT%s*%(%s*Assimp%s+VERSION%s+(%d+)%.(%d+)%.(%d+)")
if not major then error("Cannot read Assimp resource-version declaration") end
writeHeader("include/assimp/config.h", configureHeader("include/assimp/config.h.in", {}))
writeHeader("include/assimp/revision.h", configureHeader("include/assimp/revision.h.in", {
    GIT_COMMIT_HASH = "0", GIT_BRANCH = "user-provided-source",
    ASSIMP_VERSION_MAJOR = major, ASSIMP_VERSION_MINOR = minor, ASSIMP_VERSION_PATCH = patch,
    ASSIMP_PACKAGE_VERSION = "0", CMAKE_SHARED_LIBRARY_PREFIX = "",
    LIBRARY_SUFFIX = "", CMAKE_DEBUG_POSTFIX = ""
}))
writeHeader("zlib/zconf.h", configureHeader("contrib/zlib/zconf.h.cmakein", {}))

-- 随包zlib独立为C静态库，使用同一工作区的CRT和配置，不构建其DLL/示例。
filter {}
project "AssimpZlib"
    kind "StaticLib"
    language "C"
    cdialect "C11"
    staticruntime "Off"
    location(path.join(engineRoot, "out/projects/AssimpZlib"))
    targetdir(path.join(engineRoot, "bin/" .. outputdir .. "/%{prj.name}"))
    objdir(path.join(engineRoot, "bin-int/" .. outputdir .. "/%{prj.name}"))
    includedirs { path.join(generatedRoot, "zlib"), path.join(assimpRoot, "contrib/zlib") }
    defines { "_CRT_SECURE_NO_WARNINGS", "_CRT_NONSTDC_NO_DEPRECATE", "NO_FSEEKO" }
    files {
        path.join(assimpRoot, "contrib/zlib/adler32.c"),
        path.join(assimpRoot, "contrib/zlib/compress.c"),
        path.join(assimpRoot, "contrib/zlib/crc32.c"),
        path.join(assimpRoot, "contrib/zlib/deflate.c"),
        path.join(assimpRoot, "contrib/zlib/gzclose.c"),
        path.join(assimpRoot, "contrib/zlib/gzlib.c"),
        path.join(assimpRoot, "contrib/zlib/gzread.c"),
        path.join(assimpRoot, "contrib/zlib/gzwrite.c"),
        path.join(assimpRoot, "contrib/zlib/inflate.c"),
        path.join(assimpRoot, "contrib/zlib/infback.c"),
        path.join(assimpRoot, "contrib/zlib/inftrees.c"),
        path.join(assimpRoot, "contrib/zlib/inffast.c"),
        path.join(assimpRoot, "contrib/zlib/trees.c"),
        path.join(assimpRoot, "contrib/zlib/uncompr.c"),
        path.join(assimpRoot, "contrib/zlib/zutil.c"),
        path.join(assimpRoot, "contrib/zlib/**.h"),
        path.join(generatedRoot, "zlib/zconf.h")
    }

-- 与上游默认配置对应的普通C/C++实现清单；不把可选平台、工具或测试递归编入库。
-- 该清单包含默认importer/exporter及其随包依赖，并对共享实现去重。
local assimpSources = {
    -- Core_SRCS
    "code/Common/Assimp.cpp",
    -- Logging_SRCS
    "code/Common/DefaultLogger.cpp",
    -- Common_SRCS
    "code/Common/Compression.cpp",
    "code/Common/BaseImporter.cpp",
    "code/Common/BaseProcess.cpp",
    "code/Common/PostStepRegistry.cpp",
    "code/Common/ImporterRegistry.cpp",
    "code/Common/DefaultIOStream.cpp",
    "code/Common/IOSystem.cpp",
    "code/Common/DefaultIOSystem.cpp",
    "code/Common/ZipArchiveIOSystem.cpp",
    "code/Common/Importer.cpp",
    "code/Common/SGSpatialSort.cpp",
    "code/Common/VertexTriangleAdjacency.cpp",
    "code/Common/SpatialSort.cpp",
    "code/Common/SceneCombiner.cpp",
    "code/Common/ScenePreprocessor.cpp",
    "code/Common/SkeletonMeshBuilder.cpp",
    "code/Common/StandardShapes.cpp",
    "code/Common/TargetAnimation.cpp",
    "code/Common/RemoveComments.cpp",
    "code/Common/Subdivision.cpp",
    "code/Common/scene.cpp",
    "code/Common/Bitmap.cpp",
    "code/Common/Version.cpp",
    "code/Common/CreateAnimMesh.cpp",
    "code/Common/simd.cpp",
    "code/Common/material.cpp",
    "code/Common/AssertHandler.cpp",
    "code/Common/Exceptional.cpp",
    "code/Common/Base64.cpp",
    -- CApi_SRCS
    "code/CApi/CInterfaceIOWrapper.cpp",
    -- Geometry_SRCS
    "code/Geometry/GeometryUtils.cpp",
    -- MaterialSystem_SRCS
    "code/Material/MaterialSystem.cpp",
    -- PostProcessing_SRCS
    "code/PostProcessing/CalcTangentsProcess.cpp",
    "code/PostProcessing/ComputeUVMappingProcess.cpp",
    "code/PostProcessing/ConvertToLHProcess.cpp",
    "code/PostProcessing/EmbedTexturesProcess.cpp",
    "code/PostProcessing/FindDegenerates.cpp",
    "code/PostProcessing/FindInstancesProcess.cpp",
    "code/PostProcessing/FindInvalidDataProcess.cpp",
    "code/PostProcessing/FixNormalsStep.cpp",
    "code/PostProcessing/DropFaceNormalsProcess.cpp",
    "code/PostProcessing/GenFaceNormalsProcess.cpp",
    "code/PostProcessing/GenVertexNormalsProcess.cpp",
    "code/PostProcessing/PretransformVertices.cpp",
    "code/PostProcessing/ImproveCacheLocality.cpp",
    "code/PostProcessing/JoinVerticesProcess.cpp",
    "code/PostProcessing/LimitBoneWeightsProcess.cpp",
    "code/PostProcessing/RemoveRedundantMaterials.cpp",
    "code/PostProcessing/RemoveVCProcess.cpp",
    "code/PostProcessing/SortByPTypeProcess.cpp",
    "code/PostProcessing/SplitLargeMeshes.cpp",
    "code/PostProcessing/TextureTransform.cpp",
    "code/PostProcessing/TriangulateProcess.cpp",
    "code/PostProcessing/ValidateDataStructure.cpp",
    "code/PostProcessing/OptimizeGraph.cpp",
    "code/PostProcessing/OptimizeMeshes.cpp",
    "code/PostProcessing/DeboneProcess.cpp",
    "code/PostProcessing/ProcessHelper.cpp",
    "code/PostProcessing/MakeVerboseFormat.cpp",
    "code/PostProcessing/ScaleProcess.cpp",
    "code/PostProcessing/ArmaturePopulate.cpp",
    "code/PostProcessing/GenBoundingBoxesProcess.cpp",
    "code/PostProcessing/SplitByBoneCountProcess.cpp",
    -- STEPParser_SRCS
    "code/AssetLib/STEPParser/STEPFileReader.cpp",
    "code/AssetLib/STEPParser/STEPFileEncoding.cpp",
    -- glTFCommon_src
    "code/AssetLib/glTFCommon/glTFCommon.cpp",
    -- Exporter_SRCS
    "code/Common/Exporter.cpp",
    "code/CApi/AssimpCExport.cpp",
    -- FBX_COMMON_SRCS
    "code/AssetLib/FBX/FBXUtil.cpp",
    -- Pugixml_SRCS
    "code/../contrib/pugixml/src/pugixml.cpp",
    -- Clipper_SRCS
    "code/../contrib/clipper/clipper.cpp",
    -- Poly2Tri_SRCS
    "code/../contrib/poly2tri/poly2tri/common/shapes.cc",
    "code/../contrib/poly2tri/poly2tri/sweep/advancing_front.cc",
    "code/../contrib/poly2tri/poly2tri/sweep/cdt.cc",
    "code/../contrib/poly2tri/poly2tri/sweep/sweep.cc",
    "code/../contrib/poly2tri/poly2tri/sweep/sweep_context.cc",
    -- unzip_SRCS
    "code/../contrib/unzip/ioapi.c",
    "code/../contrib/unzip/unzip.c",
    -- ziplib_SRCS
    "code/../contrib/zip/src/zip.c",
    -- openddl_parser_SRCS
    "code/../contrib/openddlparser/code/OpenDDLParser.cpp",
    "code/../contrib/openddlparser/code/DDLNode.cpp",
    "code/../contrib/openddlparser/code/OpenDDLCommon.cpp",
    "code/../contrib/openddlparser/code/OpenDDLExport.cpp",
    "code/../contrib/openddlparser/code/Value.cpp",
    "code/../contrib/openddlparser/code/OpenDDLStream.cpp",
    -- open3dgc_SRCS
    "code/../contrib/Open3DGC/o3dgcArithmeticCodec.cpp",
    "code/../contrib/Open3DGC/o3dgcDynamicVectorDecoder.cpp",
    "code/../contrib/Open3DGC/o3dgcDynamicVectorEncoder.cpp",
    "code/../contrib/Open3DGC/o3dgcTools.cpp",
    "code/../contrib/Open3DGC/o3dgcTriangleFans.cpp",
    -- IMPORTER:AMF
    "code/AssetLib/AMF/AMFImporter.cpp",
    "code/AssetLib/AMF/AMFImporter_Geometry.cpp",
    "code/AssetLib/AMF/AMFImporter_Material.cpp",
    "code/AssetLib/AMF/AMFImporter_Postprocess.cpp",
    -- IMPORTER:3DS
    "code/AssetLib/3DS/3DSConverter.cpp",
    "code/AssetLib/3DS/3DSLoader.cpp",
    -- IMPORTER:AC
    "code/AssetLib/AC/ACLoader.cpp",
    -- IMPORTER:ASE
    "code/AssetLib/ASE/ASELoader.cpp",
    "code/AssetLib/ASE/ASEParser.cpp",
    -- IMPORTER:ASSBIN
    "code/AssetLib/Assbin/AssbinLoader.cpp",
    -- IMPORTER:B3D
    "code/AssetLib/B3D/B3DImporter.cpp",
    -- IMPORTER:BVH
    "code/AssetLib/BVH/BVHLoader.cpp",
    -- IMPORTER:COLLADA
    "code/AssetLib/Collada/ColladaHelper.cpp",
    "code/AssetLib/Collada/ColladaLoader.cpp",
    "code/AssetLib/Collada/ColladaParser.cpp",
    -- IMPORTER:DXF
    "code/AssetLib/DXF/DXFLoader.cpp",
    -- IMPORTER:CSM
    "code/AssetLib/CSM/CSMLoader.cpp",
    -- IMPORTER:HMP
    "code/AssetLib/HMP/HMPLoader.cpp",
    -- IMPORTER:IRRMESH
    "code/AssetLib/Irr/IRRMeshLoader.cpp",
    "code/AssetLib/Irr/IRRShared.cpp",
    -- IMPORTER:IQM
    "code/AssetLib/IQM/IQMImporter.cpp",
    -- IMPORTER:IRR
    "code/AssetLib/Irr/IRRLoader.cpp",
    -- IMPORTER:LWO
    "code/AssetLib/LWO/LWOAnimation.cpp",
    "code/AssetLib/LWO/LWOBLoader.cpp",
    "code/AssetLib/LWO/LWOLoader.cpp",
    "code/AssetLib/LWO/LWOMaterial.cpp",
    -- IMPORTER:LWS
    "code/AssetLib/LWS/LWSLoader.cpp",
    -- IMPORTER:MD2
    "code/AssetLib/MD2/MD2Loader.cpp",
    -- IMPORTER:MD3
    "code/AssetLib/MD3/MD3Loader.cpp",
    -- IMPORTER:MD5
    "code/AssetLib/MD5/MD5Loader.cpp",
    "code/AssetLib/MD5/MD5Parser.cpp",
    -- IMPORTER:MDC
    "code/AssetLib/MDC/MDCLoader.cpp",
    -- IMPORTER:MDL
    "code/AssetLib/MDL/MDLLoader.cpp",
    "code/AssetLib/MDL/MDLMaterialLoader.cpp",
    "code/AssetLib/MDL/HalfLife/HL1MDLLoader.cpp",
    "code/AssetLib/MDL/HalfLife/UniqueNameGenerator.cpp",
    -- IMPORTER:NFF
    "code/AssetLib/NFF/NFFLoader.cpp",
    -- IMPORTER:NDO
    "code/AssetLib/NDO/NDOLoader.cpp",
    -- IMPORTER:OFF
    "code/AssetLib/OFF/OFFLoader.cpp",
    -- IMPORTER:OBJ
    "code/AssetLib/Obj/ObjFileImporter.cpp",
    "code/AssetLib/Obj/ObjFileMtlImporter.cpp",
    "code/AssetLib/Obj/ObjFileParser.cpp",
    -- IMPORTER:OGRE
    "code/AssetLib/Ogre/OgreImporter.cpp",
    "code/AssetLib/Ogre/OgreStructs.cpp",
    "code/AssetLib/Ogre/OgreBinarySerializer.cpp",
    "code/AssetLib/Ogre/OgreXmlSerializer.cpp",
    "code/AssetLib/Ogre/OgreMaterial.cpp",
    -- IMPORTER:OPENGEX
    "code/AssetLib/OpenGEX/OpenGEXImporter.cpp",
    -- IMPORTER:PLY
    "code/AssetLib/Ply/PlyLoader.cpp",
    "code/AssetLib/Ply/PlyParser.cpp",
    -- IMPORTER:MS3D
    "code/AssetLib/MS3D/MS3DLoader.cpp",
    -- IMPORTER:COB
    "code/AssetLib/COB/COBLoader.cpp",
    -- IMPORTER:BLEND
    "code/AssetLib/Blender/BlenderLoader.cpp",
    "code/AssetLib/Blender/BlenderDNA.cpp",
    "code/AssetLib/Blender/BlenderScene.cpp",
    "code/AssetLib/Blender/BlenderModifier.cpp",
    "code/AssetLib/Blender/BlenderBMesh.cpp",
    "code/AssetLib/Blender/BlenderTessellator.cpp",
    "code/AssetLib/Blender/BlenderCustomData.cpp",
    -- IMPORTER:IFC
    "code/AssetLib/IFC/IFCLoader.cpp",
    "code/AssetLib/IFC/IFCReaderGen1_2x3.cpp",
    "code/AssetLib/IFC/IFCReaderGen2_2x3.cpp",
    "code/AssetLib/IFC/IFCUtil.cpp",
    "code/AssetLib/IFC/IFCGeometry.cpp",
    "code/AssetLib/IFC/IFCMaterial.cpp",
    "code/AssetLib/IFC/IFCProfile.cpp",
    "code/AssetLib/IFC/IFCCurve.cpp",
    "code/AssetLib/IFC/IFCBoolean.cpp",
    "code/AssetLib/IFC/IFCOpenings.cpp",
    -- IMPORTER:XGL
    "code/AssetLib/XGL/XGLLoader.cpp",
    -- IMPORTER:FBX
    "code/AssetLib/FBX/FBXImporter.cpp",
    "code/AssetLib/FBX/FBXParser.cpp",
    "code/AssetLib/FBX/FBXTokenizer.cpp",
    "code/AssetLib/FBX/FBXConverter.cpp",
    "code/AssetLib/FBX/FBXDocument.cpp",
    "code/AssetLib/FBX/FBXProperties.cpp",
    "code/AssetLib/FBX/FBXMeshGeometry.cpp",
    "code/AssetLib/FBX/FBXMaterial.cpp",
    "code/AssetLib/FBX/FBXModel.cpp",
    "code/AssetLib/FBX/FBXAnimation.cpp",
    "code/AssetLib/FBX/FBXNodeAttribute.cpp",
    "code/AssetLib/FBX/FBXDeformer.cpp",
    "code/AssetLib/FBX/FBXBinaryTokenizer.cpp",
    "code/AssetLib/FBX/FBXDocumentUtil.cpp",
    -- IMPORTER:Q3D
    "code/AssetLib/Q3D/Q3DLoader.cpp",
    -- IMPORTER:Q3BSP
    "code/AssetLib/Q3BSP/Q3BSPFileParser.cpp",
    "code/AssetLib/Q3BSP/Q3BSPFileImporter.cpp",
    -- IMPORTER:RAW
    "code/AssetLib/Raw/RawLoader.cpp",
    -- IMPORTER:SIB
    "code/AssetLib/SIB/SIBImporter.cpp",
    -- IMPORTER:SMD
    "code/AssetLib/SMD/SMDLoader.cpp",
    -- IMPORTER:STL
    "code/AssetLib/STL/STLLoader.cpp",
    -- IMPORTER:TERRAGEN
    "code/AssetLib/Terragen/TerragenLoader.cpp",
    -- IMPORTER:3D
    "code/AssetLib/Unreal/UnrealLoader.cpp",
    -- IMPORTER:X
    "code/AssetLib/X/XFileImporter.cpp",
    "code/AssetLib/X/XFileParser.cpp",
    -- IMPORTER:X3D
    "code/AssetLib/X3D/X3DImporter.cpp",
    "code/AssetLib/X3D/X3DImporter_Geometry2D.cpp",
    "code/AssetLib/X3D/X3DImporter_Geometry3D.cpp",
    "code/AssetLib/X3D/X3DImporter_Group.cpp",
    "code/AssetLib/X3D/X3DImporter_Light.cpp",
    "code/AssetLib/X3D/X3DImporter_Metadata.cpp",
    "code/AssetLib/X3D/X3DImporter_Networking.cpp",
    "code/AssetLib/X3D/X3DImporter_Postprocess.cpp",
    "code/AssetLib/X3D/X3DImporter_Rendering.cpp",
    "code/AssetLib/X3D/X3DImporter_Shape.cpp",
    "code/AssetLib/X3D/X3DImporter_Texturing.cpp",
    "code/AssetLib/X3D/X3DGeoHelper.cpp",
    "code/AssetLib/X3D/X3DXmlHelper.cpp",
    "code/AssetLib/VRML/VrmlConverter.cpp",
    -- IMPORTER:GLTF
    "code/AssetLib/glTF/glTFImporter.cpp",
    "code/AssetLib/glTF2/glTF2Importer.cpp",
    -- IMPORTER:3MF
    "code/AssetLib/3MF/XmlSerializer.cpp",
    "code/AssetLib/3MF/D3MFImporter.cpp",
    "code/AssetLib/3MF/D3MFOpcPackage.cpp",
    -- IMPORTER:MMD
    "code/AssetLib/MMD/MMDImporter.cpp",
    "code/AssetLib/MMD/MMDPmxParser.cpp",
    -- EXPORTER:OBJ
    "code/AssetLib/Obj/ObjExporter.cpp",
    -- EXPORTER:OPENGEX
    "code/AssetLib/OpenGEX/OpenGEXExporter.cpp",
    -- EXPORTER:PLY
    "code/AssetLib/Ply/PlyExporter.cpp",
    -- EXPORTER:3DS
    "code/AssetLib/3DS/3DSExporter.cpp",
    -- EXPORTER:ASSBIN
    "code/AssetLib/Assbin/AssbinExporter.cpp",
    "code/AssetLib/Assbin/AssbinFileWriter.cpp",
    -- EXPORTER:ASSXML
    "code/AssetLib/Assxml/AssxmlExporter.cpp",
    "code/AssetLib/Assxml/AssxmlFileWriter.cpp",
    -- EXPORTER:COLLADA
    "code/AssetLib/Collada/ColladaExporter.cpp",
    -- EXPORTER:FBX
    "code/AssetLib/FBX/FBXExporter.cpp",
    "code/AssetLib/FBX/FBXExportNode.cpp",
    "code/AssetLib/FBX/FBXExportProperty.cpp",
    -- EXPORTER:STL
    "code/AssetLib/STL/STLExporter.cpp",
    -- EXPORTER:X
    "code/AssetLib/X/XFileExporter.cpp",
    -- EXPORTER:X3D
    "code/AssetLib/X3D/X3DExporter.cpp",
    -- EXPORTER:GLTF
    "code/AssetLib/glTF/glTFExporter.cpp",
    "code/AssetLib/glTF2/glTF2Exporter.cpp",
    -- EXPORTER:3MF
    "code/AssetLib/3MF/D3MFExporter.cpp",
    -- EXPORTER:PBRT
    "code/Pbrt/PbrtExporter.cpp",
    -- EXPORTER:ASSJSON
    "code/AssetLib/Assjson/cencode.c",
    "code/AssetLib/Assjson/json_exporter.cpp",
    "code/AssetLib/Assjson/mesh_splitter.cpp",
    -- EXPORTER:STEP
    "code/AssetLib/Step/StepExporter.cpp",
}

filter {}
project "assimp"
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    cdialect "C11"
    staticruntime "Off"
    location(path.join(engineRoot, "out/projects/assimp"))
    targetdir(path.join(engineRoot, "bin/" .. outputdir .. "/%{prj.name}"))
    objdir(path.join(engineRoot, "bin-int/" .. outputdir .. "/%{prj.name}"))

    files {
        path.join(assimpRoot, "include/assimp/**.h"),
        path.join(assimpRoot, "include/assimp/**.hpp"),
        path.join(generatedRoot, "include/assimp/config.h"),
        path.join(generatedRoot, "include/assimp/revision.h")
    }
    for _, relative in ipairs(assimpSources) do files { path.join(assimpRoot, relative) } end

    includedirs {
        path.join(generatedRoot, "include"), path.join(generatedRoot, "zlib"),
        path.join(assimpRoot, "include"), path.join(assimpRoot, "code"), assimpRoot,
        path.join(assimpRoot, "contrib"), path.join(assimpRoot, "contrib/zlib"),
        path.join(assimpRoot, "contrib/unzip"), path.join(assimpRoot, "contrib/utf8cpp/source"),
        path.join(assimpRoot, "contrib/pugixml/src"), path.join(assimpRoot, "contrib/rapidjson/include"),
        path.join(assimpRoot, "contrib/openddlparser/include")
    }
    defines {
        "WIN32_LEAN_AND_MEAN", "NOMINMAX", "_CRT_SECURE_NO_WARNINGS", "_SCL_SECURE_NO_WARNINGS",
        "OPENDDLPARSER_BUILD", "OPENDDL_STATIC_LIBARY", "P2T_STATIC_EXPORTS",
        "RAPIDJSON_HAS_STDSTRING=1", "RAPIDJSON_NOMEMBERITERATORCLASS",
        "ASSIMP_IMPORTER_GLTF_USE_OPEN3DGC=1", "MINIZ_USE_UNALIGNED_LOADS_AND_STORES=0",
        -- 保留上游原本默认关闭的额外依赖/非免费或实验组件；不裁剪默认格式。
        "ASSIMP_BUILD_NO_C4D_IMPORTER", "ASSIMP_BUILD_NO_USD_IMPORTER",
        "ASSIMP_BUILD_NO_M3D_IMPORTER", "ASSIMP_BUILD_NO_M3D_EXPORTER", "ASSIMP_BUILD_NO_VRML_IMPORTER"
    }
    links { "AssimpZlib" }

    filter "files:**.c"
        compileas "C"
    filter "files:**/IFCReaderGen*.cpp"
        buildoptions { "/bigobj" }
    filter "configurations:Debug or Dist"
        defines { "ASSIMP_BUILD_DEBUG" }
    filter {}
