#include "catpch.h"
#include "Project/ProjectConfig.h"
#include "Core/IO/AtomicFileWriter.h"
#include "Core/PlatformServices.h"
#include "Core/Serialization/JsonArchive.h"

namespace Catalpa::Tools {
    namespace {
        constexpr size_t MaxProjectBytes = 1024 * 1024;
        constexpr size_t MaxMetadataBytes = 4 * 1024 * 1024;

        bool Fail(std::string& error, std::string_view path, std::string_view message) {
            error = (path.empty() ? "/" : std::string(path)) + ": " + std::string(message);
            return false;
        }
        bool Fields(const ArchiveValue& value, std::initializer_list<std::string_view> allowed,
            std::string_view path, std::string& error) {
            const auto* object = std::get_if<ArchiveValue::Object>(&value.data);
            if (!object) return Fail(error, path, "expected object");
            for (const auto& [key, child] : *object) {
                (void)child;
                if (std::find(allowed.begin(), allowed.end(), key) == allowed.end())
                    return Fail(error, ArchiveChildPath(path, key), "unknown field");
            }
            return true;
        }
        bool Text(const ArchiveValue& object, std::string_view field, std::string& output,
            std::string_view path, std::string& error, size_t maxLength = 4096) {
            const auto* value = object.Find(field);
            const auto* text = value ? std::get_if<std::string>(&value->data) : nullptr;
            if (!text || text->empty() || text->size() > maxLength || text->find('\0') != std::string::npos)
                return Fail(error, ArchiveChildPath(path, field), "expected nonempty bounded text without NUL");
            output = *text;
            return true;
        }
        bool Version(const ArchiveValue& value, std::string_view path, std::string& error) {
            ArchiveError diagnostic;
            if (RequireArchiveVersion(value, 1, diagnostic, path)) return true;
            error = diagnostic.ToString();
            return false;
        }
        std::string PathText(const std::filesystem::path& path) {
            const auto utf8 = path.generic_u8string();
            return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
        }
        bool ResolvePath(const std::filesystem::path& base, std::string_view text,
            std::filesystem::path& output, std::string& error, std::string_view field) {
            if (text.empty() || text.size() > 4096 || text.find('\0') != std::string_view::npos)
                return Fail(error, field, "invalid filesystem path text");
            const auto raw = std::filesystem::u8path(text.begin(), text.end());
            // C:relative 和根相对路径不是可靠作者路径，也不能隐式借用 cwd。
            if (!raw.is_absolute() && (raw.has_root_name() || raw.has_root_directory()))
                return Fail(error, field, "drive-relative and root-relative paths are unsupported");
            // 允许作者相对目录的 . / ..，但在 canonical 之前拒绝 ADS、DOS设备、尾点/空格等别名。
            // 否则 Windows 归一化可能掩盖一个会写入源目录的输出路径。
            const auto nativeText = PathText(raw);
            if (nativeText.starts_with("//?/") || nativeText.starts_with("//./"))
                return Fail(error, field, "device namespace paths are unsupported");
            for (const auto& part : raw.relative_path()) {
                const auto segment = PathText(part);
                if (segment == "." || segment == "..") continue;
                VirtualPath checked;
                if (!VirtualPath::Parse("path:/" + segment, checked, error))
                    return Fail(error, field, "path contains a noncanonical or reserved component");
            }
            std::error_code code;
            auto canonical = std::filesystem::weakly_canonical(raw.is_absolute() ? raw : base / raw, code);
            if (code || !canonical.is_absolute()) return Fail(error, field, "cannot resolve absolute canonical path");
            output = std::move(canonical);
            return true;
        }
        bool ContainsPath(const std::filesystem::path& root, const std::filesystem::path& candidate) {
            return IsPathWithinRoot(candidate, root);
        }
        bool Overlaps(const std::filesystem::path& a, const std::filesystem::path& b) {
            return ContainsPath(a, b) || ContainsPath(b, a);
        }
        bool ReadDocument(const std::filesystem::path& absoluteFile, size_t maxBytes,
            ArchiveValue& output, std::string& error) {
            VirtualFileSystem input;
            if (!input.MountReadOnly("input", absoluteFile.parent_path(), 0, error)) return false;
            VirtualPath locator;
            if (!VirtualPath::Parse("input:/" + PathText(absoluteFile.filename()), locator, error)) return false;
            std::vector<std::byte> bytes;
            if (!input.ReadFile(locator, maxBytes, bytes, error)) return false;
            ArchiveLimits limits; limits.maxBytes = maxBytes;
            ArchiveError diagnostic;
            const auto text = bytes.empty() ? std::string_view{} :
                std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            std::unique_ptr<ArchiveValue> parsed;
            if (!JsonArchive::Parse(text, parsed, diagnostic, limits)) {
                error = PathText(absoluteFile) + diagnostic.ToString();
                return false;
            }
            // 私有读取只填充尚未发布的构建候选；值移动若抛异常，由外层加载事务收束。
            output = std::move(*parsed);
            return true;
        }
        bool RootBoundaries(const ProjectConfig& config, std::string& error) {
            for (const auto* output : { &config.derivedDataRoot, &config.cookedRoot }) {
                const std::string_view field = output == &config.derivedDataRoot ? "/derivedDataRoot" : "/cookedRoot";
                std::error_code code;
                const auto status = std::filesystem::status(*output, code);
                if (code && code != std::errc::no_such_file_or_directory)
                    return Fail(error, field, "cannot inspect derived output root");
                if (std::filesystem::exists(status) && !std::filesystem::is_directory(status))
                    return Fail(error, field, "existing output root must be a directory");
            }
            if (Overlaps(config.derivedDataRoot, config.cookedRoot))
                return Fail(error, "/cookedRoot", "derived-data and cooked roots must be disjoint");
            const auto check = [&](const std::filesystem::path& source, std::string_view field) {
                if (Overlaps(source, config.derivedDataRoot) || Overlaps(source, config.cookedRoot))
                    return Fail(error, field, "source and derived output roots overlap");
                return true;
            };
            if (!check(config.contentRoot, "/contentRoot")) return false;
            for (const auto& [name, source] : config.externalSourceRoots)
                if (!check(source, ArchiveChildPath("/externalSourceRoots", name))) return false;
            // 保存配置或配置被输出覆盖同样不可接受，即使它在内容根之外。
            if (ContainsPath(config.derivedDataRoot, config.projectFile) || ContainsPath(config.cookedRoot, config.projectFile)
                || ContainsPath(config.derivedDataRoot, config.buildProfilesFile) || ContainsPath(config.cookedRoot, config.buildProfilesFile))
                return Fail(error, "/derivedDataRoot", "output root contains an author configuration file");
            return true;
        }

