#pragma once
#include "Scene/PersistentEntityId.h"
#include "catpch.h"
#include "Core/TextInput.h"
namespace Catalpa {
    struct UIEvent {
        uint64_t sequence = 0;
        std::string element, type, value;
        uint64_t world = 0;
        PersistentEntityId source;
    };
    struct UIInputEvent {
        enum class Type { Move, ButtonDown, ButtonUp, Wheel, KeyDown, KeyUp, Text, Blur, Composition };
        Type type = Type::Move;
        float x = 0, y = 0;
        int key = 0, modifiers = 0;
        uint32_t codepoint = 0;
        TextComposition composition;
    };
} // namespace Catalpa
