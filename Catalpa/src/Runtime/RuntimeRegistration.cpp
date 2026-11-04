#include "catpch.h"
#include "Runtime/RuntimeRegistration.h"
#include "ECS/ComponentRegistry.h"
#include "Core/Math/TransformMath.h"
#include "Assets/BuiltinAssetIds.h"
#include "Geometry/GeometryAssetLoader.h"
#include "Images/ImageAssetLoader.h"
#include "Materials/MaterialAssetLoader.h"
#include "Shaders/ShaderLibrary.h"

namespace Catalpa {
    namespace {
        // 注册表已验证类型和float32界限，这里仅提取候选数值进行TRS语义检查。
        float FieldNumber(const ArchiveValue& value) {
            if (const auto* v=std::get_if<double>(&value.data)) return static_cast<float>(*v);
            if (const auto* v=std::get_if<int64_t>(&value.data)) return static_cast<float>(*v);
            return static_cast<float>(std::get<uint64_t>(value.data));
        }
        // 组件schema到真正数学契约的适配，四元数按w/x/y/z读取，不编辑输入值树。
        bool TransformSchema(const ArchiveValue& value,std::string& error) {
            const auto& translation=std::get<ArchiveValue::Array>(value.Find("translation")->data);
            const auto& rotation=std::get<ArchiveValue::Array>(value.Find("rotation")->data);
            const auto& scale=std::get<ArchiveValue::Array>(value.Find("scale")->data);
            Math::TransformTRS transform;
            transform.translation={FieldNumber(translation[0]),FieldNumber(translation[1]),FieldNumber(translation[2])};
            transform.rotation={FieldNumber(rotation[0]),FieldNumber(rotation[1]),FieldNumber(rotation[2]),FieldNumber(rotation[3])};
            transform.scale={FieldNumber(scale[0]),FieldNumber(scale[1]),FieldNumber(scale[2])};
            const auto status=Math::ValidateTRS(transform);
            if(!status.IsSuccess()){error=status.message;return false;}return true;
        }
        // 与Scene现有颜色契约一致；schema只验作者引用，具体mesh槽数在依赖解析后检查。
        bool MeshRendererSchema(const ArchiveValue& value,std::string& error) {
            const auto& materials=std::get<ArchiveValue::Array>(value.Find("materials")->data);
            const auto& color=std::get<ArchiveValue::Array>(value.Find("color")->data);
            if(materials.empty()){error="Mesh renderer requires material asset references";return false;}
            for(size_t channel=0;channel<4;++channel){
                const float component=FieldNumber(color[channel]);
                if(component<0||(channel==3&&component>1)){error="Mesh renderer requires nonnegative linear RGB and alpha in [0,1]";return false;}
            }
            return true;
        }
    }
    bool RegisterRuntimeComponentSchemas(ComponentRegistry& registry,std::string& error)
    {
        try {
            std::vector<ComponentSchema> schemas;
            schemas.push_back({"catalpa.identity",1,{{"persistent_id","Persistent ID",ComponentFieldKind::StableId,true},{"name","Name",ComponentFieldKind::String,true}}, {}});
            schemas.push_back({"catalpa.transform",1,{{"translation","Translation (m)",ComponentFieldKind::Float3,true},
                {"rotation","Rotation (w/x/y/z)",ComponentFieldKind::Float4,true},{"scale","Scale",ComponentFieldKind::Float3,true}},TransformSchema});
            schemas.push_back({"catalpa.relationship",1,{{"parent","Parent persistent ID",ComponentFieldKind::StableId,false}}, {}});
            // 只描述持久身份和作者字段，resolved meshVersion/material版本引用及worldMatrix不序列化为指针。
            schemas.push_back({"catalpa.mesh_renderer",1,{{"mesh","Mesh asset ID",ComponentFieldKind::StableId,true},
                {"materials","Material asset IDs",ComponentFieldKind::StableIdArray,true},{"color","Instance color",ComponentFieldKind::Float4,true},
                {"visible","Visible",ComponentFieldKind::Bool,true}},MeshRendererSchema});
            return registry.RegisterMany(schemas,error);
        }catch(const std::exception& exception){error=exception.what();return false;}
    }
    bool RegisterRuntimeAssetLoaders(AssetManager& manager,std::string& error)
    {
        // 只供新建manager的启动组合使用；失败即由宿主丢弃未发布manager，不伪称已注册全部领域。
        return ShaderLibrary::RegisterAssetLoader(manager,error)
            && RegisterGeometryAssetLoaders(manager,error)
            && RegisterImageAssetLoader(manager,error)
            && RegisterMaterialAssetLoaders(manager,error);
    }
    AssetId BuiltinMaterialId(BuiltinMaterialKind kind,bool definition) noexcept
    {
        switch(kind){
        case BuiltinMaterialKind::Unlit:return definition?BuiltinAssetIds::DefaultUnlitDefinition:BuiltinAssetIds::DefaultUnlitInstance;
        case BuiltinMaterialKind::Pbr:return definition?BuiltinAssetIds::DefaultPbrDefinition:BuiltinAssetIds::DefaultPbrInstance;
        case BuiltinMaterialKind::Error:return definition?BuiltinAssetIds::ErrorDefinition:BuiltinAssetIds::ErrorInstance;
        default:return {};
        }
    }
}
