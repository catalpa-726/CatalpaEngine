#pragma once
#include "catpch.h"
#include "TestRunner.h"
#include "Assets/AssetManager.h"
#include "Assets/AssetContainerReader.h"
#include "Core/IO/AtomicFileWriter.h"
#include "Core/Id/StableIdGenerator.h"
#include "Core/Serialization/BinaryArchive.h"

namespace Catalpa::Tests::AssetTest {
    // Small real payload used to observe async ownership without involving GPU objects.
    struct Value { uint32_t number = 0; std::thread::id decodedOn; };
    inline AssetId Id(uint64_t low) { return AssetId::FromWords(0x4341545445535400ULL, low); }
    inline AssetType Type() { AssetType value; if (!AssetType::TryParse("test_value", value)) throw std::logic_error("Invalid test type"); return value; }
    inline AssetManagerConfig Config() { return {4096,4096,128 * 1024,8}; }
    inline AssetContainer Container(AssetId id, uint32_t number, uint64_t version = 1) {
        AssetContainer value; value.id = id; value.type = Type(); value.version = version;
        BinaryWriter writer; writer.U32(number); value.payload = writer.Data(); return value;
    }
    inline AssetLoader Loader(std::function<void(uint32_t, const CancellationToken&)> before = {}) {
        AssetLoader loader; loader.type = Type(); loader.cppType = std::type_index(typeid(Value));
        loader.decode = [before = std::move(before)](const AssetContainer& container, const AssetDecodeContext& context, AssetLoadResult& output, std::string& error) {
            BinaryReader reader(container.payload); uint32_t number;
            if (container.payloadVersion != 1 || !reader.U32(number) || !reader.AtEnd()) { error = "Invalid test payload"; return false; }
            if (before) before(number, context.cancellation);
            output = MakeAssetLoadResult<Value>(std::make_shared<const Value>(Value{number,std::this_thread::get_id()}), sizeof(Value));
            error.clear(); return true;
        };
        return loader;
    }
    // Only this invocation's exclusive temporary directory and listed leaves are ever removed.
    struct Fixture {
        std::filesystem::path root;
        std::vector<std::filesystem::path> files;
        ~Fixture() {
            std::error_code ignored;
            for (const auto& file : files) std::filesystem::remove(file, ignored);
            if (!root.empty()) std::filesystem::remove(root, ignored);
        }
        bool Create(std::string& error) {
            const auto parent = std::filesystem::canonical(std::filesystem::temp_directory_path());
            for (unsigned attempt = 0; attempt < 8; ++attempt) {
                StableId unique; if (!GenerateStableId(unique, error)) return false;
                const auto candidate = parent / ("Catalpa-Assets-" + unique.ToString());
                if (std::filesystem::create_directory(candidate)) { root = candidate; return true; }
            }
            error = "Cannot create exclusive asset fixture directory"; return false;
        }
        bool Save(const AssetContainer& container, AssetRecord& record, std::string& error) {
            std::vector<std::byte> bytes;
            if (!WriteAssetContainer(container, bytes, error, 4096)) return false;
            const auto name = container.id.ToString() + ".asset";
            const auto target = root / name;
            if (std::find(files.begin(), files.end(), target) == files.end()) files.push_back(target);
            if (!AtomicFileWriter::Write(target, bytes, error)) return false;
            AssetRecord candidate; candidate.id = container.id; candidate.type = container.type; candidate.version = container.version;
            candidate.locator.kind = AssetLocationKind::Cooked;
            if (!VirtualPath::Parse("test:/" + name, candidate.locator.path, error) || !ComputeAssetContentHash(bytes, candidate.contentHash, error)) return false;
            for (const auto& dependency : container.dependencies) candidate.dependencies.push_back(dependency.id);
            record = std::move(candidate); return true;
        }
        bool Mount(VirtualFileSystem& filesOut, std::string& error) const { return filesOut.MountReadOnly("test", root, 0, error); }
    };
    // 两个编辑器 GPU 验收共用的独占 PNG 和 PBR 材质输入。
    bool CreateThumbnailSources(Fixture& fixture, std::string& error);

    inline bool Drain(AssetManager& manager) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        do { manager.Pump(); if (manager.GetPendingCount() == 0) return true; std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        while (std::chrono::steady_clock::now() < deadline);
        return false;
    }
    inline bool Await(AssetManager& manager, AssetId id) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        do {
            manager.Pump(); const auto state = manager.GetStatus(id);
            if (state != AssetStatus::Loading && state != AssetStatus::WaitingForDependencies) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (std::chrono::steady_clock::now() < deadline);
        return false;
    }
    // Bounded gate intentionally permits a late result even after cancellation, exercising generations.
    struct Gate {
        std::atomic_bool entered{false}, release{false};
        void Enter() {
            entered.store(true, std::memory_order_release);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (!release.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        bool AwaitEntry() const {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (!entered.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return entered.load(std::memory_order_acquire);
        }
    };
}
