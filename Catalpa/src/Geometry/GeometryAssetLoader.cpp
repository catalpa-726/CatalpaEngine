#include "catpch.h"

#include "Geometry/MeshLod.h"
#include "Animation/AnimationAssetLoader.h"
#include "Geometry/GeometryAssetLoader.h"

#include "Assets/AssetManager.h"
#include "Materials/MaterialInstance.h"
#include "Core/Serialization/BinaryArchive.h"
namespace Catalpa {
	namespace {
		void Vec(BinaryWriter& w, const Math::Vec3& v) {
			w.F32(v.x);
			w.F32(v.y);
			w.F32(v.z);
		}

		bool Vec(BinaryReader& r, Math::Vec3& v) {
			return r.F32(v.x) && r.F32(v.y) && r.F32(v.z);
		}

		size_t MeshBytes(const MeshAsset& asset) {
			const auto& data = asset.mesh->GetData();
			size_t bytes = sizeof(MeshAsset) + sizeof(MeshResource) + data.vertexData.capacity() +
			    data.indexData.capacity() + data.attributes.capacity() * sizeof(VertexAttribute) +
			    data.submeshes.capacity() * sizeof(Submesh) + asset.materialSlots.capacity() * sizeof(std::string);
			for (const auto& name : asset.materialSlots)
				bytes += name.capacity() + 1;
			return bytes;
		}

		bool Finish(BinaryWriter& writer, std::vector<std::byte>& output, std::string& error) {
			if (!writer.Error().empty()) {
				error = writer.Error();
				return false;
			}
			output = writer.Data();
			error.clear();
			return true;
		}
	}

	bool GeometryAssetLoader::EncodeMesh(const MeshAsset& asset, std::vector<std::byte>& output, std::string& error) {
		try {
			if (!ValidateMeshAsset(asset, error))
				return false;
			const auto& data = asset.mesh->GetData();
			if (data.vertexData.size() % 4) {
				error = "Mesh float stream requires complete 32-bit words";
				return false;
			}
			BinaryWriter w;
			w.U32(data.vertexCount);
			w.U32(data.vertexStride);
			w.U32(static_cast<uint32_t>(data.indexType));
			w.U32(static_cast<uint32_t>(data.attributes.size()));
			for (const auto& a : data.attributes) {
				w.U32(a.location);
				w.U32(static_cast<uint32_t>(a.format));
				w.U32(a.offset);
				w.U32(static_cast<uint32_t>(a.semantic));
			}
			w.U64(data.vertexData.size());
			for (size_t offset = 0; offset < data.vertexData.size(); offset += 4) {
				uint32_t bits;
				std::memcpy(&bits, data.vertexData.data() + offset, 4);
				w.U32(bits);
			}
			w.U64(data.GetIndexCount());
			for (size_t index = 0; index < data.GetIndexCount(); ++index) {
				if (data.indexType == MeshIndexType::UInt16) {
					uint16_t value;
					std::memcpy(&value, data.indexData.data() + index * 2, 2);
					w.U8(static_cast<uint8_t>(value));
					w.U8(static_cast<uint8_t>(value >> 8));
				} else {
					uint32_t value;
					std::memcpy(&value, data.indexData.data() + index * 4, 4);
					w.U32(value);
				}
			}
			w.U32(static_cast<uint32_t>(data.submeshes.size()));
			for (const auto& s : data.submeshes) {
				w.U32(s.firstIndex);
				w.U32(s.indexCount);
				w.U32(static_cast<uint32_t>(s.baseVertex));
				w.U32(s.firstVertex);
				w.U32(s.vertexCount);
				w.U32(s.materialSlot);
				w.U32(static_cast<uint32_t>(s.topology));
			}
			w.U8(data.localBounds.empty ? 1 : 0);
			Vec(w, data.localBounds.minimum);
			Vec(w, data.localBounds.maximum);
			w.U32(static_cast<uint32_t>(asset.materialSlots.size()));
			for (const auto& slot : asset.materialSlots)
				w.String(slot);
			return Finish(w, output, error);
		} catch (const std::exception& exception) {
			error = exception.what();
			return false;
		}
	}

