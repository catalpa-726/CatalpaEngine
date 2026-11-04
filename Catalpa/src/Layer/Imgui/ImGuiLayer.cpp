#include "catpch.h"
#include "ImGuiLayer.h"

#include "Core/Application.h"
#include "Log/Log.h"
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include <GLFW/glfw3.h>

namespace Catalpa {

    // S01 仅注册静态字体 atlas；其它 UI 纹理需要后续资源注册协议。
    static constexpr ImTextureID FontTextureId = 1;

    ImGuiLayer::ImGuiLayer() : Layer("ImGuiLayer") {}
    ImGuiLayer::~ImGuiLayer() { OnDetach(); }

    void ImGuiLayer::OnAttach()
    {
        CAT_CORE_ASSERT(!m_ContextCreated, "ImGui layer already attached!");
        if (m_ContextCreated) return;
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        m_ContextCreated = true;

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
        // 只支持主窗口；多原生窗口需要各自的交换链和完成/退休协议。
        io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
        io.BackendRendererName = "Catalpa_RenderGraph";
        io.IniFilename = nullptr; // 宿主工作区通过内存接口保存布局，不让 ImGui 隐式写入当前目录。
        ImGui::StyleColorsDark();

        auto* window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
        // 上游适配会链式调用 WindowsWindow 已安装的事件回调，Shutdown 时恢复。
        if (!ImGui_ImplGlfw_InitForVulkan(window, true)) {
            CAT_CORE_ERROR("Cannot initialize ImGui GLFW platform backend.");
            OnDetach();
            return;
        }
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
        m_FontExtent = { static_cast<unsigned int>(width), static_cast<unsigned int>(height) };
        m_FontPixels = { reinterpret_cast<const std::byte*>(pixels), static_cast<size_t>(width) * height * 4 };
        io.Fonts->SetTexID(FontTextureId);
    }

    void ImGuiLayer::OnDetach()
    {
        if (m_Ready) ImGui_ImplGlfw_Shutdown();
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
        const ImDrawData* data = ImGui::GetDrawData();
        m_FrameData.vertices.clear();
        m_FrameData.indices.clear();
        m_FrameData.draws.clear();
        if (!data || !data->Valid || data->TotalVtxCount < 0 || data->TotalIdxCount < 0)
            return { RHIErrorCode::InvalidState, 0, "ImGuiLayer::End/DrawData" };

        m_FrameData.displayPos[0] = data->DisplayPos.x;
        m_FrameData.displayPos[1] = data->DisplayPos.y;
        m_FrameData.displaySize[0] = data->DisplaySize.x;
        m_FrameData.displaySize[1] = data->DisplaySize.y;
        m_FrameData.framebufferScale[0] = data->FramebufferScale.x;
        m_FrameData.framebufferScale[1] = data->FramebufferScale.y;
        m_FrameData.vertices.reserve(data->TotalVtxCount);
        m_FrameData.indices.reserve(data->TotalIdxCount);

        for (const ImDrawList* list : data->CmdLists) {
            // baseVertex 使用有符号 32 位，累计顶点数不能在强转前溢出。
            if (!list || list->VtxBuffer.Size < 0 || list->IdxBuffer.Size < 0
                || static_cast<size_t>(list->VtxBuffer.Size) > static_cast<size_t>(std::numeric_limits<int32_t>::max()) - m_FrameData.vertices.size()
                || static_cast<size_t>(list->IdxBuffer.Size) > static_cast<size_t>(std::numeric_limits<uint32_t>::max()) - m_FrameData.indices.size())
                return { RHIErrorCode::InvalidDescription, 0, "ImGuiLayer::End/ListRange" };

            const uint32_t indexBase = static_cast<uint32_t>(m_FrameData.indices.size());
            const int32_t vertexBase = static_cast<int32_t>(m_FrameData.vertices.size());
            for (const ImDrawVert& vertex : list->VtxBuffer) {
                m_FrameData.vertices.push_back({
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
            for (const ImDrawIdx index : list->IdxBuffer) m_FrameData.indices.push_back(index);

            for (const ImDrawCmd& command : list->CmdBuffer) {
                if (command.UserCallback) {
                    // 每条图内 draw 都完整绑定自身状态，因此 reset 标记无需额外 GPU 命令。
                    if (command.UserCallback == ImDrawCallback_ResetRenderState) continue;
                    CAT_CORE_ERROR("Unsupported ImGui draw callback; arbitrary callbacks cannot bypass RenderGraph.");
                    return { RHIErrorCode::Unsupported, 0, "ImGuiLayer::End/DrawCallback" };
                }
                if (command.ElemCount == 0) continue;
                if (command.TexRef.GetTexID() != FontTextureId) {
                    CAT_CORE_ERROR("Unsupported ImGui texture; S01 only registers the font atlas.");
                    return { RHIErrorCode::Unsupported, 0, "ImGuiLayer::End/Texture" };
                }
                if (command.IdxOffset > static_cast<uint32_t>(list->IdxBuffer.Size)
                    || command.ElemCount > static_cast<uint32_t>(list->IdxBuffer.Size) - command.IdxOffset
                    || command.VtxOffset > static_cast<uint32_t>(list->VtxBuffer.Size))
                    return { RHIErrorCode::InvalidDescription, 0, "ImGuiLayer::End/DrawRange" };

                m_FrameData.draws.push_back({
                    .firstIndex = indexBase + command.IdxOffset,
                    .indexCount = command.ElemCount,
                    .baseVertex = vertexBase + static_cast<int32_t>(command.VtxOffset),
                    .clipRect = { command.ClipRect.x, command.ClipRect.y, command.ClipRect.z, command.ClipRect.w }
                });
            }
        }
        return {};
    }

}
