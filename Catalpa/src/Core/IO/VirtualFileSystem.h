#pragma once

#include "catpch.h"
#include "Core/Base.h"
#include "Core/IO/VirtualPath.h"

namespace Catalpa {
    class PackageReader;
    // Configure on the owning thread; concurrent reads are allowed only while mounts are unchanged.
    // Roots are explicit absolute directories. No current-directory or sandbox fallback is provided.
    class CATALPA_API VirtualFileSystem {
    public:
        // 候选挂载表准备成功后一次无分配发布，供Runtime安装Cooked catalog事务使用。
      void Swap(VirtualFileSystem& other) noexcept {
          m_Mounts.swap(other.m_Mounts);
          m_Packages.swap(other.m_Packages);
      }
        // Higher priority wins. Equal priorities or duplicate roots for a mount are rejected.
        [[nodiscard]] bool MountReadOnly(std::string_view mount, const std::filesystem::path& absoluteRoot,
            int priority, std::string& error);
        [[nodiscard]] bool MountPackage(std::shared_ptr<const PackageReader>, std::string& error);
        // Only a missing file allows fallback; access, alias, escape and size errors stop resolution.
        // Read into owned bytes, bounded before allocation. Failure preserves output.
        [[nodiscard]] bool ReadFile(const VirtualPath& path, size_t maxBytes,
            std::vector<std::byte>& output, std::string& error) const;

    private:
        struct Mount {
            std::string name;
            std::filesystem::path root;
            int priority = 0;
        };
        std::vector<Mount> m_Mounts;
        std::vector<std::shared_ptr<const PackageReader>> m_Packages;
    };
}