        bool ParseConfig(const ArchiveValue& document, const std::filesystem::path& projectFile,
            ProjectConfig& output, std::string& error) {
            if (!Fields(document, { "schemaVersion", "id", "name", "contentRoot", "externalSourceRoots",
                "derivedDataRoot", "cookedRoot", "metadata", "buildProfiles", "buildProfile",
                "targetPlatform", "buildConfiguration", "startupScene", "modules" }, {}, error)
                || !Version(document, {}, error)) return false;
            ProjectConfig config; config.projectFile = projectFile;
            std::string id;
            if (!Text(document, "id", id, {}, error, 36) || !StableId::TryParse(id, config.id))
                return Fail(error, "/id", "expected nonzero stable identifier");
            if (!Text(document, "name", config.name, {}, error, 128)
                || !Text(document, "metadata", config.metadataRelativePath, {}, error)
                || !Text(document, "buildProfile", config.buildProfile, {}, error, 64)
                || !Text(document, "targetPlatform", config.targetPlatform, {}, error, 32)
                || !Text(document, "buildConfiguration", config.buildConfiguration, {}, error, 32)) return false;
            if (config.targetPlatform != "windows") return Fail(error, "/targetPlatform", "only windows is supported");
            if (config.buildConfiguration != "Debug" && config.buildConfiguration != "Release" && config.buildConfiguration != "Dist")
                return Fail(error, "/buildConfiguration", "expected Debug, Release or Dist");
            AssetType profileName;
            if (!AssetType::TryParse(config.buildProfile, profileName)) return Fail(error, "/buildProfile", "invalid profile key");
            const auto base = projectFile.parent_path();
            auto readPath = [&](std::string_view field, std::filesystem::path& path) {
                std::string text;
                return Text(document, field, text, {}, error) && ResolvePath(base, text, path, error, ArchiveChildPath({}, field));
            };
            if (!readPath("contentRoot", config.contentRoot) || !readPath("derivedDataRoot", config.derivedDataRoot)
                || !readPath("cookedRoot", config.cookedRoot) || !readPath("buildProfiles", config.buildProfilesFile)) return false;
            VirtualPath metadata;
            if (!VirtualPath::Parse("content:/" + config.metadataRelativePath, metadata, error)
                || metadata.RelativePath() != config.metadataRelativePath)
                return Fail(error, "/metadata", "expected canonical relative content path");
            const auto* rootsValue = document.Find("externalSourceRoots");
            const auto* roots = rootsValue ? std::get_if<ArchiveValue::Object>(&rootsValue->data) : nullptr;
            if (!roots || roots->size() > 64) return Fail(error, "/externalSourceRoots", "expected at most 64 named roots");
            for (const auto& [name, value] : *roots) {
                const auto field = ArchiveChildPath("/externalSourceRoots", name);
                VirtualPath probe;
                if (name == "content" || name == "cooked" || name == "input"
                    || !VirtualPath::Parse(name + ":/probe", probe, error))
                    return Fail(error, field, "invalid or reserved source root name");
                const auto* text = std::get_if<std::string>(&value.data);
                std::filesystem::path path;
                if (!text || !ResolvePath(base, *text, path, error, field)) return Fail(error, field, "invalid source path");
                config.externalSourceRoots.emplace(name, std::move(path));
            }
            if (const auto* scene = document.Find("startupScene"); scene && !std::holds_alternative<std::nullptr_t>(scene->data)) {
                const auto* text = std::get_if<std::string>(&scene->data); AssetId sceneId;
                if (!text || !AssetId::TryParse(*text, sceneId)) return Fail(error, "/startupScene", "expected stable ID or null");
                config.startupScene = sceneId;
            }
            if (const auto* modulesValue = document.Find("modules")) {
                const auto* modules = std::get_if<ArchiveValue::Array>(&modulesValue->data);
                if (!modules || modules->size() > 64) return Fail(error, "/modules", "expected at most 64 module keys");
                std::set<std::string> unique;
                for (const auto& module : *modules) {
                    const auto* text = std::get_if<std::string>(&module.data); AssetType key;
                    if (!text || !AssetType::TryParse(*text, key) || !unique.insert(*text).second)
                        return Fail(error, "/modules", "module keys must be valid and unique");
                    config.modules.push_back(*text);
                }
            }
            if (!RootBoundaries(config, error)) return false;
            output = std::move(config);
            return true;
        }

