#include "catpch.h"
#include "UI/UIRuntime.h"
#include "Core/Memory/MemoryBudget.h"
#include <RmlUi/Core.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/SystemInterface.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/TextInputHandler.h>
#include <RmlUi/Core/TextInputContext.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>

namespace Catalpa {
    namespace {
        uint64_t nextUIHandle = 1;
        MemoryBudget uiTextureBudget{64 * 1024 * 1024};
        uintptr_t NewHandle() {
            if (nextUIHandle == UINT64_MAX)
                throw std::overflow_error("UI generation exhausted");
            return uintptr_t(nextUIHandle++);
        }
        float ToLinear(float value) {
            return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
        }
        std::string EscapeText(std::string_view text) {
            std::string result;
            for (char c : text)
                switch (c) {
                case '&':
                    result += "&amp;";
                    break;
                case '<':
                    result += "&lt;";
                    break;
                case '>':
                    result += "&gt;";
                    break;
                default:
                    result += c;
                }
            return result;
        }

        class UIDrawAdapter final : public Rml::RenderInterface {
            struct Geometry {
                std::vector<Rml::Vertex> vertices;
                std::vector<int> indices;
            };
            std::map<Rml::CompiledGeometryHandle, Geometry> geometries;
            std::map<Rml::TextureHandle, std::shared_ptr<const UITextureData>> textures;
            std::shared_ptr<UIDrawData> frame;
            std::shared_ptr<const UIDocumentAsset> asset;
            std::string prefix;
            Rml::Matrix4f transform = Rml::Matrix4f::Identity();
            Rml::Rectanglei scissor{};
            bool scissorEnabled = false, failed = false;
            std::string resourceError;
            size_t vertexCount = 0, indexCount = 0, textureBytes = 0;
            void Reject(std::string error) {
                failed = true;
                if (!frame && resourceError.empty())
                    resourceError = error;
                if (frame && frame->diagnostics.size() < 32)
                    frame->diagnostics.push_back(std::move(error));
            }

