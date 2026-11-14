#include "catpch.h"

#include "TestRunner.h"
#include "Assets/AssetHandle.h"
#include "Assets/AssetRegistry.h"
#include "Scene/PersistentEntityId.h"

namespace Catalpa::Tests {
	namespace {

		/* 拥有数据的最小载荷，用 weak_ptr 验证版本引用的真实保活而非 ID 比较。 */
		struct RegistryPayload {
			std::vector<int> values;
		};
		struct OtherRegistryPayload {};
		static_assert(!std::is_convertible_v<StableId, AssetId>);
		static_assert(!std::is_convertible_v<AssetId, PersistentEntityId>);
		static_assert(!std::is_convertible_v<PersistentEntityId, AssetId>);
		static_assert(!std::is_convertible_v<AssetHandle<RegistryPayload>, AssetHandle<OtherRegistryPayload>>);

	}

	void RunAssetRegistryTests(TestContext& test) {
		std::string error;
		const auto meshId = AssetId::FromWords(1, 1);
		const auto materialId = AssetId::FromWords(1, 2);
		const auto missingId = AssetId::FromWords(1, 3);
		test.Check(!AssetId{}.HasValue() && meshId.HasValue(), "Asset ID has an explicit invalid zero value");
		AssetId parsedId;
		test.Check(AssetId::TryParse(meshId.ToString(), parsedId) && parsedId == meshId,
		    "Asset ID round trips through the shared stable ID text protocol");
		parsedId = materialId;
		test.Check(!AssetId::TryParse("invalid", parsedId) && parsedId == materialId,
		    "Invalid asset ID text preserves the previous output");
		test.Check(!AssetId::TryParse("00000000-0000-0000-0000-000000000000", parsedId) && parsedId == materialId,
		    "Zero asset ID text is rejected without changing output");

		AssetType meshType, materialType;
		const bool typesReady =
		    AssetType::TryParse("catalpa.mesh", meshType) && AssetType::TryParse("catalpa.material", materialType);
		test.Check(typesReady, "Asset types use stable explicit text keys");
		if (!typesReady)
			return;
		auto typeOutput = meshType;
		for (const auto invalid : {"", "Mesh", "1mesh", "mesh/type", "mesh type"}) {
			test.Check(!AssetType::TryParse(invalid, typeOutput) && typeOutput == meshType,
			    "Invalid type keys preserve the previous value without normalization");
		}
		test.Check(!AssetType::TryParse(std::string(129, 'a'), typeOutput) && typeOutput == meshType,
		    "Oversized type keys are rejected");

		VirtualPath meshPath, materialPath, casePath;
		const bool pathsReady = VirtualPath::Parse("project:/Content/Cube.mesh", meshPath, error) &&
		    VirtualPath::Parse("project:/Content/Base.material", materialPath, error) &&
		    VirtualPath::Parse("project:/Content/cube.mesh", casePath, error);
		test.Check(pathsReady, "Asset locators use the shared canonical virtual path protocol");
		if (!pathsReady)
			return;
		AssetRecord mesh{meshId, meshType, {meshPath, AssetLocationKind::Cooked}, 1, {materialId, missingId}, ""};
		AssetRecord material{
		    materialId, materialType, {materialPath, AssetLocationKind::Source}, 1, {}, std::string(64, 'a')};
		std::vector<AssetRecord> records{mesh, material};
		AssetRegistry registry;
		test.Check(!registry.Find(meshId) && !registry.Find(meshPath) && !registry.Find(AssetId{}),
		    "A new registry has no implicit assets or invalid ID matches");
		const bool registered = registry.Replace(records, error);
		test.Check(registered && error.empty(), "A whole registry replacement validates before publishing");
		if (!registered)
			return;
		const auto* original = registry.Find(meshId);
		test.Check(original && registry.Find(meshPath) == original && registry.Find(mesh.locator) == original,
		    "ID, canonical address and locator resolve to the same owned record");
		test.Check(!registry.Find(AssetLocator{meshPath, AssetLocationKind::Source}),
		    "Locator lookup distinguishes source from cooked content");
		test.Check(registry.GetMissingDependencies().size() == 1 &&
		        registry.GetMissingDependencies()[0].asset == meshId &&
		        registry.GetMissingDependencies()[0].dependency == missingId,
		    "Missing dependencies remain diagnosable while later input records resolve normally");
		AssetRegistry copied = registry;
		AssetRegistry assigned;
		assigned = registry;
		AssetRegistry moved = std::move(copied);
		test.Check(moved.Find(meshId) && moved.Find(meshId) != original && assigned.Find(materialId) &&
		        assigned.GetMissingDependencies().size() == 1 && copied.GetRecords().empty() && !copied.Find(meshId),
		    "Copy and move preserve index ownership while the moved-from registry becomes empty");
		test.Check(assigned.Replace(assigned.GetRecords(), error) && assigned.Find(meshId) &&
		        assigned.Find(meshPath) == assigned.Find(meshId),
		    "Replacement can safely use the registry's own immutable record view as input");
		records[0].version = 40;
		test.Check(original && original->version == 1, "Registry metadata owns a copy of caller records");
		records[0] = mesh;

		const auto unchanged = [&]() {
			return registry.GetRecords().size() == 2 && registry.Find(meshId) == original && original &&
			    original->version == 1 && registry.Find(meshPath) == original &&
			    registry.GetMissingDependencies().size() == 1;
		};
		const auto rejects = [&](std::vector<AssetRecord> candidate, std::string_view reason) {
			error.clear();
			test.Check(!registry.Replace(candidate, error) && !error.empty() && unchanged(), reason);
		};
		auto invalid = records;
		invalid[1].id = meshId;
		rejects(invalid, "Duplicate IDs reject the transaction and retain all prior indices");
		invalid = records;
		invalid[1].locator.path = meshPath;
		rejects(invalid, "Duplicate addresses reject the transaction even across source/cooked kinds");
		invalid = records;
		invalid[0].id = {};
		rejects(invalid, "Invalid asset IDs retain the previous registry");
		invalid = records;
		invalid[0].type = {};
		rejects(invalid, "Invalid type keys retain the previous registry");
		invalid = records;
		invalid[0].locator.path = {};
		rejects(invalid, "Invalid virtual addresses retain the previous registry");
		invalid = records;
		invalid[0].locator.kind = static_cast<AssetLocationKind>(99);
		rejects(invalid, "Unknown locator kinds retain the previous registry");
		invalid = records;
		invalid[0].version = 0;
		rejects(invalid, "Zero content versions retain the previous registry");
		invalid = records;
		invalid[0].dependencies = {meshId};
		rejects(invalid, "An asset cannot depend on itself");
		invalid = records;
		invalid[0].dependencies = {AssetId{}};
		rejects(invalid, "An invalid dependency ID cannot masquerade as a missing asset");
		invalid = records;
		invalid[0].dependencies = {materialId, materialId};
		rejects(invalid, "Repeated dependency declarations are rejected deterministically");
		invalid = records;
		invalid[0].contentHash = "not-a-digest";
		rejects(invalid, "Content hash declarations must use the fixed SHA-256 representation");
		invalid = records;
		invalid[0].contentHash = std::string(64, 'A');
		rejects(invalid, "Content hash validation does not silently normalize uppercase input");

		/* 逻辑身份、内容版本与载荷寿命相互独立；所有载荷由真正 const 对象提供。 */
		AssetHandle<RegistryPayload> handle(meshId);
		AssetVersionRef<RegistryPayload> firstVersion, secondVersion;
		auto firstPayload = std::make_shared<const RegistryPayload>(RegistryPayload{{1, 2, 3}});
		const std::weak_ptr<const RegistryPayload> firstWeak = firstPayload;
		const bool firstReady =
		    AssetVersionRef<RegistryPayload>::TryCreate(meshId, 1, firstPayload, firstVersion, error);
		test.Check(
		    firstReady && firstVersion.Get() && firstVersion.GetId() == handle.id && firstVersion.GetVersion() == 1,
		    "A version reference owns a non-null fixed payload with explicit identity and content version");
		if (!firstReady || !firstVersion.Get())
			return;
		firstPayload.reset();
		auto retainedVersion = firstVersion;
		test.Check(!firstWeak.expired() && retainedVersion.Get()->values[0] == 1,
		    "Copied version references keep the same payload alive after the producer releases it");
		const auto* retainedPointer = firstVersion.Get();
		test.Check(
		    !AssetVersionRef<RegistryPayload>::TryCreate({}, 2, retainedVersion.GetShared(), firstVersion, error) &&
		        firstVersion.Get() == retainedPointer && firstVersion.GetVersion() == 1,
		    "Version creation rejects invalid identity while preserving the published output");
		test.Check(
		    !AssetVersionRef<RegistryPayload>::TryCreate(meshId, 0, retainedVersion.GetShared(), firstVersion, error) &&
		        firstVersion.Get() == retainedPointer,
		    "Version creation rejects zero content version without changing output");
		test.Check(!AssetVersionRef<RegistryPayload>::TryCreate(meshId, 2, {}, firstVersion, error) &&
		        firstVersion.Get() == retainedPointer,
		    "Version creation rejects null payload without changing output");
		const bool secondReady = AssetVersionRef<RegistryPayload>::TryCreate(
		    meshId, 2, std::make_shared<const RegistryPayload>(RegistryPayload{{9}}), secondVersion, error);
		test.Check(secondReady && firstVersion.GetVersion() == 1 && firstVersion.Get()->values[0] == 1,
		    "Publishing a new version does not mutate or retarget retained old references");
		records[0].version = 2;
		const bool replaced = registry.Replace(records, error);
		const auto* replacedRecord = registry.Find(handle.id);
		test.Check(replaced && replacedRecord && replacedRecord->version == 2 && retainedVersion.GetVersion() == 1 &&
		        retainedVersion.Get()->values.size() == 3,
		    "Metadata replacement keeps logical identity separate from previously retained version payloads");
		firstVersion = {};
		test.Check(!firstWeak.expired(), "Another retained version reference still owns the old payload");
		retainedVersion = {};
		test.Check(firstWeak.expired() && handle.HasValue(), "A logical handle alone does not retain the old payload");

		records[0].version = std::numeric_limits<uint64_t>::max();
		records[1].locator.path = casePath;
		const bool maxVersionReplaced = registry.Replace(records, error);
		const auto* caseRecord = registry.Find(casePath);
		const auto* maxVersionRecord = registry.Find(meshPath);
		test.Check(maxVersionReplaced && caseRecord && caseRecord->id == materialId && maxVersionRecord &&
		        maxVersionRecord->version == std::numeric_limits<uint64_t>::max(),
		    "Metadata versions are not incremented as runtime generations and canonical path spelling is preserved");
		test.Check(registry.Replace({}, error) && registry.GetRecords().empty() &&
		        registry.GetMissingDependencies().empty() && !registry.Find(meshId),
		    "An empty successful replacement clears records, indices and missing-dependency diagnostics together");
	}

}