        bool BuildProfile(const ProjectConfig& config, ArchiveValue& output, std::string& error) {
            ArchiveValue document;
            if (!ReadDocument(config.buildProfilesFile, MaxProjectBytes, document, error)
                || !Fields(document, { "schemaVersion", "profiles" }, {}, error) || !Version(document, {}, error)) return false;
            const auto* profiles = document.Find("profiles");
            const auto* object = profiles ? std::get_if<ArchiveValue::Object>(&profiles->data) : nullptr;
            if (!object || object->size() > 64) return Fail(error, "/profiles", "expected at most 64 profiles");
            for (const auto& [name, profile] : *object) {
                AssetType key;
                const auto path = ArchiveChildPath("/profiles", name);
                if (!AssetType::TryParse(name, key)) return Fail(error, path, "invalid profile key");
                if (!Fields(profile, { "platform", "architecture", "shaderTarget", "shaderProfile", "targetEnvironment", "assetPipeline" }, path, error)) return false;
                if (const auto* pipeline = profile.Find("assetPipeline")) {
                    const auto* text = std::get_if<std::string>(&pipeline->data);
                    if (!text || *text != "static-model-v1")
                        return Fail(error, ArchiveChildPath(path, "assetPipeline"),
                                    "unsupported asset pipeline; expected static-model-v1");
                }
                std::string platform, architecture, target, shaderProfile, environment;
                if (!Text(profile, "platform", platform, path, error, 32)
                    || !Text(profile, "architecture", architecture, path, error, 32)
                    || !Text(profile, "shaderTarget", target, path, error, 32)
                    || !Text(profile, "shaderProfile", shaderProfile, path, error, 32)
                    || !Text(profile, "targetEnvironment", environment, path, error, 32)) return false;
                if (platform != "windows" || architecture != "x86_64" || target != "spirv"
                    || shaderProfile != "spirv_1_5" || environment != "vulkan1.3")
                    return Fail(error, path, "unsupported build target; this version supports windows/x86_64/spirv_1_5/vulkan1.3");
            }
            const auto selected = object->find(config.buildProfile);
            if (selected == object->end()) return Fail(error, "/buildProfile", "selected build profile is missing");
            output = selected->second;
            return true;
        }

