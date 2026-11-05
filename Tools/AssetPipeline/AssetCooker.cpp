#include "catpch.h"
#include "AssetPipeline/AssetCooker.h"
#include "AssetPipeline/AssetBuildCache.h"
#include "AssetPipeline/AssetDependencyScanner.h"
#include "AssetPipeline/AssetImportRegistry.h"
#include "AssetPipeline/AssetArchiveWriter.h"
#include "AssetPipeline/Cookers/MeshCooker.h"
#include "AssetPipeline/Cookers/ModelCooker.h"
#include "AssetPipeline/Cookers/TextureCooker.h"
#include "AssetPipeline/Cookers/MaterialCooker.h"
#include "AssetPipeline/Importers/MaterialImporter.h"
#include "Assets/AssetDependencyGraph.h"
#include "Assets/AssetManifest.h"
#include "Assets/AssetContainerReader.h"
#include "Core/IO/AtomicFileWriter.h"
#include "Core/PlatformServices.h"
#include "Shaders/ShaderLibrary.h"
#include <assimp/version.h>
#include <fastgltf/types.hpp>
#include <nlohmann/json.hpp>
#define CATALPA_IMPORT_STRINGIFY_IMPL(value) #value
#define CATALPA_IMPORT_STRINGIFY(value) CATALPA_IMPORT_STRINGIFY_IMPL(value)

namespace Catalpa::Tools {
    namespace {
        using Json = nlohmann::json;

        bool Read(const std::filesystem::path& file, size_t limit, std::vector<std::byte>& bytes,
                  std::string& error) {
            VirtualFileSystem files;
            VirtualPath path;
            return files.MountReadOnly("input", file.parent_path(), 0, error) &&
                   VirtualPath::Parse("input:/" + AssetPathText(file.filename()), path, error) &&
                   files.ReadFile(path, limit, bytes, error);
        }

        // 同一输出目录一次只允许一个发布者。析构仅释放本次取得的锁。
        struct BuildLock {
            std::filesystem::path path;
            bool owned = false;

            ~BuildLock() {
                if (owned) {
                    std::error_code ignored;
                    std::filesystem::remove(path, ignored);
                }
            }
        };

        bool WriteDerived(const std::filesystem::path& root, const std::filesystem::path& relative,
                          std::span<const std::byte> bytes, bool immutable, std::string& error) {
            const auto target = root / relative;
            if (!IsPathWithinRoot(std::filesystem::weakly_canonical(target.parent_path()), root)) {
                error = "Derived ancestor escaped output root";
                return false;
            }
            std::filesystem::create_directories(target.parent_path());
            if (!IsPathWithinRoot(std::filesystem::canonical(target.parent_path()), root)) {
                error = "Derived parent escaped output root";
                return false;
            }
            if (immutable && std::filesystem::exists(target)) {
                std::vector<std::byte> old;
                if (!Read(target, 96 * 1024 * 1024, old, error))
                    return false;
                if (old.size() != bytes.size() || !std::equal(old.begin(), old.end(), bytes.begin())) {
                    error = "Immutable derived artifact has different bytes: " + AssetPathText(target);
                    return false;
                }
                return true;
            }
            return AtomicFileWriter::Write(target, bytes, error);
        }

