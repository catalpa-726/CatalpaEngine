#include "catpch.h"
#include "ImGuiLayer.h"

#include "Core/Application.h"
#include "Runtime/EngineRuntime.h"
#include "Core/Localization/LocalizationCatalog.h"
#include "Core/PlatformServices.h"
#include "Log/Log.h"
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include <GLFW/glfw3.h>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <imm.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

namespace Catalpa {
    namespace {
        void BuildWorkspaceGlyphRanges(ImFontAtlas& atlas, const std::filesystem::path& resources,
                                       ImVector<ImWchar>& ranges)
        {
            ImFontGlyphRangesBuilder builder;
            builder.AddRanges(atlas.GetGlyphRangesChineseSimplifiedCommon());
            builder.AddText("简体中文 (zh-CN)");
            // 补上翻译中超出常用简体集合的字和符号，JSON 转义也通过同一语言包解析器解码。
            for (const char* language : {"en-US", "zh-CN"}) {
                LocalizationCatalog catalog;
                std::string error;
                const auto path = resources / "Localization" / (std::string(language) + ".json");
                if (!catalog.LoadFile(path, language, error)) {
                    CAT_CORE_WARN("Cannot preload glyphs for {0}: {1}", language, error);
                    continue;
                }
                for (const auto& entry : catalog.Strings())
                    builder.AddText(entry.second.c_str());
            }
            builder.BuildRanges(&ranges);
        }

        ImFont* LoadWorkspaceFont(ImFontAtlas& atlas, ImVector<ImWchar>& ranges)
        {
            // 图集始终以 24 px 烘焙，工作区字号设置只改变显示比例。
            ImFontConfig config;
            config.SizePixels = 24.0f;
            config.OversampleH = 3;
            config.OversampleV = 2;
            config.PixelSnapH = false;
            config.Flags |= ImFontFlags_NoLoadError;

            std::filesystem::path executableDirectory;
            std::string error;
            if (!GetExecutableDirectory(executableDirectory, error)) {
                CAT_CORE_WARN("Cannot locate workspace fonts; using the embedded font: {0}", error);
                return atlas.AddFontDefaultVector(&config);
            }

            const auto resources = executableDirectory / "Resources";
            const auto latinPath = (resources / "Fonts/Inter/Inter-Regular.ttf").u8string();
            const auto* latinFile = reinterpret_cast<const char*>(latinPath.c_str());
            ImFont* primary = atlas.AddFontFromFileTTF(latinFile, config.SizePixels, &config);
            if (!primary) {
                CAT_CORE_WARN("Cannot load {0}; using the embedded Latin font", latinFile);
                primary = atlas.AddFontDefaultVector(&config);
            }
            if (!primary) return nullptr;

            BuildWorkspaceGlyphRanges(atlas, resources, ranges);
            ImFontConfig chinese = config;
            chinese.MergeMode = true;
            chinese.DstFont = primary;
            // 中文笔画按目标像素烘焙，避免整套常用字沿用西文字形的 3×2 过采样。
            chinese.OversampleH = chinese.OversampleV = 1;
            const auto chinesePath = (resources / "Fonts/NotoSansSC/NotoSansSC-Regular.otf").u8string();
            const auto* chineseFile = reinterpret_cast<const char*>(chinesePath.c_str());
            if (!atlas.AddFontFromFileTTF(chineseFile, chinese.SizePixels, &chinese, ranges.Data))
                CAT_CORE_WARN("Cannot load Chinese workspace font: {0}", chineseFile);
            return primary;
        }
    }

    ImGuiLayer::ImGuiLayer() : Layer("ImGuiLayer") {}
    ImGuiLayer::~ImGuiLayer() { OnDetach(); }

