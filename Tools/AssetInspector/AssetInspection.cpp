#include "catpch.h"

#include "AssetInspector/AssetInspection.h"

#include "Assets/AssetDependencyGraph.h"
#include "Assets/AssetManager.h"
#include "Audio/AudioClipAsset.h"
#include "Runtime/RuntimeRegistration.h"

namespace Catalpa::Tools {
	namespace {
		bool ReadMetadata(std::span<const std::byte> bytes, const AssetRecord& record, const AssetRegistry& catalog,
		    AssetInspection& output, size_t limit, std::string& error) {
			AssetContainer container;
			AssetInspection value;
			if (!AssetContainerReader::Read(bytes, container, error, limit) ||
			    !ComputeAssetContentHash(bytes, value.sha256, error))
				return false;
			if (container.id != record.id || container.type != record.type || container.version != record.version ||
			    value.sha256 != record.contentHash || container.dependencies.size() != record.dependencies.size()) {
				error = "Inspection container identity/type/version/hash/dependency table differs from catalog";
				return false;
			}
			std::set<AssetId> expected(record.dependencies.begin(), record.dependencies.end());
			for (const auto& dependency : container.dependencies) {
				const auto* target = catalog.Find(dependency.id);
				if (!expected.erase(dependency.id) || !target || target->version != dependency.version ||
				    target->contentHash != dependency.contentHash) {
					error =
					    "Inspection dependency has missing or stale identity/version/hash: " + dependency.id.ToString();
					return false;
				}
			}
			value.id = container.id;
			value.type = container.type.ToString();
			value.version = container.version;
			value.payloadBytes = container.payload.size();
			value.dependencies = std::move(container.dependencies);
			output = std::move(value);
			return true;
		}
	} /* 命名空间 */

	bool AssetInspection::Read(
	    const std::filesystem::path& file, AssetInspection& output, std::string& error, size_t limit) {
		try {
			if (!file.is_absolute()) {
				error = "Inspection requires an absolute file path";
				return false;
			}
			VirtualFileSystem files;
			VirtualPath path;
			const auto name = file.filename().u8string();
			std::vector<std::byte> bytes;
			if (!files.MountReadOnly("inspection", file.parent_path(), 0, error) ||
			    !VirtualPath::Parse(
			        "inspection:/" + std::string(reinterpret_cast<const char*>(name.data()), name.size()), path,
			        error) ||
			    !files.ReadFile(path, limit, bytes, error))
				return false;
			AssetContainer container;
			std::string hash;
			if (!AssetContainerReader::Read(bytes, container, error, limit) ||
			    !ComputeAssetContentHash(bytes, hash, error))
				return false;
			if (!container.dependencies.empty()) {
				error = "This asset declares dependencies; inspect its .manifest/Cooked directory or .catpkg "
				        "to validate the complete domain payload";
				return false;
			}
			AssetRecord record{
			    container.id, container.type, {path, AssetLocationKind::Cooked}, container.version, {}, hash};
			AssetRegistry catalog;
			std::vector<AssetInspection> values;
			AssetInspectionLimits limits;
			limits.maxAssetBytes = limit;
			limits.maxTotalBytes = limit;
			if (!catalog.Replace({record}, error) || !ReadClosure(catalog, files, {}, values, error, {}, limits))
				return false;
			output = std::move(values.front());
			error.clear();
			return true;
		} catch (const std::exception& exception) {
			error = exception.what();
			return false;
		}
	}

