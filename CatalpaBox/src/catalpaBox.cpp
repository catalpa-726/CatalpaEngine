#include <Catalpa.h>

#include "Application/EditorWorkspace.h"
#include "BoxDefaultEnvironment.h"
#include "BoxGameplay.h"
#include "Core/PlatformServices.h"
#include "Scene/SceneManager.h"
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
        auto& application = Catalpa::Application::Get();
        if (!m_Gameplay.Attach(application.GetScenes(), application.GetAssets(), m_ReversedZ, m_ErrorMaterial)) {
            application.RequestClose(1);
            return;
        }

        // 默认天空只属于刚创建的样例场景，后台处理不会覆盖随后打开的作者场景。
        std::string error;
        if (!m_Environment.Start(application.GetScenes(), application.GetAssets(), application.GetJobs(),
                                 application.GetScenes().GetActiveHandle(), error))
            CAT_WARN("Default sky unavailable: {0}", error);
    }
    void OnDetach() override {
        m_Environment.Stop();
        m_Gameplay.Detach();
        CAT_INFO("Box layer detached.");
    }
    void OnUpdate(Catalpa::Deltatime) override {
        m_Environment.Update();
        m_Gameplay.ReportHierarchyOnce();
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
    Catalpa::BoxDefaultEnvironment m_Environment;
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
