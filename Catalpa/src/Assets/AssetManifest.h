#pragma once
#include "catpch.h"
#include "Core/Base.h"
#include "Assets/AssetRegistry.h"
namespace Catalpa {
    // Runtime-only LE manifest: cooked locators and exact whole-container digests, no author roots.
    class CATALPA_API AssetManifest {
    public:
        static bool Read(std::span<const std::byte>, AssetRegistry&, std::string& error);
        static bool Write(const AssetRegistry&, std::vector<std::byte>&, std::string& error);
    };
}