	bool GeometryAssetLoader::DecodeMesh(std::span<const std::byte> input, const AssetDecodeContext& context,
	    AssetLoadResult& output, std::string& error) {
		try {
			if (context.cancellation.IsCancellationRequested() || input.size() > context.maxResidentBytes / 2) {
				error = "Mesh decode cancelled or exceeds working budget";
				return false;
			}
			BinaryReader r(input);
			MeshData data;
			MeshAsset asset;
			uint32_t type, count;
			size_t allowance = context.maxResidentBytes / 2;
			const auto charge = [&](size_t bytes) {
				if (bytes > allowance) {
					error = "Mesh allocation exceeds working budget";
					return false;
				}
				allowance -= bytes;
				return true;
			};
			if (!charge(sizeof(MeshAsset) + sizeof(MeshResource)))
				return false;
			if (!r.U32(data.vertexCount) || !r.U32(data.vertexStride) || !r.U32(type) || type > 1 || !r.U32(count) ||
			    count > 64 || count > r.Remaining() / 16) {
				error = "Invalid mesh header";
				return false;
			}
			if (!charge(size_t(count) * sizeof(VertexAttribute)))
				return false;
			data.indexType = static_cast<MeshIndexType>(type);
			data.attributes.resize(count);
			for (auto& a : data.attributes) {
				uint32_t format, semantic;
				if (!r.U32(a.location) || !r.U32(format) || !r.U32(a.offset) || !r.U32(semantic) || format > 2 ||
				    semantic > static_cast<uint32_t>(VertexSemantic::Tangent)) {
					error = "Invalid mesh attribute";
					return false;
				}
				a.format = static_cast<VertexFormat>(format);
				a.semantic = static_cast<VertexSemantic>(semantic);
			}
			uint64_t bytes;
			if (!r.U64(bytes) || bytes % 4 || bytes > r.Remaining() || bytes > context.maxResidentBytes / 2) {
				error = "Invalid vertex byte range";
				return false;
			}
			if (!charge(static_cast<size_t>(bytes)))
				return false;
			data.vertexData.resize(static_cast<size_t>(bytes));
			for (size_t offset = 0; offset < data.vertexData.size(); offset += 4) {
				uint32_t bits;
				if (!r.U32(bits)) {
					error = r.Error();
					return false;
				}
				std::memcpy(data.vertexData.data() + offset, &bits, 4);
			}
			uint64_t indices;
			const size_t stride = GetMeshIndexByteSize(data.indexType);
			if (!r.U64(indices) || indices > r.Remaining() / stride ||
			    indices > context.maxResidentBytes / 2 / stride) {
				error = "Invalid mesh index range";
				return false;
			}
			if (!charge(static_cast<size_t>(indices) * stride))
				return false;
			data.indexData.resize(static_cast<size_t>(indices) * stride);
			for (size_t i = 0; i < indices; ++i) {
				if (stride == 2) {
					uint8_t low, high;
					if (!r.U8(low) || !r.U8(high)) {
						error = r.Error();
						return false;
					}
					const uint16_t value = uint16_t(low) | (uint16_t(high) << 8);
					std::memcpy(data.indexData.data() + i * stride, &value, stride);
				} else {
					uint32_t value;
					if (!r.U32(value)) {
						error = r.Error();
						return false;
					}
					std::memcpy(data.indexData.data() + i * stride, &value, stride);
				}
			}
			if (!r.U32(count) || count > 65536 || count > r.Remaining() / 28) {
				error = "Invalid submesh count";
				return false;
			}
			if (!charge(size_t(count) * sizeof(Submesh)))
				return false;
			data.submeshes.resize(count);
			for (auto& s : data.submeshes) {
				uint32_t base, topology;
				if (!r.U32(s.firstIndex) || !r.U32(s.indexCount) || !r.U32(base) || !r.U32(s.firstVertex) ||
				    !r.U32(s.vertexCount) || !r.U32(s.materialSlot) || !r.U32(topology) || topology != 0) {
					error = "Invalid submesh";
					return false;
				}
				std::memcpy(&s.baseVertex, &base, 4);
				s.topology = MeshTopology::TriangleList;
			}
			uint8_t empty;
			if (!r.U8(empty) || empty > 1 || !Vec(r, data.localBounds.minimum) || !Vec(r, data.localBounds.maximum) ||
			    !r.U32(count) || count == 0 || count > 65536 || count > r.Remaining() / 5) {
				error = "Invalid mesh bounds or material slots";
				return false;
			}
			if (!charge(size_t(count) * (sizeof(std::string) + 129)))
				return false;
			data.localBounds.empty = empty != 0;
			asset.materialSlots.resize(count);
			for (auto& slot : asset.materialSlots)
				if (!r.String(slot, 128)) {
					error = r.Error();
					return false;
				}
			if (!r.AtEnd() || context.cancellation.IsCancellationRequested()) {
				error = "Mesh payload has trailing bytes or was cancelled";
				return false;
			}
			const size_t estimated = sizeof(MeshAsset) + sizeof(MeshResource) + data.vertexData.capacity() +
			    data.indexData.capacity() + data.attributes.capacity() * sizeof(VertexAttribute) +
			    data.submeshes.capacity() * sizeof(Submesh) + asset.materialSlots.capacity() * sizeof(std::string) +
			    count * 128;
			if (estimated > context.maxResidentBytes / 2) {
				error = "Mesh immutable copy exceeds working budget";
				return false;
			}
			const auto status = CreateMeshResource(data, asset.mesh);
			if (!status.IsSuccess()) {
				error = status.message;
				return false;
			}
			if (!ValidateMeshAsset(asset, error))
				return false;
			const auto resident = MeshBytes(asset);
			output = MakeAssetLoadResult<MeshAsset>(std::make_shared<const MeshAsset>(std::move(asset)), resident);
			error.clear();
			return true;
		} catch (const std::exception& exception) {
			error = exception.what();
			return false;
		}
	}

