#include "catpch.h"
#include "TestRunner.h"
#include "Assets/AssetTestFixture.h"
#include "Assets/AssetManifest.h"
#include "AssetInspector/AssetInspection.h"
#include "Audio/AudioAssetLoader.h"
#include "UI/UIDocumentAsset.h"
#include "Commands/PackageCommands.h"

namespace Catalpa::Tests {
    void RunAssetInspectionTests(TestContext& test) {
        std::string error;
        AssetTest::Fixture fixture;
        if (!fixture.Create(error)) {
            test.Check(false, error);
            return;
        }
        VirtualFileSystem files;
        if (!fixture.Mount(files, error)) {
            test.Check(false, error);
            return;
        }
        Tools::AssetInspectionLimits limits;
        limits.maxAssetBytes = 4096;
        limits.maxTotalBytes = 32768;
        limits.maxResidentBytes = 65536;
        limits.timeoutSeconds = 5;

        AssetContainer chunk;
        chunk.id = AssetTest::Id(990);
        chunk.version = 1;
        AssetType::TryParse("audio_chunk", chunk.type);
        const std::array<float, 8> pcm{.2f, .2f, -.3f, -.3f, .1f, .1f, 0, 0};
        AssetRecord chunkRecord;
        if (!AudioAssetLoader::EncodeChunk(pcm, chunk.payload, error) ||
            !fixture.Save(chunk, chunkRecord, error)) {
            test.Check(false, error);
            return;
        }
        Tools::AssetInspection inspected;
        test.Check(
            Tools::AssetInspection::Read(fixture.files.back(), inspected, error, 4096) &&
                inspected.domainValidated && inspected.type == "audio_chunk",
            "Inspector uses the production binary PCM loader without JSON, audio device, window or GPU");

        UIDocumentAsset document;
        document.entry = "hud.rml";
        document.files[document.entry] = "<rml><head></head><body>Owned bundle</body></rml>";
        AssetContainer ui;
        ui.id = AssetTest::Id(991);
        ui.version = 1;
        AssetType::TryParse("ui_document", ui.type);
        AssetRecord uiRecord;
        if (!UIDocumentLoader::Encode(document, ui.payload, error) || !fixture.Save(ui, uiRecord, error)) {
            test.Check(false, error);
            return;
        }
        AssetRegistry catalog;
        std::vector<Tools::AssetInspection> reports;
        test.Check(catalog.Replace({chunkRecord, uiRecord}, error) &&
                       Tools::AssetInspection::ReadClosure(catalog, files, {}, reports, error, {}, limits) &&
                       reports.size() == 2 && reports[0].domainValidated && reports[1].domainValidated,
                   "Inspector validates heterogeneous binary domain payloads through all registered runtime "
                   "loaders");
        const auto prior = reports;

        // 容器摘要合法仍不足以证明领域数据合法；不接受随机 PCM、未知类型或缺失闭包。
        auto corrupt = chunk;
        corrupt.payload.pop_back();
        AssetRecord invalid;
        if (!fixture.Save(corrupt, invalid, error)) {
            test.Check(false, error);
            return;
        }
        test.Check(
            catalog.Replace({invalid}, error) &&
                !Tools::AssetInspection::ReadClosure(catalog, files, {}, reports, error, {}, limits) &&
                reports.size() == prior.size() && reports.front().id == prior.front().id,
            "Valid container with invalid binary domain layout fails and preserves the previous report");
        corrupt = chunk;
        corrupt.id = AssetTest::Id(992);
        AssetType::TryParse("future_unknown", corrupt.type);
        if (!fixture.Save(corrupt, invalid, error)) {
            test.Check(false, error);
            return;
        }
        test.Check(catalog.Replace({invalid}, error) &&
                       !Tools::AssetInspection::ReadClosure(catalog, files, {}, reports, error, {}, limits),
                   "An unregistered domain type is rejected instead of accepted as an opaque payload");

        AssetRecord missing = uiRecord;
        missing.dependencies = {AssetTest::Id(999)};
        test.Check(catalog.Replace({missing}, error) &&
                       !Tools::AssetInspection::ReadClosure(catalog, files, {}, reports, error, {}, limits),
                   "Inspector checks dependency closure before invoking any decoder");
        auto bounded = limits;
        bounded.maxTotalBytes = 1;
        test.Check(catalog.Replace({uiRecord}, error) &&
                       !Tools::AssetInspection::ReadClosure(catalog, files, {}, reports, error, {}, bounded),
                   "Inspector enforces aggregate container bytes before domain decode");

        std::vector<std::byte> manifest;
        const auto manifestFile = fixture.root / L"Assets.manifest";
        fixture.files.push_back(manifestFile);
        if (!AssetManifest::Write(catalog, manifest, error) ||
            !AtomicFileWriter::Write(manifestFile, manifest, error)) {
            test.Check(false, error);
            return;
        }
        const auto text = fixture.root.u8string();
        const std::vector<std::string> args{
            "--inspect", std::string(reinterpret_cast<const char*>(text.data()), text.size())};
        const auto command = Tools::RunPackageCommand(args, {});
        test.Check(
            command.exitCode == 0 && !command.diagnostics.empty() &&
                command.diagnostics.back().find("domain payloads") != std::string::npos,
            "Box inspection CLI consumes a Cooked directory and performs actual headless domain validation");
    }
} // namespace Catalpa::Tests