	bool AssetInspection::ReadClosure(const AssetRegistry& catalog, const VirtualFileSystem& files,
	    std::span<const AssetId> roots, std::vector<AssetInspection>& output, std::string& error,
	    CancellationToken cancellation, AssetInspectionLimits limits) {
		try {
			if (!limits.maxAssetBytes || !limits.maxTotalBytes || !limits.maxResidentBytes || !limits.maxEntries ||
			    limits.maxEntries > 65536 || !std::isfinite(limits.timeoutSeconds) || limits.timeoutSeconds <= 0 ||
			    limits.timeoutSeconds > 3600 || limits.maxAssetBytes > (SIZE_MAX - limits.maxResidentBytes) / 3) {
				error = "Invalid inspection budgets";
				return false;
			}
			const auto deadline =
			    std::chrono::steady_clock::now() + std::chrono::duration<double>(limits.timeoutSeconds);
			auto interrupted = [&]() {
				if (cancellation.IsCancellationRequested()) {
					error = "Asset inspection cancelled";
					return true;
				}
				if (std::chrono::steady_clock::now() >= deadline) {
					error = "Asset inspection timed out";
					return true;
				}
				return false;
			};
			/* 在条目与边数预算内收集根资产的传递依赖，重复引用的节点只加入一次。 */
			std::set<AssetId> selected;
			std::vector<AssetId> pending;
			const auto enqueue = [&](AssetId id) {
				if (!selected.insert(id).second)
					return true;
				if (selected.size() > limits.maxEntries) {
					error = "Inspection closure exceeds entry budget";
					return false;
				}
				pending.push_back(id);
				return true;
			};
			if (roots.empty()) {
				for (const auto& record : catalog.GetRecords())
					if (!enqueue(record.id))
						return false;
			} else
				for (auto id : roots)
					if (!enqueue(id))
						return false;
			size_t edges = 0;
			for (size_t index = 0; index < pending.size(); ++index) {
				if (interrupted())
					return false;
				const auto id = pending[index];
				const auto* record = catalog.Find(id);
				if (!record) {
					error = "Inspection closure contains a missing asset or exceeds entry budget: " + id.ToString();
					return false;
				}
				if (record->dependencies.size() > 1048576 - edges) {
					error = "Inspection dependency edge budget exceeded";
					return false;
				}
				edges += record->dependencies.size();
				for (auto dependency : record->dependencies)
					if (!enqueue(dependency))
						return false;
			}
			if (selected.empty()) {
				error = "Inspection catalog is empty";
				return false;
			}
			std::vector<AssetRecord> records;
			for (auto id : selected)
				records.push_back(*catalog.Find(id));
			AssetRegistry closure;
			AssetDependencyGraph graph;
			if (!closure.Replace(records, error) || !graph.Build(records, error))
				return false;

			/* 先完整核对容器和闭包；payload 交给实际领域 loader，不能把所有二进制都当 JSON。 */
			std::vector<AssetInspection> reports;
			/* 逐个校验容器，记录各依赖的消费者数，为后续加载与释放安排顺序。 */
			std::map<AssetId, size_t> reportIndex, remainingConsumers;
			size_t totalBytes = 0;
			for (auto id : graph.Order()) {
				if (interrupted())
					return false;
				const auto& record = *closure.Find(id);
				std::vector<std::byte> bytes;
				AssetInspection report;
				if (record.locator.kind != AssetLocationKind::Cooked || record.contentHash.empty() ||
				    !files.ReadFile(record.locator.path, limits.maxAssetBytes, bytes, error)) {
					if (error.empty())
						error = "Inspection requires cooked records with exact digests";
					error = id.ToString() + ": " + error;
					return false;
				}
				if (bytes.size() > limits.maxTotalBytes - totalBytes) {
					error = "Inspection total container byte budget exceeded";
					return false;
				}
				totalBytes += bytes.size();
				if (!ReadMetadata(bytes, record, closure, report, limits.maxAssetBytes, error)) {
					error = id.ToString() + ": " + error;
					return false;
				}
				reportIndex[id] = reports.size();
				reports.push_back(std::move(report));
				remainingConsumers.try_emplace(id, 0);
				for (auto dependency : record.dependencies)
					++remainingConsumers[dependency];
			}

			JobSystem jobs;
			if (!jobs.Start({1, 8}, error))
				return false;
			/* 每次仅提交一个已就绪节点；预算包含一个解码工作区和仍被后续节点需要的驻留值。 */
			AssetManagerConfig config;
			config.maxFileBytes = limits.maxAssetBytes;
			config.maxResidentBytes = limits.maxAssetBytes;
			config.memoryBudgetBytes = limits.maxResidentBytes + limits.maxAssetBytes * 3;
			config.maxPending = 8;
			config.maxKnownVersions = limits.maxEntries;
			AssetManager manager(jobs, files, closure, config);
			if (!RegisterRuntimeAssetLoaders(manager, error))
				return false;
			for (auto id : graph.Order()) {
				if (interrupted())
					return false;
				if (!manager.Request(id, error)) {
					error = id.ToString() + ": " + error;
					return false;
				}
				while (manager.GetStatus(id) != AssetStatus::Ready) {
					if (interrupted())
						return false;
					manager.Pump();
					const auto state = manager.GetStatus(id);
					if (state == AssetStatus::Failed || state == AssetStatus::Cancelled ||
					    state == AssetStatus::Unloaded) {
						error = id.ToString() + " (" + closure.Find(id)->type.ToString() +
						    "): " + manager.GetDiagnostic(id);
						return false;
					}
					if (state != AssetStatus::Ready)
						std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
				reports[reportIndex.at(id)].domainValidated = true;
				/* 惰性音频的 main loader 只持 seek table；检查器还须验证每块实际长度，不能只核对块数。 */
				if (closure.Find(id)->type.ToString() == "audio_clip") {
					AssetSnapshot snapshot;
					if (!manager.GetAsset(id, typeid(AudioClipAsset), snapshot, error))
						return false;
					const auto clip = std::static_pointer_cast<const AudioClipAsset>(snapshot.asset.value);
					if (clip->Streaming())
						for (size_t index = 0; index < clip->chunks.size(); ++index) {
							AssetSnapshot chunk;
							if (!manager.GetAsset(clip->chunks[index].id, typeid(AudioChunkAsset), chunk, error))
								return false;
							const auto value = std::static_pointer_cast<const AudioChunkAsset>(chunk.asset.value);
							const auto expected = std::min<uint64_t>(65536, clip->Frames() - uint64_t(index) * 65536);
							if (value->samples.size() != expected * 2) {
								error = "Inspection audio seek-table chunk frame count mismatch: " +
								    clip->chunks[index].id.ToString();
								return false;
							}
						}
				}
				if (manager.GetBudgetStats().used > limits.maxResidentBytes) {
					error = "Inspection live domain payload exceeds aggregate resident budget";
					return false;
				}
				if (!remainingConsumers.at(id))
					manager.Unload(id);
				for (auto dependency : closure.Find(id)->dependencies)
					if (--remainingConsumers.at(dependency) == 0)
						manager.Unload(dependency);
			}
			/* 成员声明顺序保证管理器先停止并等回调结束，作业系统随后才释放调度器。 */
			manager.Shutdown();
			jobs.Shutdown();
			output = std::move(reports);
			error.clear();
			return true;
		} catch (const std::exception& exception) {
			error = exception.what();
			return false;
		}
	}
} /* 命名空间 Catalpa::Tools */
