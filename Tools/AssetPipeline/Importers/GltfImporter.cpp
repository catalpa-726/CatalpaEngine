#include "catpch.h"

#include "AssetPipeline/Importers/GltfImporter.h"

#include "AssetPipeline/Importers/ImportGeometry.h"
#include "AssetPipeline/Importers/TextureImporter.h"
#include "AssetPipeline/Importers/MaterialImporter.h"
#include "AssetPipeline/Animation/AnimationImport.h"
#include "AssetPipeline/Cookers/ModelLodBuilder.h"

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>

namespace Catalpa::Tools {
	namespace {
		using Asset = fastgltf::Asset;

		std::span<const std::byte> Buffer(const fastgltf::DataSource& source) {
			if (const auto* p = std::get_if<fastgltf::sources::Vector>(&source))
				return p->bytes;
			if (const auto* p = std::get_if<fastgltf::sources::Array>(&source))
				return {p->bytes.data(), p->bytes.size_bytes()};
			if (const auto* p = std::get_if<fastgltf::sources::ByteView>(&source))
				return p->bytes;
			throw std::runtime_error("Unsupported glTF buffer representation");
		}

		/**
		 * @brief 解析图像索引，根据数据来源读取内嵌或外部图像字节。
		 *
		 * @param[in] ctx 导入任务共享的项目、依赖与构建预算。
		 * @param[in] asset 本次操作使用的资产。
		 * @param[in] index 目标元素的索引。
		 * @param[in] bytes 字节数据。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool ImageBytes(
		    AssetBuildContext& ctx, Asset& asset, size_t index, std::vector<std::byte>& bytes, std::string& error) {
			if (index >= asset.images.size()) {
				error = "Image index out of range";
				return false;
			}
			const auto& source = asset.images[index].data;
			if (const auto* p = std::get_if<fastgltf::sources::URI>(&source)) {
				if (p->fileByteOffset) {
					error = "Image URI has unsupported file offset";
					return false;
				}
				std::filesystem::path file;
				if (!ctx.Resolve(ctx.request.source, p->uri->string(), file, error))
					return false;
				return ctx.Read(file, bytes, error);
			}
			if (const auto* p = std::get_if<fastgltf::sources::BufferView>(&source)) {
				if (p->bufferViewIndex >= asset.bufferViews.size()) {
					error = "Image view index out of range";
					return false;
				}
				const auto& view = asset.bufferViews[p->bufferViewIndex];
				const auto data = Buffer(asset.buffers.at(view.bufferIndex).data);
				if (view.byteOffset > data.size() || view.byteLength > data.size() - view.byteOffset) {
					error = "Image buffer view outside actual buffer";
					return false;
				}
				bytes.assign(data.begin() + view.byteOffset, data.begin() + view.byteOffset + view.byteLength);
				return true;
			}
			const auto data = Buffer(source);
			bytes.assign(data.begin(), data.end());
			return true;
		}

		/**
		 * @brief 检查访问器。
		 *
		 * @param[in] a 第一个资产操作数。
		 * @param[in] limits 导入设置中的 maxIndices，限定单个 glTF 访问器允许的元素数。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool CheckAccessors(const Asset& a, const AssetImportSettings& limits, std::string& error) {
			for (const auto& view : a.bufferViews) {
				if (view.bufferIndex >= a.buffers.size()) {
					error = "Buffer view index out of range";
					return false;
				}
				const auto& source = a.buffers[view.bufferIndex];
				const auto data = Buffer(source.data);
				if (view.byteOffset > data.size() || view.byteLength > data.size() - view.byteOffset ||
				    view.byteOffset > source.byteLength || view.byteLength > source.byteLength - view.byteOffset) {
					error = "Buffer view exceeds declared/actual buffer bytes";
					return false;
				}
				if (view.meshoptCompression) {
					error = "Compressed buffer views require an approved meshopt importer";
					return false;
				}
			}
			for (const auto& accessor : a.accessors) {
				if (accessor.count > limits.maxIndices) {
					error = "Accessor count exceeds import bound";
					return false;
				}
				const auto component = fastgltf::getComponentByteSize(accessor.componentType),
				           elementBytes = fastgltf::getElementByteSize(accessor.type, accessor.componentType);
				if (!component || !elementBytes) {
					error = "Invalid accessor element type";
					return false;
				}
				if (accessor.bufferViewIndex) {
					const auto& v = a.bufferViews.at(*accessor.bufferViewIndex);
					if ((v.byteOffset + accessor.byteOffset) % component ||
					    (v.byteStride && *v.byteStride < elementBytes)) {
						error = "Accessor alignment/stride is invalid";
						return false;
					}
					const size_t stride = v.byteStride ? *v.byteStride : elementBytes;
					if (accessor.byteOffset > v.byteLength ||
					    (accessor.count &&
					        (elementBytes > v.byteLength - accessor.byteOffset ||
					            (accessor.count - 1) > (v.byteLength - accessor.byteOffset - elementBytes) / stride))) {
						error = "Accessor element range exceeds its buffer view";
						return false;
					}
				}
				if (accessor.sparse) {
					const auto& sparse = *accessor.sparse;
					if (sparse.indicesBufferView >= a.bufferViews.size() || sparse.count == 0 ||
					    sparse.count > accessor.count) {
						error = "Sparse accessor bounds invalid";
						return false;
					}
					if (sparse.valuesBufferView >= a.bufferViews.size()) {
						error = "Sparse value view is invalid";
						return false;
					}
					const auto& values = a.bufferViews[sparse.valuesBufferView];
					const auto element = fastgltf::getElementByteSize(accessor.type, accessor.componentType);
					if (!element || (values.byteOffset + sparse.valuesByteOffset) % component ||
					    sparse.valuesByteOffset > values.byteLength ||
					    sparse.count > (values.byteLength - sparse.valuesByteOffset) / element) {
						error = "Sparse value buffer truncated or misaligned";
						return false;
					}
					const auto& v = a.bufferViews[sparse.indicesBufferView];
					const auto bytes = Buffer(a.buffers.at(v.bufferIndex).data);
					size_t stride = 0;
					switch (sparse.indexComponentType) {
					case fastgltf::ComponentType::UnsignedByte:
						stride = 1;
						break;
					case fastgltf::ComponentType::UnsignedShort:
						stride = 2;
						break;
					case fastgltf::ComponentType::UnsignedInt:
						stride = 4;
						break;
					default:
						error = "Invalid sparse index type";
						return false;
					}
					if ((v.byteOffset + sparse.indicesByteOffset) % stride || sparse.indicesByteOffset > v.byteLength ||
					    sparse.count > (v.byteLength - sparse.indicesByteOffset) / stride) {
						error = "Sparse index buffer truncated or misaligned";
						return false;
					}
					uint32_t previous = 0;
					for (size_t i = 0; i < sparse.count; ++i) {
						uint32_t index = 0;
						for (size_t k = 0; k < stride; ++k)
							index |= std::to_integer<uint32_t>(
							             bytes[v.byteOffset + sparse.indicesByteOffset + i * stride + k])
							    << (k * 8);
						if (index >= accessor.count || (i && index <= previous)) {
							error = "Sparse indices must be increasing and in range";
							return false;
						}
						previous = index;
					}
				}
			}
			return true;
		}
	}

	bool GltfImporter::Import(AssetBuildContext& ctx, AssetImportResult& output, std::string& error) const try {
		std::vector<std::byte> source;
		if (!ctx.Read(ctx.request.source, source, error))
			return false;
		auto data = fastgltf::GltfDataBuffer::FromBytes(source.data(), source.size());
		if (!data) {
			error = std::string(fastgltf::getErrorMessage(data.error()));
			return false;
		}
		fastgltf::Parser parser(fastgltf::Extensions::KHR_materials_unlit);
		auto parsed = parser.loadGltf(data.get(), ctx.request.source.parent_path(), fastgltf::Options::LoadGLBBuffers);
		if (!parsed) {
			error = std::string("glTF parse: ") + std::string(fastgltf::getErrorMessage(parsed.error()));
			return false;
		}
		auto asset = std::move(parsed.get());
		if (asset.nodes.size() > ctx.request.settings.maxNodes || asset.meshes.size() > 65536 ||
		    asset.materials.size() > 4096 || asset.images.size() > 4096 || asset.buffers.size() > 4096) {
			error = "glTF collection limit exceeded";
			return false;
		}
		for (const auto& extension : asset.extensionsUsed)
			if (extension != "KHR_materials_unlit") {
				error = "Unsupported glTF extension (no silent approximation): " + extension;
				return false;
			}
		if (asset.skins.size() > 256 || asset.animations.size() > 256) {
			error = "Skin/animation collection limit exceeded";
			return false;
		}
		for (auto& buffer : asset.buffers) {
			if (const auto* uri = std::get_if<fastgltf::sources::URI>(&buffer.data)) {
				std::filesystem::path path;
				std::vector<std::byte> bytes;
				if (uri->fileByteOffset) {
					error = "External buffer URI has an unsupported file offset";
					return false;
				}
				if (!ctx.Resolve(ctx.request.source, uri->uri->string(), path, error) || !ctx.Read(path, bytes, error))
					return false;
				buffer.data = fastgltf::sources::Vector{std::move(bytes)};
			}
			if (buffer.byteLength > Buffer(buffer.data).size()) {
				error = "glTF buffer shorter than declared byteLength";
				return false;
			}
		}
		if (const auto valid = fastgltf::validate(asset); valid != fastgltf::Error::None) {
			error = "glTF validation: " + std::string(fastgltf::getErrorMessage(valid));
			return false;
		}
		if (!CheckAccessors(asset, ctx.request.settings, error))
			return false;

		AssetImportResult result;
		result.primary = ctx.Identity("model");
		std::map<std::pair<size_t, std::string>, AssetId> textures;
		const auto texture = [&](size_t textureIndex, std::string semantic, ImportedMaterial& material) -> bool {
			if (textureIndex >= asset.textures.size() || !asset.textures[textureIndex].imageIndex) {
				error = "Texture has no supported image source";
				return false;
			}
			const auto imageIndex = *asset.textures[textureIndex].imageIndex;
			const auto [kind, imageKind] = TextureImporter::MaterialChannel(semantic);
			const auto cacheKey = std::make_pair(imageIndex, imageKind);
			auto it = textures.find(cacheKey);
			if (it == textures.end()) {
				std::vector<std::byte> bytes;
				ImageAsset image;
				if (!ImageBytes(ctx, asset, imageIndex, bytes, error) ||
				    !TextureImporter::Decode(ctx, bytes,
				        AssetPathText(ctx.request.source) + "#image/" + std::to_string(imageIndex), kind, image, error))
					return false;
				const auto key = "image/" + std::to_string(imageIndex) + "/" + imageKind;
				const auto id = ctx.Identity(key);
				result.assets.push_back({id, key, asset.images[imageIndex].name, std::move(image)});
				result.mappings.push_back({id, key, asset.images[imageIndex].name, "image"});
				it = textures.emplace(cacheKey, id).first;
			}
			material.textures[semantic] = it->second;
			const auto& t = asset.textures[textureIndex];
			if (t.samplerIndex) {
				const auto& sampler = asset.samplers.at(*t.samplerIndex);
				material.sourceProperties[semantic + ".sampler"] = "wrapS=" + std::to_string(uint32_t(sampler.wrapS)) +
				    ",wrapT=" + std::to_string(uint32_t(sampler.wrapT)) +
				    ",mag=" + (sampler.magFilter ? std::to_string(uint32_t(*sampler.magFilter)) : "default") +
				    ",min=" + (sampler.minFilter ? std::to_string(uint32_t(*sampler.minFilter)) : "default");
				uint32_t flags = 0;
				if (!MaterialImporter::SamplerFlags(uint32_t(sampler.wrapS), uint32_t(sampler.wrapT),
				        sampler.minFilter ? uint32_t(*sampler.minFilter) : 9987u,
				        sampler.magFilter ? uint32_t(*sampler.magFilter) : 9729u, flags, error))
					return false;
				material.samplers[semantic] = flags;
			}
			return true;
		};

		/* 模型末尾的默认材质供缺少材质引用的图元使用。 */
		for (size_t i = 0; i <= asset.materials.size(); ++i) {
			ImportedMaterial m;
			m.metallic = 1;
			std::string name = "Default material";
			if (i < asset.materials.size()) {
				const auto& in = asset.materials[i];
				name = in.name;
				for (size_t k = 0; k < 4; ++k)
					m.baseColor[k] = float(in.pbrData.baseColorFactor[k]);
				for (size_t k = 0; k < 3; ++k)
					m.emissive[k] = float(in.emissiveFactor[k]);
				m.metallic = float(in.pbrData.metallicFactor);
				m.roughness = float(in.pbrData.roughnessFactor);
				m.doubleSided = in.doubleSided;
				m.alphaCutoff = float(in.alphaCutoff);
				m.alphaMode = in.alphaMode == fastgltf::AlphaMode::Mask ? "mask"
				    : in.alphaMode == fastgltf::AlphaMode::Blend        ? "blend"
				                                                        : "opaque";
				m.unlit = in.unlit;
				m.sourceProperties["unlit"] = in.unlit ? "true" : "false";
				const auto add = [&](const auto& info, const char* semantic) {
					if (!info)
						return true;
					if (info->texCoordIndex != 0 || info->transform) {
						error = "Only untransformed TEXCOORD_0 is currently supported";
						return false;
					}
					return texture(info->textureIndex, semantic, m);
				};
				if (!add(in.pbrData.baseColorTexture, "baseColor") ||
				    !add(in.pbrData.metallicRoughnessTexture, "metallicRoughness") ||
				    !add(in.normalTexture, "normal") || !add(in.occlusionTexture, "occlusion") ||
				    !add(in.emissiveTexture, "emissive"))
					return false;
				if (in.normalTexture)
					m.normalScale = float(in.normalTexture->scale);
				if (in.occlusionTexture)
					m.occlusionStrength = float(in.occlusionTexture->strength);
			}
			const auto key = "material/" + std::to_string(i);
			const auto id = ctx.Identity(key);
			result.assets.push_back({id, key, name, std::move(m)});
			result.mappings.push_back({id, key, name, "material"});
		}

