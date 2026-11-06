#include "Documents/DocumentRecovery.h"
#include "Core/IO/AtomicFileWriter.h"
#include "Core/IO/VirtualFileSystem.h"
#include "Core/PlatformServices.h"
#include "Core/Serialization/AuthorJson.h"
#include "AssetPipeline/AssetImportRegistry.h"
#include "Scene/PrefabAsset.h"
#include "Materials/MaterialValidation.h"
#include "MaterialGraph/MaterialGraphSource.h"
#include "Scripting/BehaviorGraphAsset.h"
#include "Scripting/ScriptAsset.h"

namespace Catalpa::Editor {
    bool DocumentRecovery::Read(const std::filesystem::path& path, std::string& output, std::string& error,
        size_t maxBytes) try {
        if (!path.is_absolute()) { error = "Author document requires an absolute path"; return false; }
        if (!maxBytes || maxBytes > MaxRecoveryBytes) { error = "Document read budget is invalid"; return false; }
        VirtualFileSystem files;
        VirtualPath locator;
        std::vector<std::byte> bytes;
        const auto utf = path.filename().u8string();
        const std::string name(reinterpret_cast<const char*>(utf.data()), utf.size());
        if (!files.MountReadOnly("document", path.parent_path(), 0, error) ||
            !VirtualPath::Parse("document:/" + name, locator, error) ||
            !files.ReadFile(locator, maxBytes, bytes, error)) return false;
        output.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        error.clear();
        return true;
    } catch (const std::exception& exception) { error = exception.what(); return false; }

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
        const bool matching = (type == "scene" && extension == ".scene") ||
            (type == "prefab" && extension == ".prefab") ||
            (type == "script" && (extension == ".lua" || extension == ".script")) ||
            (type == "material_graph" && extension == ".matgraph") ||
            (type == "behavior_graph" && extension == ".behavior") ||
            ((type == "material_definition" || type == "material_instance") && extension == ".material");
        if (!matching) { error = "Author file extension does not match its document type"; return false; }
        if (!doc.Serialize(text, error)) return false;
        if (text.size() > MaxAuthorBytes) { error = "Author document exceeds the save budget"; return false; }
        if (type == "script") {
            const auto previousExtension = Tools::AssetSourceExtension(doc.AuthorPath());
            if ((previousExtension == ".lua" && extension == ".script") ||
                (previousExtension == ".script" && extension == ".lua")) {
                error = "Save As must preserve Lua source or script document format; changing the extension is not a conversion";
                return false;
            }
            ScriptAsset checked;
            if (extension == ".script") {
                if (!ReadScriptAsset(text, checked, error)) return false;
            } else {
                // 恢复的新文档可能没有原路径，也不能把完整 JSON 包装保存成 Lua 源码。
                std::string ignored;
                if (ReadScriptAsset(text, checked, ignored)) {
                    error = "Structured script documents must use the .script extension";
                    return false;
                }
            }
        }
        if (type == "prefab") { PrefabAsset checked; if (!ReadPrefab(text, checked, error)) return false; }
        if (type == "material_graph") { Tools::MaterialGraphSource checked; if (!Tools::ReadMaterialGraph(text, checked, error)) return false; }
        if (type == "behavior_graph") { BehaviorGraphAsset checked; if (!ReadBehaviorGraph(text, checked, error)) return false; }
        if (type == "material_definition") {
            std::shared_ptr<const MaterialDefinition> checked;
            if (!ParseMaterialDefinition(text, checked, error)) return false;
        }
        // Lua 草稿允许保存语法错误；Cook 与 Play 的 VM 编译仍严格拒绝它。
        if (!AtomicFileWriter::Write(destination, text, error)) return false;
        doc.SetAuthorPath(std::move(committedPath));
        doc.History().MarkSaved();
        return true;
    } catch (const std::exception& exception) { error = exception.what(); return false; }

    bool DocumentRecovery::Checkpoint(const Document& doc, const std::filesystem::path& root,
                                       std::string& error) try {
        if (!root.is_absolute()) { error = "Recovery directory must be absolute"; return false; }
        if (std::filesystem::weakly_canonical(root) != root.lexically_normal()) {
            error = "Recovery directory must not resolve through a link or alias";
            return false;
        }
        std::string text;
        if (!doc.Serialize(text, error)) return false;
        if (text.size() > MaxAuthorBytes) { error = "Author draft exceeds the recovery budget"; return false; }
        std::filesystem::create_directories(root);
        const auto utf = doc.AuthorPath().u8string();
        const AuthorJson::Json envelope{{"schemaVersion", 1}, {"document", doc.Id().ToString()}, {"type", doc.TypeKey()},
            {"path", std::string(reinterpret_cast<const char*>(utf.data()), utf.size())}, {"content", text}};
        const auto encoded = envelope.dump();
        if (encoded.size() > MaxRecoveryBytes) { error = "Recovery envelope exceeds the read budget"; return false; }
        return AtomicFileWriter::Write(root / (doc.Id().ToString() + ".recovery.json"), encoded, error);
    } catch (const std::exception& exception) { error = exception.what(); return false; }
}
