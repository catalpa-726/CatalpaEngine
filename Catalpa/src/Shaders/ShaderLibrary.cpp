#include "catpch.h"

#include "Shaders/ShaderLibrary.h"

#include "Assets/AssetManager.h"
#include "Assets/AssetContainerReader.h"
#include "Assets/AssetManifest.h"
#include "Core/IO/VirtualFileSystem.h"
#include "Core/PlatformServices.h"
#include "Core/Serialization/BinaryArchive.h"

#include <map>

namespace Catalpa {
	namespace {
		constexpr size_t MaxWords = 4 * 1024 * 1024, MaxItems = 4096, MaxDepth = 16;

		bool Fail(std::string& e, std::string_view text) {
			e.assign(text);
			return false;
		}

		bool Name(std::string_view s) {
			return !s.empty() && s.size() <= 1024 && s.find('\0') == s.npos;
		}

		bool Hash(std::string_view s) {
			return s.size() == 64 && std::all_of(s.begin(), s.end(), [](char c) {
				return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
			});
		}

		bool Shape(const ShaderTypeShape& s, bool allowStruct) {
			if (s.arrayCount == 0 || s.arrayCount > 1048576 || s.rows == 0 || s.columns == 0 || s.rows > 4 ||
			    s.columns > 4)
				return false;
			if (s.scalar == ShaderScalarKind::Structure)
				return allowStruct && s.width == 0 && s.rows == 1 && s.columns == 1;
			if (s.scalar < ShaderScalarKind::Float || s.scalar > ShaderScalarKind::Boolean)
				return false;
			/* 第 1 版格式记录 32 位标量存储，包含 Vulkan 统一缓冲区内布尔值的存储宽度。 */
			return s.width == 32 && (s.rows == 1 || s.scalar == ShaderScalarKind::Float);
		}

