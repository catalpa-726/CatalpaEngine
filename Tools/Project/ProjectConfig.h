#pragma once

#include "catpch.h"
#include "Core/Id/StableId.h"
#include "Core/IO/VirtualFileSystem.h"
#include "Core/Serialization/Archive.h"
#include "Assets/AssetRegistry.h"

namespace Catalpa::Tools {

    // 作者配置解析后的显式路径；路径以项目文件目录解析，不依赖进程 cwd。
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

    // 只读作者会话；注册身份不等于加载场景、创建GPU资源或完成异步任务。
    struct ProjectSession {
        ProjectConfig config;
        VirtualFileSystem files;
        AssetRegistry assets;
        ArchiveValue metadata;
        ArchiveValue selectedBuildProfile;
        std::vector<std::string> diagnostics;
    };

    // 构建独占候选会话后交换指针发布，不依赖值树/STL容器的移动构造是否抛异常。
    // 读取/分配/类型/路径/注册失败时保持原指针及其完整状态；成功使旧借用失效。
    bool LoadProject(const std::filesystem::path& absoluteProjectFile,
        std::unique_ptr<ProjectSession>& output, std::string& error);
    // 显式保存作者项目描述；目标必须为 Load 返回的已 canonical 路径，别名目标直接拒绝。
    // 调用者保证作者目录拓扑稳定；不承诺抵御并发恶意替换，不写原素材/元数据、不自动建目录。
    bool SaveProjectConfig(const ProjectConfig& config, std::string& error);
}
