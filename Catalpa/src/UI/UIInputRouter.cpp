#include "catpch.h"
#include "UI/UIInputRouter.h"
#include "Events/KeyEvent.h"
#include "Events/MouseEvent.h"
#include <RmlUi/Core/Input.h>
namespace Catalpa {
    namespace {
        int KeyIdentifier(int key) {
            using namespace Rml::Input;
            if (key >= Key::A && key <= Key::Z)
                return KI_A + key - Key::A;
            if (key >= Key::D0 && key <= Key::D9)
                return KI_0 + key - Key::D0;
            if (key >= Key::F1 && key <= Key::F24)
                return KI_F1 + key - Key::F1;
            if (key >= Key::KP0 && key <= Key::KP9)
                return KI_NUMPAD0 + key - Key::KP0;
            switch (key) {
            case Key::Space:
                return KI_SPACE;
            case Key::Enter:
                return KI_RETURN;
            case Key::Tab:
                return KI_TAB;
            case Key::Backspace:
                return KI_BACK;
            case Key::Escape:
                return KI_ESCAPE;
            case Key::Insert:
                return KI_INSERT;
            case Key::Delete:
                return KI_DELETE;
            case Key::Home:
                return KI_HOME;
            case Key::End:
                return KI_END;
            case Key::Left:
                return KI_LEFT;
            case Key::Right:
                return KI_RIGHT;
            case Key::Up:
                return KI_UP;
            case Key::Down:
                return KI_DOWN;
            case Key::PageUp:
                return KI_PRIOR;
            case Key::PageDown:
                return KI_NEXT;
            case Key::LeftShift:
                return KI_LSHIFT;
            case Key::RightShift:
                return KI_RSHIFT;
            case Key::LeftControl:
                return KI_LCONTROL;
            case Key::RightControl:
                return KI_RCONTROL;
            case Key::LeftAlt:
                return KI_LMENU;
            case Key::RightAlt:
                return KI_RMENU;
            case Key::Minus:
                return KI_OEM_MINUS;
            case Key::Equal:
                return KI_OEM_PLUS;
            case Key::Comma:
                return KI_OEM_COMMA;
            case Key::Period:
                return KI_OEM_PERIOD;
            case Key::Slash:
                return KI_OEM_2;
            case Key::Semicolon:
                return KI_OEM_1;
            case Key::Apostrophe:
                return KI_OEM_7;
            case Key::LeftBracket:
                return KI_OEM_4;
            case Key::RightBracket:
                return KI_OEM_6;
            case Key::Backslash:
                return KI_OEM_5;
            case Key::GraveAccent:
                return KI_OEM_3;
            default:
                return KI_UNKNOWN;
            }
        }
    } // namespace
    int UIInputRouter::TranslatePlatformKey(int key) {
        return KeyIdentifier(key);
    }
    bool UIInputRouter::Translate(const Event& event, float x, float y, float sx, float sy,
                                  UIInputEvent& output) {
        UIInputEvent value;
        value.modifiers = m_Modifiers;
        switch (event.GetEventType()) {
        case EventType::MouseMoved: {
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(sx) || !std::isfinite(sy) ||
                sx <= 0 || sy <= 0)
                return false;
            const auto& move = static_cast<const MouseMovedEvent&>(event);
            value.type = UIInputEvent::Type::Move;
            value.x = (move.GetX() - x) * sx;
            value.y = (move.GetY() - y) * sy;
            break;
        }
        case EventType::MouseButtonPressed:
        case EventType::MouseButtonReleased: {
            value.type = event.GetEventType() == EventType::MouseButtonPressed
                             ? UIInputEvent::Type::ButtonDown
                             : UIInputEvent::Type::ButtonUp;
            const auto button = static_cast<const MouseButtonEvent&>(event).GetMouseButton();
            // GLFW: left/right/middle；RmlUi: left/right/middle，索引一致。
            if (button > 2)
                return false;
            value.key = button;
            break;
        }
        case EventType::MouseScrolled: {
            const auto& wheel = static_cast<const MouseScrolledEvent&>(event);
            value.type = UIInputEvent::Type::Wheel;
            value.x = -wheel.GetXOffset();
            value.y = -wheel.GetYOffset();
            break;
        }
        case EventType::KeyPressed:
        case EventType::KeyReleased: {
            const int key = static_cast<const KeyEvent&>(event).GetKeyCode();
            const bool down = event.GetEventType() == EventType::KeyPressed;
            if (key >= 0 && key < int(m_Pressed.size()))
                m_Pressed[key] = down;
            m_Modifiers =
                (m_Pressed[Key::LeftShift] || m_Pressed[Key::RightShift] ? Rml::Input::KM_SHIFT : 0) |
                (m_Pressed[Key::LeftControl] || m_Pressed[Key::RightControl] ? Rml::Input::KM_CTRL : 0) |
                (m_Pressed[Key::LeftAlt] || m_Pressed[Key::RightAlt] ? Rml::Input::KM_ALT : 0) |
                (m_Pressed[Key::LeftSuper] || m_Pressed[Key::RightSuper] ? Rml::Input::KM_META : 0);
            value.modifiers = m_Modifiers;
            value.key = KeyIdentifier(key);
            value.type = down ? UIInputEvent::Type::KeyDown : UIInputEvent::Type::KeyUp;
            break;
        }
        case EventType::KeyTyped:
            value.type = UIInputEvent::Type::Text;
            value.codepoint = static_cast<const KeyTypedEvent&>(event).GetCodepoint();
            break;
        case EventType::WindowLostFocus:
            value.type = UIInputEvent::Type::Blur;
            Reset();
            break;
        default:
            return false;
        }
        output = value;
        return true;
    }
} // namespace Catalpa
