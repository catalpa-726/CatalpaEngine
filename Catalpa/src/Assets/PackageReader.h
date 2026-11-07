#pragma once
#include "Assets/PackageFormat.h"

namespace Catalpa {
    // 完整验证后才发布的只读快照，可跨读任务共享；不提取到源目录或临时目录。
    class CATALPA_API PackageReader final {
      public:
        static bool Open(const std::filesystem::path& absoluteFile, std::shared_ptr<const PackageReader>&,
                         std::string& error, PackageLimits limits = {});
        static bool Decode(std::vector<std::byte>, std::shared_ptr<const PackageReader>&, std::string& error,
                           PackageLimits limits = {});
        bool Read(const VirtualPath&, size_t maxBytes, std::vector<std::byte>&, std::string& error) const;
        const AssetRegistry& Catalog() const noexcept {
            return m_Catalog;
        }
        bool OwnsMount(std::string_view mount) const noexcept {
            return m_Mounts.contains(mount);
        }
        size_t StorageBytes() const noexcept {
            return m_Bytes.capacity();
        }
        AssetId EntryScene() const noexcept {
            return m_EntryScene;
        }

      private:
        AssetRegistry m_Catalog;
        AssetId m_EntryScene;
        std::set<std::string, std::less<>> m_Mounts;
        std::vector<std::byte> m_Bytes;
        std::map<std::string, PackageEntry, std::less<>> m_Entries;
    };
} // namespace Catalpa