	bool GeometryAssetLoader::EncodeModel(const ModelAsset& model, std::vector<std::byte>& output, std::string& error) {
		try {
			if (!ValidateModelAsset(model, error))
				return false;
			BinaryWriter w;
			w.U32(static_cast<uint32_t>(model.nodes.size()));
			for (const auto& node : model.nodes) {
				w.String(node.id.ToString());
				w.String(node.name);
				w.U32(static_cast<uint32_t>(node.parent));
				Vec(w, node.local.translation);
				w.F32(node.local.rotation.w);
				w.F32(node.local.rotation.x);
				w.F32(node.local.rotation.y);
				w.F32(node.local.rotation.z);
				Vec(w, node.local.scale);
			}
			w.U32(static_cast<uint32_t>(model.meshes.size()));
			for (const auto& mesh : model.meshes) {
				w.U32(mesh.node);
				w.String(mesh.mesh.id.ToString());
				w.U32(static_cast<uint32_t>(mesh.materials.size()));
				for (auto material : mesh.materials)
					w.String(material.id.ToString());
			}
			/* 可选尾部扩展保持既有无动画模型兼容，读端仍拒绝未知标记与尾随字节。 */
			w.U32(0x314C444D);
			w.U32(1);
			w.String(model.skeleton.HasValue() ? model.skeleton.id.ToString() : std::string{});
			w.U32(uint32_t(model.animations.size()));
			for (auto clip : model.animations)
				w.String(clip.id.ToString());
			for (const auto& mesh : model.meshes) {
				w.String(mesh.skin.HasValue() ? mesh.skin.id.ToString() : std::string{});
				w.U32(uint32_t(mesh.lods.size()));
				for (size_t lod = 0; lod < mesh.lods.size(); ++lod) {
					w.String(mesh.lods[lod].id.ToString());
					w.F32(mesh.lodErrors[lod]);
				}
			}
			return Finish(w, output, error);
		} catch (const std::exception& exception) {
			error = exception.what();
			return false;
		}
	}

