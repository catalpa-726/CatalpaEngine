#include "AssetEditors/TexturePreviewEditor.h"
#include "Core/Application.h"
#include "Renderer/RenderSurface.h"
#include "imgui.h"

namespace Catalpa::Editor {
    namespace {
        constexpr uint64_t ImageSurface = 0x434154494d474501ULL;
    }

    void TexturePreviewEditor::Close(Application& app) {
        app.CloseEditorPreview(ImageSurface);
        m_Document = {};
    }

    void TexturePreviewEditor::Draw(Application& app, AssetDocument& doc, const PanelText& text,
                                    std::string& error) {
        if (m_Document != doc.Id()) {
            m_Mip = m_Channels = 0;
            m_Document = doc.Id();
        }
        const auto& image = doc.Image()->image;
        ImGui::Text("%u x %u  |  %s  |  %u mip(s)", image.width, image.height,
                    image.colorSpace == ImageColorSpace::SRGB ? "sRGB" : "Linear", image.mipLevels);
        constexpr std::array<const char*, 18> formats{
            "Unknown",  "RGBA8 UNorm", "RGBA8 sRGB",   "BGRA8 UNorm",  "BGRA8 sRGB", "D32 float",
            "R8 UNorm", "R16 float",   "RGBA16 float", "RGBA32 float", "R32 uint",   "BC1 UNorm",
            "BC1 sRGB", "BC3 UNorm",   "BC3 sRGB",     "BC5 UNorm",    "BC7 UNorm",  "BC7 sRGB"};
        const auto format = static_cast<size_t>(image.format);
        ImGui::Text("%s  |  %u layer(s)", format < formats.size() ? formats[format] : "Unknown",
                    image.arrayLayers);
        ImGui::TextWrapped("%s", doc.Image()->sourceUri.c_str());
        if (image.dimension != ImageDimension::Texture2D || image.arrayLayers != 1) {
            ImGui::TextUnformatted(
                text("texture.unsupported", "Preview supports single-layer 2D images.", nullptr).c_str());
            app.CloseEditorPreview(ImageSurface);
            return;
        }
        ImGui::SliderInt(text("texture.mip", "Mip", "TextureMip").c_str(), &m_Mip, 0,
                         int(image.mipLevels) - 1);
        ImGui::Combo(text("texture.channel", "Channel", "TextureChannel").c_str(), &m_Channels,
                     "RGBA\0Red\0Green\0Blue\0Alpha\0");
        constexpr uint32_t masks[]{15, 1, 2, 4, 8};
        std::string previewError;
        if (!app.SetEditorPreviewImage(ImageSurface, doc.ImageVersion(), uint32_t(m_Mip), 0,
                                       masks[m_Channels], previewError)) {
            error = std::move(previewError);
            return;
        }
        RenderSurfaceSnapshot surface;
        if (app.GetEditorPreview(ImageSurface, surface) && surface.textureToken) {
            const float width = std::clamp(ImGui::GetContentRegionAvail().x, 1.f, 512.f);
            ImGui::Image(ImTextureRef(static_cast<ImTextureID>(surface.textureToken)),
                         {width, width * image.height / image.width});
        }
    }
}