        bool ReadMetadata(ProjectSession& session, std::string& error) {
            VirtualPath metadataPath;
            if (!VirtualPath::Parse("content:/" + session.config.metadataRelativePath, metadataPath, error)) return false;
            std::vector<std::byte> bytes;
            if (!session.files.ReadFile(metadataPath, MaxMetadataBytes, bytes, error)) return false;
            ArchiveError diagnostic;
            const auto text = bytes.empty() ? std::string_view{} :
                std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            std::unique_ptr<ArchiveValue> parsed;
            if (!JsonArchive::Parse(text, parsed, diagnostic)) {
                error = "metadata" + diagnostic.ToString(); return false;
            }
            // session是LoadProject独占的候选，失败不会改变调用方的旧会话。
            session.metadata = std::move(*parsed);
            if (!Fields(session.metadata, { "schemaVersion", "assets" }, {}, error) || !Version(session.metadata, {}, error)) return false;
            const auto* assetsValue = session.metadata.Find("assets");
            const auto* assets = assetsValue ? std::get_if<ArchiveValue::Array>(&assetsValue->data) : nullptr;
            if (!assets || assets->size() > 4096) return Fail(error, "/assets", "expected at most 4096 asset records");
            std::vector<AssetRecord> records; records.reserve(assets->size());
            for (size_t index = 0; index < assets->size(); ++index) {
                const auto& asset = (*assets)[index];
                const auto path = ArchiveChildPath("/assets", std::to_string(index));
                if (!Fields(asset, { "id", "type", "path", "kind", "version", "dependencies", "contentHash", "source", "importSettings" }, path, error)) return false;
                std::string id, type, locator, kind;
                if (!Text(asset, "id", id, path, error, 36) || !Text(asset, "type", type, path, error, 128)
                    || !Text(asset, "path", locator, path, error) || !Text(asset, "kind", kind, path, error, 16)) return false;
                AssetRecord record;
                if (!AssetId::TryParse(id, record.id)) return Fail(error, ArchiveChildPath(path, "id"), "invalid asset ID");
                if (!AssetType::TryParse(type, record.type)) return Fail(error, ArchiveChildPath(path, "type"), "invalid asset type");
                if (!VirtualPath::Parse(locator, record.locator.path, error)) return Fail(error, ArchiveChildPath(path, "path"), error);
                if (kind == "source") {
                    record.locator.kind = AssetLocationKind::Source;
                    if (record.locator.path.MountName() != "content"
                        && !session.config.externalSourceRoots.contains(record.locator.path.MountName()))
                        return Fail(error, ArchiveChildPath(path, "path"), "source mount is not declared by project");
                } else if (kind == "cooked") {
                    record.locator.kind = AssetLocationKind::Cooked;
                    if (record.locator.path.MountName() != "cooked") return Fail(error, ArchiveChildPath(path, "path"), "cooked locator must use cooked mount");
                } else return Fail(error, ArchiveChildPath(path, "kind"), "expected source or cooked");
                const auto* version = asset.Find("version");
                if (!version || !version->ReadUnsigned(record.version) || record.version == 0)
                    return Fail(error, ArchiveChildPath(path, "version"), "expected positive integer content version");
                if (const auto* hash = asset.Find("contentHash")) {
                    const auto* text = std::get_if<std::string>(&hash->data);
                    if (!text) return Fail(error, ArchiveChildPath(path, "contentHash"), "expected SHA-256 text");
                    record.contentHash = *text;
                }
                if (const auto* dependenciesValue = asset.Find("dependencies")) {
                    const auto* dependencies = std::get_if<ArchiveValue::Array>(&dependenciesValue->data);
                    if (!dependencies || dependencies->size() > 1024) return Fail(error, ArchiveChildPath(path, "dependencies"), "expected at most 1024 dependency IDs");
                    for (const auto& value : *dependencies) {
                        const auto* text = std::get_if<std::string>(&value.data); AssetId dependency;
                        if (!text || !AssetId::TryParse(*text, dependency)) return Fail(error, ArchiveChildPath(path, "dependencies"), "invalid dependency ID");
                        record.dependencies.push_back(dependency);
                    }
                }
                if (const auto* source = asset.Find("source")) {
                    const auto sourcePath = ArchiveChildPath(path, "source");
                    if (!Fields(*source, { "uri", "license", "note" }, sourcePath, error)) return false;
                    std::string uri, license;
                    if (!Text(*source, "uri", uri, sourcePath, error) || !Text(*source, "license", license, sourcePath, error)) return false;
                    if (source->Find("note")) { std::string note; if (!Text(*source, "note", note, sourcePath, error)) return false; }
                }
                if (const auto* settings = asset.Find("importSettings"); settings && !std::holds_alternative<ArchiveValue::Object>(settings->data))
                    return Fail(error, ArchiveChildPath(path, "importSettings"), "expected preserved settings object");
                records.push_back(std::move(record));
            }
            if (!session.assets.Replace(records, error)) { error = "metadata: " + error; return false; }
            for (const auto& issue : session.assets.GetMissingDependencies())
                session.diagnostics.push_back("asset " + issue.asset.ToString() + " references missing asset " + issue.dependency.ToString());
            if (session.config.startupScene && !session.assets.Find(*session.config.startupScene))
                session.diagnostics.push_back("startup scene ID is not registered; scene loading is not performed by project configuration");
            return true;
        }