		/**
		 * @brief 按嵌套深度与预算校验块成员的布局。
		 *
		 * @param[in] members 当前反射块或嵌套结构的成员，按布局顺序检查。
		 * @param[in] bound 当前块或数组元素可占用的最大字节数。
		 * @param[in] depth 当前嵌套层深度，用于限制递归验证。
		 * @param[in,out] budget 递归遍历共享的剩余成员数，进入每一层时扣减。
		 * @param[out] error 失败时接收原因说明。
		 * @param[in] runtimeStorage 运行时存储。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Members(const std::vector<ShaderBlockMember>& members, uint32_t bound, size_t depth, size_t& budget,
		    std::string& error, bool runtimeStorage = false) {
			if (depth > MaxDepth || members.size() > budget)
				return Fail(error, "shader members exceed bounded schema");
			budget -= members.size();
			std::set<std::string> names;
			uint32_t end = 0;
			for (const auto& m : members) {
				if (m.type.arrayCount == 0) {
					auto element = m.type;
					element.arrayCount = 1;
					if (!runtimeStorage || &m != &members.back() || !Name(m.name) || !names.insert(m.name).second ||
					    !Shape(element, true) || m.offset < end || m.offset > bound || m.arrayStride == 0 ||
					    m.size != 0 || m.paddedSize != 0)
						return Fail(error, "invalid runtime storage array: " + m.name);
					if (element.scalar == ShaderScalarKind::Structure) {
						if (m.members.empty() || m.matrixStride || m.rowMajor ||
						    !Members(m.members, m.arrayStride, depth + 1, budget, error))
							return Fail(error, "invalid runtime structure array: " + m.name);
					} else {
						uint64_t elementBytes = uint64_t(element.columns) * 4;
						if (element.rows > 1) {
							const uint32_t vectors = m.rowMajor ? element.rows : element.columns;
							const uint32_t components = m.rowMajor ? element.columns : element.rows;
							if (m.matrixStride < components * 4)
								return Fail(error, "runtime matrix stride is smaller than its vector");
							elementBytes = uint64_t(vectors - 1) * m.matrixStride + components * 4;
						} else if (m.matrixStride || m.rowMajor)
							return Fail(error, "non-matrix runtime array has matrix layout");
						if (!m.members.empty() || m.arrayStride < elementBytes)
							return Fail(error, "runtime array elements overlap");
					}
					continue;
				}
				if (!Name(m.name) || !names.insert(m.name).second || !Shape(m.type, true) || m.size == 0 ||
				    m.paddedSize < m.size || m.offset < end || m.offset > bound || m.paddedSize > bound - m.offset)
					return Fail(error, "invalid/overlapping shader block member: " + m.name);
				if ((m.type.arrayCount > 1 &&
				        (m.arrayStride == 0 || uint64_t(m.arrayStride) * m.type.arrayCount > m.paddedSize)) ||
				    (m.type.rows > 1 && m.matrixStride == 0))
					return Fail(error, "shader array/matrix stride is invalid");
				if (m.type.scalar != ShaderScalarKind::Structure) {
					uint64_t elementBytes = uint64_t(m.type.columns) * 4;
					if (m.type.rows > 1) {
						const uint32_t vectors = m.rowMajor ? m.type.rows : m.type.columns,
						               components = m.rowMajor ? m.type.columns : m.type.rows;
						if (m.matrixStride < components * 4)
							return Fail(error, "matrix stride is smaller than its vector");
						elementBytes = uint64_t(vectors - 1) * m.matrixStride + components * 4;
					}
					if (m.type.arrayCount > 1 && m.arrayStride < elementBytes)
						return Fail(error, "array stride overlaps adjacent shader elements");
					const uint64_t required = elementBytes + uint64_t(m.type.arrayCount - 1) * m.arrayStride;
					if (required > m.size)
						return Fail(error, "shader member storage is smaller than reflected type");
				}
				if (m.type.scalar == ShaderScalarKind::Structure) {
					if (m.members.empty() ||
					    !Members(m.members, m.type.arrayCount > 1 ? m.arrayStride : m.size, depth + 1, budget, error))
						return false;
				} else if (!m.members.empty())
					return Fail(error, "scalar shader member has child members");
				end = m.offset + m.paddedSize;
			}
			return true;
		}

		bool WriteShape(BinaryWriter& w, const ShaderTypeShape& s) {
			return w.U32(uint32_t(s.scalar)) && w.U32(s.width) && w.U32(s.rows) && w.U32(s.columns) &&
			    w.U32(s.arrayCount);
		}

		bool ReadShape(BinaryReader& r, ShaderTypeShape& s) {
			uint32_t k;
			if (!r.U32(k) || !r.U32(s.width) || !r.U32(s.rows) || !r.U32(s.columns) || !r.U32(s.arrayCount))
				return false;
			s.scalar = ShaderScalarKind(k);
			return true;
		}

		bool WriteMembers(BinaryWriter& w, const std::vector<ShaderBlockMember>& v) {
			if (!w.U32(uint32_t(v.size())))
				return false;
			for (const auto& m : v)
				if (!w.String(m.name) || !w.U32(m.offset) || !w.U32(m.size) || !w.U32(m.paddedSize) ||
				    !w.U32(m.arrayStride) || !w.U32(m.matrixStride) || !w.U8(m.rowMajor ? 1 : 0) ||
				    !WriteShape(w, m.type) || !WriteMembers(w, m.members))
					return false;
			return true;
		}

		bool ReadMembers(BinaryReader& r, std::vector<ShaderBlockMember>& v, size_t depth, size_t& budget) {
			uint32_t n;
			if (depth > MaxDepth || !r.U32(n) || n > budget || n > r.Remaining() / 41)
				return false;
			budget -= n;
			v.resize(n);
			for (auto& m : v) {
				uint8_t b;
				if (!r.String(m.name, 1024) || !r.U32(m.offset) || !r.U32(m.size) || !r.U32(m.paddedSize) ||
				    !r.U32(m.arrayStride) || !r.U32(m.matrixStride) || !r.U8(b) || b > 1 || !ReadShape(r, m.type) ||
				    !ReadMembers(r, m.members, depth + 1, budget))
					return false;
				m.rowMajor = b != 0;
			}
			return true;
		}

		bool WriteIO(BinaryWriter& w, const std::vector<ShaderInterfaceVariable>& v) {
			if (!w.U32(uint32_t(v.size())))
				return false;
			for (const auto& x : v)
				if (!w.String(x.name) || !w.U32(x.location) || !WriteShape(w, x.type))
					return false;
			return true;
		}

		bool ReadIO(BinaryReader& r, std::vector<ShaderInterfaceVariable>& v) {
			uint32_t n;
			if (!r.U32(n) || n > MaxItems || n > r.Remaining() / 28)
				return false;
			v.resize(n);
			for (auto& x : v)
				if (!r.String(x.name, 1024) || !r.U32(x.location) || !ReadShape(r, x.type))
					return false;
			return true;
		}

		size_t MemberBytes(const std::vector<ShaderBlockMember>& members) {
			size_t size = members.capacity() * sizeof(ShaderBlockMember);
			for (const auto& m : members)
				size += m.name.capacity() + 1 + MemberBytes(m.members);
			return size;
		}

		size_t ResidentBytes(const ShaderAsset& a) {
			size_t bytes = sizeof(a) + a.spirv.capacity() * sizeof(uint32_t) + a.variant.sha256.capacity() +
			    a.variant.inputFingerprint.capacity() + 2 + a.metadata.bytecodeSha256.capacity() +
			    a.metadata.targetEnvironment.capacity() + 2 +
			    a.metadata.entries.capacity() * sizeof(ShaderEntryPointMetadata);
			for (const auto& e : a.metadata.entries) {
				bytes += e.name.capacity() + 1 + e.bindings.capacity() * sizeof(ShaderBinding) +
				    MemberBytes(e.pushConstantMembers);
				for (const auto& b : e.bindings)
					bytes += b.name.capacity() + 1 + MemberBytes(b.members);
				for (const auto* io : {&e.inputs, &e.outputs}) {
					bytes += io->capacity() * sizeof(ShaderInterfaceVariable);
					for (const auto& x : *io)
						bytes += x.name.capacity() + 1;
				}
			}
			return bytes;
		}

		std::string PathText(const std::filesystem::path& p) {
			auto s = p.generic_u8string();
			return {reinterpret_cast<const char*>(s.data()), s.size()};
		}
	}

	size_t ShaderLibrary::EstimateResidentBytes(const ShaderAsset& value) noexcept {
		return ResidentBytes(value);
	}

	const ShaderEntryPointMetadata* FindShaderEntry(
	    const ShaderMetadata& m, std::string_view name, ShaderStage stage) noexcept {
		for (const auto& e : m.entries)
			if (e.stage == stage && e.name == name)
				return &e;
		return nullptr;
	}

	bool ValidateShaderMetadata(const ShaderMetadata& m, std::string& error) {
		if ((m.schemaVersion != 1 && m.schemaVersion != 2 && m.schemaVersion != 3) ||
		    m.targetEnvironment != "vulkan1.3" || m.spirvVersion != 0x00010500 || !Hash(m.bytecodeSha256) ||
		    m.entries.empty() || m.entries.size() > 64)
			return Fail(error, "unsupported shader metadata/target/hash/entries");
		size_t budget = MaxItems;
		std::set<std::string> entries;
		for (const auto& e : m.entries) {
			if (!Name(e.name) || e.stage < ShaderStage::Vertex || e.stage > ShaderStage::Compute ||
			    !entries.insert(e.name).second || e.bindings.size() > 256)
				return Fail(error, "invalid/duplicate shader entry");
			std::map<std::pair<uint32_t, uint32_t>, std::string_view> slots;
			for (const auto& b : e.bindings) {
				const auto displayName = [](std::string_view name) {
					std::string displayed(name.substr(0, 128));
					std::replace(displayed.begin(), displayed.end(), '\0', '?');
					if (name.size() > 128)
						displayed += "...";
					return displayed;
				};
				const auto description = [&] {
					return "entry=" + displayName(e.name) + ", name=" + displayName(b.name) +
					    ", set=" + std::to_string(b.set) + ", binding=" + std::to_string(b.binding) +
					    ", count=" + std::to_string(b.count) +
					    ", kind=" + std::to_string(static_cast<uint32_t>(b.kind));
				};
				const char* invalidField = nullptr;
				if (!Name(b.name))
					invalidField = "name";
				else if (b.set > 31)
					invalidField = "set";
				else if (b.binding > 65535)
					invalidField = "binding";
				else if (b.count == 0 || b.count > 65536)
					invalidField = "count";
				else if (b.kind < ShaderResourceKind::UniformBuffer ||
				    b.kind > ShaderResourceKind::CombinedImageSampler)
					invalidField = "kind";
				if (invalidField)
					return Fail(
					    error, "invalid shader binding field " + std::string(invalidField) + ": " + description());

				const auto [existing, inserted] = slots.emplace(std::pair{b.set, b.binding}, b.name);
				if (!inserted)
					return Fail(error,
					    "duplicate shader binding slot: " + description() +
					        ", existing resource=" + displayName(existing->second));
				const bool block =
				    b.kind == ShaderResourceKind::UniformBuffer || b.kind == ShaderResourceKind::StorageBuffer;
				const bool image = b.kind == ShaderResourceKind::SampledImage ||
				    b.kind == ShaderResourceKind::StorageImage || b.kind == ShaderResourceKind::CombinedImageSampler;
				if (image) {
					if (b.imageDimension != ShaderImageDimension::Texture2D &&
					    !(m.schemaVersion >= 2 && b.imageDimension == ShaderImageDimension::Cube))
						return Fail(error, "Shader image has an unsupported or absent dimension");
				} else if (b.imageDimension != ShaderImageDimension::None)
					return Fail(error, "Non-image binding cannot declare an image dimension");
				if (block) {
					if ((b.blockSize == 0 && !(m.schemaVersion >= 3 && b.kind == ShaderResourceKind::StorageBuffer)) ||
					    b.blockSize > 16 * 1024 * 1024 || b.members.empty() ||
					    !Members(b.members, b.blockSize, 0, budget, error,
					        m.schemaVersion >= 3 && b.kind == ShaderResourceKind::StorageBuffer))
						return Fail(error, "invalid shader buffer members: " + error);
				} else if (b.blockSize != 0 || !b.members.empty())
					return Fail(error, "non-buffer shader binding contains block layout");
			}
			for (const auto* io : {&e.inputs, &e.outputs}) {
				std::set<uint32_t> locations;
				if (io->size() > 256)
					return Fail(error, "too many shader interface variables");
				for (const auto& x : *io)
					if (!Name(x.name) || x.location > 65535 || !locations.insert(x.location).second ||
					    !Shape(x.type, false) || x.type.arrayCount != 1 || x.type.rows != 1)
						return Fail(error, "unsupported/duplicate shader interface location/type");
			}
			if (e.pushConstantBytes > 65536 || (e.pushConstantBytes == 0) != e.pushConstantMembers.empty() ||
			    !Members(e.pushConstantMembers, e.pushConstantBytes, 0, budget, error))
				return Fail(error, "invalid push constant layout: " + error);
		}
		error.clear();
		return true;
	}

	bool ValidateShaderAsset(const ShaderAsset& a, std::string& error) {
		if (!ValidateShaderMetadata(a.metadata, error) || !Hash(a.variant.sha256) || !Hash(a.variant.inputFingerprint))
			return Fail(error, "invalid shader asset: " + error);
		if (a.spirv.size() < 5 || a.spirv.size() > MaxWords || a.spirv[0] != 0x07230203 ||
		    a.spirv[1] != a.metadata.spirvVersion || a.spirv[3] < 2 || a.spirv[4] != 0)
			return Fail(error, "invalid shader bytecode header");
		/* 运行时仅检查指令结构边界以发现损坏；工具链仍须先通过 spirv-val 验证。 */
		std::set<std::pair<ShaderStage, std::string>> actualEntries;
		for (size_t p = 5; p < a.spirv.size();) {
			size_t n = a.spirv[p] >> 16;
			if (n == 0 || n > a.spirv.size() - p)
				return Fail(error, "truncated shader instruction");
			const uint32_t opcode = a.spirv[p] & 65535u;
			/* 当前 Vulkan 目标没有启用 shaderDrawParameters。Cook、缓存和运行时加载共用此检查。 */
			constexpr uint32_t opCapability = 17, drawParameters = 4427;
			if (opcode == opCapability) {
				if (n != 2)
					return Fail(error, "Malformed SPIR-V OpCapability");
				if (a.spirv[p + 1] == drawParameters)
					return Fail(error,
					    "DrawParameters requires shaderDrawParameters, which is disabled in this Vulkan profile; use SV_VulkanVertexID for a direct vertex index");
			}
			if (opcode == 15u) {
				if (n < 4 || a.spirv[p + 2] == 0 || a.spirv[p + 2] >= a.spirv[3])
					return Fail(error, "invalid SPIR-V entry header");
				ShaderStage stage;
				switch (a.spirv[p + 1]) {
				case 0:
					stage = ShaderStage::Vertex;
					break;
				case 4:
					stage = ShaderStage::Fragment;
					break;
				case 5:
					stage = ShaderStage::Compute;
					break;
				default:
					return Fail(error, "unsupported SPIR-V entry stage");
				}
				std::string name;
				bool terminated = false;
				for (size_t i = 3; i < n && !terminated; ++i)
					for (uint32_t b = 0; b < 4; ++b) {
						const char c = char((a.spirv[p + i] >> (8 * b)) & 255u);
						if (c == 0) {
							if (b < 3 && (a.spirv[p + i] >> (8 * (b + 1))) != 0)
								return Fail(error, "invalid SPIR-V entry name padding");
							terminated = true;
							break;
						}
						name += c;
						if (name.size() > 1024)
							return Fail(error, "SPIR-V entry name limit exceeded");
					}
				if (!terminated || !Name(name) || !actualEntries.emplace(stage, name).second ||
				    !FindShaderEntry(a.metadata, name, stage))
					return Fail(error, "metadata/SPIR-V entry mismatch");
			}
			p += n;
		}
		if (actualEntries.size() != a.metadata.entries.size())
			return Fail(error, "metadata contains entries absent from SPIR-V");
		std::string hash;
		if (!ComputeAssetContentHash(std::as_bytes(std::span(a.spirv)), hash, error))
			return false;
		if (hash != a.metadata.bytecodeSha256)
			return Fail(error, "shader metadata does not match final bytecode hash");
		error.clear();
		return true;
	}