	bool GeometryAssetLoader::DecodeModel(std::span<const std::byte> input, const AssetDecodeContext& context,
	    AssetLoadResult& output, std::string& error) {
		try {
			if (context.cancellation.IsCancellationRequested() || input.size() > context.maxResidentBytes / 4) {
				error = "Model decode cancelled or exceeds working budget";
				return false;
			}
			BinaryReader r(input);
			ModelAsset model;
			uint32_t count;
			size_t allowance = context.maxResidentBytes;
			const auto charge = [&](size_t bytes) {
				if (bytes > allowance) {
					error = "Model allocation exceeds working budget";
					return false;
				}
				allowance -= bytes;
				return true;
			};
			/* 基本记录包含节点层级和网格材质槽，在按记录数分配前扣减解码预算。 */
			if (!charge(sizeof(ModelAsset)))
				return false;
			if (!r.U32(count) || count == 0 || count > 65536 || count > r.Remaining() / 84 ||
			    size_t(count) * sizeof(ModelNode) > context.maxResidentBytes) {
				error = "Invalid model node count";
				return false;
			}
			if (!charge(size_t(count) * sizeof(ModelNode)))
				return false;
			model.nodes.resize(count);
			for (auto& node : model.nodes) {
				std::string id;
				uint32_t parent;
				if (!r.String(id, 36) || !StableId::TryParse(id, node.id) || !r.String(node.name, 1024) ||
				    !r.U32(parent) || !Vec(r, node.local.translation) || !r.F32(node.local.rotation.w) ||
				    !r.F32(node.local.rotation.x) || !r.F32(node.local.rotation.y) || !r.F32(node.local.rotation.z) ||
				    !Vec(r, node.local.scale)) {
					error = "Invalid model node";
					return false;
				}
				std::memcpy(&node.parent, &parent, 4);
				if (!charge(node.name.capacity() + 1))
					return false;
			}
			if (!r.U32(count) || count > 65536 || count > r.Remaining() / 48) {
				error = "Invalid model mesh count";
				return false;
			}
			if (!charge(size_t(count) * sizeof(ModelMeshInstance)))
				return false;
			model.meshes.resize(count);
			for (auto& mesh : model.meshes) {
				std::string id;
				uint32_t materials;
				if (!r.U32(mesh.node) || !r.String(id, 36) || !AssetId::TryParse(id, mesh.mesh.id) ||
				    !r.U32(materials) || materials > 65536 || materials > r.Remaining() / 40) {
					error = "Invalid model mesh instance";
					return false;
				}
				if (!charge(size_t(materials) * sizeof(AssetHandle<MaterialInstance>)))
					return false;
				mesh.materials.resize(materials);
				for (auto& material : mesh.materials)
					if (!r.String(id, 36) || !AssetId::TryParse(id, material.id)) {
						error = "Invalid model material ID";
						return false;
					}
			}
			/* 旧格式在基本记录后结束，新格式继续读取骨架、片段、蒙皮和细节层级。 */
			if (!r.AtEnd()) {
				uint32_t marker, version;
				std::string id;
				if (!r.U32(marker) || marker != 0x314C444D || !r.U32(version) || version != 1 || !r.String(id, 36)) {
					error = "Invalid model extension";
					return false;
				}
				if (!id.empty() && !AssetId::TryParse(id, model.skeleton.id)) {
					error = "Invalid model skeleton";
					return false;
				}
				if (!r.U32(count) || count > 1024 ||
				    !charge(size_t(count) *
				        (sizeof(AssetHandle<AnimationClip>) + sizeof(AssetVersionRef<AnimationClip>)))) {
					error = "Model clip budget";
					return false;
				}
				model.animations.resize(count);
				for (auto& clip : model.animations)
					if (!r.String(id, 36) || !AssetId::TryParse(id, clip.id)) {
						error = "Invalid model clip";
						return false;
					}
				for (auto& mesh : model.meshes) {
					if (!r.String(id, 36) || (!id.empty() && !AssetId::TryParse(id, mesh.skin.id)) || !r.U32(count) ||
					    count > 8) {
						error = "Invalid model skin/LOD";
						return false;
					}
					if (!charge(size_t(count) *
					        (sizeof(AssetHandle<MeshAsset>) + sizeof(float) + sizeof(AssetVersionRef<MeshAsset>))))
						return false;
					mesh.lods.resize(count);
					mesh.lodErrors.resize(count);
					for (size_t lod = 0; lod < count; ++lod)
						if (!r.String(id, 36) || !AssetId::TryParse(id, mesh.lods[lod].id) ||
						    !r.F32(mesh.lodErrors[lod])) {
							error = "Invalid model LOD";
							return false;
						}
				}
			}
			/* 按标识、类型和版本解析依赖，后续校验使用同一批固定资源。 */
			const auto fixed = [&]<class T>(AssetId id, AssetVersionRef<T>& output) {
				const auto value = context.FindDependency<T>(id, [&] {
					for (const auto& d : context.dependencies)
						if (d.id == id)
							return d.version;
					return uint64_t(0);
				}());
				if (!value) {
					error = "Model fixed dependency missing/type mismatch: " + id.ToString();
					return false;
				}
				for (const auto& d : context.dependencies)
					if (d.id == id)
						return AssetVersionRef<T>::TryCreate(id, d.version, value, output, error);
				return false;
			};
			if (model.skeleton.HasValue() && !fixed(model.skeleton.id, model.skeletonVersion))
				return false;
			for (auto clip : model.animations) {
				AssetVersionRef<AnimationClip> version;
				if (!fixed(clip.id, version))
					return false;
				model.animationVersions.push_back(std::move(version));
			}
			std::set<AssetId> seenSkins, seenLods;
			for (const auto& mesh : model.meshes) {
				if (mesh.skin.HasValue() && seenSkins.insert(mesh.skin.id).second) {
					AssetVersionRef<MeshSkinBinding> version;
					if (!fixed(mesh.skin.id, version))
						return false;
					model.skinVersions.push_back(std::move(version));
				}
				for (auto lod : mesh.lods)
					if (seenLods.insert(lod.id).second) {
						AssetVersionRef<MeshAsset> version;
						if (!fixed(lod.id, version))
							return false;
						model.lodVersions.push_back(std::move(version));
					}
			}
			if (!r.AtEnd() || !ValidateModelAsset(model, error)) {
				if (error.empty())
					error = "Trailing model payload";
				return false;
			}
			std::vector<AssetVersionRef<MeshAsset>> meshes;
			for (const auto& dependency : context.dependencies)
				if (dependency.cppType == std::type_index(typeid(MeshAsset))) {
					AssetVersionRef<MeshAsset> value;
					if (!AssetVersionRef<MeshAsset>::TryCreate(dependency.id, dependency.version,
					        std::static_pointer_cast<const MeshAsset>(dependency.value), value, error))
						return false;
					meshes.push_back(std::move(value));
				}
			if (!ValidateModelMeshBindings(model, meshes, error))
				return false;
			/* 检查动画与蒙皮引用同一骨架，并逐实例核对基础网格、细节层级和材质。 */
			const auto sameSkeleton = [&](const AssetVersionRef<SkeletonAsset>& value) {
				return value.HasValue() && model.skeletonVersion.HasValue() &&
				    value.GetId() == model.skeletonVersion.GetId() &&
				    value.GetVersion() == model.skeletonVersion.GetVersion() &&
				    value.Get() == model.skeletonVersion.Get();
			};
			for (const auto& clip : model.animationVersions)
				if (!sameSkeleton(clip.Get()->skeletonVersion)) {
					error = "Model clip references a different fixed skeleton";
					return false;
				}
			for (const auto& instance : model.meshes) {
				AssetVersionRef<MeshAsset> base;
				if (!fixed(instance.mesh.id, base))
					return false;
				if (instance.skin.HasValue()) {
					const auto skin = std::find_if(model.skinVersions.begin(), model.skinVersions.end(),
					    [&](const auto& value) { return value.GetId() == instance.skin.id; });
					if (skin == model.skinVersions.end() || !sameSkeleton(skin->Get()->skeletonVersion) ||
					    skin->Get()->meshVersion.GetId() != base.GetId() ||
					    skin->Get()->meshVersion.GetVersion() != base.GetVersion() ||
					    skin->Get()->meshVersion.Get() != base.Get()) {
						error = "Model skin references another mesh or skeleton version";
						return false;
					}
				}
				std::vector<AssetVersionRef<MeshAsset>> levels;
				for (const auto id : instance.lods) {
					AssetVersionRef<MeshAsset> level;
					if (!fixed(id.id, level))
						return false;
					levels.push_back(std::move(level));
				}
				if (!ValidateMeshLodChain(base, levels, instance.lodErrors, error))
					return false;
			}
			for (const auto& mesh : model.meshes)
				for (auto material : mesh.materials) {
					const auto found =
					    std::find_if(context.dependencies.begin(), context.dependencies.end(), [&](const auto& d) {
						    return d.id == material.id && d.cppType == std::type_index(typeid(MaterialInstance));
					    });
					if (found == context.dependencies.end()) {
						error = "Model material dependency is missing or has the wrong type";
						return false;
					}
				}
			/* 按最终容器容量统计常驻字节数，再发布不可变模型。 */
			size_t resident = sizeof(ModelAsset) + model.nodes.capacity() * sizeof(ModelNode) +
			    model.meshes.capacity() * sizeof(ModelMeshInstance);
			for (const auto& node : model.nodes)
				resident += node.name.capacity() + 1;
			for (const auto& mesh : model.meshes)
				resident += mesh.materials.capacity() * sizeof(AssetHandle<MaterialInstance>) +
				    mesh.lods.capacity() * sizeof(AssetHandle<MeshAsset>) + mesh.lodErrors.capacity() * sizeof(float);
			resident += model.animations.capacity() * sizeof(AssetHandle<AnimationClip>) +
			    model.animationVersions.capacity() * sizeof(AssetVersionRef<AnimationClip>) +
			    model.skinVersions.capacity() * sizeof(AssetVersionRef<MeshSkinBinding>) +
			    model.lodVersions.capacity() * sizeof(AssetVersionRef<MeshAsset>);
			if (resident > context.maxResidentBytes || context.cancellation.IsCancellationRequested()) {
				error = "Model exceeds budget or was cancelled";
				return false;
			}
			output = MakeAssetLoadResult<ModelAsset>(std::make_shared<const ModelAsset>(std::move(model)), resident);
			error.clear();
			return true;
		} catch (const std::exception& exception) {
			error = exception.what();
			return false;
		}
	}

	bool RegisterGeometryAssetLoaders(AssetManager& manager, std::string& error) {
		AssetLoader mesh, model;
		if (!AssetType::TryParse("mesh", mesh.type) || !AssetType::TryParse("model", model.type)) {
			error = "Cannot create geometry type keys";
			return false;
		}
		mesh.cppType = std::type_index(typeid(MeshAsset));
		model.cppType = std::type_index(typeid(ModelAsset));
		mesh.decode = [](const AssetContainer& c, const AssetDecodeContext& x, AssetLoadResult& out,
		                  std::string& message) {
			if (c.payloadVersion != 1) {
				message = "Unsupported mesh payload version";
				return false;
			}
			return GeometryAssetLoader::DecodeMesh(c.payload, x, out, message);
		};
		model.decode = [](const AssetContainer& c, const AssetDecodeContext& x, AssetLoadResult& out,
		                   std::string& message) {
			if (c.payloadVersion != 1) {
				message = "Unsupported model payload version";
				return false;
			}
			return GeometryAssetLoader::DecodeModel(c.payload, x, out, message);
		};
		return manager.RegisterLoader(std::move(mesh), error) && manager.RegisterLoader(std::move(model), error);
	}
}
