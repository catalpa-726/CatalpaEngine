#pragma once
#include "Audio/AudioTypes.h"
namespace Catalpa {
    CATALPA_API bool ReadAudioConfig(std::string_view, AudioServiceConfig&, std::string& error);
}
