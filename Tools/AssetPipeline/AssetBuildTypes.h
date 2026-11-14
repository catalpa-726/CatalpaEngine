#pragma once
#include "catpch.h"

#include "Project/ProjectConfig.h"
#include "Core/Jobs/CancellationToken.h"
#include "Geometry/ModelAsset.h"
#include "Images/ImageAsset.h"
#include "Lighting/EnvironmentAsset.h"

namespace Catalpa::Tools {
	/* unitScale 是转换到米后的附加倍率，仅影响派生模型。 */
	struct AssetImportSettings {
		double unitScale = 1.0;
		std::string sourceUp = "auto"; /* auto: glTF规范/FBX元数据；OBJ按Y-up/米并诊断。 */
		bool generateNormals = true, generateTangents = true;
		/* 将源文件中的引用映射到指定文件路径，目标必须属于项目声明的源目录。 */
		std::map<std::string, std::string, std::less<>> dependencyRemaps;
		/* HDR 单图解码上限为 512 MiB；总工作区还需容纳源字节、stb 临时像素和积分结果。 */
		size_t maxSourceBytes = 64 * 1024 * 1024, maxWorkingBytes = 1536 * 1024 * 1024;
		uint32_t maxVertices = 2000000, maxIndices = 6000000, maxNodes = 65536;
	};

	struct AssetBuildRequest {
		ProjectConfig project;
		std::filesystem::path source, shaderRoot;
		AssetImportSettings settings;
		CancellationToken cancellation;
		/* 构建线程通过此回调报告进度，UI 和场景操作留在宿主线程。 */
		std::function<void(std::string_view, size_t, size_t)> progress;
	};
	enum class AssetDiagnosticLevel { Info, Warning, Error };

	struct AssetBuildDiagnostic {
		AssetDiagnosticLevel level = AssetDiagnosticLevel::Info;
		std::string source, element, message;
	};

	struct AssetSourceDependency {
		std::filesystem::path path;
		std::string sha256;
		uint64_t bytes = 0;
	};

	struct AssetSourceMapping {
		AssetId asset;
		std::string sourceKey, name, kind;
		StableId node;
	};

	/* 保留源材质参数与贴图语义，Unlit 预览使用独立资产身份。 */
	struct ImportedMaterial {
		std::array<float, 4> baseColor{1, 1, 1, 1};
		std::array<float, 3> emissive{0, 0, 0};
		float metallic = 0, roughness = 1, alphaCutoff = 0.5f, normalScale = 1, occlusionStrength = 1;
		std::map<std::string, std::string, std::less<>> sourceProperties;
		std::string alphaMode = "opaque";
		bool doubleSided = false, unlit = false;
		/* 图像保留解码行序。采样时以此参数将 Assimp 的 UV 原点与图像行序对齐。 */
		bool textureFlipV = false;
		std::map<std::string, AssetId, std::less<>> textures;
		std::map<std::string, uint32_t, std::less<>> samplers;
	};

	struct ImportedAuthorProduct {
		std::string type;
		std::vector<std::byte> payload;
		std::vector<AssetId> dependencies;
	};
	using ImportedValue =
	    std::variant<MeshAsset, ImageAsset, ImportedMaterial, ModelAsset, EnvironmentAsset, ImportedAuthorProduct>;

	struct ImportedAsset {
		AssetId id;
		std::string sourceKey, name;
		ImportedValue value;
	};

	struct AssetImportResult {
		AssetId primary;
		std::vector<ImportedAsset> assets;
		std::vector<AssetSourceMapping> mappings;
	};

	struct AssetBuildProduct {
		AssetRecord record;
		std::vector<std::byte> bytes;
		bool external = false; /* 已发布依赖保留原 locator 与来源，不被当前作者文件认领。 */
	};

	struct AssetBuildResult {
		bool success = false, cacheHit = false;
		AssetId primary;
		std::string key, error;
		std::filesystem::path cookedRoot, manifest, report;
		std::vector<AssetRecord> records; /* 本次源文件及其 Cook 依赖。 */
		/* 主清单原子替换成功后交付，包含同一挂载内其他来源的有效记录。 */
		std::vector<AssetRecord> publishedCatalog;
		std::vector<AssetSourceMapping> mappings;
		std::vector<AssetBuildDiagnostic> diagnostics;
	};

	/**
	 * @brief 工具命令、编辑器和导入器共用 UTF-8 路径及确定性身份计算。
	 *
	 * @param[in] p 要以 UTF-8 通用路径形式写入命令、界面或身份计算的路径。
	 *
	 * @return 文件系统路径的 UTF-8 文本。
	 */
	std::string AssetPathText(const std::filesystem::path&);
	/**
	 * @brief 从稳定字符串的 SHA-256 派生 128 位资源标识。
	 *
	 * @param[in] text 用于确定性派生资产标识的稳定字符串。
	 *
	 * @return SHA-256 前 16 字节派生的非零 128 位资产标识；计算失败或结果全零时抛出异常。
	 */
	AssetId AssetBuildIdentity(std::string_view);
	/**
	 * @brief 计算构建输入的摘要。
	 *
	 * @param[in] b 第二个字节序列操作数。
	 *
	 * @return 输入字节的 SHA-256 摘要，以 64 个小写十六进制字符表示。
	 */
	std::string AssetBuildDigest(std::span<const std::byte>);
	/**
	 * @brief 根据资产内容生成用于区分版本的值。
	 *
	 * @param[in] digest 64 个小写十六进制字符组成的内容摘要，前 16 位用于派生非零版本号。
	 *
	 * @return 取合法小写 SHA-256 文本前 16 位转换的非零 64 位版本；零映射为 1，输入不合法时抛出异常。
	 */
	uint64_t AssetContentVersion(std::string_view digest);
	/**
	 * @brief 序列化影响导入结果的设置，参与缓存键计算。
	 *
	 * @param[in] s 参与处理的 AssetImportSettings。
	 *
	 * @return 包含导入选项和模式版本的 JSON 文本，用于构建缓存键。
	 */
	std::string AssetImportSettingsKey(const AssetImportSettings&);
}
