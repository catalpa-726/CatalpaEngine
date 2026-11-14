#pragma once

#include "catpch.h"

#include "Core/Id/StableId.h"
#include "Core/IO/VirtualFileSystem.h"
#include "Core/Serialization/Archive.h"
#include "Assets/AssetRegistry.h"

namespace Catalpa::Tools {

	/* 作者配置给出显式路径，相对路径以项目文件所在目录为基准。 */
	struct ProjectConfig {
		StableId id;
		std::string name;
		std::filesystem::path projectFile;
		std::filesystem::path contentRoot;
		std::map<std::string, std::filesystem::path, std::less<>> externalSourceRoots;
		std::filesystem::path derivedDataRoot;
		std::filesystem::path cookedRoot;
		std::string metadataRelativePath;
		std::filesystem::path buildProfilesFile;
		std::string buildProfile;
		std::string targetPlatform;
		std::string buildConfiguration;
		std::optional<AssetId> startupScene;
		std::vector<std::string> modules;
	};

	/* 只读作者会话；注册身份不等于加载场景、创建GPU资源或完成异步任务。 */
	struct ProjectSession {
		ProjectConfig config;
		VirtualFileSystem files;
		AssetRegistry assets;
		ArchiveValue metadata;
		ArchiveValue selectedBuildProfile;
		std::vector<std::string> diagnostics;
	};

	/**
	 * @brief 打开项目、装配挂载与资产目录，再发布完整候选会话。
	 * 构建独占候选会话后交换指针发布，不依赖值树/STL容器的移动构造是否抛异常。
	 * 读取/分配/类型/路径/注册失败时保持原指针及其完整状态；成功使旧借用失效。
	 *
	 * @param[in] absoluteProjectFile 项目配置文件的绝对路径。
	 * @param[out] output 成功时接收新建的项目会话，失败保留原会话。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 项目加载成功并安装候选会话时为 true，失败保留原会话。
	 */
	bool LoadProject(
	    const std::filesystem::path& absoluteProjectFile, std::unique_ptr<ProjectSession>& output, std::string& error);
	/**
	 * @brief 把已解析的项目配置安全地写回规范路径。
	 * 显式保存作者项目描述；目标必须为 Load 返回的已 canonical 路径，别名目标直接拒绝。
	 * 调用者保证作者目录拓扑稳定；不承诺抵御并发恶意替换，不写原素材/元数据、不自动建目录。
	 *
	 * @param[in] config 要写入原项目配置路径的完整作者项目设置。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 项目配置成功写入时为 true，路径无效或文件替换失败时为 false。
	 */
	bool SaveProjectConfig(const ProjectConfig& config, std::string& error);
	/**
	 * @brief 根据已经解析的项目配置准备可用会话。
	 * 设置面板先构造完整会话，再保存并无分配地交换；不在磁盘提交后重新执行可能失败的加载。
	 *
	 * @param[in] config 用于构造候选会话的作者项目配置。
	 * @param[out] output 成功时接收已校验并准备好挂载的候选会话。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 候选项目会话构造并校验成功时为 true，失败保留原会话。
	 */
	bool PrepareProjectSession(const ProjectConfig&, std::unique_ptr<ProjectSession>&, std::string& error);
}
