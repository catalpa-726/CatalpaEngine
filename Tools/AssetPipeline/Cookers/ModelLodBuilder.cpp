#include "catpch.h"

#include "AssetPipeline/Cookers/ModelLodBuilder.h"

#include "Geometry/MeshValidation.h"

#include <meshoptimizer.h>

namespace Catalpa::Tools {
	bool SimplifyMeshLod(const MeshAsset& input, float ratio, float errorLimit, MeshAsset& output, float& absoluteError,
	    std::string& error) try {
		if (!ValidateMeshAsset(input, error) || !std::isfinite(ratio) || ratio <= 0 || ratio >= 1 ||
		    !std::isfinite(errorLimit) || errorLimit <= 0 || errorLimit > 1) {
			if (error.empty())
				error = "Invalid mesh LOD request";
			return false;
		}
		const auto& source = input.mesh->GetData();
		const auto position = std::find_if(source.attributes.begin(), source.attributes.end(),
		    [](const auto& attribute) { return attribute.semantic == VertexSemantic::Position; });
		if (position == source.attributes.end() || position->format != VertexFormat::Float3) {
			error = "LOD requires a Float3 position stream";
			return false;
		}
		std::vector<std::array<float, 3>> positions(source.vertexCount);
		for (size_t i = 0; i < positions.size(); ++i)
			std::memcpy(positions[i].data(), source.vertexData.data() + i * source.vertexStride + position->offset, 12);
		const float scale =
		    meshopt_simplifyScale(positions.front().data(), positions.size(), sizeof(positions.front()));
		MeshData derived = source;
		derived.indexType = MeshIndexType::UInt32;
		derived.indexData.clear();
		derived.submeshes.clear();
		std::vector<uint32_t> indices;
		float maximum = 0;
		for (const auto& submesh : source.submeshes) {
			std::vector<uint32_t> original(submesh.indexCount);
			for (size_t i = 0; i < original.size(); ++i) {
				uint32_t raw = 0;
				const auto* bytes =
				    source.indexData.data() + (submesh.firstIndex + i) * GetMeshIndexByteSize(source.indexType);
				if (source.indexType == MeshIndexType::UInt16) {
					uint16_t value;
					std::memcpy(&value, bytes, 2);
					raw = value;
				} else
					std::memcpy(&raw, bytes, 4);
				original[i] = uint32_t(int64_t(raw) + submesh.baseVertex);
			}
			std::vector<uint32_t> simplified(original.size());
			float relative = 0;
			const auto target = std::max(size_t(3), (size_t(double(original.size()) * ratio) / 3) * 3);
			const auto count = meshopt_simplify(simplified.data(), original.data(), original.size(),
			    positions.front().data(), positions.size(), sizeof(positions.front()), target, errorLimit,
			    meshopt_SimplifyLockBorder, &relative);
			if (!count || count % 3 || count > original.size() || !std::isfinite(relative)) {
				error = "Meshoptimizer returned invalid LOD geometry/error";
				return false;
			}
			simplified.resize(count);
			std::vector<uint32_t> cached(count);
			meshopt_optimizeVertexCache(cached.data(), simplified.data(), count, source.vertexCount);
			derived.submeshes.push_back({uint32_t(indices.size()), uint32_t(count), 0, 0, source.vertexCount,
			    submesh.materialSlot, submesh.topology});
			indices.insert(indices.end(), cached.begin(), cached.end());
			maximum = std::max(maximum, relative * scale);
		}
		if (indices.size() >= source.GetIndexCount()) {
			error = "No topology-preserving LOD reduction was possible";
			return false;
		}
		const auto vertices = meshopt_optimizeVertexFetch(derived.vertexData.data(), indices.data(), indices.size(),
		    source.vertexData.data(), source.vertexCount, source.vertexStride);
		derived.vertexCount = uint32_t(vertices);
		derived.vertexData.resize(vertices * source.vertexStride);
		for (auto& submesh : derived.submeshes)
			submesh.vertexCount = derived.vertexCount;
		derived.indexData.resize(indices.size() * 4);
		std::memcpy(derived.indexData.data(), indices.data(), derived.indexData.size());
		/* 重排只复制完整原顶点记录；UV/切线/颜色不重采样，LOD0 保留原始源顺序。 */
		MeshAsset candidate;
		candidate.materialSlots = input.materialSlots;
		const auto status = CreateMeshResource(derived, candidate.mesh);
		if (!status.IsSuccess()) {
			error = status.message;
			return false;
		}
		output = std::move(candidate);
		absoluteError = maximum;
		error.clear();
		return true;
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}

