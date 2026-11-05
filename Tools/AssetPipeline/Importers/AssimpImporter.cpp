#include "catpch.h"
#include "AssetPipeline/Importers/AssimpImporter.h"
#include "AssetPipeline/AssetImportRegistry.h"
#include "AssetPipeline/Importers/ImportGeometry.h"
#include "AssetPipeline/Importers/TextureImporter.h"
#include "AssetPipeline/Importers/MaterialImporter.h"
#include "Images/ImageValidation.h"
#include <assimp/Importer.hpp>
#include <assimp/IOSystem.hpp>
#include <assimp/IOStream.hpp>
#include <assimp/ProgressHandler.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/config.h>
#include <assimp/material.h>

namespace Catalpa::Tools {
    namespace {
        // Assimp只能通过同一只读上下文取数据；绝不打开写入流或绕过源根访问。
        class SourceStream final : public Assimp::IOStream {
          public:
            explicit SourceStream(std::vector<std::byte> bytes) : m_Bytes(std::move(bytes)) {}

            size_t Read(void* p, size_t size, size_t count) override {
                if (!size)
                    return 0;
                const auto n = std::min(count, (m_Bytes.size() - m_Position) / size);
                if (n)
                    std::memcpy(p, m_Bytes.data() + m_Position, n * size);
                m_Position += n * size;
                return n;
            }

            size_t Write(const void*, size_t, size_t) override {
                return 0;
            }

            aiReturn Seek(size_t offset, aiOrigin origin) override {
                const int64_t delta = static_cast<int64_t>(offset);
                int64_t next = 0;
                if (origin == aiOrigin_SET) {
                    if (offset > m_Bytes.size())
                        return aiReturn_FAILURE;
                    next = int64_t(offset);
                } else if (origin == aiOrigin_CUR) {
                    if (delta > int64_t(m_Bytes.size()) || delta < -int64_t(m_Bytes.size()))
                        return aiReturn_FAILURE;
                    next = int64_t(m_Position) + delta;
                } else if (origin == aiOrigin_END) {
                    if (delta > 0 || delta < -int64_t(m_Bytes.size()))
                        return aiReturn_FAILURE;
                    next = int64_t(m_Bytes.size()) + delta;
                } else
                    return aiReturn_FAILURE;
                if (next < 0 || next > int64_t(m_Bytes.size()))
                    return aiReturn_FAILURE;
                m_Position = size_t(next);
                return aiReturn_SUCCESS;
            }

            size_t Tell() const override {
                return m_Position;
            }

            size_t FileSize() const override {
                return m_Bytes.size();
            }

            void Flush() override {}

          private:
            std::vector<std::byte> m_Bytes;
            size_t m_Position = 0;
        };

        class SourceIO final : public Assimp::IOSystem {
          public:
            SourceIO(AssetBuildContext& c, bool isFbx) : context(c), m_CheckFbxSource(isFbx) {}

            bool Exists(const char* file) const override {
                try {
                    std::filesystem::path p;
                    std::string e;
                    return file &&
                           context.Resolve(context.request.source, file, p, e,
                                           SourceReferenceSyntax::FilePath) &&
                           std::filesystem::is_regular_file(p);
                } catch (...) {
                    return false;
                }
            }

            char getOsSeparator() const override {
                return '/';
            }

