#include <Catalpa.h>

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
    CatalpaBox() { PushLayer(std::make_unique<BoxLayer>()); }

    ~CatalpaBox() {}
};

Catalpa::Application* Catalpa::CreateApplication() {
    return new CatalpaBox();
}