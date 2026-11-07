#pragma once
#include "AssetPipeline/IAssetImporter.h"

namespace Catalpa::Tools {
    class PhysicsNavigationImporter final : public IAssetImporter {
      public:
        explicit PhysicsNavigationImporter(std::string type) : m_Type(std::move(type)) {}
        std::string_view Name() const noexcept override {
            return "physics-navigation/2";
        }
        bool Import(AssetBuildContext&, AssetImportResult&, std::string& error) const override;

      private:
        std::string m_Type;
    };
} // namespace Catalpa::Tools
