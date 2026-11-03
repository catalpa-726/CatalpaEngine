#include <Catalpa.h>

#include "Application/EditorWorkspace.h"

class BoxLayer final : public Catalpa::Layer {
public:
    BoxLayer() : Layer("BoxLayer") {}

    void OnAttach() override { CAT_INFO("Box layer attached."); }
    void OnDetach() override { CAT_INFO("Box layer detached."); }

    void OnEvent(Catalpa::Event& event) override
    {
        Catalpa::EventDispatcher dispatcher(event);
        dispatcher.Dispatch<Catalpa::WindowResizeEvent>([](auto& e) {
            CAT_INFO("Window resized: {0}x{1}", e.GetWidth(), e.GetHeight());
            return false;
        });
    }
};

class CatalpaBox : public Catalpa::Application {
public:
    // CatalpaBox 是唯一启动应用；编辑器静态库只提供由宿主挂载的功能。
    CatalpaBox() {
        PushLayer(std::make_unique<BoxLayer>());
        PushOverlay(std::make_unique<Catalpa::Editor::EditorWorkspace>());
    }

    ~CatalpaBox() {}
};

Catalpa::Application* Catalpa::CreateApplication() {
    return new CatalpaBox();
}
