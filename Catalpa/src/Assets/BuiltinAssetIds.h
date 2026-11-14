#pragma once
#include "Assets/AssetId.h"
namespace Catalpa::BuiltinAssetIds {
	/* 引擎为内置资产显式指定的身份，与文件内容摘要无关。 */
	inline constexpr auto WhiteImage = AssetId::FromWords(0x434154414C504100ULL, 1);
	inline constexpr auto FlatNormalImage = AssetId::FromWords(0x434154414C504100ULL, 2);
	inline constexpr auto ErrorImage = AssetId::FromWords(0x434154414C504100ULL, 3);
	inline constexpr auto FontAtlasImage = AssetId::FromWords(0x434154414C504100ULL, 4);
	inline constexpr auto BlackEnvironmentCubeImage = AssetId::FromWords(0x434154414C504100ULL, 5);
	inline constexpr auto DefaultUnlitDefinition = AssetId::FromWords(0x434154414C504100ULL, 11);
	inline constexpr auto DefaultPbrDefinition = AssetId::FromWords(0x434154414C504100ULL, 12);
	inline constexpr auto ErrorDefinition = AssetId::FromWords(0x434154414C504100ULL, 13);
	inline constexpr auto ErrorInstance = AssetId::FromWords(0x434154414C504100ULL, 14);
	inline constexpr auto DefaultUnlitInstance = AssetId::FromWords(0x434154414C504100ULL, 15);
	inline constexpr auto DefaultPbrInstance = AssetId::FromWords(0x434154414C504100ULL, 16);
}
