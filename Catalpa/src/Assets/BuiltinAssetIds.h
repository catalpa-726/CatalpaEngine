#pragma once
#include "Assets/AssetId.h"
#include <array>
namespace Catalpa::BuiltinAssetIds {
	/* 引擎为内置资产显式指定的身份，与文件内容摘要无关。 */
	inline constexpr auto WhiteImage = AssetId::FromWords(0x434154414C504100ULL, 1);
	inline constexpr auto FlatNormalImage = AssetId::FromWords(0x434154414C504100ULL, 2);
	inline constexpr auto ErrorImage = AssetId::FromWords(0x434154414C504100ULL, 3);
	inline constexpr auto FontAtlasImage = AssetId::FromWords(0x434154414C504100ULL, 4);
	inline constexpr auto BlackEnvironmentCubeImage = AssetId::FromWords(0x434154414C504100ULL, 5);
	/* 随宿主发布的阴天 HDR。身份对应资源的 Cook 容器，不引用作者项目或源目录。 */
	inline constexpr auto DefaultEnvironment = AssetId::FromWords(0x87ec2048323ca4d4ULL, 0x0d57dbb8a6503d80ULL);
	inline constexpr auto DefaultUnlitDefinition = AssetId::FromWords(0x434154414C504100ULL, 11);
	inline constexpr auto DefaultPbrDefinition = AssetId::FromWords(0x434154414C504100ULL, 12);
	inline constexpr auto ErrorDefinition = AssetId::FromWords(0x434154414C504100ULL, 13);
	inline constexpr auto ErrorInstance = AssetId::FromWords(0x434154414C504100ULL, 14);
	inline constexpr auto DefaultUnlitInstance = AssetId::FromWords(0x434154414C504100ULL, 15);
	inline constexpr auto DefaultPbrInstance = AssetId::FromWords(0x434154414C504100ULL, 16);
	/* 作者材质随包携带的标准定义，使用内容版本，与运行时内置的版本 1 分开。 */
	inline constexpr auto CookedUnlitDefinition = AssetId::FromWords(0x434154414C504100ULL, 17);
	inline constexpr auto CookedPbrDefinition = AssetId::FromWords(0x434154414C504100ULL, 18);
	/* 已保存的示例场景使用相同的立方体和 12 米地面身份。 */
	inline constexpr auto CubeMesh = AssetId::FromWords(0x434154414C504142ULL, 1);
	inline constexpr auto GroundMesh = AssetId::FromWords(0x434154414C504142ULL, 2);
	inline constexpr auto CheckerGroundImage = AssetId::FromWords(0x434154414C504142ULL, 10);
	inline constexpr auto CheckerGroundDefinition = AssetId::FromWords(0x434154414C504142ULL, 11);
	inline constexpr auto CheckerGround = AssetId::FromWords(0x434154414C504142ULL, 12);
	inline constexpr auto SphereMesh = AssetId::FromWords(0x434154414C504100ULL, 32);
	inline constexpr auto CapsuleMesh = AssetId::FromWords(0x434154414C504100ULL, 33);
	inline constexpr auto CylinderMesh = AssetId::FromWords(0x434154414C504100ULL, 34);
	inline constexpr auto PlaneMesh = AssetId::FromWords(0x434154414C504100ULL, 35);
	inline constexpr auto QuadMesh = AssetId::FromWords(0x434154414C504100ULL, 36);
	inline constexpr auto BoxCollisionShape = AssetId::FromWords(0x434154414C504100ULL, 64);
	inline constexpr auto GroundCollisionShape = AssetId::FromWords(0x434154414C504100ULL, 65);

	/* 启动时由引擎发布。场景与预制体保存这些身份，解码时固定当前宿主提供的版本。 */
	inline constexpr std::array SceneBuiltins{CubeMesh, SphereMesh, CapsuleMesh, CylinderMesh, PlaneMesh, QuadMesh,
	    GroundMesh, CheckerGround, CheckerGroundDefinition, CheckerGroundImage, DefaultPbrInstance, DefaultUnlitInstance, BoxCollisionShape, GroundCollisionShape,
	    DefaultEnvironment, DefaultUnlitDefinition, DefaultPbrDefinition, ErrorDefinition, WhiteImage, FlatNormalImage,
	    ErrorImage};
	inline constexpr bool IsSceneBuiltin(AssetId id) noexcept {
		for (auto builtin : SceneBuiltins)
			if (id == builtin)
				return true;
		return false;
	}
}
