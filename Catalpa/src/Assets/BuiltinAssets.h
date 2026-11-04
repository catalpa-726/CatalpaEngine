#pragma once
#include "catpch.h"
#include "Core/Base.h"
namespace Catalpa {
    class AssetManager;
    // Call once after domain registration and the real Unlit ShaderAsset becomes Ready.
    // Bind each immutable material definition to that exact shader version before publication.
    // On a partial failure, discard the startup manager;
    // already-published identities are never silently replaced under the same content revision.
    // Publish mathematical one-pixel images and schema-validated engine materials through the manager.
    CATALPA_API bool RegisterBuiltinAssets(AssetManager&, std::string& error);
}
