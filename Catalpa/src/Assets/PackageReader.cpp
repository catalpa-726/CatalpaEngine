#include "catpch.h"

#include "Assets/PackageReader.h"

#include "Assets/AssetContainerReader.h"
#include "Assets/AssetDependencyGraph.h"
#include "Core/Serialization/BinaryArchive.h"

namespace Catalpa {
	bool PackageReader::Open(const std::filesystem::path& file, std::shared_ptr<const PackageReader>& out,
	    std::string& error, PackageLimits limits) {
		try {
			if (!file.is_absolute()) {
				error = "Package path must be absolute";
				return false;
			}
			std::ifstream stream(file, std::ios::binary | std::ios::ate);
			const auto length = stream.tellg();
			if (!stream || length < 0 || uint64_t(length) > limits.maxPackageBytes) {
				error = "Package is unavailable or exceeds byte budget";
				return false;
			}
			std::vector<std::byte> bytes(static_cast<size_t>(length));
			stream.seekg(0);
			if (!stream.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()))) {
				error = "Package read failed";
				return false;
			}
			return Decode(std::move(bytes), out, error, limits);
		} catch (const std::exception& e) {
			error = e.what();
			return false;
		}
	}

	bool PackageReader::Decode(std::vector<std::byte> bytes, std::shared_ptr<const PackageReader>& out,
	    std::string& error, PackageLimits limits) {
		try {
			if (bytes.size() > limits.maxPackageBytes) {
				error = "Package byte budget exceeded";
				return false;
			}
			BinaryReader reader(bytes);
			std::string magic, entryText;
			uint32_t version = 0, count = 0;
			uint64_t manifestSize = 0;
			std::span<const std::byte> manifest;
			auto candidate = std::make_shared<PackageReader>();
			if (!reader.String(magic, 16) || magic != "CATPACKAGE" || !reader.U32(version) || version != 1 ||
			    !reader.String(entryText, 36) || !AssetId::TryParse(entryText, candidate->m_EntryScene) ||
			    !reader.U64(manifestSize) || manifestSize > 16 * 1024 * 1024 ||
			    !reader.Bytes(size_t(manifestSize), manifest) ||
			    !AssetManifest::Read(manifest, candidate->m_Catalog, error) || !reader.U32(count) || !count ||
			    count > limits.maxEntries || count != candidate->m_Catalog.GetRecords().size()) {
				if (error.empty())
					error = "Invalid package header, manifest or entry count";
				return false;
			}
			const auto* scene = candidate->m_Catalog.Find(candidate->m_EntryScene);
			if (!scene || scene->type.ToString() != "scene") {
				error = "Package entry must name a scene";
				return false;
			}
			AssetDependencyGraph graph;
			if (!candidate->m_Catalog.GetMissingDependencies().empty() ||
			    !graph.Build(candidate->m_Catalog.GetRecords(), error)) {
				if (error.empty())
					error = "Package has missing dependencies";
				return false;
			}
			for (const auto& record : candidate->m_Catalog.GetRecords())
				candidate->m_Mounts.insert(record.locator.path.MountName());
			for (uint32_t i = 0; i < count; ++i) {
				std::string path;
				uint64_t length = 0;
				std::span<const std::byte> data;
				VirtualPath virtualPath;
				if (!reader.String(path, 32760) || !VirtualPath::Parse(path, virtualPath, error) ||
				    !reader.U64(length) || length > limits.maxAssetBytes || !reader.Bytes(size_t(length), data)) {
					error = "Invalid package entry range or path";
					return false;
				}
				const auto* record = candidate->m_Catalog.Find(virtualPath);
				AssetContainer asset;
				std::string digest;
				if (!record || record->locator.kind != AssetLocationKind::Cooked ||
				    !AssetContainerReader::Read(data, asset, error, limits.maxAssetBytes) ||
				    !ComputeAssetContentHash(data, digest, error))
					return false;
				if (asset.id != record->id || asset.type != record->type || asset.version != record->version ||
				    digest != record->contentHash || asset.dependencies.size() != record->dependencies.size()) {
					error = "Package asset identity, version, hash or dependency count mismatch";
					return false;
				}
				std::set<AssetId> declared(record->dependencies.begin(), record->dependencies.end());
				for (const auto& dependency : asset.dependencies) {
					const auto* target = candidate->m_Catalog.Find(dependency.id);
					if (!declared.erase(dependency.id) || !target || target->version != dependency.version ||
					    target->contentHash != dependency.contentHash) {
						error = "Package contains a stale or undeclared dependency";
						return false;
					}
				}
				if (!candidate->m_Entries
				        .emplace(path, PackageEntry{path, size_t(data.data() - bytes.data()), data.size()})
				        .second) {
					error = "Duplicate package path";
					return false;
				}
			}
			if (!reader.AtEnd()) {
				error = "Package contains trailing or truncated data";
				return false;
			}
			candidate->m_Bytes = std::move(bytes);
			out = std::move(candidate);
			error.clear();
			return true;
		} catch (const std::exception& e) {
			error = e.what();
			return false;
		}
	}

	bool PackageReader::Read(
	    const VirtualPath& path, size_t limit, std::vector<std::byte>& out, std::string& error) const {
		const auto found = m_Entries.find(path.ToString());
		if (found == m_Entries.end()) {
			error = "Asset is not in this package";
			return false;
		}
		if (found->second.bytes > limit) {
			error = "Package entry exceeds read budget";
			return false;
		}
		const auto& entry = found->second;
		try {
			std::vector<std::byte> value(m_Bytes.begin() + entry.offset, m_Bytes.begin() + entry.offset + entry.bytes);
			out.swap(value);
			error.clear();
			return true;
		} catch (const std::exception& e) {
			error = e.what();
			return false;
		}
	}
} /* 命名空间 Catalpa */
