#include "catpch.h"

#include "TestRunner.h"
#include "Geometry/ModelAsset.h"
#include "Geometry/PrimitiveMesh.h"

namespace Catalpa::Tests {
	void RunGeometryAssetTests(TestContext& test) {
		std::string error;
		MeshData source;
		MeshResourceRef resource;
		test.Check(CreateCubeMesh(1, source).IsSuccess() && CreateMeshResource(source, resource).IsSuccess(),
		    "Geometry fixture freezes real validated cube bytes");
		if (!resource)
			return;
		MeshAsset mesh{resource, {"surface"}};
		test.Check(ValidateMeshAsset(mesh, error), "Mesh asset owns geometry and named material slots");
		auto badMesh = mesh;
		badMesh.materialSlots.clear();
		test.Check(!ValidateMeshAsset(badMesh, error), "A mesh asset cannot omit material slot declarations");
		badMesh = mesh;
		badMesh.materialSlots.push_back("surface");
		test.Check(!ValidateMeshAsset(badMesh, error), "Material slot names must be unique");
		source.submeshes[0].materialSlot = 1;
		MeshResourceRef slotResource;
		test.Check(CreateMeshResource(source, slotResource).IsSuccess(),
		    "Mesh geometry validation is independent of asset slot count");
		badMesh = {slotResource, {"one"}};
		test.Check(!ValidateMeshAsset(badMesh, error), "Submesh cannot address an undeclared material slot");
		const auto meshId = AssetId::FromWords(70, 1), materialId = AssetId::FromWords(70, 2);
		ModelAsset model;
		model.nodes.push_back({StableId::FromWords(71, 1), "child", 1, {}});
		model.nodes.push_back({StableId::FromWords(71, 2), "root", -1, {}});
		model.meshes.push_back({0, AssetHandle<MeshAsset>{meshId}, {AssetHandle<MaterialInstance>{materialId}}});
		model.meshes.push_back({1, AssetHandle<MeshAsset>{meshId}, {AssetHandle<MaterialInstance>{materialId}}});
		test.Check(ValidateModelAsset(model, error), "Model parent indices need not precede their children");
		auto owner = std::make_shared<const MeshAsset>(mesh);
		AssetVersionRef<MeshAsset> version;
		test.Check(AssetVersionRef<MeshAsset>::TryCreate(meshId, 1, owner, version, error),
		    "Model resolved mesh reference pins one immutable version");
		const std::array versions{version};
		test.Check(ValidateModelMeshBindings(model, versions, error),
		    "Multiple model instances share one mesh asset and validate slots");
		test.Check(!ValidateModelMeshBindings(model, {}, error), "Missing resolved mesh dependencies are reported");
		auto bad = model;
		bad.nodes[1].parent = 0;
		test.Check(!ValidateModelAsset(bad, error), "Model parent cycle is rejected");
		bad = model;
		bad.nodes[0].id = bad.nodes[1].id;
		test.Check(!ValidateModelAsset(bad, error), "Model node persistent IDs cannot collide");
		bad = model;
		bad.nodes[0].local.rotation = Math::Quat(2, 0, 0, 0);
		test.Check(!ValidateModelAsset(bad, error), "Model quaternions are validated without normalization");
		bad = model;
		bad.meshes[0].node = 2;
		test.Check(!ValidateModelAsset(bad, error), "Mesh instance node range is checked");
		bad = model;
		bad.meshes[0].materials.push_back(AssetHandle<MaterialInstance>{materialId});
		test.Check(
		    !ValidateModelMeshBindings(bad, versions, error), "Resolved model material count must match mesh slots");
		test.Check(model.nodes[0].parent == 1 && owner->mesh == resource,
		    "Validation leaves hierarchy and shared geometry unchanged");
	}
}
