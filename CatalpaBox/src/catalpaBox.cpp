#include <Catalpa.h>

#include "Application/EditorWorkspace.h"
#include "BoxGameplay.h"
#include <string_view>
#if !defined(CATALPA_DIST)
#include "TestRunner.h"
#endif

namespace {
    // 只在进程启动分派中写入，再按值传给沙盒Layer；引擎不读取此宿主配置。
    bool sandboxReversedZ = false;
}

class BoxLayer final : public Catalpa::Layer {
public:
    explicit BoxLayer(bool reversedZ) : Layer("BoxLayer"), m_ReversedZ(reversedZ) {}

    void OnAttach() override {
        CAT_INFO("Box layer attached.");
        if (!m_Gameplay.Attach(Catalpa::Application::Get().GetScenes(), m_ReversedZ)) Catalpa::Application::Get().RequestClose(1);
    }
    void OnDetach() override {
        m_Gameplay.Detach();
        CAT_INFO("Box layer detached.");
    }
    void OnUpdate(Catalpa::Deltatime) override {
        const auto& application = Catalpa::Application::Get();
        m_Gameplay.Update(application.GetFrameTime(), application.GetSceneFrame());
    }

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
    bool m_ReversedZ = false;
};

class CatalpaBox : public Catalpa::Application {
public:
    // CatalpaBox 是唯一启动应用；编辑器静态库只提供由宿主挂载的功能。
    explicit CatalpaBox(bool reversedZ) {
        PushLayer(std::make_unique<BoxLayer>(reversedZ));
        PushOverlay(std::make_unique<Catalpa::Editor::EditorWorkspace>());
    }

    ~CatalpaBox() {}
};

Catalpa::Application* Catalpa::CreateApplication() {
    return new CatalpaBox(sandboxReversedZ);
}

// 工具参数在 Core/Entry 调用 CreateApplication 前处理；普通启动路径保持唯一 Box。
Catalpa::ApplicationStartupResult Catalpa::HandleApplicationArguments(int argc, char** argv)
{
    sandboxReversedZ = false;
    if (argc == 1) return {};
    if (argc == 2 && std::string_view(argv[1]) == "--sample-reversed-z") {
        sandboxReversedZ = true;
        return {}; // 继续唯一正常应用入口，由样例镜头启用反向Z。
    }
    if (argc == 2 && (std::string_view(argv[1]) == "--test-s03-1" || std::string_view(argv[1]) == "--test-s03-2"
        || std::string_view(argv[1]) == "--test-s03-3" || std::string_view(argv[1]) == "--test-s03-4")) {
#if !defined(CATALPA_DIST)
        const std::string_view mode(argv[1]);
        if (mode == "--test-s03-4") return { true, Tests::RunS034Tests() };
        if (mode == "--test-s03-3") return { true, Tests::RunS033Tests() };
        return { true, mode == "--test-s03-2" ? Tests::RunS032Tests() : Tests::RunS031Tests() };
#else
        CAT_ERROR("CPU contracts are available in Debug/Release builds only.");
        return { true, 2 };
#endif
    }
    const bool help = argc == 2 && std::string_view(argv[1]) == "--help";
    CAT_INFO("Usage: CatalpaBox [--test-s03-1 | --test-s03-2 | --test-s03-3 | --test-s03-4 | --sample-reversed-z | --help]. No arguments opens the workspace.");
    return { true, help ? 0 : 2 };
}
