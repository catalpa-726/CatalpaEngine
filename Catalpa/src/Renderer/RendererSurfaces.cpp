#include "catpch.h"
#include "Renderer/RendererInternal.h"

namespace Catalpa {

    bool Renderer::SetPreviewFrame(uint64_t surfaceId, RenderFramePacket packet, std::string& error)
    {
        if (surfaceId == 0 || packet.views.size() != 1 || packet.views[0].viewport.width == 0
            || packet.views[0].viewport.height == 0 || packet.views[0].id != surfaceId) {
            error = "Preview requires one matching nonzero view and a nonempty pixel target";
            return false;
        }
        auto found = m_Previews.find(surfaceId);
        const bool created = found == m_Previews.end();
        if (found == m_Previews.end()) {
            if (m_Previews.size() >= 8) { error = "Preview surface budget is eight independent views"; return false; }
            found = m_Previews.emplace(surfaceId, std::make_unique<Surface>(m_Device, m_Textures)).first;
        }
        auto& surface = *found->second;
        if (const auto resolved = ResolveEnvironmentSnapshot(packet, surface.scene.GetFrame()); !resolved.IsSuccess()) {
            error = resolved.message;
            if (created) m_Previews.erase(found);
            return false;
        }
        const auto status = surface.scene.Apply(std::move(packet));
        if (!status.IsSuccess()) {
            error = status.message;
            if (created) m_Previews.erase(found);
            return false;
        }
        if (surface.imageOnly) {
            m_UI.ReleaseTexture(surface.token);
            surface.token = 0;
            surface.previewImage.reset();
            surface.targets.reset();
            surface.publishedTargets.reset();
            surface.published = {};
        }
        surface.imageOnly = false;
        error.clear();
        return true;
    }

    bool Renderer::GetPreviewSurface(uint64_t surfaceId, RenderSurfaceSnapshot& output) const
    {
        const auto found = m_Previews.find(surfaceId);
        if (found == m_Previews.end() || found->second->published.textureToken == 0) return false;
        output = found->second->published;
        return true;
    }

    void Renderer::ReleasePreviewSurface(uint64_t surfaceId)
    {
        const auto found = m_Previews.find(surfaceId);
        if (found == m_Previews.end()) return;
        m_Readbacks.CancelSurface(surfaceId);
        m_UI.ReleaseTexture(found->second->token);
        if (found->second->published.textureToken != found->second->token)
            m_UI.ReleaseTexture(found->second->published.textureToken);
        m_Previews.erase(found);
    }

    bool Renderer::SetPreviewImage(uint64_t surfaceId, const AssetVersionRef<ImageAsset>& image,
        uint32_t mip, uint32_t layer, uint32_t channelMask, std::string& error)
    {
        if (surfaceId == 0 || !image.HasValue() || layer != 0 || mip >= image.Get()->image.mipLevels
            || image.Get()->image.dimension != ImageDimension::Texture2D || image.Get()->image.arrayLayers != 1
            || channelMask == 0 || channelMask > 15) {
            error = "Texture preview requires a valid 2D image, mip, layer zero and RGBA channel mask";
            return false;
        }
        auto found = m_Previews.find(surfaceId);
        const bool created = found == m_Previews.end();
        if (found == m_Previews.end()) {
            if (m_Previews.size() >= 8) { error = "Preview surface budget is eight independent views"; return false; }
            found = m_Previews.emplace(surfaceId, std::make_unique<Surface>(m_Device, m_Textures)).first;
        }
        auto& surface = *found->second;
        if (surface.imageOnly && surface.previewImage && surface.previewImage->asset.Get() == image.Get()
            && surface.imageMip == mip && surface.imageChannels == channelMask) { error.clear(); return true; }
        std::shared_ptr<TextureGpuResource> texture;
        const auto status = m_Textures.Acquire(image, texture);
        if (!status.IsSuccess()) {
            error = std::string("Texture preview upload: ") + status.operation;
            if (created) m_Previews.erase(found);
            return false;
        }
        uint64_t token = 0;
        if (const auto registered = m_UI.RegisterTexture(texture->view, texture, token, mip, channelMask, true); !registered.IsSuccess()) {
            error = "Texture preview token budget or descriptor is invalid";
            if (created) m_Previews.erase(found);
            return false;
        }
        m_Readbacks.CancelSurface(surfaceId);
        m_UI.ReleaseTexture(surface.token);
        if (surface.published.textureToken != surface.token) m_UI.ReleaseTexture(surface.published.textureToken);
        surface.token = token;
        surface.imageOnly = true;
        surface.previewImage = std::move(texture);
        surface.imageMip = mip;
        surface.imageChannels = channelMask;
        surface.targets.reset();
        surface.publishedTargets.reset();
        surface.scene.Clear();
        surface.instances.clear();
        surface.published = { .surfaceId = surfaceId, .targetGeneration = token, .frameSequence = image.GetVersion(),
            .extent = { std::max(1u, image.Get()->image.width >> std::min(mip, 31u)),
                std::max(1u, image.Get()->image.height >> std::min(mip, 31u)) }, .textureToken = token };
        error.clear();
        return true;
    }

    bool Renderer::RequestPick(const PickingRequest& request, uint64_t& ticket, std::string& error)
    {
        const auto found = m_Previews.find(request.surfaceId);
        if (found == m_Previews.end() || found->second->imageOnly || !found->second->publishedTargets) {
            error = "Picking requires a rendered scene preview";
            return false;
        }
        const auto& surface = *found->second;
        const auto& shown = surface.published;
        if (shown.frameSequence == 0 || request.sceneIdentity != shown.sceneIdentity
            || request.frameSequence != shown.frameSequence || request.targetGeneration != shown.targetGeneration
            || request.x >= shown.extent.width || request.y >= shown.extent.height
            || request.x > INT32_MAX || request.y > INT32_MAX) {
            error = "Picking request no longer matches the displayed scene, frame, target generation or pixel";
            return false;
        }
        const auto status = m_Readbacks.Request(request, surface.publishedTargets->picking.view, surface.publishedTargets,
            surface.publishedIdentities, ticket);
        if (!status.IsSuccess()) { error = std::string("Picking request: ") + status.operation; return false; }
        error.clear();
        return true;
    }
}
