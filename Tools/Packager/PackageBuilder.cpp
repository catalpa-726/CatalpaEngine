#include "catpch.h"
#include "Packager/PackageBuilder.h"
#include "Assets/AssetDependencyGraph.h"
#include "Assets/PackageReader.h"
#include "Core/Serialization/BinaryArchive.h"
#include "Core/IO/AtomicFileWriter.h"

namespace Catalpa::Tools {
    bool PackageBuilder::Build(const AssetRegistry& source, const VirtualFileSystem& files, AssetId entry,
                               const std::filesystem::path& output, CancellationToken cancellation,
                               std::string& error, PackageLimits limits) {
        try {
            if (!output.is_absolute() || output.extension() != L".catpkg") {
                error = "Package output must be an absolute .catpkg path";
                return false;
            }
            std::set<AssetId> closure;
            std::vector<AssetId> pending{entry};
            while (!pending.empty()) {
                const auto id = pending.back();
                pending.pop_back();
                if (!closure.insert(id).second)
                    continue;
                const auto* record = source.Find(id);
                if (!record || closure.size() > limits.maxEntries) {
                    error = "Package dependency missing or entry budget exceeded";
                    return false;
                }
                pending.insert(pending.end(), record->dependencies.begin(), record->dependencies.end());
            }
            std::vector<AssetRecord> records;
            for (auto id : closure) {
                auto record = *source.Find(id);
                if (!VirtualPath::Parse("package:/" + id.ToString() + ".catasset", record.locator.path,
                                        error))
                    return false;
                records.push_back(std::move(record));
            }
            AssetRegistry catalog;
            std::vector<std::byte> manifest;
            if (!catalog.Replace(records, error) || !AssetManifest::Write(catalog, manifest, error))
                return false;
            BinaryWriter writer(limits.maxPackageBytes);
            if (!writer.String("CATPACKAGE") || !writer.U32(1) || !writer.String(entry.ToString()) ||
                !writer.U64(manifest.size()) || !writer.Bytes(manifest) ||
                !writer.U32(uint32_t(records.size()))) {
                error = writer.Error();
                return false;
            }
            for (const auto& record : records) {
                if (cancellation.IsCancellationRequested()) {
                    error = "Package build cancelled";
                    return false;
                }
                std::vector<std::byte> bytes;
                if (!files.ReadFile(source.Find(record.id)->locator.path, limits.maxAssetBytes, bytes,
                                    error) ||
                    !writer.String(record.locator.path.ToString()) || !writer.U64(bytes.size()) ||
                    !writer.Bytes(bytes)) {
                    if (error.empty())
                        error = writer.Error();
                    return false;
                }
            }
            // 与运行读取器共用验证。全部成功后，只替换一个文件，旧包始终有效。
            std::shared_ptr<const PackageReader> verified;
            if (!PackageReader::Decode(writer.Data(), verified, error, limits))
                return false;
            if (cancellation.IsCancellationRequested()) {
                error = "Package build cancelled before publication";
                return false;
            }
            return AtomicFileWriter::Write(output, writer.Data(), error);
        } catch (const std::exception& e) {
            error = e.what();
            return false;
        }
    }
} // namespace Catalpa::Tools