	bool ShaderLibrary::EncodePayload(const ShaderAsset& a, std::vector<std::byte>& output, std::string& error) {
		try {
			if (!ValidateShaderAsset(a, error))
				return false;
			BinaryWriter w;
			bool ok = w.U32(2) && w.String(a.variant.sha256) && w.String(a.variant.inputFingerprint) &&
			    w.U32(a.metadata.schemaVersion) && w.String(a.metadata.targetEnvironment) &&
			    w.U32(a.metadata.spirvVersion) && w.String(a.metadata.bytecodeSha256) &&
			    w.U32(uint32_t(a.metadata.entries.size()));
			for (const auto& e : a.metadata.entries) {
				ok = ok && w.String(e.name) && w.U32(uint32_t(e.stage)) && w.U32(uint32_t(e.bindings.size()));
				for (const auto& b : e.bindings)
					ok = ok && w.U32(b.set) && w.U32(b.binding) && w.U32(b.count) && w.U32(uint32_t(b.kind)) &&
					    w.String(b.name) && w.U32(b.blockSize) && WriteMembers(w, b.members) &&
					    w.U32(uint32_t(b.imageDimension));
				ok = ok && WriteIO(w, e.inputs) && WriteIO(w, e.outputs) && w.U32(e.pushConstantBytes) &&
				    WriteMembers(w, e.pushConstantMembers);
			}
			ok = ok && w.U32(uint32_t(a.spirv.size()));
			for (auto word : a.spirv)
				ok = ok && w.U32(word);
			if (!ok)
				return Fail(error, "shader payload encode: " + w.Error());
			auto candidate = w.Data();
			output.swap(candidate);
			error.clear();
			return true;
		} catch (const std::exception& e) {
			return Fail(error, e.what());
		}
	}