    void ImGuiLayer::OnAttach()
    {
        CAT_CORE_ASSERT(!m_ContextCreated, "ImGui layer already attached!");
        if (m_ContextCreated) return;
        m_PlatformStatus = {};
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        m_ContextCreated = true;

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
        if (m_PlatformRuntime) io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
        io.BackendRendererName = "Catalpa_RenderGraph";
        io.IniFilename = nullptr; // 宿主工作区通过内存接口保存布局，不让 ImGui 隐式写入当前目录。

        io.FontDefault = LoadWorkspaceFont(*io.Fonts, m_WorkspaceGlyphRanges);
        ImGui::StyleColorsDark();

        auto* window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
        // 上游适配会链式调用 WindowsWindow 已安装的事件回调，Shutdown 时恢复。
        if (!ImGui_ImplGlfw_InitForVulkan(window, true)) {
            CAT_CORE_ERROR("Cannot initialize ImGui GLFW platform backend.");
            OnDetach();
            return;
        }
        if (m_PlatformRuntime && (io.BackendFlags & ImGuiBackendFlags_PlatformHasViewports))
            InstallPlatformRenderer();
        else
            io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
        ImGui::GetMainViewport()->PlatformHandleRaw = glfwGetWin32Window(window);
        ImGui::GetPlatformIO().Platform_SetImeDataFn = [](ImGuiContext*, ImGuiViewport* viewport,
                                                          ImGuiPlatformImeData* data) {
            const auto hwnd = static_cast<HWND>(viewport->PlatformHandleRaw);
            if (!hwnd || !data->WantVisible)
                return;
            // RmlUi 的显式文本目标拥有当前输入焦点时，不能被 ImGui 的旧 caret 覆盖。
            if (Application::Get().GetWindow().HasTextInputTarget() && GetFocus() == hwnd)
                return;
            const auto context = ImmGetContext(hwnd);
            if (!context)
                return;
            COMPOSITIONFORM composition{};
            composition.dwStyle = CFS_FORCE_POSITION;
            composition.ptCurrentPos = {LONG(data->InputPos.x - viewport->Pos.x),
                                        LONG(data->InputPos.y - viewport->Pos.y)};
            ImmSetCompositionWindow(context, &composition);
            CANDIDATEFORM candidate{};
            candidate.dwStyle = CFS_CANDIDATEPOS;
            candidate.ptCurrentPos = {composition.ptCurrentPos.x,
                                      composition.ptCurrentPos.y + LONG(data->InputLineHeight)};
            ImmSetCandidateWindow(context, &candidate);
            ImmReleaseContext(hwnd, context);
        };
        m_Ready = true;

        // 使用当前库保留的静态字体路径，不声明尚未实现的 RendererHasTextures。
        unsigned char* pixels = nullptr;
        int width = 0, height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        if (!pixels || width <= 0 || height <= 0) {
            CAT_CORE_ERROR("Cannot build ImGui font atlas.");
            OnDetach();
            return;
        }
        if (uint64_t(width) * uint64_t(height) > 16 * 1024 * 1024) {
            CAT_CORE_ERROR("Static font atlas exceeds the 64 MiB limit.");
            OnDetach();
            return;
        }
        m_FontExtent = { static_cast<unsigned int>(width), static_cast<unsigned int>(height) };
        m_FontPixels = { reinterpret_cast<const std::byte*>(pixels), static_cast<size_t>(width) * height * 4 };
        io.Fonts->SetTexID(ImGuiFontAtlasId);
    }

    void ImGuiLayer::OnDetach()
    {
        if (m_Ready) {
            ImGui::DestroyPlatformWindows();
            auto& io = ImGui::GetIO();
            io.BackendRendererUserData = nullptr;
            io.BackendFlags &= ~ImGuiBackendFlags_RendererHasViewports;
            ImGui::GetPlatformIO().ClearRendererHandlers();
            ImGui_ImplGlfw_Shutdown();
        }
        if (m_ContextCreated) ImGui::DestroyContext();
        m_Ready = false;
        m_ContextCreated = false;
        m_FontPixels = {};
        m_FontExtent = {};
        m_FrameData = {};
    }

    void ImGuiLayer::Begin()
    {
        CAT_CORE_ASSERT(m_Ready, "ImGui platform backend is not ready!");
        ImGui_ImplGlfw_NewFrame(); // 更新逻辑尺寸、framebuffer 比例、时间、焦点与光标。
        ImGui::NewFrame();
    }

