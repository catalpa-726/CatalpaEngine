#include "Documents/DocumentRecovery.h"
#include "Core/IO/AtomicFileWriter.h"
#include "Core/IO/VirtualFileSystem.h"
#include "Core/PlatformServices.h"
#include "Core/Serialization/AuthorJson.h"
#include "AssetPipeline/AssetImportRegistry.h"
#include "Scene/PrefabAsset.h"
#include "Scene/SceneSerializer.h"
#include "Materials/MaterialValidation.h"
#include "MaterialGraph/MaterialGraphSource.h"
#include "Scripting/BehaviorGraphAsset.h"
#include "Scripting/ScriptAsset.h"
#include "Animation/AnimationAssetLoader.h"
#include "Particles/ParticleEffect.h"
#include "Physics/CollisionShapeAsset.h"
#include "Physics/PhysicsMaterialAsset.h"
#include "Navigation/NavMeshAsset.h"
#include "Terrain/TerrainAsset.h"
#include "UI/UIDocumentAsset.h"

namespace Catalpa::Editor {
    namespace {
        constexpr std::string_view Base64Alphabet =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string EncodeBase64(std::string_view bytes) {
            std::string result;
            result.reserve((bytes.size() + 2) / 3 * 4);
            for (size_t i = 0; i < bytes.size(); i += 3) {
                const uint32_t a = static_cast<uint8_t>(bytes[i]),
                               b = i + 1 < bytes.size() ? static_cast<uint8_t>(bytes[i + 1]) : 0,
                               c = i + 2 < bytes.size() ? static_cast<uint8_t>(bytes[i + 2]) : 0;
                const uint32_t word = (a << 16) | (b << 8) | c;
                result += Base64Alphabet[(word >> 18) & 63];
                result += Base64Alphabet[(word >> 12) & 63];
                result += i + 1 < bytes.size() ? Base64Alphabet[(word >> 6) & 63] : '=';
                result += i + 2 < bytes.size() ? Base64Alphabet[word & 63] : '=';
            }
            return result;
        }
        std::string DecodeBase64(std::string_view encoded, size_t limit) {
            if (encoded.size() % 4 || encoded.size() > (limit + 2) / 3 * 4)
                throw std::invalid_argument("Recovery base64 length exceeds its budget");
            std::string result;
            result.reserve(encoded.size() / 4 * 3);
            for (size_t i = 0; i < encoded.size(); i += 4) {
                uint32_t word = 0;
                unsigned padding = 0;
                for (unsigned k = 0; k < 4; ++k) {
                    const char c = encoded[i + k];
                    if (c == '=') {
                        if (k < 2 || i + 4 != encoded.size())
                            throw std::invalid_argument("Invalid recovery base64 padding");
                        ++padding;
                        word <<= 6;
                    } else {
                        const auto index = Base64Alphabet.find(c);
                        if (padding || index == std::string_view::npos)
                            throw std::invalid_argument("Invalid recovery base64 character");
                        word = (word << 6) | uint32_t(index);
                    }
                }
                if (padding > 2 || (padding == 1 && (word & 255)) || (padding == 2 && (word & 65535)))
                    throw std::invalid_argument("Noncanonical recovery base64 tail");
                result += char((word >> 16) & 255);
                if (padding < 2)
                    result += char((word >> 8) & 255);
                if (!padding)
                    result += char(word & 255);
            }
            if (result.size() > limit)
                throw std::invalid_argument("Recovered content exceeds its domain budget");
            return result;
        }
        bool ValidateUISource(std::string_view source, std::string& error) {
            const auto value = AuthorJson::Parse(source, 1024 * 1024);
            AuthorJson::Fields(value, {"schemaVersion", "document", "files", "inlineFiles", "images", "fonts",
                                       "fontSources", "values"});
            if (AuthorJson::Unsigned(value.at("schemaVersion")) != 1)
                throw std::invalid_argument("Unsupported UI author schema");
            UIDocumentAsset document;
            document.entry = value.at("document").get<std::string>();
            const auto& files = value.at("files");
            if (!files.is_array() || files.size() > 128)
                throw std::invalid_argument("Invalid UI source file table");
            // 保存时只核对作者路径/文本结构；真实外置文件、字体和样式由 Cook/预览严格解析。
            for (const auto& file : files) {
                const auto path = file.get<std::string>();
                if (!document.files.emplace(path, std::string{}).second)
                    throw std::invalid_argument("Duplicate UI source path");
            }
            if (value.contains("inlineFiles")) {
                if (!value["inlineFiles"].is_object())
                    throw std::invalid_argument("Invalid inline UI files");
                for (auto it = value["inlineFiles"].begin(); it != value["inlineFiles"].end(); ++it)
                    if (!document.files.emplace(it.key(), it.value().get<std::string>()).second)
                        throw std::invalid_argument("Duplicate inline UI path");
            }
            for (const auto* key : {"images", "fonts"})
                if (value.contains(key)) {
                    const auto& table = value.at(key);
                    if (!table.is_object())
                        throw std::invalid_argument("Invalid UI binding table");
                    auto& destination =
                        std::string_view(key) == "images" ? document.imageIds : document.fontIds;
                    for (auto it = table.begin(); it != table.end(); ++it)
                        destination.emplace(it.key(), AuthorJson::Identity<AssetId>(it.value()));
                }
            if (value.contains("fontSources")) {
                const auto& table = value.at("fontSources");
                if (!table.is_object() || table.size() > 16)
                    throw std::invalid_argument("UI font source budget");
                for (auto it = table.begin(); it != table.end(); ++it) {
                    if (!IsUIRelativePath(it.key()) || document.fontIds.contains(it.key()) ||
                        document.imageIds.contains(it.key()) || document.files.contains(it.key()) ||
                        !it.value().is_string() || it.value().get_ref<const std::string&>().empty())
                        throw std::invalid_argument("Invalid UI font source binding");
                }
            }
            if (value.contains("values"))
                document.values = value.at("values").get<decltype(document.values)>();
            return ValidateUIDocument(document, error);
        }
    } // namespace