        // 按直接依赖顺序编码。每个领域对象的容器记录确定后，才能固定引用者的版本。
        bool CookDomainProducts(AssetBuildContext& context, AssetImportResult& imported,
                                const AssetRecord& shader, std::vector<AssetBuildProduct>& products,
                                std::vector<AssetSourceMapping>& mappings, std::string& error) {
            std::map<AssetId, AssetRecord> ready;
            ready.emplace(products.front().record.id, products.front().record);
            size_t encodedBytes = products.front().bytes.size();
            const auto append = [&](AssetBuildProduct product) {
                if (product.bytes.size() >
                    context.request.settings.maxWorkingBytes -
                        std::min(encodedBytes, context.request.settings.maxWorkingBytes)) {
                    error = "Encoded batch exceeds configured working budget";
                    return false;
                }
                encodedBytes += product.bytes.size();
                if (!ready.emplace(product.record.id, product.record).second) {
                    error = "Duplicate imported identity";
                    return false;
                }
                products.push_back(std::move(product));
                return true;
            };
            for (auto& asset : imported.assets) {
                if (!context.CheckCancelled(error))
                    return false;
                std::vector<std::byte> payload;
                std::string_view type;
                if (const auto* mesh = std::get_if<MeshAsset>(&asset.value)) {
                    type = "mesh";
                    if (!MeshCooker::Encode(*mesh, payload, error))
                        return false;
                } else if (const auto* image = std::get_if<ImageAsset>(&asset.value)) {
                    type = "image";
                    if (!TextureCooker::Encode(*image, payload, error))
                        return false;
                } else
                    continue;
                AssetBuildProduct product;
                if (!AssetArchiveWriter::Encode(asset.id, type, payload, {}, product, error) ||
                    !append(std::move(product)))
                    return false;
                // 后续阶段只读取材质与模型，几何和图像的源缓冲可在编码后立即释放。
                if (type == "image")
                    asset.value = ImageAsset{};
                else
                    asset.value = MeshAsset{};
            }
            for (const auto& asset : imported.assets)
                if (std::holds_alternative<ImportedMaterial>(asset.value)) {
                    if (!context.CheckCancelled(error))
                        return false;
                    std::vector<AssetBuildProduct> materials;
                    if (!MaterialCooker::Cook(context, asset, shader, ready, materials, error))
                        return false;
                    for (auto& product : materials)
                        if (!append(std::move(product)))
                            return false;
                    mappings.push_back({MaterialImporter::PreviewId(context, asset.sourceKey),
                                        asset.sourceKey + "/preview", asset.name, "material_preview"});
                }
            for (const auto& asset : imported.assets)
                if (const auto* model = std::get_if<ModelAsset>(&asset.value)) {
                    if (!context.CheckCancelled(error))
                        return false;
                    std::set<AssetId> ids;
                    std::vector<AssetRecord> dependencies;
                    for (const auto& instance : model->meshes) {
                        ids.insert(instance.mesh.id);
                        for (const auto material : instance.materials)
                            ids.insert(material.id);
                    }
                    for (auto id : ids) {
                        const auto it = ready.find(id);
                        if (it == ready.end()) {
                            error = "Model dependency was not cooked";
                            return false;
                        }
                        dependencies.push_back(it->second);
                    }
                    std::vector<std::byte> payload;
                    AssetBuildProduct product;
                    if (!ModelCooker::Encode(*model, payload, error) ||
                        !AssetArchiveWriter::Encode(asset.id, "model", payload, dependencies, product,
                                                    error) ||
                        !append(std::move(product)))
                        return false;
                }
            return true;
        }

        // 来源报告描述实际参与本次构建的输入、材质与节点映射，冷构建与缓存命中都使用这份记录。
        std::string BuildSourceReport(const AssetBuildContext& context, const AssetImportResult& imported,
                                      const AssetBuildResult& result, const Json& signatureFields) {
            Json report{{"schemaVersion", 1},
                        {"source", AssetPathText(context.request.source)},
                        {"key", result.key},
                        {"build", signatureFields},
                        {"primary", result.primary.ToString()},
                        {"settings", Json::parse(AssetImportSettingsKey(context.request.settings))},
                        {"inputs", Json::array()},
                        {"mappings", Json::array()},
                        {"materials", Json::array()},
                        {"diagnostics", Json::array()}};
            for (const auto& dependency : AssetDependencyScanner::Snapshot(context))
                report["inputs"].push_back({{"path", AssetPathText(dependency.path)},
                                            {"sha256", dependency.sha256},
                                            {"bytes", dependency.bytes}});
            for (const auto& mapping : result.mappings)
                report["mappings"].push_back(
                    {{"id", mapping.asset.ToString()},
                     {"key", mapping.sourceKey},
                     {"name", mapping.name},
                     {"kind", mapping.kind},
                     {"nodeId", mapping.node.HasValue() ? mapping.node.ToString() : ""}});
            for (const auto& asset : imported.assets)
                if (const auto* material = std::get_if<ImportedMaterial>(&asset.value)) {
                    Json value{{"id", asset.id.ToString()},
                               {"source", asset.sourceKey},
                               {"baseColor", material->baseColor},
                               {"emissive", material->emissive},
                               {"metallic", material->metallic},
                               {"roughness", material->roughness},
                               {"alphaMode", material->alphaMode},
                               {"alphaCutoff", material->alphaCutoff},
                               {"doubleSided", material->doubleSided},
                               {"unlit", material->unlit},
                               {"normalScale", material->normalScale},
                               {"occlusionStrength", material->occlusionStrength},
                               {"uvFlipV", material->textureFlipV},
                               {"properties", material->sourceProperties}};
                    for (const auto& [semantic, id] : material->textures)
                        value["textures"][semantic] = id.ToString();
                    report["materials"].push_back(std::move(value));
                }
            for (const auto& diagnostic : context.diagnostics)
                report["diagnostics"].push_back({{"level", int(diagnostic.level)},
                                                 {"source", diagnostic.source},
                                                 {"element", diagnostic.element},
                                                 {"message", diagnostic.message}});
            return report.dump(2);
        }

