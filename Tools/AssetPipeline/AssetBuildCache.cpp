#include "catpch.h"
#include "AssetPipeline/AssetBuildCache.h"
#include "AssetPipeline/AssetDependencyScanner.h"
#include "Assets/AssetContainerReader.h"
#include "Assets/AssetManifest.h"
#include "Assets/AssetDependencyGraph.h"
#include "Core/IO/AtomicFileWriter.h"
#include "Core/PlatformServices.h"
#include "Core/Serialization/JsonArchive.h"
#include <nlohmann/json.hpp>

namespace Catalpa::Tools {
    namespace {
        std::filesystem::path Index(const AssetBuildContext& c) {
            return c.request.project.derivedDataRoot / "requests" /
                   (c.Identity("request").ToString() + ".json");
        }

        bool ReadBounded(const std::filesystem::path& root, const std::filesystem::path& relative,
                         size_t limit, std::vector<std::byte>& bytes, std::string& e) {
            VirtualFileSystem fs;
            VirtualPath path;
            return fs.MountReadOnly("ddc", root, 0, e) &&
                   VirtualPath::Parse("ddc:/" + AssetPathText(relative), path, e) &&
                   fs.ReadFile(path, limit, bytes, e);
        }

        bool Parse(const std::vector<std::byte>& bytes, nlohmann::json& value, std::string& e) {
            if (bytes.empty()) {
                e = "Empty cache metadata";
                return false;
            }
            const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            std::unique_ptr<ArchiveValue> checked;
            ArchiveError diagnostic;
            ArchiveLimits limits;
            limits.maxCollectionEntries = 65536;
            limits.maxNodes = 1000000;
            if (!JsonArchive::Parse(text, checked, diagnostic, limits)) {
                e = diagnostic.ToString();
                return false;
            }
            value = nlohmann::json::parse(text);
            return true;
        }
    }