        ArchiveValue StringValue(std::string text) { ArchiveValue value; value.data = std::move(text); return value; }
        ArchiveValue ConfigDocument(const ProjectConfig& config) {
            ArchiveValue document; document.data = ArchiveValue::Object{};
            auto& fields = std::get<ArchiveValue::Object>(document.data);
            fields["schemaVersion"].data = uint64_t{ 1 };
            fields["id"] = StringValue(config.id.ToString()); fields["name"] = StringValue(config.name);
            const auto relative = [&](const std::filesystem::path& target) {
                const auto path = target.lexically_relative(config.projectFile.parent_path());
                return PathText(path.empty() ? target : path);
            };
            fields["contentRoot"] = StringValue(relative(config.contentRoot));
            fields["derivedDataRoot"] = StringValue(relative(config.derivedDataRoot));
            fields["cookedRoot"] = StringValue(relative(config.cookedRoot));
            fields["metadata"] = StringValue(config.metadataRelativePath);
            fields["buildProfiles"] = StringValue(relative(config.buildProfilesFile));
            fields["buildProfile"] = StringValue(config.buildProfile);
            fields["targetPlatform"] = StringValue(config.targetPlatform);
            fields["buildConfiguration"] = StringValue(config.buildConfiguration);
            fields["externalSourceRoots"].data = ArchiveValue::Object{};
            auto& roots = std::get<ArchiveValue::Object>(fields["externalSourceRoots"].data);
            for (const auto& [name, path] : config.externalSourceRoots) roots[name] = StringValue(relative(path));
            fields["startupScene"] = config.startupScene ? StringValue(config.startupScene->ToString()) : ArchiveValue{};
            fields["modules"].data = ArchiveValue::Array{};
            auto& modules = std::get<ArchiveValue::Array>(fields["modules"].data);
            for (const auto& module : config.modules) modules.push_back(StringValue(module));
            return document;
        }
    }

