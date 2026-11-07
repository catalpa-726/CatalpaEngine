#include "catpch.h"
#include "AssetPipeline/AssetArchiveWriter.h"
#include "Assets/AssetContainerReader.h"
#include "Assets/AssetCompression.h"
#include <zstd.h>
#include <nlohmann/json.hpp>

namespace Catalpa::Tools {
    bool AssetArchiveWriter::Encode(AssetId id, std::string_view type, std::span<const std::byte> payload,
                                    std::span<const AssetRecord> deps, AssetBuildProduct& out,
                                    std::string& e) {
        AssetContainer container;
        container.id = id;
        if (!AssetType::TryParse(type, container.type)) {
            e = "Invalid cooked asset type";
            return false;
        }
        container.payload.assign(payload.begin(), payload.end());
        std::vector<AssetRecord> ordered(deps.begin(), deps.end());
        std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
        nlohmann::json key{{"format", 2},
                           {"compression", "zstd-level-3-or-raw"},
                           {"zstd", ZSTD_VERSION_NUMBER},
                           {"payloadVersion", 1},
                           {"type", type},
                           {"payload", AssetBuildDigest(payload)}};
        key["dependencies"] = nlohmann::json::array();
        for (const auto& dep : ordered) {
            container.dependencies.push_back({dep.id, dep.version, dep.contentHash});
            key["dependencies"].push_back({dep.id.ToString(), dep.version, dep.contentHash});
        }
        const auto text = key.dump();
        container.version =
            AssetContentVersion(AssetBuildDigest(std::as_bytes(std::span(text.data(), text.size()))));
        AssetBuildProduct product;
        if (!WriteCompressedAssetContainer(container, product.bytes, e, 96 * 1024 * 1024))
            return false;
        product.record.id = id;
        product.record.version = container.version;
        product.record.type = container.type;
        product.record.locator.kind = AssetLocationKind::Cooked;
        for (const auto& dep : ordered)
            product.record.dependencies.push_back(dep.id);
        if (!ComputeAssetContentHash(product.bytes, product.record.contentHash, e))
            return false;
        out = std::move(product);
        return true;
    }
}
