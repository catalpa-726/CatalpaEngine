#include "catpch.h"
#include "Renderer/Resources/PipelineLibrary.h"
#include "RHI/RHIDevice.h"
namespace Catalpa {
    PipelineResource::~PipelineResource() { if (device) device->ReleaseGraphicsPipeline(pipeline); }
    RHIStatus PipelineLibrary::Acquire(const RHIGraphicsPipelineDesc& desc, std::shared_ptr<PipelineResource>& output)
    {
        std::string error;
        if (!desc.vertex.asset.HasValue() || !desc.fragment.asset.HasValue() || !desc.vertex.entryPoint || !desc.fragment.entryPoint)
            return { .code = RHIErrorCode::InvalidDescription, .operation = "PipelineLibrary/metadata" };
        // 显式编码字段，禁止hash含padding的struct、span地址或裸对象指针。
        std::string key;
        const auto number = [&](uint64_t value) { for (size_t i=0;i<8;++i) key.push_back(static_cast<char>((value>>(i*8))&255)); };
        const auto text = [&](std::string_view value) { number(value.size()); key.append(value); };
        for (const auto* shader : {&desc.vertex,&desc.fragment}) {
            text(shader->asset.GetId().ToString()); number(shader->asset.GetVersion());
            text(shader->asset.Get()->metadata.bytecodeSha256); text(shader->entryPoint); number(static_cast<uint32_t>(shader->stage));
        }
        number(static_cast<uint32_t>(desc.colorFormat)); number(static_cast<uint32_t>(desc.depthFormat));
        number(desc.sampleCount); number(static_cast<uint32_t>(desc.blendMode)); number(desc.vertexLayout.stride);
        number(desc.vertexLayout.attributes.size());
        for (const auto& a : desc.vertexLayout.attributes) { number(a.location); number(static_cast<uint32_t>(a.format)); number(a.offset); }
        number(desc.pushConstants.size); number(static_cast<uint32_t>(desc.pushConstants.visibility));
        number(desc.bindingLayout.entries.size());
        for (const auto& b : desc.bindingLayout.entries) {
            number(b.binding); number(static_cast<uint32_t>(b.type)); number(static_cast<uint32_t>(b.visibility));
            number(b.minimumBufferBytes); number(static_cast<uint32_t>(b.imageViewType));
        }
        number(desc.depthState.testEnabled); number(desc.depthState.writeEnabled); number(static_cast<uint32_t>(desc.depthState.compareOp));
        number(static_cast<uint32_t>(desc.cullMode)); number(static_cast<uint32_t>(desc.frontFace));
        number(static_cast<uint32_t>(desc.topology));
        const auto floatBits = [&](float value) { uint32_t bits = 0; std::memcpy(&bits, &value, sizeof(bits)); number(bits); };
        floatBits(desc.depthBiasConstant);
        floatBits(desc.depthBiasSlope);
        if (const auto found=m_Entries.find(key);found!=m_Entries.end()) {
            if(found->second->vertex.Get()!=desc.vertex.asset.Get()||found->second->fragment.Get()!=desc.fragment.asset.Get())
                return {.code=RHIErrorCode::InvalidState,.operation="PipelineLibrary/identity-version collision"};
            output=found->second; return {};
        }
        if(!ValidateRHIPipelineShaderInterface(desc,error))
            return {.code=RHIErrorCode::InvalidDescription,.operation="PipelineLibrary/metadata"};

        auto resource=std::make_shared<PipelineResource>();
        resource->device=&m_Device; resource->vertex=desc.vertex.asset; resource->fragment=desc.fragment.asset;
        resource->bindings.assign(desc.bindingLayout.entries.begin(), desc.bindingLayout.entries.end());
        if (const auto status=m_Device.CreateGraphicsPipeline(desc,resource->pipeline);!status.IsSuccess()) return status;
        m_Entries.emplace(std::move(key),resource); output=std::move(resource); return {};
    }
    void PipelineLibrary::Prune() {
        for(auto it=m_Entries.begin();it!=m_Entries.end();)
            if(it->second.use_count()==1) it=m_Entries.erase(it); else ++it;
    }
}