	bool ShaderLibrary::DecodePayload(
	    std::span<const std::byte> bytes, std::shared_ptr<const ShaderAsset>& output, std::string& error) {
		try {
			BinaryReader r(bytes);
			auto a = std::make_shared<ShaderAsset>();
			uint32_t v, n;
			size_t budget = MaxItems;
			if (!r.U32(v) || (v != 1 && v != 2) || !r.String(a->variant.sha256, 64) ||
			    !r.String(a->variant.inputFingerprint, 64) || !r.U32(a->metadata.schemaVersion) ||
			    !r.String(a->metadata.targetEnvironment, 32) || !r.U32(a->metadata.spirvVersion) ||
			    !r.String(a->metadata.bytecodeSha256, 64) || !r.U32(n) || n == 0 || n > 64)
				return Fail(error, "invalid shader payload header");
			a->metadata.entries.resize(n);
			for (auto& e : a->metadata.entries) {
				uint32_t stage;
				if (!r.String(e.name, 1024) || !r.U32(stage) || !r.U32(n) || n > 256)
					return Fail(error, "invalid shader entry payload");
				e.stage = ShaderStage(stage);
				e.bindings.resize(n);
				for (auto& b : e.bindings) {
					uint32_t kind;
					if (!r.U32(b.set) || !r.U32(b.binding) || !r.U32(b.count) || !r.U32(kind) ||
					    !r.String(b.name, 1024) || !r.U32(b.blockSize) || !ReadMembers(r, b.members, 0, budget))
						return Fail(error, "invalid shader binding payload");
					b.kind = ShaderResourceKind(kind);
					if (v >= 2) {
						uint32_t dimension;
						if (!r.U32(dimension))
							return Fail(error, "Missing shader image dimension");
						b.imageDimension = ShaderImageDimension(dimension);
					} else if (b.kind == ShaderResourceKind::SampledImage ||
					    b.kind == ShaderResourceKind::StorageImage ||
					    b.kind == ShaderResourceKind::CombinedImageSampler) {
						/* 旧版本模式与载荷仅支持二维图像，读取时保留原有类型。 */
						b.imageDimension = ShaderImageDimension::Texture2D;
					}
				}
				if (!ReadIO(r, e.inputs) || !ReadIO(r, e.outputs) || !r.U32(e.pushConstantBytes) ||
				    !ReadMembers(r, e.pushConstantMembers, 0, budget))
					return Fail(error, "invalid shader interface payload");
			}
			if (!r.U32(n) || n < 5 || n > MaxWords || n > r.Remaining() / 4)
				return Fail(error, "invalid shader word count");
			a->spirv.resize(n);
			for (auto& w : a->spirv)
				if (!r.U32(w))
					return Fail(error, "truncated shader words");
			if (!r.AtEnd() || !ValidateShaderAsset(*a, error))
				return Fail(error, "shader payload verification: " + error);
			std::shared_ptr<const ShaderAsset> candidate = std::move(a);
			output.swap(candidate);
			error.clear();
			return true;
		} catch (const std::exception& e) {
			return Fail(error, e.what());
		}
	}