    size_t DocumentRecovery::AuthorLimit(std::string_view type) noexcept {
        return type == "animation_clip" || type == "mesh_skin_binding" ? MaxAnimationBytes : MaxAuthorBytes;
    }
    std::string_view DocumentRecovery::AuthorSuffix(std::string_view type) noexcept {
        for (const auto [key, suffix] : {std::pair{"scene", "scene"},
                                         {"prefab", "prefab"},
                                         {"material_graph", "matgraph"},
                                         {"behavior_graph", "behavior"},
                                         {"script", "script"},
                                         {"material_definition", "material"},
                                         {"material_instance", "material"},
                                         {"skeleton", "skeleton"},
                                         {"animation_clip", "animclip"},
                                         {"mesh_skin_binding", "skin"},
                                         {"blend_tree", "blendtree"},
                                         {"particle_effect", "particle"},
                                         {"collision_shape", "collision"},
                                         {"physics_material", "physics-material"},
                                         {"navmesh", "navmesh"},
                                         {"terrain", "terrain"},
                                         {"ui_document", "ui"}})
            if (type == key)
                return suffix;
        return {};
    }
    std::string DocumentRecovery::AuthorType(std::string_view extension, std::string_view source) {
        if (extension == ".material")
            return AuthorJson::Parse(source).value("kind", std::string{}) == "definition"
                       ? "material_definition"
                       : "material_instance";
        if (extension == ".lua")
            return "script";
        if (extension == ".animation")
            return "animation_clip";
        for (auto type : {"scene", "prefab", "material_graph", "behavior_graph", "script", "skeleton",
                          "animation_clip", "mesh_skin_binding", "blend_tree", "particle_effect",
                          "collision_shape", "physics_material", "navmesh", "terrain", "ui_document"})
            if (extension == "." + std::string(AuthorSuffix(type)))
                return type;
        return {};
    }
    bool DocumentRecovery::ValidateAuthorSource(std::string_view type, std::string_view source,
                                                std::string& error) try {
        if (AuthorSuffix(type).empty()) {
            error = "Original image, audio and font sources are read-only; use Content Browser to import or "
                    "preview them";
            return false;
        }
        if (source.size() > AuthorLimit(type)) {
            error = "Author source exceeds its domain byte budget";
            return false;
        }
        if (type == "scene") {
            SceneAsset value;
            return SceneSerializer::Read(source, value, error);
        }
        if (type == "prefab") {
            PrefabAsset value;
            return ReadPrefab(source, value, error);
        }
        if (type == "material_graph") {
            Tools::MaterialGraphSource value;
            return Tools::ReadMaterialGraph(source, value, error);
        }
        if (type == "behavior_graph") {
            BehaviorGraphAsset value;
            return ReadBehaviorGraph(source, value, error);
        }
        if (type == "material_definition") {
            std::shared_ptr<const MaterialDefinition> value;
            return ParseMaterialDefinition(source, value, error);
        }
        if (type == "material_instance" || type == "script") {
            error.clear();
            return true;
        }
        if (type == "skeleton") {
            SkeletonAsset value;
            return ReadSkeleton(source, value, error);
        }
        if (type == "animation_clip") {
            AnimationClip value;
            return ReadAnimationClip(source, value, error);
        }
        if (type == "mesh_skin_binding") {
            MeshSkinBinding value;
            return ReadMeshSkinBinding(source, value, error);
        }
        if (type == "blend_tree") {
            BlendTreeAsset value;
            return ReadBlendTree(source, value, error);
        }
        if (type == "particle_effect") {
            ParticleEffect value;
            return ReadParticleEffect(source, value, error);
        }
        if (type == "collision_shape") {
            CollisionShapeAsset value;
            return ReadCollisionShape(source, value, error);
        }
        if (type == "physics_material") {
            PhysicsMaterialAsset value;
            return ReadPhysicsMaterial(source, value, error);
        }
        if (type == "navmesh") {
            NavigationBuildInput value;
            return ReadNavigationSource(source, value, error);
        }
        if (type == "terrain") {
            TerrainSource value;
            return ReadTerrainSource(source, value, error);
        }
        if (type == "ui_document")
            return ValidateUISource(source, error);
        error = "Unknown author source domain";
        return false;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }

    bool DocumentRecovery::DecodeCheckpoint(std::string_view text, std::string& type, std::string& content,
                                            std::string& error) try {
        const auto value = AuthorJson::Parse(text, MaxRecoveryBytes);
        AuthorJson::Fields(value,
                           {"schemaVersion", "document", "type", "path", "content", "contentEncoding"});
        if (AuthorJson::Unsigned(value.at("schemaVersion")) != 1)
            throw std::invalid_argument("Unknown recovery schema");
        (void)AuthorJson::Identity<StableId>(value.at("document"));
        (void)value.at("path").get<std::string>();
        auto candidateType = value.at("type").get<std::string>();
        if (AuthorSuffix(candidateType).empty())
            throw std::invalid_argument("Unsupported recovery domain");
        const auto& encoded = value.at("content").get_ref<const std::string&>();
        const auto encoding = value.value("contentEncoding", std::string("utf8"));
        auto candidate = encoding == "base64" ? DecodeBase64(encoded, AuthorLimit(candidateType))
                         : encoding == "utf8"
                             ? encoded
                             : throw std::invalid_argument("Unknown recovery content encoding");
        if (candidate.size() > AuthorLimit(candidateType))
            throw std::invalid_argument("Recovery source exceeds its domain byte budget");
        type.swap(candidateType);
        content.swap(candidate);
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }

    bool DocumentRecovery::Read(const std::filesystem::path& path, std::string& output, std::string& error,
                                size_t maxBytes) try {
        if (!path.is_absolute()) {
            error = "Author document requires an absolute path";
            return false;
        }
        if (!maxBytes || maxBytes > MaxRecoveryBytes) {
            error = "Document read budget is invalid";
            return false;
        }
        VirtualFileSystem files;
        VirtualPath locator;
        std::vector<std::byte> bytes;
        const auto utf = path.filename().u8string();
        const std::string name(reinterpret_cast<const char*>(utf.data()), utf.size());
        if (!files.MountReadOnly("document", path.parent_path(), 0, error) ||
            !VirtualPath::Parse("document:/" + name, locator, error) ||
            !files.ReadFile(locator, maxBytes, bytes, error))
            return false;
        output.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }

    bool DocumentRecovery::Save(Document& doc, const std::filesystem::path& target,
                                const std::filesystem::path& contentRoot, std::string& error) try {
        if (!target.is_absolute() || !contentRoot.is_absolute()) {
            error = "Save requires resolved project and author paths";
            return false;
        }
        const auto root = std::filesystem::canonical(contentRoot);
        const auto parent = std::filesystem::canonical(target.parent_path());
        const auto destination = parent / target.filename();
        if (!IsPathWithinRoot(parent, root) || target.filename().empty() ||
            std::filesystem::is_symlink(std::filesystem::symlink_status(destination))) {
            error = "Author save target must be a regular path inside project Content";
            return false;
        }
        auto committedPath = destination;
        std::string text;
        const auto type = doc.TypeKey();
        const auto extension = Tools::AssetSourceExtension(destination);
        const auto suffix = AuthorSuffix(type);
        const bool matching = !suffix.empty() && (extension == "." + std::string(suffix) ||
                                                  (type == "script" && extension == ".lua") ||
                                                  (type == "animation_clip" && extension == ".animation"));
        if (!matching) {
            error = "Author file extension does not match its document type";
            return false;
        }
        if (!doc.Serialize(text, error))
            return false;
        if (!ValidateAuthorSource(type, text, error))
            return false;
        if (type == "script") {
            const auto previousExtension = Tools::AssetSourceExtension(doc.AuthorPath());
            if ((previousExtension == ".lua" && extension == ".script") ||
                (previousExtension == ".script" && extension == ".lua")) {
                error = "Save As must preserve Lua source or script document format; changing the extension "
                        "is not a conversion";
                return false;
            }
            ScriptAsset checked;
            if (extension == ".script") {
                if (!ReadScriptAsset(text, checked, error))
                    return false;
            } else {
                // 恢复的新文档可能没有原路径，也不能把完整 JSON 包装保存成 Lua 源码。
                std::string ignored;
                if (ReadScriptAsset(text, checked, ignored)) {
                    error = "Structured script documents must use the .script extension";
                    return false;
                }
            }
        }
        if (type == "prefab") {
            PrefabAsset checked;
            if (!ReadPrefab(text, checked, error))
                return false;
        }
        if (type == "material_graph") {
            Tools::MaterialGraphSource checked;
            if (!Tools::ReadMaterialGraph(text, checked, error))
                return false;
        }
        if (type == "behavior_graph") {
            BehaviorGraphAsset checked;
            if (!ReadBehaviorGraph(text, checked, error))
                return false;
        }
        if (type == "material_definition") {
            std::shared_ptr<const MaterialDefinition> checked;
            if (!ParseMaterialDefinition(text, checked, error))
                return false;
        }
        // Lua 草稿允许保存语法错误；Cook 与 Play 的 VM 编译仍严格拒绝它。
        if (!AtomicFileWriter::Write(destination, text, error))
            return false;
        doc.SetAuthorPath(std::move(committedPath));
        doc.History().MarkSaved();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }

    bool DocumentRecovery::Checkpoint(const Document& doc, const std::filesystem::path& root,
                                      std::string& error) try {
        if (!root.is_absolute()) {
            error = "Recovery directory must be absolute";
            return false;
        }
        if (std::filesystem::weakly_canonical(root) != root.lexically_normal()) {
            error = "Recovery directory must not resolve through a link or alias";
            return false;
        }
        std::string text;
        if (!doc.Serialize(text, error))
            return false;
        if (AuthorSuffix(doc.TypeKey()).empty() || text.size() > AuthorLimit(doc.TypeKey())) {
            error = "Author draft domain or recovery budget rejected";
            return false;
        }
        std::filesystem::create_directories(root);
        const auto utf = doc.AuthorPath().u8string();
        const bool binary = text.starts_with("CANI1") || text.starts_with("CSKN") ||
                            text.size() > MaxAuthorBytes || !IsUIUtf8(text);
        const AuthorJson::Json envelope{
            {"schemaVersion", 1},
            {"document", doc.Id().ToString()},
            {"type", doc.TypeKey()},
            {"path", std::string(reinterpret_cast<const char*>(utf.data()), utf.size())},
            {"content", binary ? EncodeBase64(text) : std::move(text)},
            {"contentEncoding", binary ? "base64" : "utf8"}};
        const auto encoded = envelope.dump();
        if (encoded.size() > MaxRecoveryBytes) {
            error = "Recovery envelope exceeds the read budget";
            return false;
        }
        return AtomicFileWriter::Write(root / (doc.Id().ToString() + ".recovery.json"), encoded, error);
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}
