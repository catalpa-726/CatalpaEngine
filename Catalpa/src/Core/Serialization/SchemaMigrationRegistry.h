#pragma once
#include "Core/Serialization/Archive.h"
#include "Core/Base.h"

namespace Catalpa {
    // 迁移按单个版本前进，只在完整成功后交付候选值树。
    class CATALPA_API SchemaMigrationRegistry {
      public:
        using Step = std::function<bool(ArchiveValue&, std::string&)>;
        bool Register(std::string type, uint32_t from, Step, std::string& error);
        bool Migrate(std::string_view type, uint32_t target, const ArchiveValue& source,
                     ArchiveValue& output, std::string& error) const;
      private:
        std::map<std::pair<std::string, uint32_t>, Step> m_Steps;
    };
}