	bool ShaderLibrary::RegisterAssetLoader(AssetManager& manager, std::string& error) {
		AssetLoader loader;
		if (!AssetType::TryParse("shader", loader.type))
			return Fail(error, "invalid internal shader type");
		loader.cppType = typeid(ShaderAsset);
		loader.decode = [](const AssetContainer& c, const AssetDecodeContext& context, AssetLoadResult& result,
		                    std::string& e) {
			if (context.cancellation.IsCancellationRequested())
				return Fail(e, "shader load cancelled");
			if (c.payloadVersion != 1 || !c.dependencies.empty())
				return Fail(e, "unsupported shader payload/dependencies");
			if (c.payload.size() > context.maxResidentBytes)
				return Fail(e, "shader exceeds resident budget");
			std::shared_ptr<const ShaderAsset> a;
			if (!DecodePayload(c.payload, a, e))
				return false;
			if (context.cancellation.IsCancellationRequested())
				return Fail(e, "shader load cancelled");
			const size_t resident = ResidentBytes(*a);
			if (resident > context.maxResidentBytes)
				return Fail(e, "decoded shader exceeds resident budget");
			result = MakeAssetLoadResult<ShaderAsset>(std::move(a), resident);
			return true;
		};
		return manager.RegisterLoader(std::move(loader), error);
	}