		/* 一个 glTF mesh 可以有多个独立绘制的图元，各自生成网格并保留材质引用。 */
		std::map<std::pair<size_t, size_t>, AssetId> meshes;
		PrimitiveRemaps skinRemaps;
		for (size_t mi = 0; mi < asset.meshes.size(); ++mi)
			for (size_t pi = 0; pi < asset.meshes[mi].primitives.size(); ++pi) {
				if (!ctx.CheckCancelled(error))
					return false;
				const auto& p = asset.meshes[mi].primitives[pi];
				if (p.type != fastgltf::PrimitiveType::Triangles || !p.targets.empty() || p.dracoCompression) {
					error = "Only non-morph uncompressed triangle primitives are supported";
					return false;
				}
				const auto pos = p.findAttribute("POSITION");
				if (pos == p.attributes.end()) {
					error = "Primitive missing POSITION";
					return false;
				}
				const auto& pa = asset.accessors.at(pos->accessorIndex);
				if (pa.type != fastgltf::AccessorType::Vec3 || pa.componentType != fastgltf::ComponentType::Float ||
				    pa.normalized || pa.count == 0 || pa.count > ctx.request.settings.maxVertices) {
					error = "Invalid POSITION float3 shape/count";
					return false;
				}
				const auto indexCount = p.indicesAccessor ? asset.accessors.at(*p.indicesAccessor).count : pa.count;
				if (indexCount > ctx.request.settings.maxIndices) {
					error = "Primitive exceeds the configured index limit";
					return false;
				}
				if (!ctx.WorkspaceFits(pa.count * sizeof(ImportVertex) + indexCount * sizeof(uint32_t), error))
					return false;
				std::vector<ImportVertex> vertices(pa.count);
				fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(asset, pa, [&](auto v, size_t k) {
					for (size_t c = 0; c < 3; ++c)
						vertices[k].position[c] = v[c];
				});
				bool n = false, uv = false, t = false;
				for (const auto& attribute : p.attributes) {
					const auto& acc = asset.accessors.at(attribute.accessorIndex);
					if (acc.count != pa.count) {
						error = "Attribute count differs from POSITION";
						return false;
					}
					/*
					 * 核心 glTF 的 UV 和顶点色使用浮点或归一化无符号整数。
					 * 未归一化的 U8/U16 会被读取成 0..255/65535，而非预期的 0..1。
					 */
					if (attribute.name == "TEXCOORD_0" || attribute.name == "COLOR_0") {
						const bool floating = acc.componentType == fastgltf::ComponentType::Float && !acc.normalized;
						const bool normalizedInteger = acc.normalized &&
						    (acc.componentType == fastgltf::ComponentType::UnsignedByte ||
						        acc.componentType == fastgltf::ComponentType::UnsignedShort);
						if (!floating && !normalizedInteger) {
							error = attribute.name + " requires FLOAT or normalized UNSIGNED_BYTE/UNSIGNED_SHORT";
							return false;
						}
					}
					if (attribute.name == "NORMAL") {
						if (acc.type != fastgltf::AccessorType::Vec3) {
							error = "NORMAL must be VEC3";
							return false;
						}
						n = true;
						fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(asset, acc, [&](auto v, size_t k) {
							for (size_t c = 0; c < 3; ++c)
								vertices[k].normal[c] = v[c];
						});
					} else if (attribute.name == "TEXCOORD_0") {
						if (acc.type != fastgltf::AccessorType::Vec2) {
							error = "UV must be VEC2";
							return false;
						}
						uv = true;
						fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(
						    asset, acc, [&](auto v, size_t k) { vertices[k].uv = {v[0], v[1]}; });
					} else if (attribute.name == "TANGENT") {
						if (acc.type != fastgltf::AccessorType::Vec4) {
							error = "Tangent must be VEC4";
							return false;
						}
						t = true;
						fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(asset, acc, [&](auto v, size_t k) {
							for (size_t c = 0; c < 4; ++c)
								vertices[k].tangent[c] = v[c];
						});
					} else if (attribute.name == "COLOR_0") {
						if (acc.type == fastgltf::AccessorType::Vec4)
							fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(
							    asset, acc, [&](auto v, size_t k) {
								    for (size_t c = 0; c < 4; ++c)
									    vertices[k].color[c] = v[c];
							    });
						else if (acc.type == fastgltf::AccessorType::Vec3)
							fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
							    asset, acc, [&](auto v, size_t k) {
								    for (size_t c = 0; c < 3; ++c)
									    vertices[k].color[c] = v[c];
							    });
						else {
							error = "Color must be VEC3/VEC4";
							return false;
						}
					} else if (attribute.name != "POSITION" && attribute.name != "JOINTS_0" &&
					    attribute.name != "WEIGHTS_0") {
						error = "Unsupported vertex semantic: " + attribute.name;
						return false;
					}
				}
				std::vector<uint32_t> indices;
				if (p.indicesAccessor) {
					const auto& acc = asset.accessors.at(*p.indicesAccessor);
					if (acc.type != fastgltf::AccessorType::Scalar || acc.normalized ||
					    acc.count > ctx.request.settings.maxIndices ||
					    (acc.componentType != fastgltf::ComponentType::UnsignedByte &&
					        acc.componentType != fastgltf::ComponentType::UnsignedShort &&
					        acc.componentType != fastgltf::ComponentType::UnsignedInt)) {
						error = "Invalid unsigned index accessor";
						return false;
					}
					indices.resize(acc.count);
					fastgltf::copyFromAccessor<uint32_t>(asset, acc, indices.data());
				} else {
					indices.resize(pa.count);
					for (size_t i = 0; i < indices.size(); ++i)
						indices[i] = uint32_t(i);
				}
				MeshAsset mesh;
				if (!BuildImportedMesh(
				        ctx, std::move(vertices), std::move(indices), n, uv, t, mesh, error, &skinRemaps[{mi, pi}]))
					return false;
				const auto key = "mesh/" + std::to_string(mi) + "/primitive/" + std::to_string(pi);
				const auto id = ctx.Identity(key);
				meshes[{mi, pi}] = id;
				result.assets.push_back({id, key, asset.meshes[mi].name, std::move(mesh)});
				result.mappings.push_back({id, key, asset.meshes[mi].name, "mesh"});
			}

