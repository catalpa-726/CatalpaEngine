#include "catpch.h"
#include "AssetPipeline/AssetDependencyScanner.h"
#include <nlohmann/json.hpp>

namespace Catalpa::Tools {
    std::vector<AssetSourceDependency> AssetDependencyScanner::Snapshot(const AssetBuildContext& c) {
        auto d = c.Dependencies();
        std::sort(d.begin(), d.end(), [](const auto& a, const auto& b) { return a.path < b.path; });
        return d;
    }

    std::string AssetDependencyScanner::Key(const AssetBuildContext& c, std::string_view signature) {
        nlohmann::json key{{"signature", signature}, {"inputs", nlohmann::json::array()}};
        for (const auto& d : Snapshot(c))
            key["inputs"].push_back({AssetPathText(d.path), d.sha256, d.bytes});
        const auto text = key.dump();
        return AssetBuildDigest(std::as_bytes(std::span(text.data(), text.size())));
    }
}
