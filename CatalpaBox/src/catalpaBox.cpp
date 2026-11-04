#include <Catalpa.h>

#include "Application/EditorWorkspace.h"
#include "BoxGameplay.h"
#include "Core/PlatformServices.h"
#include "BoxCommandLine.h"
#include <string_view>

namespace {
    Catalpa::BoxLaunchOptions sandboxOptions;
}

class BoxLayer final : public Catalpa::Layer {
public:
    BoxLayer(bool reversedZ,bool errorMaterial) : Layer("BoxLayer"), m_ReversedZ(reversedZ), m_ErrorMaterial(errorMaterial) {}

    void OnAttach() override {
        CAT_INFO("Box layer attached.");
        if (!m_Gameplay.Attach(Catalpa::Application::Get().GetScenes(), Catalpa::Application::Get().GetAssets(), m_ReversedZ, m_ErrorMaterial)) Catalpa::Application::Get().RequestClose(1);
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
    bool m_ErrorMaterial = false;
};

class CatalpaBox : public Catalpa::Application {
public:
    // CatalpaBox 是唯一启动应用；编辑器静态库只提供由宿主挂载的功能。
    explicit CatalpaBox(const Catalpa::BoxLaunchOptions& options) {
        PushLayer(std::make_unique<BoxLayer>(options.reversedZ,options.errorMaterial));
        PushOverlay(std::make_unique<Catalpa::Editor::EditorWorkspace>(options.project));
    }

    ~CatalpaBox() {}
};

Catalpa::Application* Catalpa::CreateApplication() {
    return new CatalpaBox(sandboxOptions);
}

// 工具参数在 Core/Entry 调用 CreateApplication 前处理；普通启动路径保持唯一 Box。
Catalpa::ApplicationStartupResult Catalpa::HandleApplicationArguments(int, char**)
{
    std::vector<std::string> arguments;std::string error;
    if(!GetProcessArguments(arguments,error)||arguments.empty()) {CAT_ERROR("Cannot read arguments: {0}",error);return {true,2};}
    return BoxCommandLine::Dispatch(std::span<const std::string>(arguments).subspan(1),sandboxOptions);
}