    bool AssetBuildCache::Load(AssetBuildContext& c, std::string_view signature, AssetBuildResult& out,
                               std::string& e) {
        try {
            const auto index = Index(c);
            if (!std::filesystem::is_regular_file(index))
                return false;
            std::vector<std::byte> bytes;
            if (!ReadBounded(c.request.project.derivedDataRoot,
                             index.lexically_relative(c.request.project.derivedDataRoot), 4 * 1024 * 1024,
                             bytes, e))
                return false;
            nlohmann::json doc;
            if (!Parse(bytes, doc, e))
                return false;
            const auto& files = doc.at("files");
            if (!doc.at("schema").is_number_unsigned() || doc.at("schema").get<uint64_t>() != 1 ||
                doc.at("signature").get_ref<const std::string&>() != signature || !files.is_array() ||
                files.empty() || files.size() > 4096)
                return false;

            // 试探旧索引时单独记录依赖。未命中后，导入器只承担实际打开的文件和工作预算。
            AssetBuildContext probe = c;
            for (const auto& f : files) {
                const auto file = std::filesystem::u8path(f.at("path").get<std::string>());
                const auto previousCount = probe.Dependencies().size();
                std::vector<std::byte> raw;
                if (!file.is_absolute() || !probe.Read(file, raw, e) ||
                    probe.Dependencies().size() != previousCount + 1 ||
                    probe.Dependencies().back().sha256 != f.at("sha256").get_ref<const std::string&>())
                    return false;
            }
            const auto key = doc.at("key").get<std::string>();
            if (AssetDependencyScanner::Key(probe, signature) != key ||
                std::none_of(probe.Dependencies().begin(), probe.Dependencies().end(),
                             [&](const auto& input) { return input.path == c.request.source; }))
                return false;

            const auto relative = std::filesystem::path("versions") / key / "Build.manifest";
            if (!ReadBounded(c.request.project.cookedRoot, relative, 64 * 1024 * 1024, bytes, e) ||
                AssetBuildDigest(bytes) != doc.at("manifestHash").get_ref<const std::string&>())
                return false;
            AssetRegistry registry;
            AssetDependencyGraph graph;
            if (!AssetManifest::Read(bytes, registry, e) || !graph.Build(registry.GetRecords(), e))
                return false;
            AssetBuildResult result;
            result.primary = {};
            if (!AssetId::TryParse(doc.at("primary").get<std::string>(), result.primary))
                return false;
            for (const auto& record : registry.GetRecords()) {
                AssetContainer container;
                std::string hash;
                if (!c.CheckCancelled(e) ||
                    !ReadBounded(c.request.project.cookedRoot,
                                 std::filesystem::u8path(record.locator.path.RelativePath()),
                                 96 * 1024 * 1024, bytes, e) ||
                    !ComputeAssetContentHash(bytes, hash, e) || hash != record.contentHash ||
                    !AssetContainerReader::Read(bytes, container, e, 96 * 1024 * 1024) ||
                    container.id != record.id || container.version != record.version ||
                    container.type != record.type ||
                    container.dependencies.size() != record.dependencies.size())
                    return false;
                for (const auto& dep : container.dependencies) {
                    const auto* expected = registry.Find(dep.id);
                    if (!expected ||
                        std::find(record.dependencies.begin(), record.dependencies.end(), dep.id) ==
                            record.dependencies.end() ||
                        expected->version != dep.version || expected->contentHash != dep.contentHash)
                        return false;
                }
            }
            if (!registry.Find(result.primary))
                return false;
            result.records = registry.GetRecords();
            result.key = key;
            result.cookedRoot = c.request.project.cookedRoot;
            result.manifest = result.cookedRoot / "Assets.manifest";
            result.report = result.cookedRoot / "versions" / key / "SourceMap.json";
            result.success = true;
            result.cacheHit = true;
            if (!ReadBounded(c.request.project.cookedRoot,
                             std::filesystem::path("versions") / key / "SourceMap.json", 4 * 1024 * 1024,
                             bytes, e) ||
                AssetBuildDigest(bytes) != doc.at("reportHash").get<std::string>())
                return false;
            nlohmann::json report;
            if (!Parse(bytes, report, e) || report.at("key").get_ref<const std::string&>() != key ||
                report.at("primary").get_ref<const std::string&>() != result.primary.ToString() ||
                report.at("source").get_ref<const std::string&>() != AssetPathText(c.request.source) ||
                !report.at("diagnostics").is_array() || !report.at("mappings").is_array())
                return false;
            for (const auto& d : report.at("diagnostics")) {
                const auto level = d.at("level").get<int>();
                if (level < 0 || level > 2)
                    return false;
                result.diagnostics.push_back({AssetDiagnosticLevel(level), d.at("source").get<std::string>(),
                                              d.at("element").get<std::string>(),
                                              d.at("message").get<std::string>()});
            }
            for (const auto& m : report.at("mappings")) {
                AssetId id;
                StableId node;
                const auto nodeText = m.at("nodeId").get<std::string>();
                if (!AssetId::TryParse(m.at("id").get<std::string>(), id) || !registry.Find(id) ||
                    (!nodeText.empty() && !StableId::TryParse(nodeText, node)))
                    return false;
                result.mappings.push_back({id, m.at("key").get<std::string>(),
                                           m.at("name").get<std::string>(), m.at("kind").get<std::string>(),
                                           node});
            }
            c = std::move(probe);
            out = std::move(result);
            return true;
        } catch (const std::exception& x) {
            e = std::string("Cache miss: ") + x.what();
            return false;
        }
    }

    bool AssetBuildCache::Save(const AssetBuildContext& c, std::string_view signature,
                               const AssetBuildResult& result, std::string& e) {
        using J = nlohmann::json;
        J doc{{"schema", 1},
              {"signature", signature},
              {"key", result.key},
              {"primary", result.primary.ToString()},
              {"files", J::array()}};
        std::vector<std::byte> report;
        if (!ReadBounded(c.request.project.cookedRoot,
                         result.report.lexically_relative(c.request.project.cookedRoot), 4 * 1024 * 1024,
                         report, e))
            return false;
        doc["reportHash"] = AssetBuildDigest(report);
        std::vector<std::byte> manifest;
        if (!ReadBounded(c.request.project.cookedRoot,
                         std::filesystem::path("versions") / result.key / "Build.manifest", 64 * 1024 * 1024,
                         manifest, e))
            return false;
        doc["manifestHash"] = AssetBuildDigest(manifest);
        for (const auto& f : c.Dependencies())
            doc["files"].push_back(
                {{"path", AssetPathText(f.path)}, {"sha256", f.sha256}, {"bytes", f.bytes}});
        const auto target = Index(c);
        const auto& root = c.request.project.derivedDataRoot;
        if (!IsPathWithinRoot(std::filesystem::weakly_canonical(target.parent_path()), root)) {
            e = "DDC index ancestor escaped derived root";
            return false;
        }
        std::filesystem::create_directories(target.parent_path());
        if (!IsPathWithinRoot(std::filesystem::canonical(target.parent_path()), root)) {
            e = "DDC index parent escaped derived root";
            return false;
        }
        return AtomicFileWriter::Write(target, doc.dump(), e);
    }
}