            Assimp::IOStream* Open(const char* file, const char* mode = "rb") override {
                if (!file || !mode || (std::string_view(mode) != "r" && std::string_view(mode) != "rb")) {
                    failure = "Assimp source adapter accepts read-only streams";
                    return nullptr;
                }
                try {
                    std::filesystem::path p;
                    std::vector<std::byte> b;
                    std::string error;
                    if (!context.Resolve(context.request.source, file, p, error,
                                         SourceReferenceSyntax::FilePath) ||
                        !context.Read(p, b, error)) {
                        if (failure.empty())
                            failure = error;
                        return nullptr;
                    }
                    if (m_CheckFbxSource && p == context.request.source && !b.empty()) {
                        // Assimp 将 Phong 推导值和 Maya 显式值写到同一材质键。
                        // 属性名在 FBX 文本和二进制中都按字节保存。只用其缺席证明来源；
                        // 如果其它材质或模板也声明了该名字，则保留 Assimp 的结果并校验。
                        const std::string_view bytes(reinterpret_cast<const char*>(b.data()), b.size());
                        fbxHasOnlyPhongRoughness = bytes.find("Maya|roughness") == std::string_view::npos;
                        m_CheckFbxSource = false;
                    }
                    return new SourceStream(std::move(b));
                } catch (const std::exception& x) {
                    if (failure.empty())
                        failure = x.what();
                    return nullptr;
                }
            }

            void Close(Assimp::IOStream* stream) override {
                delete stream;
            }

            AssetBuildContext& context;
            std::string failure;
            bool fbxHasOnlyPhongRoughness = false;

          private:
            bool m_CheckFbxSource;
        };

        class Progress final : public Assimp::ProgressHandler {
          public:
            explicit Progress(CancellationToken c) : cancel(c) {}

            bool Update(float) override {
                return !cancel.IsCancellationRequested();
            }

          private:
            CancellationToken cancel;
        };

        Math::Mat4 Matrix(const aiMatrix4x4& in) {
            Math::Mat4 out;
            for (int c = 0; c < 4; ++c)
                for (int row = 0; row < 4; ++row)
                    out[c][row] = in[row][c];
            return out;
        }

        bool ReadRoughness(const aiMaterial& source, bool isFbx, bool fbxHasOnlyPhongRoughness,
                           AssetBuildContext& context, unsigned index, ImportedMaterial& material,
                           std::string& error) {
            float shininess = 0, factor = 0;
            const bool hasShininess = source.Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS;
            const bool hasFactor = source.Get(AI_MATKEY_ROUGHNESS_FACTOR, factor) == AI_SUCCESS;
            if (hasShininess)
                material.sourceProperties["phong.Ns"] = std::to_string(shininess);

            if (hasFactor && !(fbxHasOnlyPhongRoughness && hasShininess)) {
                if (isFbx && (!std::isfinite(factor) || factor < 0 || factor > 1)) {
                    error = "material/" + std::to_string(index) + ": FBX roughness " + std::to_string(factor) +
                            " is outside [0,1]; check the source material's roughness";
                    return false;
                }
                material.roughness = factor;
                return true;
            }

            if (!hasShininess)
                return true;
            if (!std::isfinite(shininess) || shininess < 0) {
                error = "Invalid Phong shininess";
                return false;
            }
            // FBX 的 Assimp 近似在 Ns > 100 时会变负。对已确认的 Phong 材质，
            // 全程使用同一指数转换，避免在 Ns = 100 两侧切换公式。
            material.roughness = std::sqrt(2.0f / (shininess + 2.0f));
            if (hasFactor)
                material.sourceProperties["phong.assimpRoughness"] = std::to_string(factor);
            const auto note = "Phong Ns to roughness sqrt(2/(Ns+2)); original Ns=" + std::to_string(shininess);
            material.sourceProperties["approximation"] = note;
            context.Diagnose(AssetDiagnosticLevel::Warning, "material/" + std::to_string(index), note);
            return true;
        }
    }