          public:
            void Bind(std::shared_ptr<const UIDocumentAsset> value, std::string path) {
                asset = std::move(value);
                prefix = std::move(path);
            }
            void Begin(uint32_t width, uint32_t height) {
                frame = std::make_shared<UIDrawData>();
                frame->width = width;
                frame->height = height;
                scissorEnabled = false;
                failed = false;
                transform = Rml::Matrix4f::Identity();
                if (!resourceError.empty())
                    Reject(resourceError);
            }
            bool Finish(std::shared_ptr<const UIDrawData>& output, std::string& error) {
                if (failed) {
                    error = frame->diagnostics.empty() ? "Unsupported or over-budget UI draw"
                                                       : frame->diagnostics.front();
                    frame.reset();
                    return false;
                }
                output = std::move(frame);
                error.clear();
                return true;
            }
            Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                        Rml::Span<const int> indices) override {
                if (vertices.size() > 1048576 - vertexCount || indices.size() > 3145728 - indexCount ||
                    indices.size() % 3) {
                    Reject("UI compiled geometry budget exceeded");
                    return 0;
                }
                for (int index : indices)
                    if (index < 0 || size_t(index) >= vertices.size()) {
                        Reject("UI index is out of range");
                        return 0;
                    }
                Geometry value;
                value.vertices.assign(vertices.begin(), vertices.end());
                value.indices.assign(indices.begin(), indices.end());
                const auto id = NewHandle();
                geometries.emplace(id, std::move(value));
                vertexCount += vertices.size();
                indexCount += indices.size();
                return id;
            }
            void ReleaseGeometry(Rml::CompiledGeometryHandle id) override {
                const auto it = geometries.find(id);
                if (it == geometries.end())
                    return;
                vertexCount -= it->second.vertices.size();
                indexCount -= it->second.indices.size();
                geometries.erase(it);
            }
            void RenderGeometry(Rml::CompiledGeometryHandle id, Rml::Vector2f translation,
                                Rml::TextureHandle texture) override {
                if (!frame)
                    return;
                const auto it = geometries.find(id);
                if (it == geometries.end() || frame->batches.size() >= 4096 ||
                    it->second.vertices.size() > 1048576 - frame->vertices.size() ||
                    it->second.indices.size() > 3145728 - frame->indices.size()) {
                    Reject("UI frame geometry budget rejected");
                    return;
                }
                const auto& geometry = it->second;
                UIDrawBatch batch;
                batch.baseVertex = int32_t(frame->vertices.size());
                batch.firstIndex = uint32_t(frame->indices.size());
                batch.indexCount = uint32_t(geometry.indices.size());
                if (texture) {
                    const auto found = textures.find(texture);
                    if (found == textures.end()) {
                        Reject("UI texture handle is stale");
                        return;
                    }
                    batch.texture = found->second;
                }
                int left = 0, top = 0, right = int(frame->width), bottom = int(frame->height);
                if (scissorEnabled) {
                    left = std::clamp(scissor.Left(), 0, right);
                    top = std::clamp(scissor.Top(), 0, bottom);
                    right = std::clamp(scissor.Right(), left, right);
                    bottom = std::clamp(scissor.Bottom(), top, bottom);
                }
                batch.scissor = {left, top, right - left, bottom - top};
                for (const auto& source : geometry.vertices) {
                    const auto p = transform * Rml::Vector4f(source.position.x + translation.x,
                                                             source.position.y + translation.y, 0, 1);
                    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.w) || p.w <= 1e-7f) {
                        Reject("UI transform projects behind the clip plane");
                        return;
                    }
                    UIDrawVertex vertex;
                    vertex.position[0] = p.x / p.w;
                    vertex.position[1] = p.y / p.w;
                    vertex.uv[0] = source.tex_coord.x;
                    vertex.uv[1] = source.tex_coord.y;
                    const float alpha = source.colour.alpha / 255.0f;
                    const auto linear = [alpha](uint8_t v) {
                        return alpha > 0 ? ToLinear(std::clamp(v / 255.0f / alpha, 0.0f, 1.0f)) * alpha
                                         : 0.0f;
                    };
                    vertex.color[0] = linear(source.colour.red);
                    vertex.color[1] = linear(source.colour.green);
                    vertex.color[2] = linear(source.colour.blue);
                    vertex.color[3] = alpha;
                    frame->vertices.push_back(vertex);
                }
                for (int index : geometry.indices)
                    frame->indices.push_back(uint32_t(index));
                frame->batches.push_back(std::move(batch));
            }
            Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source,
                                               Rml::Vector2i size) override {
                if (size.x <= 0 || size.y <= 0 || size.x > 8192 || size.y > 8192 ||
                    uint64_t(size.x) * size.y * 4 != source.size() ||
                    source.size() > 64 * 1024 * 1024 - textureBytes) {
                    Reject("UI generated texture budget rejected");
                    return 0;
                }
                auto lease = std::make_shared<MemoryReservation>();
                if (!uiTextureBudget.TryReserve(source.size(), *lease)) {
                    Reject("UI shared texture workspace exceeded");
                    return 0;
                }
                auto texture = std::make_shared<UITextureData>();
                texture->generation = NewHandle();
                texture->width = size.x;
                texture->height = size.y;
                texture->storageLease = std::move(lease);
                texture->rgba8.resize(source.size());
                std::memcpy(texture->rgba8.data(), source.data(), source.size());
                const auto id = NewHandle();
                textures.emplace(id, std::move(texture));
                textureBytes += source.size();
                return id;
            }
            Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) override {
                std::string path = source;
                if (path.starts_with(prefix))
                    path.erase(0, prefix.size());
                if (!asset || !IsUIRelativePath(path)) {
                    Reject("UI image path is outside its bundle");
                    return 0;
                }
                const auto found = asset->images.find(path);
                if (found == asset->images.end()) {
                    Reject("UI image dependency is missing: " + path);
                    return 0;
                }
                const auto& image = found->second.Get()->image;
                const bool rgba =
                    image.format == PixelFormat::RGBA8_UNorm || image.format == PixelFormat::RGBA8_SRGB;
                if (!rgba || image.dimension != ImageDimension::Texture2D || image.arrayLayers != 1 ||
                    image.subresources.empty() ||
                    (image.colorSpace != ImageColorSpace::Linear &&
                     image.colorSpace != ImageColorSpace::SRGB) ||
                    (image.alphaMode != ImageAlphaMode::Opaque &&
                     image.alphaMode != ImageAlphaMode::Straight &&
                     image.alphaMode != ImageAlphaMode::Premultiplied)) {
                    Reject("Game UI requires a derived RGBA8 2D image");
                    return 0;
                }
                const auto& sub = image.subresources.front();
                const uint64_t count = uint64_t(sub.width) * sub.height * 4;
                if (sub.width == 0 || sub.height == 0 || sub.width > 8192 || sub.height > 8192 ||
                    count > 64 * 1024 * 1024 - textureBytes || sub.rowPitch < uint64_t(sub.width) * 4 ||
                    sub.offset > image.pixels.size() ||
                    uint64_t(sub.height - 1) * sub.rowPitch + uint64_t(sub.width) * 4 >
                        image.pixels.size() - sub.offset) {
                    Reject("UI source image layout rejected");
                    return 0;
                }
                auto lease = std::make_shared<MemoryReservation>();
                if (!uiTextureBudget.TryReserve(size_t(count), *lease)) {
                    Reject("UI shared texture workspace exceeded");
                    return 0;
                }
                auto texture = std::make_shared<UITextureData>();
                texture->generation = NewHandle();
                texture->width = sub.width;
                texture->height = sub.height;
                texture->storageLease = std::move(lease);
                texture->srgb = image.colorSpace == ImageColorSpace::SRGB;
                texture->source = found->second;
                texture->rgba8.resize(size_t(count));
                for (uint32_t y = 0; y < sub.height; ++y)
                    std::memcpy(texture->rgba8.data() + size_t(y) * sub.width * 4,
                                image.pixels.data() + sub.offset + y * sub.rowPitch, size_t(sub.width) * 4);
                // 生成 UI 专用预乘副本；作者 ImageAsset 的像素和 alpha 语义保持原样。
                if (image.alphaMode != ImageAlphaMode::Premultiplied)
                    for (size_t i = 0; i < texture->rgba8.size(); i += 4) {
                        const unsigned alpha = image.alphaMode == ImageAlphaMode::Opaque
                                                   ? 255
                                                   : std::to_integer<unsigned>(texture->rgba8[i + 3]);
                        texture->rgba8[i + 3] = std::byte(alpha);
                        for (size_t c = 0; c < 3; ++c)
                            texture->rgba8[i + c] = std::byte(
                                (std::to_integer<unsigned>(texture->rgba8[i + c]) * alpha + 127) / 255);
                    }
                dimensions = {int(sub.width), int(sub.height)};
                const auto id = NewHandle();
                textureBytes += size_t(count);
                textures.emplace(id, std::move(texture));
                return id;
            }
            void ReleaseTexture(Rml::TextureHandle id) override {
                const auto it = textures.find(id);
                if (it != textures.end()) {
                    textureBytes -= it->second->rgba8.size();
                    textures.erase(it);
                }
            }
            void EnableScissorRegion(bool enabled) override {
                scissorEnabled = enabled;
            }
            void SetScissorRegion(Rml::Rectanglei value) override {
                scissor = value;
            }
            void SetTransform(const Rml::Matrix4f* value) override {
                transform = value ? *value : Rml::Matrix4f::Identity();
            }
            void EnableClipMask(bool enabled) override {
                if (enabled)
                    Reject("Game UI stencil clip masks are not supported");
            }
            void RenderToClipMask(Rml::ClipMaskOperation, Rml::CompiledGeometryHandle,
                                  Rml::Vector2f) override {
                Reject("Game UI clip mask operation is unsupported");
            }
            Rml::LayerHandle PushLayer() override {
                Reject("Game UI filter/layer compositing is unsupported");
                return 0;
            }
            void CompositeLayers(Rml::LayerHandle, Rml::LayerHandle, Rml::BlendMode,
                                 Rml::Span<const Rml::CompiledFilterHandle>) override {
                Reject("Game UI layer compositing is unsupported");
            }
            Rml::CompiledFilterHandle CompileFilter(const Rml::String&, const Rml::Dictionary&) override {
                Reject("Game UI custom filters are unsupported");
                return 0;
            }
            Rml::CompiledShaderHandle CompileShader(const Rml::String&, const Rml::Dictionary&) override {
                Reject("Game UI custom shaders are unsupported");
                return 0;
            }
        };

        struct UIHost final : Rml::FileInterface, Rml::SystemInterface {
            struct File {
                std::string bytes;
                size_t cursor = 0;
            };
            std::thread::id owner = std::this_thread::get_id();
            std::map<std::string, std::shared_ptr<const UIDocumentAsset>, std::less<>> bundles;
            std::map<Rml::FileHandle, File> files;
            std::vector<AssetVersionRef<FontAsset>> fonts;
            std::set<std::string> fontNames;
            std::vector<std::string> diagnostics;
            UIDrawAdapter fallback;
            double elapsed = 0;
            TextInputSnapshot* currentText = nullptr;
            bool initialized = false;
            ~UIHost() {
                if (initialized)
                    Rml::Shutdown();
            }
            bool Initialize(std::string& error) {
                Rml::SetFileInterface(this);
                Rml::SetSystemInterface(this);
                Rml::SetRenderInterface(&fallback);
                initialized = Rml::Initialise();
                if (!initialized) {
                    error = "RmlUi initialization failed";
                    return false;
                }
                return true;
            }
            bool Owner(std::string& error) const {
                if (owner != std::this_thread::get_id()) {
                    error = "RmlUi requires its owner thread";
                    return false;
                }
                return true;
            }
            double GetElapsedTime() override {
                return elapsed;
            }
            void ActivateKeyboard(Rml::Vector2f caret, float height) override {
                if (currentText && std::isfinite(caret.x) && std::isfinite(caret.y) &&
                    std::isfinite(height) && height > 0) {
                    currentText->x = caret.x;
                    currentText->y = caret.y;
                    currentText->width = 1;
                    currentText->height = height;
                }
            }
            bool LogMessage(Rml::Log::Type, const Rml::String& message) override {
                if (diagnostics.size() < 64)
                    diagnostics.push_back(message.substr(0, 2048));
                return true;
            }
            void JoinPath(Rml::String& out, const Rml::String& document, const Rml::String& path) override {
                if (!IsUIRelativePath(path)) {
                    out = "invalid-ui-path";
                    return;
                }
                const auto slash = document.find_last_of('/');
                out = slash == document.npos ? path : document.substr(0, slash + 1) + path;
            }
            Rml::FileHandle Open(const Rml::String& path) override {
                if (files.size() >= 128)
                    return 0;
                for (const auto& [prefix, asset] : bundles)
                    if (path.starts_with(prefix)) {
                        const auto it = asset->files.find(path.substr(prefix.size()));
                        if (it == asset->files.end())
                            return 0;
                        const auto id = NewHandle();
                        files.emplace(id, File{it->second, 0});
                        return id;
                    }
                return 0;
            }
            void Close(Rml::FileHandle file) override {
                files.erase(file);
            }
            size_t Read(void* out, size_t size, Rml::FileHandle file) override {
                const auto it = files.find(file);
                if (it == files.end() || !out)
                    return 0;
                const auto count = std::min(size, it->second.bytes.size() - it->second.cursor);
                std::memcpy(out, it->second.bytes.data() + it->second.cursor, count);
                it->second.cursor += count;
                return count;
            }
            bool Seek(Rml::FileHandle file, long offset, int origin) override {
                const auto it = files.find(file);
                if (it == files.end())
                    return false;
                const int64_t base = origin == SEEK_SET   ? 0
                                     : origin == SEEK_CUR ? int64_t(it->second.cursor)
                                     : origin == SEEK_END ? int64_t(it->second.bytes.size())
                                                          : -1;
                if (base < 0 || base + int64_t(offset) < 0 ||
                    uint64_t(base + int64_t(offset)) > it->second.bytes.size())
                    return false;
                it->second.cursor = size_t(base + offset);
                return true;
            }
            size_t Tell(Rml::FileHandle file) override {
                const auto it = files.find(file);
                return it == files.end() ? 0 : it->second.cursor;
            }
            size_t Length(Rml::FileHandle file) override {
                const auto it = files.find(file);
                return it == files.end() ? 0 : it->second.bytes.size();
            }
        };
        std::weak_ptr<UIHost> sharedHost;
    } // namespace

    struct UIRuntime::Impl final : Rml::EventListener, Rml::TextInputHandler {
        std::shared_ptr<UIHost> host;
        UIDrawAdapter renderer;
        AssetVersionRef<UIDocumentAsset> asset;
        Rml::Context* context = nullptr;
        Rml::ElementDocument* document = nullptr;
        std::string name, prefix;
        std::vector<UIEvent> events;
        std::map<std::string, std::string, std::less<>> modelValues;
        Rml::DataModelHandle model;
        uint64_t sequence = 1;
        bool eventOverflow = false;
        uint32_t width = 0, height = 0;
        double clockOrigin = 0, lastElapsed = 0;
        TextInputSnapshot textState;
        Rml::TextInputContext* textContext = nullptr;
        Rml::ElementFormControl* compositionControl = nullptr;
        std::string originalText;
        int compositionStart = 0, compositionEnd = 0, selectionEnd = 0;
        static int Scalars(std::string_view text) {
            int count = 0;
            for (unsigned char c : text)
                if ((c & 0xc0) != 0x80)
                    ++count;
            return count;
        }
        Rml::ElementFormControl* Control() const {
            return context ? dynamic_cast<Rml::ElementFormControl*>(context->GetFocusElement()) : nullptr;
        }
        void ClearComposition() {
            compositionControl = nullptr;
            originalText.clear();
            compositionStart = compositionEnd = selectionEnd = 0;
        }
        void CancelComposition() {
            if (compositionControl && textContext) {
                compositionControl->SetValue(originalText);
                textContext->SetCompositionRange(0, 0);
                textContext->SetSelectionRange(compositionStart, selectionEnd);
            }
            ClearComposition();
        }
        void OnActivate(Rml::TextInputContext* value) override {
            if (textContext != value)
                CancelComposition();
            // RmlUi 在 Focus 事件返回后才更新 GetFocusElement()；在组合输入开始时再绑定控件。
            textContext = value;
            textState.active = value != nullptr;
        }
        void OnDeactivate(Rml::TextInputContext* value) override {
            if (textContext == value) {
                CancelComposition();
                textContext = nullptr;
                textState.active = false;
            }
        }
        void OnDestroy(Rml::TextInputContext* value) override {
            if (textContext == value) {
                textContext = nullptr;
                textState.active = false;
                ClearComposition();
            }
        }
        bool Composition(const TextComposition& value, std::string& error) {
            if (value.phase == TextComposition::Phase::Cancel) {
                CancelComposition();
                error.clear();
                return true;
            }
            auto* control = Control();
            if (!textContext || !control) {
                error = "Game UI IME has no live editable focus";
                return false;
            }
            if (value.text.size() > 16384 || value.text.find('\0') != value.text.npos ||
                !IsUIUtf8(value.text) || value.caret > uint32_t(Scalars(value.text))) {
                error = "IME composition UTF-8/cursor budget rejected";
                return false;
            }
            if (value.phase == TextComposition::Phase::Start || compositionControl != control) {
                if (compositionControl)
                    CancelComposition();
                originalText = control->GetValue();
                if (originalText.size() > 65536 || !IsUIUtf8(originalText)) {
                    error = "IME input field exceeds text budget";
                    return false;
                }
                textContext->GetSelectionRange(compositionStart, selectionEnd);
                if (compositionStart > selectionEnd)
                    std::swap(compositionStart, selectionEnd);
                compositionEnd = selectionEnd;
                compositionControl = control;
                if (value.phase == TextComposition::Phase::Start) {
                    error.clear();
                    return true;
                }
            }
            const auto count = Scalars(value.text);
            if (originalText.size() + value.text.size() > 65536) {
                CancelComposition();
                error = "IME resulting input exceeds byte budget";
                return false;
            }
            if (value.phase == TextComposition::Phase::Commit) {
                const int maxLength = control->GetAttribute<int>("maxlength", -1);
                if (maxLength >= 0 &&
                    Scalars(originalText) - (selectionEnd - compositionStart) + count > maxLength) {
                    CancelComposition();
                    error = "IME commit exceeds authored maxlength; original value retained";
                    return false;
                }
            }
            textContext->SetText(Rml::StringView(value.text), compositionStart, compositionEnd);
            compositionEnd = compositionStart + count;
            textContext->SetCursorPosition(compositionStart + int(value.caret));
            if (value.phase == TextComposition::Phase::Commit) {
                textContext->SetCompositionRange(0, 0);
                textContext->SetCursorPosition(compositionEnd);
                ClearComposition();
                Rml::Dictionary parameters;
                parameters["value"] = control->GetValue();
                control->DispatchEvent("change", parameters);
            } else
                textContext->SetCompositionRange(compositionStart, compositionEnd);
            error.clear();
            return true;
        }
        bool Owner(std::string& error) const {
            if (!context || !host) {
                error = "Game UI is not initialized";
                return false;
            }
            return host->Owner(error);
        }
        void ProcessEvent(Rml::Event& event) override {
            if (events.size() >= 256 || sequence == UINT64_MAX) {
                eventOverflow = true;
                return;
            }
            const auto* target = event.GetTargetElement();
            events.push_back({sequence++, target ? target->GetId() : "", event.GetType(),
                              event.GetParameter<Rml::String>("value", "")});
        }
    };

    UIRuntime::UIRuntime() : m_Impl(std::make_unique<Impl>()) {}
    UIRuntime::~UIRuntime() {
        Shutdown();
    }
    bool UIRuntime::Initialize(AssetVersionRef<UIDocumentAsset> asset, uint32_t width, uint32_t height,
                               std::string& error) {
        if (m_Impl->context || !asset.HasValue() || !ValidateUIDocument(*asset.Get(), error) || width == 0 ||
            height == 0 || width > 16384 || height > 16384) {
            if (error.empty())
                error = "Invalid Game UI initialization";
            return false;
        }
        try {
            auto host = sharedHost.lock();
            if (!host) {
                host = std::make_shared<UIHost>();
                if (!host->Initialize(error))
                    return false;
                sharedHost = host;
            }
            if (!host->Owner(error))
                return false;
            m_Impl->host = host;
            m_Impl->asset = std::move(asset);
            m_Impl->width = width;
            m_Impl->height = height;
            m_Impl->clockOrigin = host->elapsed;
            m_Impl->lastElapsed = 0;
            m_Impl->name = "catalpa_ui_" + std::to_string(NewHandle());
            m_Impl->prefix = m_Impl->name + "/";
            host->bundles.emplace(m_Impl->prefix, m_Impl->asset.GetShared());
            m_Impl->renderer.Bind(m_Impl->asset.GetShared(), m_Impl->prefix);
            std::string firstFamily;
            for (const auto& [path, font] : m_Impl->asset.Get()->fonts) {
                const auto family =
                    "catalpa_" + font.GetId().ToString() + "_" + std::to_string(font.GetVersion());
                if (firstFamily.empty())
                    firstFamily = family;
                if (host->fontNames.contains(family))
                    continue;
                if (host->fonts.size() >= 64) {
                    error = "Live Game UI font version budget exceeded";
                    Shutdown();
                    return false;
                }
                const auto& bytes = font.Get()->bytes;
                if (!Rml::LoadFontFace(Rml::Span<const Rml::byte>(
                                           reinterpret_cast<const Rml::byte*>(bytes.data()), bytes.size()),
                                       family, Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Auto,
                                       true)) {
                    error = "RmlUi rejected pinned font: " + path;
                    Shutdown();
                    return false;
                }
                host->fontNames.insert(family);
                host->fonts.push_back(font);
            }
            m_Impl->context =
                Rml::CreateContext(m_Impl->name, {int(width), int(height)}, &m_Impl->renderer, m_Impl.get());
            if (!m_Impl->context) {
                error = "Cannot create Game UI context";
                Shutdown();
                return false;
            }
            m_Impl->modelValues = m_Impl->asset.Get()->values;
            auto model = m_Impl->context->CreateDataModel("catalpa");
            if (!model) {
                error = "Cannot create Game UI value model";
                Shutdown();
                return false;
            }
            for (auto& [name, value] : m_Impl->modelValues)
                if (!model.Bind(name, &value)) {
                    error = "Cannot bind Game UI model variable: " + name;
                    Shutdown();
                    return false;
                }
            m_Impl->model = model.GetModelHandle();
            struct TextScope {
                UIHost& host;
                TextInputSnapshot* before;
                ~TextScope() {
                    host.currentText = before;
                }
            } textScope{*host, host->currentText};
            host->currentText = &m_Impl->textState;
            m_Impl->document = m_Impl->context->LoadDocument(m_Impl->prefix + m_Impl->asset.Get()->entry);
            if (!m_Impl->document) {
                error = "Cannot load Game UI RML document";
                Shutdown();
                return false;
            }
            // 默认字体绑定到精确资产版本的别名，避免全局字体缓存悄悄复用旧版本。
            if (!firstFamily.empty())
                m_Impl->document->SetProperty("font-family", firstFamily);
            for (const char* type : {"click", "change", "input", "submit"})
                m_Impl->context->AddEventListener(type, m_Impl.get());
            for (const auto& [element, value] : m_Impl->asset.Get()->values)
                if (!SetValue(element, value, error)) {
                    Shutdown();
                    return false;
                }
            m_Impl->document->Show();
            error.clear();
            return true;
        } catch (const std::exception& e) {
            error = e.what();
            Shutdown();
            return false;
        }
    }
    bool UIRuntime::Resize(uint32_t width, uint32_t height, std::string& error) {
        if (!m_Impl->Owner(error))
            return false;
        if (width == 0 || height == 0 || width > 16384 || height > 16384) {
            error = "Game UI target dimensions rejected";
            return false;
        }
        m_Impl->context->SetDimensions({int(width), int(height)});
        m_Impl->width = width;
        m_Impl->height = height;
        error.clear();
        return true;
    }
    bool UIRuntime::Advance(double elapsed, std::string& error) {
        if (!m_Impl->Owner(error))
            return false;
        if (!std::isfinite(elapsed) || elapsed < m_Impl->lastElapsed) {
            error = "Game UI clock must be finite and monotonic";
            return false;
        }
        m_Impl->lastElapsed = elapsed;
        m_Impl->host->elapsed = std::max(m_Impl->host->elapsed, m_Impl->clockOrigin + elapsed);
        for (const auto& [name, value] : m_Impl->modelValues)
            if (value.size() > 65536) {
                error = "UI model value exceeded its text budget: " + name;
                return false;
            }
        struct TextScope {
            UIHost& host;
            TextInputSnapshot* before;
            ~TextScope() {
                host.currentText = before;
            }
        } scope{*m_Impl->host, m_Impl->host->currentText};
        m_Impl->host->currentText = &m_Impl->textState;
        if (!m_Impl->context->Update()) {
            error = "RmlUi document update failed";
            return false;
        }
        error.clear();
        return true;
    }
    bool UIRuntime::SetDensity(float value, std::string& error) {
        if (!m_Impl->Owner(error))
            return false;
        if (!std::isfinite(value) || value < 0.5f || value > 4) {
            error = "Game UI density must lie in [0.5,4]";
            return false;
        }
        m_Impl->context->SetDensityIndependentPixelRatio(value);
        error.clear();
        return true;
    }
    bool UIRuntime::Input(const UIInputEvent& input, bool& consumed, std::string& error) {
        if (!m_Impl->Owner(error))
            return false;
        if (!std::isfinite(input.x) || !std::isfinite(input.y) || std::abs(input.x) > 1e6f ||
            std::abs(input.y) > 1e6f) {
            error = "Game UI input coordinates rejected";
            return false;
        }
        auto* context = m_Impl->context;
        bool propagate = true;
        struct TextScope {
            UIHost& host;
            TextInputSnapshot* before;
            ~TextScope() {
                host.currentText = before;
            }
        } scope{*m_Impl->host, m_Impl->host->currentText};
        m_Impl->host->currentText = &m_Impl->textState;
        switch (input.type) {
        case UIInputEvent::Type::Move:
            propagate = context->ProcessMouseMove(int(input.x), int(input.y), input.modifiers);
            break;
        case UIInputEvent::Type::ButtonDown:
            if (input.key < 0 || input.key > 2) {
                error = "Invalid UI mouse button";
                return false;
            }
            propagate = context->ProcessMouseButtonDown(input.key, input.modifiers);
            break;
        case UIInputEvent::Type::ButtonUp:
            if (input.key < 0 || input.key > 2) {
                error = "Invalid UI mouse button";
                return false;
            }
            propagate = context->ProcessMouseButtonUp(input.key, input.modifiers);
            break;
        case UIInputEvent::Type::Wheel:
            propagate = context->ProcessMouseWheel(Rml::Vector2f(input.x, input.y), input.modifiers);
            break;
        case UIInputEvent::Type::KeyDown:
        case UIInputEvent::Type::KeyUp:
            if (input.key < 0 || input.key > 255) {
                error = "Invalid RmlUi key identifier";
                return false;
            }
            propagate = input.type == UIInputEvent::Type::KeyDown
                            ? context->ProcessKeyDown(Rml::Input::KeyIdentifier(input.key), input.modifiers)
                            : context->ProcessKeyUp(Rml::Input::KeyIdentifier(input.key), input.modifiers);
            break;
        case UIInputEvent::Type::Text:
            if (input.codepoint == 0 || input.codepoint > 0x10ffff ||
                (input.codepoint >= 0xd800 && input.codepoint <= 0xdfff)) {
                error = "Invalid UI Unicode scalar";
                return false;
            }
            propagate = context->ProcessTextInput(Rml::Character(input.codepoint));
            break;
        case UIInputEvent::Type::Composition:
            if (!m_Impl->Composition(input.composition, error))
                return false;
            propagate = false;
            break;
        case UIInputEvent::Type::Blur:
            m_Impl->CancelComposition();
            for (int button = 0; button < 3; ++button)
                context->ProcessMouseButtonUp(button, 0);
            if (auto* focus = context->GetFocusElement())
                focus->Blur();
            context->ProcessMouseLeave();
            break;
        }
        consumed = !propagate || HasFocus() || context->IsMouseInteracting();
        if (m_Impl->eventOverflow) {
            error = "Game UI event queue capacity exhausted";
            return false;
        }
        error.clear();
        return true;
    }
    bool UIRuntime::SetValue(std::string_view id, std::string_view value, std::string& error) {
        if (!m_Impl->Owner(error))
            return false;
        if (id.empty() || id.size() > 256 || value.size() > 65536 || value.find('\0') != value.npos ||
            !IsUIUtf8(value)) {
            error = "Game UI value binding UTF-8/budget rejected";
            return false;
        }
        if (const auto bound = m_Impl->modelValues.find(id); bound != m_Impl->modelValues.end()) {
            bound->second = std::string(value);
            m_Impl->model.DirtyVariable(std::string(id));
            error.clear();
            return true;
        }
        auto* element = m_Impl->document->GetElementById(std::string(id));
        if (!element) {
            error = "UI binding element does not exist: " + std::string(id);
            return false;
        }
        if (auto* control = dynamic_cast<Rml::ElementFormControl*>(element)) {
            const int maxLength = control->GetAttribute<int>("maxlength", -1);
            if (maxLength >= 0 && m_Impl->Scalars(value) > maxLength) {
                error = "UI form assignment exceeds authored maxlength; previous value retained";
                return false;
            }
            // 表单值不等同于元素的子标记；外部赋值先取消旧 preedit，避免随后 Cancel 覆盖新值。
            if (m_Impl->compositionControl == control)
                m_Impl->CancelComposition();
            control->SetValue(std::string(value));
        } else
            element->SetInnerRML(EscapeText(value));
        error.clear();
        return true;
    }
    bool UIRuntime::Focus(std::string_view id, std::string& error) {
        if (!m_Impl->Owner(error))
            return false;
        if (id.empty() || id.size() > 256) {
            error = "Invalid UI focus identity";
            return false;
        }
        auto* element = m_Impl->document->GetElementById(std::string(id));
        if (!element) {
            error = "UI focus element does not exist";
            return false;
        }
        struct TextScope {
            UIHost& host;
            TextInputSnapshot* before;
            ~TextScope() {
                host.currentText = before;
            }
        } scope{*m_Impl->host, m_Impl->host->currentText};
        m_Impl->host->currentText = &m_Impl->textState;
        if (!element->Focus()) {
            error = "UI element cannot accept focus";
            return false;
        }
        error.clear();
        return true;
    }
    bool UIRuntime::GetValue(std::string_view id, std::string& output, std::string& error) const {
        if (!m_Impl->Owner(error))
            return false;
        if (id.empty() || id.size() > 256) {
            error = "Invalid UI value identity";
            return false;
        }
        const auto bound = m_Impl->modelValues.find(id);
        if (bound != m_Impl->modelValues.end()) {
            output = bound->second;
            error.clear();
            return true;
        }
        const auto* element = m_Impl->document->GetElementById(std::string(id));
        const auto* control = dynamic_cast<const Rml::ElementFormControl*>(element);
        if (!control) {
            error = "UI value is neither a bound variable nor a form control";
            return false;
        }
        auto value = control->GetValue();
        if (value.size() > 65536 || !IsUIUtf8(value)) {
            error = "UI form value exceeds UTF-8/budget contract";
            return false;
        }
        output = std::move(value);
        error.clear();
        return true;
    }
    bool UIRuntime::Render(std::shared_ptr<const UIDrawData>& output, std::string& error) {
        if (!m_Impl->Owner(error))
            return false;
        try {
            m_Impl->renderer.Begin(m_Impl->width, m_Impl->height);
            if (!m_Impl->context->Render()) {
                error = "RmlUi rendering failed";
                return false;
            }
            return m_Impl->renderer.Finish(output, error);
        } catch (const std::exception& e) {
            error = e.what();
            return false;
        }
    }
    bool UIRuntime::HasFocus() const {
        if (!m_Impl->context)
            return false;
        const auto* focus = m_Impl->context->GetFocusElement();
        return focus && focus != m_Impl->context->GetRootElement() && focus != m_Impl->document;
    }
    TextInputSnapshot UIRuntime::TextInput() const {
        return m_Impl->textState;
    }
    std::vector<UIEvent> UIRuntime::TakeEvents() {
        auto out = std::move(m_Impl->events);
        m_Impl->events.clear();
        m_Impl->eventOverflow = false;
        return out;
    }
    void UIRuntime::Shutdown() noexcept {
        if (m_Impl->context) {
            m_Impl->CancelComposition();
            Rml::RemoveContext(m_Impl->name);
            Rml::ReleaseRenderManagers();
            m_Impl->context = nullptr;
            m_Impl->document = nullptr;
        }
        m_Impl->textState = {};
        m_Impl->textContext = nullptr;
        if (m_Impl->host)
            m_Impl->host->bundles.erase(m_Impl->prefix);
        m_Impl->asset = {};
        m_Impl->host.reset();
        m_Impl->events.clear();
        m_Impl->model = {};
        m_Impl->modelValues.clear();
    }
} // namespace Catalpa
