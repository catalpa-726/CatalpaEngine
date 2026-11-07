#pragma once
#include "Audio/Backends/IAudioBackend.h"
namespace Catalpa {
    std::unique_ptr<IAudioBackend> CreateMiniaudioBackend(std::string& error);
}