    bool AssimpImporter::Import(AssetBuildContext& ctx, AssetImportResult& output, std::string& error) const
        try {
        const auto extension = AssetSourceExtension(ctx.request.source);
        const bool isFbx = extension == ".fbx";
        Assimp::Importer importer;
        auto* io = new SourceIO(ctx, isFbx);
        importer.SetIOHandler(io);
        importer.SetProgressHandler(new Progress(ctx.request.cancellation));
        // 不使用预变换、法线平滑、优化、全局缩放或隐式UV翻转。
        importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_IGNORE_UP_DIRECTION, true);
        importer.SetPropertyBool(AI_CONFIG_FBX_CONVERT_TO_M, false);
        const auto* scene = importer.ReadFile(AssetPathText(ctx.request.source), aiProcess_Triangulate);
        if (!scene) {
            error = std::string("Assimp: ") + importer.GetErrorString();
            if (!io->failure.empty())
                error += "; " + io->failure;
            return false;
        }
        // OBJ 读取器找不到声明的 MTL 时仍可能构造场景，甚至尝试一个猜测的替代文件。
        // 保留读取错误，防止把缺材质的场景当作成功的完整导入。
        if (!io->failure.empty()) {
            error = "Source dependency could not be opened: " + io->failure;
            return false;
        }
        if (!scene->mRootNode || scene->mNumMeshes == 0 || scene->mNumMeshes > 65536 ||
            scene->mNumMaterials > 4096 || scene->mNumTextures > 4096 || scene->mNumAnimations) {
            error = "Unsupported empty/oversized/animated Assimp scene";
            return false;
        }
        AssetImportResult result;
        result.primary = ctx.Identity("model");
        std::map<std::pair<std::string, std::string>, AssetId> imageIds;
        const auto image = [&](const aiString& source, const std::string& semantic,
                               ImportedMaterial& material) -> bool {
            const std::string uri = source.C_Str();
            const auto [kind, imageKind] = TextureImporter::MaterialChannel(semantic);
            const auto cacheKey = std::make_pair(uri, imageKind);
            auto found = imageIds.find(cacheKey);
            material.sourceProperties[semantic + ".uri"] = uri;
            if (found != imageIds.end()) {
                material.textures[semantic] = found->second;
                return true;
            }
            ImageAsset decoded;
            if (const auto* embedded = scene->GetEmbeddedTexture(uri.c_str())) {
                if (!embedded->pcData) {
                    error = "Embedded texture has no payload";
                    return false;
                }
                if (embedded->mHeight == 0) {
                    if (embedded->mWidth > ctx.request.settings.maxSourceBytes) {
                        error = "Embedded texture exceeds the source byte limit";
                        return false;
                    }
                    if (!TextureImporter::Decode(
                            ctx,
                            std::span(reinterpret_cast<const std::byte*>(embedded->pcData), embedded->mWidth),
                            AssetPathText(ctx.request.source) + "#" + uri, kind, decoded, error))
                        return false;
                } else {
                    auto& v = decoded.image;
                    if (!embedded->mWidth || !embedded->pcData ||
                        uint64_t(embedded->mWidth) * embedded->mHeight >
                            (80 * 1024 * 1024 - 1024 * 1024) / 4 ||
                        !ctx.ReserveWorking(size_t(embedded->mWidth) * embedded->mHeight * 4, error) ||
                        !ctx.WorkspaceFits(size_t(embedded->mWidth) * embedded->mHeight * 4, error)) {
                        error = "Embedded texture dimensions exceed budget";
                        return false;
                    }
                    decoded.sourceUri = AssetPathText(ctx.request.source) + "#" + uri;
                    v.width = embedded->mWidth;
                    v.height = embedded->mHeight;
                    v.semantic = kind;
                    v.colorSpace =
                        kind == ImageSemantic::Color ? ImageColorSpace::SRGB : ImageColorSpace::Linear;
                    v.format =
                        kind == ImageSemantic::Color ? PixelFormat::RGBA8_SRGB : PixelFormat::RGBA8_UNorm;
                    v.alphaMode = ImageAlphaMode::Straight;
                    v.pixels.resize(size_t(v.width) * v.height * 4);
                    for (size_t i = 0; i < size_t(v.width) * v.height; ++i) {
                        const auto& p = embedded->pcData[i];
                        v.pixels[i * 4] = std::byte(p.r);
                        v.pixels[i * 4 + 1] = std::byte(p.g);
                        v.pixels[i * 4 + 2] = std::byte(p.b);
                        v.pixels[i * 4 + 3] = std::byte(p.a);
                    }
                    v.subresources.push_back({0, 0, 0, v.width, v.height, 1, 0, uint64_t(v.width) * 4,
                                              v.pixels.size(), v.pixels.size()});
                    if (!ValidateImageAsset(decoded, error))
                        return false;
                }
            } else {
                std::filesystem::path file;
                // Assimp 只返回贴图字符串，未记录所属 MTL。先按已读取的材质库定位，
                // 多个材质库给出不同实体时要求显式重映射，避免误选 OBJ 旁的同名贴图。
                std::set<std::filesystem::path> candidates;
                if (extension == ".obj" && !ctx.request.settings.dependencyRemaps.contains(uri)) {
                    for (const auto& dependency : ctx.Dependencies()) {
                        auto ext = dependency.path.extension().wstring();
                        std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t ch) {
                            return ch >= L'A' && ch <= L'Z' ? wchar_t(ch - L'A' + L'a') : ch;
                        });
                        if (ext != L".mtl")
                            continue;
                        std::filesystem::path candidate;
                        std::string ignored;
                        if (ctx.Resolve(dependency.path, uri, candidate, ignored,
                                        SourceReferenceSyntax::FilePath))
                            candidates.insert(candidate);
                    }
                }
                if (candidates.size() > 1) {
                    error = "Ambiguous texture dependency across material libraries: " + uri;
                    return false;
                }
                if (!candidates.empty()) {
                    file = *candidates.begin();
                } else {
                    // FBX 导出目录可能只保留贴图原名。OBJ 相对路径仍严格以 MTL/OBJ 为基准。
                    const auto syntax = extension == ".fbx" || std::filesystem::u8path(uri).is_absolute()
                                            ? SourceReferenceSyntax::FbxTexture
                                            : SourceReferenceSyntax::FilePath;
                    if (!ctx.Resolve(ctx.request.source, uri, file, error, syntax)) {
                        error = "Cannot resolve texture dependency " + uri + ": " + error;
                        return false;
                    }
                }
                std::vector<std::byte> bytes;
                if (!ctx.Read(file, bytes, error) ||
                    !TextureImporter::Decode(ctx, bytes, AssetPathText(file), kind, decoded, error))
                    return false;
            }
            const auto key = "image/" + uri + "/" + imageKind;
            const auto id = ctx.Identity(key);
            result.assets.push_back({id, key, uri, std::move(decoded)});
            result.mappings.push_back({id, key, uri, "image"});
            imageIds.emplace(cacheKey, id);
            material.textures[semantic] = id;
            material.sourceProperties[semantic + ".uri"] = uri;
            return true;
        };

        for (unsigned i = 0; i < scene->mNumMaterials; ++i) {
            const auto* in = scene->mMaterials[i];
            ImportedMaterial material;
            material.textureFlipV = true;
            aiString name;
            in->Get(AI_MATKEY_NAME, name);
            aiColor4D color(1, 1, 1, 1);
            if (aiGetMaterialColor(in, AI_MATKEY_BASE_COLOR, &color) != AI_SUCCESS)
                aiGetMaterialColor(in, AI_MATKEY_COLOR_DIFFUSE, &color);
            material.baseColor = {color.r, color.g, color.b, color.a};
            aiColor3D emission(0, 0, 0);
            in->Get(AI_MATKEY_COLOR_EMISSIVE, emission);
            material.emissive = {emission.r, emission.g, emission.b};
            in->Get(AI_MATKEY_METALLIC_FACTOR, material.metallic);
            aiColor3D specular(0, 0, 0), ambient(0, 0, 0);
            in->Get(AI_MATKEY_COLOR_SPECULAR, specular);
            in->Get(AI_MATKEY_COLOR_AMBIENT, ambient);
            material.sourceProperties["phong.specular"] = std::to_string(specular.r) + "," +
                                                          std::to_string(specular.g) + "," +
                                                          std::to_string(specular.b);
            material.sourceProperties["phong.ambient"] =
                std::to_string(ambient.r) + "," + std::to_string(ambient.g) + "," + std::to_string(ambient.b);
            int shading = 0;
            if (in->Get(AI_MATKEY_SHADING_MODEL, shading) == AI_SUCCESS) {
                material.unlit = shading == aiShadingMode_NoShading;
                material.sourceProperties["shadingModel"] = std::to_string(shading);
            }
            float ior = 1;
            if (in->Get(AI_MATKEY_REFRACTI, ior) == AI_SUCCESS)
                material.sourceProperties["indexOfRefraction"] = std::to_string(ior);
            float bump = 1;
            if (in->Get(AI_MATKEY_BUMPSCALING, bump) == AI_SUCCESS)
                material.sourceProperties["height.scale"] = std::to_string(bump);
            if (!ReadRoughness(*in, isFbx, io->fbxHasOnlyPhongRoughness, ctx, i, material, error))
                return false;
            float opacity = 1;
            in->Get(AI_MATKEY_OPACITY, opacity);
            material.baseColor[3] *= opacity;
            material.alphaMode = material.baseColor[3] < 1 ? "blend" : "opaque";
            int twoSided = 0;
            in->Get(AI_MATKEY_TWOSIDED, twoSided);
            material.doubleSided = twoSided != 0;
            const auto add = [&](aiTextureType type, std::string semantic) -> bool {
                if (!in->GetTextureCount(type))
                    return true;
                if (in->GetTextureCount(type) > 1) {
                    error = "Multiple texture layers require an explicit layer policy";
                    return false;
                }
                aiString path;
                unsigned uv = 0;
                aiTextureMapping mapping = aiTextureMapping_UV;
                aiTextureMapMode modes[3] = {aiTextureMapMode_Wrap, aiTextureMapMode_Wrap,
                                             aiTextureMapMode_Wrap};
                if (in->GetTexture(type, 0, &path, &mapping, &uv, nullptr, nullptr, modes) != AI_SUCCESS ||
                    uv != 0 || mapping != aiTextureMapping_UV) {
                    error = "Texture requires unsupported UV mapping";
                    return false;
                }
                aiUVTransform transform;
                if (in->Get(AI_MATKEY_UVTRANSFORM(type, 0), transform) == AI_SUCCESS &&
                    (transform.mTranslation.x != 0 || transform.mTranslation.y != 0 ||
                     transform.mScaling.x != 1 || transform.mScaling.y != 1 || transform.mRotation != 0)) {
                    error = semantic +
                            ": non-identity source UV transform requires a transform-aware material schema";
                    return false;
                }
                material.sourceProperties[semantic + ".wrap"] =
                    std::to_string(int(modes[0])) + "," + std::to_string(int(modes[1]));
                const auto wrap = [](aiTextureMapMode mode) -> uint32_t {
                    if (mode == aiTextureMapMode_Wrap) return 10497;
                    if (mode == aiTextureMapMode_Clamp) return 33071;
                    if (mode == aiTextureMapMode_Mirror) return 33648;
                    return 0; // Decal要求透明边界，不能静默当作Clamp。
                };
                int minFilter = 9987, magFilter = 9729;
                in->Get("$tex.mappingfiltermin", unsigned(type), 0, minFilter);
                in->Get("$tex.mappingfiltermag", unsigned(type), 0, magFilter);
                uint32_t flags = 0;
                if (!MaterialImporter::SamplerFlags(wrap(modes[0]), wrap(modes[1]), uint32_t(minFilter),
                    uint32_t(magFilter), flags, error)) return false;
                material.samplers[semantic] = flags;
                return image(path, semantic, material);
            };
            if (in->GetTextureCount(aiTextureType_BASE_COLOR)) {
                if (!add(aiTextureType_BASE_COLOR, "baseColor"))
                    return false;
            } else if (!add(aiTextureType_DIFFUSE, "baseColor"))
                return false;
            if (!add(aiTextureType_NORMALS, "normal") || !add(aiTextureType_EMISSIVE, "emissive") ||
                !add(aiTextureType_METALNESS, "metallic") ||
                !add(aiTextureType_DIFFUSE_ROUGHNESS, "roughness") ||
                !add(aiTextureType_AMBIENT_OCCLUSION, "occlusion"))
                return false;
            if (!add(aiTextureType_HEIGHT, "height") || !add(aiTextureType_SPECULAR, "specular"))
                return false;
            for (unsigned type = 1; type <= AI_TEXTURE_TYPE_MAX; ++type) {
                const auto value = aiTextureType(type);
                if (value == aiTextureType_BASE_COLOR || value == aiTextureType_DIFFUSE ||
                    value == aiTextureType_NORMALS || value == aiTextureType_EMISSIVE ||
                    value == aiTextureType_METALNESS || value == aiTextureType_DIFFUSE_ROUGHNESS ||
                    value == aiTextureType_AMBIENT_OCCLUSION || value == aiTextureType_HEIGHT ||
                    value == aiTextureType_SPECULAR)
                    continue;
                if (in->GetTextureCount(value)) {
                    if (!add(value, "source_" + std::to_string(type)))
                        return false;
                    ctx.Diagnose(
                        AssetDiagnosticLevel::Warning, "material/" + std::to_string(i),
                        "Texture type " + std::to_string(type) +
                            " retained as source data; preview does not evaluate that material channel");
                }
            }
            if (in->GetTextureCount(aiTextureType_HEIGHT))
                ctx.Diagnose(
                    AssetDiagnosticLevel::Warning, "material/" + std::to_string(i),
                    "Height/bump retained as data, not silently reinterpreted as a tangent-space normal map");
            const auto key = "material/" + std::to_string(i);
            const auto id = ctx.Identity(key);
            result.assets.push_back({id, key, name.C_Str(), std::move(material)});
            result.mappings.push_back({id, key, name.C_Str(), "material"});
        }

        // 几何只构造一次；后面的节点实例按网格索引共享同一个派生资产。
        for (unsigned i = 0; i < scene->mNumMeshes; ++i) {
            if (!ctx.CheckCancelled(error))
                return false;
            const auto* in = scene->mMeshes[i];
            if (!in || !in->mVertices || !in->mFaces || in->mNumBones || in->mNumAnimMeshes ||
                in->mNumVertices > ctx.request.settings.maxVertices ||
                in->mNumFaces > ctx.request.settings.maxIndices / 3) {
                error = "Unsupported skin/morph or invalid mesh streams/count";
                return false;
            }
            for (unsigned channel = 1; channel < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++channel)
                if (in->HasTextureCoords(channel)) {
                    error = "Only UV0 is supported by the current mesh schema";
                    return false;
                }
            for (unsigned channel = 1; channel < AI_MAX_NUMBER_OF_COLOR_SETS; ++channel)
                if (in->HasVertexColors(channel)) {
                    error = "Only COLOR_0 is supported by the current mesh schema";
                    return false;
                }
            if (in->HasTangentsAndBitangents() && !in->HasNormals()) {
                error = "Tangents require source normals";
                return false;
            }
            if (!ctx.WorkspaceFits(size_t(in->mNumVertices) * sizeof(ImportVertex) +
                                       size_t(in->mNumFaces) * 3 * sizeof(uint32_t),
                                   error))
                return false;
            std::vector<ImportVertex> vertices(in->mNumVertices);
            for (unsigned j = 0; j < in->mNumVertices; ++j) {
                auto& v = vertices[j];
                const auto& p = in->mVertices[j];
                v.position = {p.x, p.y, p.z};
                if (in->HasNormals()) {
                    const auto& n = in->mNormals[j];
                    v.normal = {n.x, n.y, n.z};
                }
                if (in->HasTextureCoords(0)) {
                    const auto& uv = in->mTextureCoords[0][j];
                    if (in->mNumUVComponents[0] > 2 && uv.z != 0.0f) {
                        error = "Three-dimensional texture coordinates need a volume texture schema";
                        return false;
                    }
                    v.uv = {uv.x, uv.y};
                }
                if (in->HasVertexColors(0)) {
                    const auto& c = in->mColors[0][j];
                    v.color = {c.r, c.g, c.b, c.a};
                }
                if (in->HasTangentsAndBitangents()) {
                    const auto& t = in->mTangents[j];
                    const auto& b = in->mBitangents[j];
                    const auto& n = in->mNormals[j];
                    const float handed = (n ^ t) * b;
                    if (!std::isfinite(handed) || handed == 0) {
                        error = "Invalid source tangent/bitangent basis";
                        return false;
                    }
                    v.tangent = {t.x, t.y, t.z, handed < 0 ? -1.0f : 1.0f};
                }
            }
            std::vector<uint32_t> indices;
            indices.reserve(size_t(in->mNumFaces) * 3);
            for (unsigned j = 0; j < in->mNumFaces; ++j) {
                const auto& f = in->mFaces[j];
                if (f.mNumIndices != 3) {
                    error = "Assimp primitive cannot be triangulated";
                    return false;
                }
                indices.insert(indices.end(), f.mIndices, f.mIndices + 3);
            }
            MeshAsset mesh;
            if (!BuildImportedMesh(ctx, std::move(vertices), std::move(indices), in->HasNormals(),
                                   in->HasTextureCoords(0), in->HasTangentsAndBitangents(), mesh, error))
                return false;
            const auto key = "mesh/" + std::to_string(i);
            const auto id = ctx.Identity(key);
            result.assets.push_back({id, key, in->mName.C_Str(), std::move(mesh)});
            result.mappings.push_back({id, key, in->mName.C_Str(), "mesh"});
        }

        ModelAsset model;

        struct Visit {
            const aiNode* node;
            int32_t parent;
            std::string key;
        };

        std::vector<Visit> nodes{{scene->mRootNode, -1, "node/0"}};
        std::set<const aiNode*> visited;
        for (size_t i = 0; i < nodes.size(); ++i) {
            if (!nodes[i].node || nodes.size() > ctx.request.settings.maxNodes ||
                !visited.insert(nodes[i].node).second) {
                error = "Assimp node graph is null, cyclic or oversized";
                return false;
            }
            const auto current = nodes[i];
            if (uint64_t(model.nodes.size()) + current.node->mNumMeshes + current.node->mNumChildren >=
                ctx.request.settings.maxNodes) {
                error = "Assimp node/mesh instance expansion exceeds node budget";
                return false;
            }
            const auto index = uint32_t(model.nodes.size());
            Math::TransformTRS local;
            const auto status = Math::DecomposeTRS(Matrix(current.node->mTransformation), local);
            if (!status.IsSuccess()) {
                error = current.key + ": " + status.message;
                return false;
            }
            model.nodes.push_back(
                {ctx.Identity(current.key).value, current.node->mName.C_Str(), current.parent, local});
            result.mappings.push_back({result.primary, current.key, current.node->mName.C_Str(), "node",
                                       ctx.Identity(current.key).value});
            for (unsigned k = 0; k < current.node->mNumMeshes; ++k) {
                const auto mesh = current.node->mMeshes[k];
                if (mesh >= scene->mNumMeshes ||
                    scene->mMeshes[mesh]->mMaterialIndex >= scene->mNumMaterials) {
                    error = "Assimp node mesh/material index out of range";
                    return false;
                }
                const auto childKey = current.key + "/mesh/" + std::to_string(k);
                const auto child = uint32_t(model.nodes.size());
                model.nodes.push_back(
                    {ctx.Identity(childKey).value, scene->mMeshes[mesh]->mName.C_Str(), int32_t(index), {}});
                model.meshes.push_back(
                    {child,
                     AssetHandle<MeshAsset>{ctx.Identity("mesh/" + std::to_string(mesh))},
                     {AssetHandle<MaterialInstance>{
                         ctx.Identity("material/" + std::to_string(scene->mMeshes[mesh]->mMaterialIndex))}}});
            }
            for (unsigned k = 0; k < current.node->mNumChildren; ++k)
                nodes.push_back(
                    {current.node->mChildren[k], int32_t(index), current.key + "/" + std::to_string(k)});
        }

        Math::Mat4 conversion{1};
        if (extension == ".fbx") {
            // Assimp 用 AI_FLOAT 保存 FBX 的厘米/单位；Get 会严格检查元数据类型。
            float centimetres = 0;
            if (!scene->mMetaData || !scene->mMetaData->Get("UnitScaleFactor", centimetres) ||
                !std::isfinite(centimetres) || centimetres <= 0) {
                error = "FBX unit metadata is missing/invalid";
                return false;
            }
            const float metres = float(centimetres * 0.01);
            conversion = Math::Mat4{0};
            conversion[3][3] = 1;
            if (ctx.request.settings.sourceUp == "auto") {
                int32_t up{}, us{}, front{}, fs{}, right{}, rs{};
                const bool axesPresent =
                    scene->mMetaData->Get("UpAxis", up) && scene->mMetaData->Get("UpAxisSign", us) &&
                    scene->mMetaData->Get("FrontAxis", front) && scene->mMetaData->Get("FrontAxisSign", fs) &&
                    scene->mMetaData->Get("CoordAxis", right) && scene->mMetaData->Get("CoordAxisSign", rs);
                const auto validSign = [](int32_t value) { return value == 1 || value == -1; };
                if (!axesPresent || up < 0 || up > 2 || front < 0 || front > 2 || right < 0 || right > 2 ||
                    up == front || up == right || front == right || !validSign(us) || !validSign(fs) ||
                    !validSign(rs)) {
                    error = "Invalid FBX axis metadata";
                    return false;
                }
                conversion[right][0] = rs * metres;
                conversion[up][1] = us * metres;
                conversion[front][2] = fs * metres;
            } else
                conversion[0][0] = conversion[1][1] = conversion[2][2] = metres;
            ctx.Diagnose(AssetDiagnosticLevel::Info, "coordinates",
                         "FBX metadata centimetres-per-unit=" + std::to_string(centimetres) +
                             "; converted to metres once at model root; Assimp axis conversion disabled");
        } else
            ctx.Diagnose(AssetDiagnosticLevel::Warning, "coordinates",
                         "OBJ has no standard unit/axis metadata; Y-up/metres assumed unless explicit import "
                         "settings override");
        if (!SetImportRoot(ctx, model, conversion, error))
            return false;
        ctx.Diagnose(AssetDiagnosticLevel::Info, "geometry",
                     "Assimp triangulation recorded; no optimization, source normalization, UV flip or mesh "
                     "pretransform");
        ctx.Diagnose(AssetDiagnosticLevel::Info, "texture-coordinates",
                     "Assimp lower-left source UV/tangent basis retained; explicit uv_flip_v material "
                     "parameter reconciles top-row image storage at sampling, without altering pixels");
        if (scene->mNumCameras || scene->mNumLights)
            ctx.Diagnose(AssetDiagnosticLevel::Warning, "non-mesh-objects",
                         "Static ModelAsset excludes source cameras/lights; original source is retained and "
                         "editor uses a separate preview camera");
        ctx.Diagnose(AssetDiagnosticLevel::Info, "material-preview",
                     "Ordinary PBR materials consume the converted parameters; original Phong values and independent Unlit previews are retained in "
                     "cooked assets/report");
        result.assets.push_back(
            {result.primary, "model", AssetPathText(ctx.request.source.stem()), std::move(model)});
        result.mappings.push_back(
            {result.primary, "model", AssetPathText(ctx.request.source.stem()), "model"});
        output = std::move(result);
        return true;
    } catch (const std::exception& exception) {

        error = "Assimp import: " + std::string(exception.what());
        return false;
    }
}
