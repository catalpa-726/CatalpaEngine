#include "catpch.h"
#include "AssetPipeline/Cookers/ShaderCooker.h"
#include "Assets/AssetContainerReader.h"
#include "Shaders/ShaderLibrary.h"
namespace Catalpa::Tools {
    bool ShaderCooker::Cook(AssetId id,const ShaderAsset& asset,std::vector<std::byte>& output,std::string& error){
        AssetContainer container;container.id=id;container.payloadVersion=1;
        if(!AssetType::TryParse("shader",container.type)||!ShaderLibrary::EncodePayload(asset,container.payload,error))return false;
        uint64_t version=0;for(size_t i=0;i<16;++i){const char c=asset.variant.sha256[i];version=(version<<4)|uint64_t(c<='9'?c-'0':c-'a'+10);}container.version=version?version:1;
        return WriteAssetContainer(container,output,error);
    }
}
