#include <Catalpa.h>

#include "Application/EditorWorkspace.h"
#include "BoxGameplay.h"
#include <string_view>
#if !defined(CATALPA_DIST)
#include "TestRunner.h"
#endif

class BoxLayer final : public Catalpa::Layer {
public:
    BoxLayer() : Layer("BoxLayer") {}

    void OnAttach() override {
        CAT_INFO("Box layer attached.");
        if (!m_Gameplay.Attach(Catalpa::Application::Get().GetScenes())) Catalpa::Application::Get().RequestClose();
    }
    void OnDetach() override {
        m_Gameplay.Detach();
        CAT_INFO("Box layer detached.");
    }
    void OnUpdate(Catalpa::Deltatime) override { m_Gameplay.Update(); }

    void OnEvent(Catalpa::Event& event) override
    {
        Catalpa::EventDispatcher dispatcher(event);
        dispatcher.Dispatch<Catalpa::WindowResizeEvent>([](auto& e) {
            CAT_INFO("Window resized: {0}x{1}", e.GetWidth(), e.GetHeight());
            return false;
        });
    }

private:
    Catalpa::BoxGameplay m_Gameplay;
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

// 工具参数在 Core/Entry 调用 CreateApplication 前处理；普通启动路径保持唯一 Box。
Catalpa::ApplicationStartupResult Catalpa::HandleApplicationArguments(int argc, char** argv)
{
    if (argc == 1) return {};
    if (argc == 2 && std::string_view(argv[1]) == "--test-s03-1") {
#if !defined(CATALPA_DIST)
        return { true, Tests::RunS031Tests() };
#else
        CAT_ERROR("CPU contracts are available in Debug/Release builds only.");
        return { true, 2 };
#endif
    }
    const bool help = argc == 2 && std::string_view(argv[1]) == "--help";
    CAT_INFO("Usage: CatalpaBox [--test-s03-1 | --help]. No arguments opens the workspace.");
    return { true, help ? 0 : 2 };
}