        bool PublishManifest(AssetBuildContext& context, AssetBuildResult& result, size_t& invalidated,
                             std::string& error) {
            const auto& batch = result.records;
            std::map<AssetId, AssetRecord> merged;
            invalidated = 0;
            const auto root = context.request.project.cookedRoot;
            std::vector<std::byte> previousManifest;
            if (std::filesystem::exists(root / "Assets.manifest")) {
                AssetRegistry old;
                if (!Read(root / "Assets.manifest", 64 * 1024 * 1024, previousManifest, error) ||
                    !AssetManifest::Read(previousManifest, old, error))
                    return false;
                for (const auto& record : old.GetRecords())
                    merged.emplace(record.id, record);
                std::vector<AssetId> changed;
                std::set<AssetId> replaced;
                for (const auto& record : batch) {
                    replaced.insert(record.id);
                    const auto* previous = old.Find(record.id);
                    if (previous &&
                        (previous->version != record.version || previous->contentHash != record.contentHash))
                        changed.push_back(record.id);
                }
                // 同源重导入时，撤下源文件已删除的子资源及依赖它们的旧记录。
                // 共享着色器使用独立的目录，其他来源的对象和历史版本仍保存在磁盘上。
                const auto ownerPrefix = "objects/" + context.Identity("request").ToString() + "/";
                for (const auto& record : old.GetRecords()) {
                    if (!replaced.contains(record.id) &&
                        record.locator.path.RelativePath().starts_with(ownerPrefix)) {
                        changed.push_back(record.id);
                    }
                }
                AssetDependencyGraph oldGraph;
                if (!oldGraph.Build(old.GetRecords(), error))
                    return false;
                for (auto id : oldGraph.Affected(changed))
                    if (!replaced.contains(id))
                        invalidated += merged.erase(id);
            }
            for (const auto& record : batch) {
                const auto old = merged.find(record.id);
                if (old != merged.end() &&
                    (old->second.type != record.type || (old->second.version == record.version &&
                                                         old->second.contentHash != record.contentHash))) {
                    error = "Published identity/type/content-version conflict";
                    return false;
                }
                merged.insert_or_assign(record.id, record);
            }
            std::vector<AssetRecord> records;
            records.reserve(merged.size());
            for (auto& [id, record] : merged) {
                (void)id;
                records.push_back(std::move(record));
            }
            AssetRegistry registry;
            AssetDependencyGraph graph;
            std::vector<std::byte> bytes;
            if (!graph.Build(records, error) || !registry.Replace(records, error) ||
                !AssetManifest::Write(registry, bytes, error) || !context.CheckCancelled(error))
                return false;
            // 完全相同的目录保持原时间戳，已持有项目发布锁，因此可以复用当前文件。
            if (bytes != previousManifest && !WriteDerived(root, "Assets.manifest", bytes, false, error))
                return false;
            result.publishedCatalog.swap(records);
            return true;
        }
    }

