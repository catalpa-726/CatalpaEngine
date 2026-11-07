#pragma once
#include "Assets/AssetContainerReader.h"
#include "Assets/AssetRegistry.h"
#include "Core/IO/VirtualFileSystem.h"
#include "Core/Jobs/CancellationToken.h"

namespace Catalpa::Tools {
    struct AssetInspectionLimits {
        size_t maxAssetBytes = 640 * 1024 * 1024;
        size_t maxTotalBytes = size_t(2) * 1024 * 1024 * 1024;
        size_t maxResidentBytes = size_t(1) * 1024 * 1024 * 1024;
        size_t maxEntries = 65536;
        double timeoutSeconds = 120;
    };
    struct AssetInspection {
        AssetId id;
        std::string type, sha256;
        uint64_t version = 0;
        size_t payloadBytes = 0;
        std::vector<AssetDependency> dependencies;
        // 成功表示领域 CPU loader 已真实解码；不等同于设备上传、脚本执行或 UI 布局验收。
        bool domainValidated = false;
        // 独立无依赖容器可直接检查；带依赖容器须提供目录，避免把缺少闭包误报为成功。
        static bool Read(const std::filesystem::path&, AssetInspection&, std::string& error,
                         size_t maxBytes = 640 * 1024 * 1024);
        // roots 为空检查整张目录，否则只检查这些根的传递闭包，包括惰性音频块。
        // 使用生产注册器和无设备 AssetManager，失败保留调用者先前的报告。
        static bool ReadClosure(const AssetRegistry&, const VirtualFileSystem&,
                                std::span<const AssetId> roots, std::vector<AssetInspection>&,
                                std::string& error, CancellationToken = {}, AssetInspectionLimits = {});
    };
} // namespace Catalpa::Tools