	bool ShaderLibrary::BuiltinId(std::string_view source, AssetId& output, std::string& error) {
		VirtualPath p;
		if (!VirtualPath::Parse("shader-source:/" + std::string(source), p, error) || p.RelativePath() != source ||
		    !source.ends_with(".slang"))
			return Fail(error, "builtin shader requires canonical relative .slang path");
		const std::string key = "Catalpa.Builtin.Shader/" + std::string(source);
		std::array<std::byte, 32> digest;
		if (!ComputeSha256(std::as_bytes(std::span(key.data(), key.size())), digest, error))
			return false;
		uint64_t high = 0, low = 0;
		for (size_t i = 0; i < 8; ++i) {
			high = (high << 8) | std::to_integer<uint8_t>(digest[i]);
			low = (low << 8) | std::to_integer<uint8_t>(digest[i + 8]);
		}
		const auto candidate = AssetId::FromWords(high, low);
		if (!candidate.HasValue())
			return Fail(error, "zero builtin identity digest");
		output = candidate;
		error.clear();
		return true;
	}

	bool ShaderLibrary::ReadBuiltinCatalog(const std::filesystem::path& root, std::string_view mount,
	    std::vector<AssetRecord>& output, std::string& error) {
		try {
			VirtualFileSystem fs;
			if (!fs.MountReadOnly(mount, root, 0, error))
				return false;
			std::vector<AssetRecord> result;
			VirtualPath manifestPath;
			if (!VirtualPath::Parse(std::string(mount) + ":/BuiltinShaders.manifest", manifestPath, error))
				return false;
			std::vector<std::byte> manifestBytes;
			AssetRegistry manifest;
			if (!fs.ReadFile(manifestPath, 4 * 1024 * 1024, manifestBytes, error) ||
			    !AssetManifest::Read(manifestBytes, manifest, error))
				return false;
			if (manifest.GetRecords().size() > 1024)
				return Fail(error, "builtin shader catalog entry limit exceeded");
			for (const auto& declared : manifest.GetRecords()) {
				const auto relative = std::filesystem::u8path(declared.locator.path.RelativePath());
				VirtualPath path;
				if (relative.extension() != L".catasset" ||
				    !VirtualPath::Parse(std::string(mount) + ":/" + PathText(relative), path, error))
					return Fail(error, "invalid shader catalog locator");
				std::vector<std::byte> bytes;
				if (!fs.ReadFile(path, 64 * 1024 * 1024, bytes, error))
					return false;
				AssetContainer c;
				std::string hash;
				if (!ComputeAssetContentHash(bytes, hash, error) || hash != declared.contentHash)
					return Fail(error, "builtin shader manifest/file hash mismatch");
				if (!AssetContainerReader::Read(bytes, c, error) || c.type.ToString() != "shader" ||
				    c.payloadVersion != 1 || !c.dependencies.empty() || c.id != declared.id ||
				    c.version != declared.version || c.type != declared.type || !declared.dependencies.empty())
					return Fail(error, "invalid builtin shader container: " + error);
				auto component = relative.begin();
				if (component == relative.end() || *component != L"versions" || ++component == relative.end() ||
				    !Hash(PathText(*component)))
					return Fail(error, "invalid shader versioned locator");
				const auto versionHash = PathText(*component);
				uint64_t version = 0;
				for (size_t i = 0; i < 16; ++i) {
					const char ch = versionHash[i];
					version = (version << 4) | uint64_t(ch <= '9' ? ch - '0' : ch - 'a' + 10);
				}
				if (c.version != (version ? version : 1))
					return Fail(error, "shader locator/version mismatch");
				std::filesystem::path source;
				while (++component != relative.end())
					source /= *component;
				source.replace_extension(L".slang");
				AssetId expected;
				if (!BuiltinId(PathText(source), expected, error) || expected != c.id)
					return Fail(error, "builtin shader name/id mismatch");
				AssetRecord rec = declared;
				rec.locator.path = std::move(path);
				result.push_back(std::move(rec));
			}
			if (result.empty())
				return Fail(error, "builtin shader catalog is empty");
			std::sort(result.begin(), result.end(),
			    [](const auto& a, const auto& b) { return a.locator.path.ToString() < b.locator.path.ToString(); });
			AssetRegistry checked;
			if (!checked.Replace(result, error))
				return false;
			output.swap(result);
			error.clear();
			return true;
		} catch (const std::exception& e) {
			return Fail(error, e.what());
		}
	}
}