    AssetBuildResult AssetCooker::Build(AssetBuildRequest request) {
        AssetBuildResult result;
        AssetBuildContext context(std::move(request));
        bool published = false;
        auto progress = [&](std::string_view phase, size_t done, size_t total) {
            // 进度回调仅用于通知，异常不参与构建结果判定。
            try {
                if (context.request.progress)
                    context.request.progress(phase, done, total);
            } catch (...) {
            }
        };
        auto fail = [&](std::string error) {
            if (published) {
                // 主清单已经提交。将后续错误记作警告并返回已发布的结果。
                result.success = true;
                try {
                    result.diagnostics.push_back(
                        {AssetDiagnosticLevel::Warning, {}, "post-publication", std::move(error)});
                } catch (...) {
                }
                return std::move(result);
            }
            result.success = false;
            result.cacheHit = false;
            result.error = AssetPathText(context.request.source) + ": " + error;
            result.diagnostics = context.diagnostics;
            return std::move(result);
        };
        try {
            std::string error;
            if (!context.Initialize(error))
                return fail(error);
            // 重新读取项目配置，核对这次构建的输入和输出目录。
            std::unique_ptr<ProjectSession> session;
            if (!LoadProject(context.request.project.projectFile, session, error))
                return fail(error);
            const auto& current = session->config;
            const auto& requested = context.request.project;
            if (current.id != requested.id || current.contentRoot != requested.contentRoot ||
                current.cookedRoot != requested.cookedRoot ||
                current.derivedDataRoot != requested.derivedDataRoot ||
                current.externalSourceRoots != requested.externalSourceRoots ||
                current.buildProfile != requested.buildProfile) {
                return fail("Project configuration changed; reopen the project before building");
            }
            std::vector<std::byte> projectBytes, profileBytes;
            if (!Read(current.projectFile, 4 * 1024 * 1024, projectBytes, error) ||
                !Read(current.buildProfilesFile, 4 * 1024 * 1024, profileBytes, error))
                return fail(error);
            const auto projectHash = AssetBuildDigest(projectBytes),
                       profileHash = AssetBuildDigest(profileBytes);
            const auto importers = AssetImportRegistry::Defaults();
            const auto* importer = importers.Find(context.request.source);
            if (!importer)
                return fail("Unsupported source extension; no parser fallback is attempted");
            progress("Checking inputs and cache", 0, 1);
            std::vector<AssetRecord> shaders;
            AssetId unlit;
            if (!ShaderLibrary::BuiltinId("Unlit.slang", unlit, error) ||
                !ShaderLibrary::ReadBuiltinCatalog(context.request.shaderRoot, "build-shaders", shaders,
                                                   error))
                return fail(error);
            const auto shader =
                std::find_if(shaders.begin(), shaders.end(), [&](const auto& s) { return s.id == unlit; });
            if (shader == shaders.end())
                return fail("Cooked Unlit shader is missing; build shaders first");
            AssetBuildProduct shaderProduct;
            shaderProduct.record = *shader;
            const auto shaderFile =
                context.request.shaderRoot / std::filesystem::u8path(shader->locator.path.RelativePath());
            if (!Read(shaderFile, 64 * 1024 * 1024, shaderProduct.bytes, error) ||
                AssetBuildDigest(shaderProduct.bytes) != shader->contentHash)
                return fail("Shader input changed: " + error);
            Json signatureFields{{"pipeline", 2},
                                 {"container", 1},
                                 {"domainSchema", 1},
                                 {"importer", importer->Name()},
                                 {"fastgltf", CATALPA_IMPORT_STRINGIFY(FASTGLTF_VERSION)},
                                 {"tangentGenerator", "MikkTSpace-orthogonal-fallback-v2"},
                                 {"imageDecoder", "stb-rgba8-v1"},
                                 {"assimp", std::to_string(aiGetVersionMajor()) + "." +
                                                std::to_string(aiGetVersionMinor()) + "." +
                                                std::to_string(aiGetVersionPatch())},
                                 {"settings", AssetImportSettingsKey(context.request.settings)},
                                 {"project", projectHash},
                                 {"profiles", profileHash},
                                 {"profile", current.buildProfile},
                                 {"platform", current.targetPlatform},
                                 {"configuration", current.buildConfiguration},
                                 {"shader", shader->contentHash},
                                 {"cookedRoot", AssetPathText(current.cookedRoot)}};
            const auto signatureText = signatureFields.dump();
            const auto signature =
                AssetBuildDigest(std::as_bytes(std::span(signatureText.data(), signatureText.size())));
            std::filesystem::create_directories(current.cookedRoot);
            BuildLock lock{current.cookedRoot / ".asset-build-lock"};
            lock.owned = std::filesystem::create_directory(lock.path);
            if (!lock.owned)
                return fail("Cooked root is locked by another build; inspect .asset-build-lock if a previous "
                            "process crashed");
            const auto inputsStable = [&]() {
                std::vector<std::byte> bytes;
                if (!context.InputsUnchanged(error) ||
                    !Read(current.projectFile, 4 * 1024 * 1024, bytes, error) ||
                    AssetBuildDigest(bytes) != projectHash ||
                    !Read(current.buildProfilesFile, 4 * 1024 * 1024, bytes, error) ||
                    AssetBuildDigest(bytes) != profileHash ||
                    !Read(shaderFile, 64 * 1024 * 1024, bytes, error) ||
                    AssetBuildDigest(bytes) != shader->contentHash) {
                    if (error.empty())
                        error = "Project/profile/shader inputs changed before publication";
                    return false;
                }
                return context.CheckCancelled(error);
            };
            if (AssetBuildCache::Load(context, signature, result, error)) {
                size_t invalidated = 0;
                if (!inputsStable() || !PublishManifest(context, result, invalidated, error))
                    return fail(error);
                published = true;
                if (invalidated)
                    result.diagnostics.push_back({AssetDiagnosticLevel::Warning,
                                                  {},
                                                  "dependencies",
                                                  "Invalidated " + std::to_string(invalidated) +
                                                      " obsolete catalog records after resolving removed "
                                                      "assets and stale dependents"});
                progress("Cache hit; catalog published", 1, 1);
                return result;
            }
            error.clear();
            result = {};
            AssetImportResult imported;
            progress("Importing read-only sources", 0, 1);
            if (!importer->Import(context, imported, error) || !context.CheckCancelled(error))
                return fail(error);
            if (imported.assets.empty() || imported.assets.size() > 16384)
                return fail("Imported asset count is outside supported bounds");
            result.primary = imported.primary;
            result.mappings = imported.mappings;
            result.key = AssetDependencyScanner::Key(context, signature);
            result.cookedRoot = current.cookedRoot;
            result.manifest = current.cookedRoot / "Assets.manifest";
            const auto versionRoot = std::filesystem::path("versions") / result.key;
            result.report = current.cookedRoot / versionRoot / "SourceMap.json";
            std::vector<AssetBuildProduct> products;
            products.push_back(std::move(shaderProduct));
            progress("Cooking geometry, images, materials and models", 0, imported.assets.size());
            if (!CookDomainProducts(context, imported, *shader, products, result.mappings, error))
                return fail(error);

            const auto mount =
                "assets-" +
                AssetBuildIdentity(current.id.ToString() + AssetPathText(current.cookedRoot)).ToString();
            const auto sourceOwner = context.Identity("request").ToString();
            for (auto& product : products) {
                // 容器哈希已包含资产身份。相同对象复用已有字节；源目录标明重导入时应替换的记录。
                const auto relative = std::filesystem::path("objects") /
                                      (product.record.id == unlit ? "shared" : sourceOwner) /
                                      (product.record.contentHash + ".catasset");
                if (!VirtualPath::Parse(mount + ":/" + AssetPathText(relative), product.record.locator.path,
                                        error))
                    return fail(error);
                result.records.push_back(product.record);
            }
            AssetRegistry registry;
            AssetDependencyGraph graph;
            std::vector<std::byte> manifest;
            if (!registry.Replace(result.records, error) || !graph.Build(result.records, error) ||
                !registry.Find(result.primary) || !AssetManifest::Write(registry, manifest, error))
                return fail("Invalid cooked catalog: " + error);
            const auto reportText = BuildSourceReport(context, imported, result, signatureFields);
            if (reportText.size() > 4 * 1024 * 1024)
                return fail("Source report exceeds 4MiB");
            // 先写入不可变产物，全部校验通过后再替换项目主清单。
            for (size_t i = 0; i < products.size(); ++i) {
                if (!context.CheckCancelled(error))
                    return fail(error);
                progress("Writing immutable cooked artifacts", i, products.size());
                if (!WriteDerived(current.cookedRoot,
                                  std::filesystem::u8path(products[i].record.locator.path.RelativePath()),
                                  products[i].bytes, true, error))
                    return fail(error);
            }
            result.diagnostics = context.diagnostics;
            size_t invalidated = 0;
            if (!WriteDerived(current.cookedRoot, versionRoot / "SourceMap.json",
                              std::as_bytes(std::span(reportText.data(), reportText.size())), true, error) ||
                !WriteDerived(current.cookedRoot, versionRoot / "Build.manifest", manifest, true, error) ||
                !inputsStable() || !PublishManifest(context, result, invalidated, error))
                return fail(error);
            published = true;
            result.success = true;
            if (invalidated)
                result.diagnostics.push_back(
                    {AssetDiagnosticLevel::Warning,
                     {},
                     "dependencies",
                     "Invalidated " + std::to_string(invalidated) +
                         " obsolete catalog records after resolving removed assets and stale dependents"});
            // 索引是可重建的派生数据。主清单已发布，索引错误以警告反馈。
            try {
                if (!AssetBuildCache::Save(context, signature, result, error))
                    result.diagnostics.push_back({AssetDiagnosticLevel::Warning,
                                                  {},
                                                  "cache",
                                                  "Cook succeeded; cache index was not saved: " + error});
            } catch (const std::exception& exception) {
                result.diagnostics.push_back({AssetDiagnosticLevel::Warning, {}, "cache", exception.what()});
            }
            progress("Cooked catalog published", 1, 1);
            return result;
        } catch (const std::exception& exception) {
            return fail(exception.what());
        }
    }
}
