#pragma once
#include "catpch.h"
namespace Catalpa {
    // CPU publication state only; Ready makes no GPU-residency or audio-device promise.
    enum class AssetStatus { Unloaded, WaitingForDependencies, Loading, Ready, Failed, Cancelled };
}