    void ImGuiLayer::OnEvent(Event& event)
    {
        if (!m_Ready) return;
        const ImGuiIO& io = ImGui::GetIO();
        event.Handled |= (event.IsInCategory(EventCategoryMouse) && io.WantCaptureMouse)
            || (event.IsInCategory(EventCategoryKeyboard) && io.WantCaptureKeyboard);
    }

    InputCapture ImGuiLayer::GetInputCapture() const
    {
        if (!m_Ready) return { true, true, false };
        const auto& io = ImGui::GetIO();
        return { io.WantCaptureKeyboard, io.WantCaptureMouse, true };
    }

    RHIStatus ImGuiLayer::End()
    {
        ImGui::Render();
        if (const auto status = CopyFrame(ImGui::GetDrawData(), m_FrameData); !status.IsSuccess()) return status;
        m_FrameData.platformTextureTokens.clear();
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGui::UpdatePlatformWindows();
            const auto& windows = ImGui::GetPlatformIO().Viewports;
            std::set<uint64_t> tokens;
            for (int i = 1; i < windows.Size; ++i) {
                const auto* data = windows[i]->DrawData;
                if (!data || !data->Valid) continue;
                for (auto* list : data->CmdLists)
                    for (const auto& cmd : list->CmdBuffer)
                        if (!cmd.UserCallback && cmd.ElemCount && cmd.GetTexID() > ImGuiFontAtlasId)
                            tokens.insert(uint64_t(cmd.GetTexID()));
            }
            m_FrameData.platformTextureTokens.assign(tokens.begin(),tokens.end());
        }
        return m_PlatformStatus;
    }

    void ImGuiLayer::InstallPlatformRenderer()
    {
        auto& io = ImGui::GetIO();
        io.BackendRendererUserData = this;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;
        auto& platform = ImGui::GetPlatformIO();
        platform.Renderer_CreateWindow = [](ImGuiViewport* view) {
            // 交换链延迟到首次有非零 framebuffer 的帧创建。
            view->RendererUserData = ImGui::GetIO().BackendRendererUserData;
        };
        platform.Renderer_DestroyWindow = [](ImGuiViewport* view) {
            if (auto* layer = static_cast<ImGuiLayer*>(view->RendererUserData)) {
                auto status = layer->m_PlatformRuntime->ReleasePlatformWindow(view->ID);
                if (!status.IsSuccess()) {
                    CAT_CORE_ERROR("Cannot retire platform window: {0}",status.operation);
                    layer->m_PlatformStatus = status;
                }
                view->RendererUserData = nullptr;
            }
        };
        platform.Renderer_RenderWindow = [](ImGuiViewport* view, void*) {
            auto* layer = static_cast<ImGuiLayer*>(view->RendererUserData);
            if (!layer || !layer->m_PlatformStatus.IsSuccess() || !view->DrawData) return;
            auto* window = static_cast<GLFWwindow*>(view->PlatformHandle);
            if (!window) return;
            int width = 0, height = 0;
            glfwGetFramebufferSize(window,&width,&height);
            if (width <= 0 || height <= 0) return;
            auto& frame = layer->m_PlatformFrame;
            auto status = CopyFrame(view->DrawData,frame);
            if (status.IsSuccess())
                status = layer->m_PlatformRuntime->RenderPlatformWindow(view->ID,window,{uint32_t(width),uint32_t(height)},frame);
            if (status.code == RHIErrorCode::Unsupported) {
                CAT_CORE_WARN("Platform window {} cannot be presented: {}", view->ID, status.operation);
                view->PlatformRequestClose = true;
            } else if (!status.IsSuccess()) {
                layer->m_PlatformStatus = status;
            }
        };
    }

    RHIStatus ImGuiLayer::PresentPlatformWindows()
    {
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
            ImGui::RenderPlatformWindowsDefault();
        return m_PlatformStatus;
    }

    RHIStatus ImGuiLayer::CopyFrame(const ImDrawData* data, ImGuiFrameData& frame)
    {
        frame.vertices.clear();
        frame.indices.clear();
        frame.draws.clear();
        if (!data || !data->Valid || data->TotalVtxCount < 0 || data->TotalIdxCount < 0)
            return { RHIErrorCode::InvalidState, 0, "ImGuiLayer::End/DrawData" };

        frame.displayPos[0] = data->DisplayPos.x;
        frame.displayPos[1] = data->DisplayPos.y;
        frame.displaySize[0] = data->DisplaySize.x;
        frame.displaySize[1] = data->DisplaySize.y;
        frame.framebufferScale[0] = data->FramebufferScale.x;
        frame.framebufferScale[1] = data->FramebufferScale.y;
        frame.vertices.reserve(data->TotalVtxCount);
        frame.indices.reserve(data->TotalIdxCount);

        for (const ImDrawList* list : data->CmdLists) {
            // baseVertex 使用有符号 32 位，累计顶点数不能在强转前溢出。
            if (!list || list->VtxBuffer.Size < 0 || list->IdxBuffer.Size < 0
                || static_cast<size_t>(list->VtxBuffer.Size) > static_cast<size_t>(std::numeric_limits<int32_t>::max()) - frame.vertices.size()
                || static_cast<size_t>(list->IdxBuffer.Size) > static_cast<size_t>(std::numeric_limits<uint32_t>::max()) - frame.indices.size())
                return { RHIErrorCode::InvalidDescription, 0, "ImGuiLayer::End/ListRange" };

            const uint32_t indexBase = static_cast<uint32_t>(frame.indices.size());
            const int32_t vertexBase = static_cast<int32_t>(frame.vertices.size());
            for (const ImDrawVert& vertex : list->VtxBuffer) {
                frame.vertices.push_back({
                    .position = { vertex.pos.x, vertex.pos.y },
                    .uv = { vertex.uv.x, vertex.uv.y },
                    .color = {
                        static_cast<float>((vertex.col >> IM_COL32_R_SHIFT) & 0xff) / 255.0f,
                        static_cast<float>((vertex.col >> IM_COL32_G_SHIFT) & 0xff) / 255.0f,
                        static_cast<float>((vertex.col >> IM_COL32_B_SHIFT) & 0xff) / 255.0f,
                        static_cast<float>((vertex.col >> IM_COL32_A_SHIFT) & 0xff) / 255.0f
                    }
                });
            }
            for (const ImDrawIdx index : list->IdxBuffer) frame.indices.push_back(index);

            for (const ImDrawCmd& command : list->CmdBuffer) {
                if (command.UserCallback) {
                    // 每条图内 draw 都完整绑定自身状态，因此 reset 标记无需额外 GPU 命令。
                    if (command.UserCallback == ImDrawCallback_ResetRenderState) continue;
                    CAT_CORE_ERROR("Unsupported ImGui draw callback; arbitrary callbacks cannot bypass RenderGraph.");
                    return { RHIErrorCode::Unsupported, 0, "ImGuiLayer::End/DrawCallback" };
                }
                if (command.ElemCount == 0) continue;
                if (command.IdxOffset > static_cast<uint32_t>(list->IdxBuffer.Size)
                    || command.ElemCount > static_cast<uint32_t>(list->IdxBuffer.Size) - command.IdxOffset
                    || command.VtxOffset > static_cast<uint32_t>(list->VtxBuffer.Size))
                    return { RHIErrorCode::InvalidDescription, 0, "ImGuiLayer::End/DrawRange" };

                const ImTextureID textureId = command.GetTexID();
                if (textureId == ImTextureID_Invalid)
                    return { RHIErrorCode::InvalidDescription, 0, "ImGuiLayer::End/InvalidTextureId" };

                frame.draws.push_back({
                    .firstIndex = indexBase + command.IdxOffset,
                    .indexCount = command.ElemCount,
                    .baseVertex = vertexBase + static_cast<int32_t>(command.VtxOffset),
                    .clipRect = { command.ClipRect.x, command.ClipRect.y, command.ClipRect.z, command.ClipRect.w },
                    // 字体使用内部零 token；其它 token 由 Renderer 注册并验证，不解释为原生地址。
                    .textureToken = textureId == ImGuiFontAtlasId ? 0 : static_cast<uint64_t>(textureId)
                });
            }
        }
        return {};
    }

}