		ModelAsset model;
		std::vector<int32_t> parents(asset.nodes.size(), -1);
		for (size_t i = 0; i < asset.nodes.size(); ++i)
			for (auto child : asset.nodes[i].children) {
				if (child >= parents.size() || parents[child] != -1) {
					error = "Node has invalid/multiple parents";
					return false;
				}
				parents[child] = int32_t(i);
			}
		/* 即使没有被默认scene引用，非法环也不能藏在未选节点里。 */
		std::vector<uint8_t> marks(asset.nodes.size());
		for (size_t i = 0; i < parents.size(); ++i) {
			int32_t node = int32_t(i);
			while (node >= 0 && marks[size_t(node)] == 0) {
				marks[size_t(node)] = 1;
				node = parents[size_t(node)];
			}
			if (node >= 0 && marks[size_t(node)] == 1) {
				error = "glTF hierarchy contains a cycle";
				return false;
			}
			node = int32_t(i);
			while (node >= 0 && marks[size_t(node)] == 1) {
				marks[size_t(node)] = 2;
				node = parents[size_t(node)];
			}
		}
		/* 只实例化默认scene；其他scene仍由原件和来源报告保留，不混入显示。 */
		std::set<size_t> selected;
		std::vector<size_t> queue;
		if (asset.defaultScene && *asset.defaultScene >= asset.scenes.size()) {
			error = "Default glTF scene index is out of range";
			return false;
		}
		if (!asset.scenes.empty()) {
			const auto scene = asset.defaultScene ? *asset.defaultScene : 0;
			queue = asset.scenes.at(scene).nodeIndices;
		} else
			for (size_t i = 0; i < parents.size(); ++i)
				if (parents[i] == -1)
					queue.push_back(i);
		for (auto root : queue)
			if (root >= parents.size() || parents[root] != -1) {
				error = "Scene root has an invalid index or a parent";
				return false;
			}
		for (size_t i = 0; i < queue.size(); ++i) {
			if (queue[i] >= asset.nodes.size() || !selected.insert(queue[i]).second) {
				error = "Selected scene contains duplicate/cyclic node";
				return false;
			}
			queue.insert(queue.end(), asset.nodes[queue[i]].children.begin(), asset.nodes[queue[i]].children.end());
		}
		std::map<size_t, uint32_t> mapped;
		for (auto i : queue) {
			const auto& in = asset.nodes[i];
			const auto primitiveCount = in.meshIndex ? asset.meshes.at(*in.meshIndex).primitives.size() : 0;
			if (model.nodes.size() + primitiveCount + 1 >= ctx.request.settings.maxNodes) {
				error = "glTF node/primitive expansion exceeds node budget";
				return false;
			}
			if (!in.weights.empty() || !in.instancingAttributes.empty()) {
				error = "Skin/morph/GPU instancing requires its dedicated importer";
				return false;
			}
			Math::TransformTRS local;
			if (const auto* trs = std::get_if<fastgltf::TRS>(&in.transform)) {
				for (size_t c = 0; c < 3; ++c) {
					local.translation[int(c)] = float(trs->translation[c]);
					local.scale[int(c)] = float(trs->scale[c]);
				}
				local.rotation = {
				    float(trs->rotation[3]), float(trs->rotation[0]), float(trs->rotation[1]), float(trs->rotation[2])};
			} else {
				Math::Mat4 matrix;
				const auto& src = std::get<fastgltf::math::nmat4x4>(in.transform);
				for (size_t c = 0; c < 4; ++c)
					for (size_t row = 0; row < 4; ++row)
						matrix[int(c)][int(row)] = float(src[c][row]);
				if (const auto s = Math::DecomposeTRS(matrix, local); !s.IsSuccess()) {
					error = s.message;
					return false;
				}
			}
			const auto key = "node/" + std::to_string(i);
			mapped[i] = uint32_t(model.nodes.size());
			const int32_t parent = parents[i] < 0 ? -1 : int32_t(mapped.at(size_t(parents[i])));
			model.nodes.push_back({ctx.Identity(key).value, in.name, parent, local});
			result.mappings.push_back({result.primary, key, in.name, "node", ctx.Identity(key).value});
			if (in.meshIndex) {
				const auto mi = *in.meshIndex;
				for (size_t pi = 0; pi < asset.meshes.at(mi).primitives.size(); ++pi) {
					const auto& p = asset.meshes[mi].primitives[pi];
					const auto childKey = key + "/primitive/" + std::to_string(pi);
					const auto node = uint32_t(model.nodes.size());
					model.nodes.push_back(
					    {ctx.Identity(childKey).value, asset.meshes[mi].name, int32_t(mapped[i]), {}});
					const size_t material = p.materialIndex ? *p.materialIndex : asset.materials.size();
					model.meshes.push_back({node, AssetHandle<MeshAsset>{meshes.at({mi, pi})},
					    {AssetHandle<MaterialInstance>{ctx.Identity("material/" + std::to_string(material))}}});
				}
			}
		}
		if (model.meshes.empty()) {
			error = "Selected glTF scene contains no supported mesh instances";
			return false;
		}
		if (!SetImportRoot(ctx, model, Math::Mat4{1}, error))
			return false;
		if (!AppendGltfAnimations(ctx, asset, model, result, skinRemaps, error))
			return false;
		if (!BuildModelLods(ctx, model, result, error))
			return false;
		ctx.Diagnose(
		    AssetDiagnosticLevel::Info, "coordinates", "glTF right-handed Y-up/metres; explicit root conversion only");
		ctx.Diagnose(AssetDiagnosticLevel::Info, "texture-coordinates",
		    "glTF upper-left texture coordinates; source UV/tangent/pixel orientation retained");
		if (!asset.cameras.empty())
			ctx.Diagnose(AssetDiagnosticLevel::Warning, "cameras",
			    "Source camera nodes retain transforms; camera lenses are outside static ModelAsset "
			    "and the editor owns preview framing");
		ctx.Diagnose(AssetDiagnosticLevel::Info, "material-preview",
		    "Ordinary materials retain PBR/Unlit and texture semantics; separate Unlit base-color previews remain available in "
		    "cooked assets and report");
		result.assets.push_back({result.primary, "model", AssetPathText(ctx.request.source.stem()), std::move(model)});
		result.mappings.push_back({result.primary, "model", AssetPathText(ctx.request.source.stem()), "model"});
		output = std::move(result);
		return true;
	} catch (const std::exception& exception) {

		error = "glTF import: " + std::string(exception.what());
		return false;
	}
}