	bool BuildModelLods(
	    AssetBuildContext& context, ModelAsset& model, AssetImportResult& imported, std::string& error) try {
		std::set<AssetId> animated;
		for (const auto& instance : model.meshes)
			if (instance.skin.HasValue())
				animated.insert(instance.mesh.id);
		struct Chain {
			std::vector<AssetHandle<MeshAsset>> meshes;
			std::vector<float> errors;
		};
		std::map<AssetId, Chain> chains;
		const size_t originalCount = imported.assets.size();
		std::vector<ImportedAsset> additions;
		for (size_t ai = 0; ai < originalCount; ++ai) {
			const auto& asset = imported.assets[ai];
			const auto* mesh = std::get_if<MeshAsset>(&asset.value);
			if (!mesh || animated.contains(asset.id) || mesh->mesh->GetData().GetIndexCount() < 384)
				continue;
			if (!context.CheckCancelled(error))
				return false;
			Chain chain;
			float previous = 0;
			for (size_t level = 0; level < 2; ++level) {
				const auto& source = mesh->mesh->GetData();
				if (!context.CheckCancelled(error) ||
				    !context.WorkspaceFits(
				        source.vertexData.size() * 3 + source.indexData.size() * 4 + size_t(source.vertexCount) * 12,
				        error))
					return false;
				MeshAsset lod;
				float measured = 0;
				std::string reason;
				const float ratio = level == 0 ? .5f : .25f, relative = level == 0 ? .01f : .03f;
				if (!SimplifyMeshLod(*mesh, ratio, relative, lod, measured, reason)) {
					context.Diagnose(AssetDiagnosticLevel::Info, asset.sourceKey, "LOD skipped: " + reason);
					continue;
				}
				if (!context.ReserveWorking(
				        lod.mesh->GetData().vertexData.size() + lod.mesh->GetData().indexData.size(), error))
					return false;
				/* 完全共面简化可能得到零几何误差；一个float ULP的尺度界用于稳定屏幕误差选择。 */
				const auto& bounds = mesh->mesh->GetData().localBounds;
				const float epsilon =
				    std::max(glm::length(bounds.maximum - bounds.minimum) * std::numeric_limits<float>::epsilon(),
				        std::numeric_limits<float>::min());
				const float bound = std::max(measured, epsilon);
				if (bound <= previous)
					continue;
				previous = bound;
				const auto key = asset.sourceKey + "/lod/" + std::to_string(level + 1);
				const auto id = context.Identity(key);
				chain.meshes.emplace_back(id);
				chain.errors.push_back(bound);
				additions.push_back({id, key, asset.name + " LOD" + std::to_string(level + 1), std::move(lod)});
				imported.mappings.push_back({id, key, asset.name, "mesh"});
				context.Diagnose(AssetDiagnosticLevel::Info, key,
				    "meshoptimizer LockBorder; ratio=" + std::to_string(ratio) +
				        "; geometric error bound source units=" + std::to_string(bound) +
				        "; source vertices and material slots unchanged");
			}
			if (!chain.meshes.empty())
				chains.emplace(asset.id, std::move(chain));
		}
		for (auto& instance : model.meshes)
			if (const auto found = chains.find(instance.mesh.id); found != chains.end()) {
				instance.lods = found->second.meshes;
				instance.lodErrors = found->second.errors;
			}
		for (auto& asset : additions)
			imported.assets.push_back(std::move(asset));
		return ValidateModelAsset(model, error);
	} catch (const std::exception& exception) {
		error = exception.what();
		return false;
	}
} /* 命名空间 Catalpa::Tools */