    bool LoadProject(const std::filesystem::path& absoluteProjectFile, std::unique_ptr<ProjectSession>& output, std::string& error) {
        error.clear();
        try {
            if (!absoluteProjectFile.is_absolute()) return Fail(error, "/projectFile", "explicit absolute project path is required");
            std::filesystem::path projectFile;
            if (!ResolvePath(absoluteProjectFile.parent_path(), PathText(absoluteProjectFile), projectFile, error, "/projectFile")) return false;
            ArchiveValue document;
            auto pending = std::make_unique<ProjectSession>();
            auto& candidate = *pending;
            if (!ReadDocument(projectFile, MaxProjectBytes, document, error)
                || !ParseConfig(document, projectFile, candidate.config, error)
                || !BuildProfile(candidate.config, candidate.selectedBuildProfile, error)) return false;
            if (!candidate.files.MountReadOnly("content", candidate.config.contentRoot, 0, error)) return false;
            for (const auto& [name, path] : candidate.config.externalSourceRoots)
                if (!candidate.files.MountReadOnly(name, path, 0, error)) return false;
            std::error_code code;
            const bool cookedExists = std::filesystem::is_directory(candidate.config.cookedRoot, code);
            if (code && code != std::errc::no_such_file_or_directory) return Fail(error, "/cookedRoot", "cannot inspect cooked root");
            if (cookedExists && !candidate.files.MountReadOnly("cooked", candidate.config.cookedRoot, 0, error)) return false;
            if (!ReadMetadata(candidate, error)) return false;
            // 所有可能分配的构造/解析已经完成；发布只交换所有权，随后释放旧会话。
            static_assert(noexcept(output.swap(pending)), "Project publication must not partially fail");
            output.swap(pending);
            return true;
        } catch (const std::exception& exception) { error = exception.what(); return false; }
    }

    bool SaveProjectConfig(const ProjectConfig& config, std::string& error) {
        error.clear();
        try {
            if (!config.projectFile.is_absolute() || !config.contentRoot.is_absolute() || !config.derivedDataRoot.is_absolute()
                || !config.cookedRoot.is_absolute() || !config.buildProfilesFile.is_absolute())
                return Fail(error, "/projectFile", "save requires resolved absolute paths");
            for (const auto& [name, path] : config.externalSourceRoots)
                if (!path.is_absolute()) return Fail(error, ArchiveChildPath("/externalSourceRoots", name), "save requires resolved absolute path");
            std::filesystem::path canonicalProjectFile;
            if (!ResolvePath(config.projectFile.parent_path(), PathText(config.projectFile), canonicalProjectFile, error, "/projectFile")) return false;
            // 不跟随作者保存目标的 dot/junction/leaf link 别名；后续始终使用这一已验证目标。
            if (config.projectFile != canonicalProjectFile)
                return Fail(error, "/projectFile", "save target must already be canonical; reload the resolved author project first");
            ProjectConfig normalized = config;
            normalized.projectFile = canonicalProjectFile;
            auto normalizeRoot = [&](std::filesystem::path& path, std::string_view field) {
                return ResolvePath(canonicalProjectFile.parent_path(), PathText(path), path, error, field);
            };
            if (!normalizeRoot(normalized.contentRoot, "/contentRoot")
                || !normalizeRoot(normalized.derivedDataRoot, "/derivedDataRoot")
                || !normalizeRoot(normalized.cookedRoot, "/cookedRoot")
                || !normalizeRoot(normalized.buildProfilesFile, "/buildProfiles")) return false;
            for (auto& [name, path] : normalized.externalSourceRoots)
                if (!normalizeRoot(path, ArchiveChildPath("/externalSourceRoots", name))) return false;
            const auto document = ConfigDocument(normalized);
            ProjectConfig checked;
            if (!ParseConfig(document, canonicalProjectFile, checked, error)) return false;
            // 显式配置保存也不能覆盖项目声明的原件根中的文件。
            if (ContainsPath(checked.contentRoot, checked.projectFile))
                return Fail(error, "/projectFile", "project save target is within a read-only content root");
            for (const auto& [name, root] : checked.externalSourceRoots) {
                (void)name;
                if (ContainsPath(root, checked.projectFile)) return Fail(error, "/projectFile", "project save target is within a read-only source root");
            }
            std::string text; ArchiveError diagnostic; ArchiveLimits limits; limits.maxBytes = MaxProjectBytes;
            if (!JsonArchive::Write(ConfigDocument(checked), text, diagnostic, limits)) { error = diagnostic.ToString(); return false; }
            return AtomicFileWriter::Write(checked.projectFile, std::string_view(text), error);
        } catch (const std::exception& exception) { error = exception.what(); return false; }
    }
}
