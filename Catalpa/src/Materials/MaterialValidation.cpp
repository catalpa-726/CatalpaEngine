#include "catpch.h"
#include "Materials/MaterialValidation.h"
#include "Core/Serialization/JsonArchive.h"
#include "Images/ImageValidation.h"
#include "Shaders/ShaderMetadata.h"
#include "Shaders/ShaderAsset.h"

namespace Catalpa {
    namespace {
        // shader字段/入口使用C风格名称；family允许分段稳定名称，显示文本不参与身份。
        bool Name(std::string_view name, bool family = false) noexcept {
            if (name.empty() || name.size() > 128) return false;
            const auto letter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
            if (!letter(name.front())) return false;
            for (const char c : name) if (!letter(c) && !(c >= '0' && c <= '9') && !(family && (c == '.' || c == '-'))) return false;
            return true;
        }
        // 领域状态约束独立于某个pass能力，不允许禁用深度测试但仍声明深度写。
        bool State(const MaterialDefinition& definition, const MaterialRenderState& state, std::string& error) {
            if ((state.cull != MaterialCullMode::None && state.cull != MaterialCullMode::Back && state.cull != MaterialCullMode::Front)
                || (state.blend != MaterialBlendMode::Opaque && state.blend != MaterialBlendMode::Alpha && state.blend != MaterialBlendMode::Additive)) {
                error = "Unknown material render state"; return false;
            }
            if (state.depthWrite && !state.depthTest) { error = "Depth writes require depth testing"; return false; }
            if (definition.alphaMode == MaterialAlphaMode::Blend) {
                if (state.blend == MaterialBlendMode::Opaque || state.depthWrite) { error = "Blended materials require blending and disabled depth writes"; return false; }
            } else if (state.blend != MaterialBlendMode::Opaque) { error = "Opaque/masked materials cannot enable blending"; return false; }
            return true;
        }
        // 有界JSON对象只接受已声明字段；错误包含稳定字段路径。
        bool Fields(const ArchiveValue& value, std::initializer_list<std::string_view> allowed,
            std::string_view path, std::string& error) {
            const auto* object = std::get_if<ArchiveValue::Object>(&value.data);
            if (!object) { error = std::string(path) + ": expected object"; return false; }
            for (const auto& [name, item] : *object) {
                (void)item;
                if (std::find(allowed.begin(), allowed.end(), name) == allowed.end()) { error = ArchiveChildPath(path, name) + ": unknown field"; return false; }
            }
            return true;
        }
        // 从归档取有界文本，不接受NUL或隐式数值转换。
        bool Text(const ArchiveValue& object, std::string_view field, std::string& output, std::string& error, bool required = true) {
            const auto* value = object.Find(field);
            if (!value && !required) return true;
            const auto* text = value ? std::get_if<std::string>(&value->data) : nullptr;
            if (!text || text->empty() || text->size() > 4096 || text->find('\0') != std::string::npos) { error = std::string(field) + ": expected bounded text"; return false; }
            output = *text; return true;
        }
        // 读取布尔字段；缺失可沿候选默认值，错误类型不能转换为bool。
        bool Boolean(const ArchiveValue& object, std::string_view field, bool& output, std::string& error) {
            const auto* value = object.Find(field); if (!value) return true;
            const auto* result = std::get_if<bool>(&value->data);
            if (!result) { error = std::string(field) + ": expected boolean"; return false; }
            output = *result; return true;
        }
        // JSON整数和浮点可以用于连续参数，但布尔、非有限和溢出均不接受。
        bool Number(const ArchiveValue& value, double& output) noexcept {
            if (const auto* v = std::get_if<double>(&value.data)) output = *v;
            else if (const auto* v = std::get_if<int64_t>(&value.data)) output = static_cast<double>(*v);
            else if (const auto* v = std::get_if<uint64_t>(&value.data)) output = static_cast<double>(*v);
            else return false;
            return std::isfinite(output);
        }
        // 连续材质参数显式落为float32；不将溢出或非零下溢静默变成Inf/0。
        bool Float(const ArchiveValue& value, float& output) noexcept {
            double number = 0;
            if (!Number(value, number) || std::abs(number) > std::numeric_limits<float>::max()) return false;
            const float converted = static_cast<float>(number);
            if (number != 0 && converted == 0) return false;
            output = converted; return true;
        }
        // 文本枚举集中映射，未知值一律拒绝，不依赖枚举的二进制表示。
        template<typename T>
        bool Enum(std::string_view name, std::initializer_list<std::pair<std::string_view, T>> choices, T& output) {
            for (const auto& [text, value] : choices) if (name == text) { output = value; return true; }
            return false;
        }
        // 对已知参数类型解析实际值；纹理null只表示显式允许fallback的候选，随后统一校验。
        bool ParseValue(const ArchiveValue& value, MaterialParameterType type, MaterialValue& output, std::string& error) {
            switch (type) {
            case MaterialParameterType::Float: { float number = 0; if (Float(value, number)) { output = number; return true; } break; }
            case MaterialParameterType::Float2:
            case MaterialParameterType::Float3:
            case MaterialParameterType::Float4: {
                const auto* array = std::get_if<ArchiveValue::Array>(&value.data);
                const size_t count = type == MaterialParameterType::Float2 ? 2 : type == MaterialParameterType::Float3 ? 3 : 4;
                if (!array || array->size() != count) break;
                std::array<float, 4> values{};
                bool valid = true;
                for (size_t i = 0; i < count; ++i) valid = Float((*array)[i], values[i]) && valid;
                if (!valid) break;
                if (count == 2) output = std::array<float, 2>{ values[0], values[1] };
                else if (count == 3) output = std::array<float, 3>{ values[0], values[1], values[2] };
                else output = values;
                return true;
            }
            case MaterialParameterType::Int: {
                if (const auto* v = std::get_if<int64_t>(&value.data); v && *v >= std::numeric_limits<int32_t>::min() && *v <= std::numeric_limits<int32_t>::max()) { output = static_cast<int32_t>(*v); return true; }
                if (const auto* v = std::get_if<uint64_t>(&value.data); v && *v <= uint64_t(std::numeric_limits<int32_t>::max())) { output = static_cast<int32_t>(*v); return true; }
                break;
            }
            case MaterialParameterType::UInt: { uint64_t v = 0; if (value.ReadUnsigned(v) && v <= std::numeric_limits<uint32_t>::max()) { output = static_cast<uint32_t>(v); return true; } break; }
            case MaterialParameterType::Bool: { if (const auto* v = std::get_if<bool>(&value.data)) { output = *v; return true; } break; }
            case MaterialParameterType::Texture2D: {
                if (std::holds_alternative<std::nullptr_t>(value.data)) { output = AssetHandle<ImageAsset>{}; return true; }
                const auto* text = std::get_if<std::string>(&value.data); AssetId id;
                if (text && AssetId::TryParse(*text, id)) { output = AssetHandle<ImageAsset>{ id }; return true; }
                break;
            }
            default: break;
            }
            error = "Material value does not match its declared type"; return false;
        }
        // 解析状态覆盖的有无，不能把未声明字段误当false或默认覆盖。
        bool ParseStates(const ArchiveValue& object, MaterialStateOverrides& output, std::string& error) {
            if (!Fields(object, { "cull", "depthTest", "depthWrite", "blend" }, "/renderState", error)) return false;
            if (object.Find("cull")) {
                std::string text; MaterialCullMode value;
                if (!Text(object, "cull", text, error) || !Enum<MaterialCullMode>(text, { {"none",MaterialCullMode::None},{"back",MaterialCullMode::Back},{"front",MaterialCullMode::Front} }, value)) { error = "Invalid cull mode"; return false; }
                output.cull = value;
            }
            if (object.Find("blend")) {
                std::string text; MaterialBlendMode value;
                if (!Text(object, "blend", text, error) || !Enum<MaterialBlendMode>(text, { {"opaque",MaterialBlendMode::Opaque},{"alpha",MaterialBlendMode::Alpha},{"additive",MaterialBlendMode::Additive} }, value)) { error = "Invalid blend mode"; return false; }
                output.blend = value;
            }
            bool value = false;
            if (object.Find("depthTest")) { if (!Boolean(object, "depthTest", value, error)) return false; output.depthTest = value; }
            if (object.Find("depthWrite")) { if (!Boolean(object, "depthWrite", value, error)) return false; output.depthWrite = value; }
            return true;
        }
        // 读取唯一支持版本及文档种类；返回独占候选，旧消费者状态始终未发布。
        bool Document(std::string_view text, std::string_view kind, std::unique_ptr<ArchiveValue>& output, std::string& error, const ArchiveLimits& limits) {
            ArchiveError diagnostic;
            if (!JsonArchive::Parse(text, output, diagnostic, limits)) { error = diagnostic.ToString(); return false; }
            if (!RequireArchiveVersion(*output, 1, diagnostic)) { error = diagnostic.ToString(); return false; }
            std::string found;
            if (!Text(*output, "kind", found, error) || found != kind) { error = "Material document kind is incorrect"; return false; }
            return true;
        }
    }

    bool ValidateMaterialValue(const MaterialParameter& parameter, const MaterialValue& value, std::string& error)
    {
        if ((parameter.minimum && !std::isfinite(*parameter.minimum)) || (parameter.maximum && !std::isfinite(*parameter.maximum))
            || (parameter.minimum && parameter.maximum && *parameter.minimum > *parameter.maximum)) {
            error = "Material parameter range is invalid: " + parameter.id; return false;
        }
        const auto numeric = [&](double number) { return std::isfinite(number) && (!parameter.minimum || number >= *parameter.minimum) && (!parameter.maximum || number <= *parameter.maximum); };
        bool valid = false;
        switch (parameter.type) {
        case MaterialParameterType::Float: if (const auto* v = std::get_if<float>(&value)) valid = numeric(*v); break;
        case MaterialParameterType::Float2: if (const auto* v = std::get_if<std::array<float, 2>>(&value)) valid = std::all_of(v->begin(), v->end(), numeric); break;
        case MaterialParameterType::Float3: if (const auto* v = std::get_if<std::array<float, 3>>(&value)) valid = std::all_of(v->begin(), v->end(), numeric); break;
        case MaterialParameterType::Float4: if (const auto* v = std::get_if<std::array<float, 4>>(&value)) valid = std::all_of(v->begin(), v->end(), numeric); break;
        case MaterialParameterType::Int: if (const auto* v = std::get_if<int32_t>(&value)) valid = numeric(*v); break;
        case MaterialParameterType::UInt: if (const auto* v = std::get_if<uint32_t>(&value)) valid = numeric(*v); break;
        case MaterialParameterType::Bool: valid = std::holds_alternative<bool>(value); break;
        case MaterialParameterType::Texture2D: if (const auto* v = std::get_if<AssetHandle<ImageAsset>>(&value)) valid = v->HasValue() || parameter.allowTextureFallback; break;
        default: break;
        }
        if (!valid) { error = "Material parameter type/range/reference mismatch: " + parameter.id; return false; }
        // 公用普通材质字段另有领域约束，不能依赖作者可省略的min/max或等待GPU阶段报错。
        if (parameter.id == "base_color") {
            const auto* color = std::get_if<std::array<float, 4>>(&value);
            valid = color && (*color)[3] >= 0 && (*color)[3] <= 1 &&
                std::all_of(color->begin(), color->end(), [](float number) { return number >= 0 && number <= 1e8f; });
        } else if (parameter.id == "emissive") {
            const auto* color = std::get_if<std::array<float, 3>>(&value);
            valid = color && std::all_of(color->begin(), color->end(), [](float number) { return number >= 0 && number <= 1e8f; });
        } else if (parameter.id == "normal_scale") {
            const auto* scale = std::get_if<float>(&value);
            valid = scale && std::abs(*scale) <= 1e8f;
        } else if (parameter.id == "uv_scale_offset") {
            const auto* transform = std::get_if<std::array<float, 4>>(&value);
            valid = transform && std::all_of(transform->begin(), transform->end(), [](float number) { return std::abs(number) <= 1e8f; });
        } else if (parameter.id == "metallic" || parameter.id == "roughness" || parameter.id == "occlusion_strength") {
            const auto* number = std::get_if<float>(&value);
            valid = number && *number >= 0 && *number <= 1;
        } else if (parameter.id == "alpha_cutoff") {
            const auto* number = std::get_if<float>(&value);
            valid = number && *number >= 0;
        } else if (parameter.id == "uv_cycles") {
            const auto* cycles = std::get_if<std::array<float, 2>>(&value);
            valid = cycles && std::all_of(cycles->begin(), cycles->end(), [](float number) {
                return std::abs(number) <= 65536 && std::trunc(number) == number;
            });
        } else if (parameter.id == "pulse_cycles") {
            const auto* cycles = std::get_if<uint32_t>(&value);
            valid = cycles && *cycles <= 65536;
        } else if (parameter.id.ends_with("_sampler")) {
            const auto* flags = std::get_if<uint32_t>(&value);
            valid = flags && *flags <= 255 && (*flags & 3) <= 2 && ((*flags >> 2) & 3) <= 2;
        }
        if (!valid) { error = "Material surface semantic constraint failed: " + parameter.id; return false; }
        error.clear(); return true;
    }
    bool ValidateMaterialDefinition(const MaterialDefinition& definition, std::string& error)
    {
        if (definition.schemaVersion != 1 || !Name(definition.family, true) || !definition.shader.HasValue()
            || !Name(definition.vertexEntry) || !Name(definition.fragmentEntry) || !Name(definition.parameterBlock)
            || definition.parameters.empty() || definition.parameters.size() > 1024 || (definition.allowedStateOverrides & ~15u) != 0
            || (definition.shadingModel != MaterialShadingModel::Unlit && definition.shadingModel != MaterialShadingModel::Pbr)
            || definition.domain != MaterialDomain::Surface || (definition.alphaMode != MaterialAlphaMode::Opaque
                && definition.alphaMode != MaterialAlphaMode::Mask && definition.alphaMode != MaterialAlphaMode::Blend)) {
            error = "Material definition version, identity or domain is invalid"; return false;
        }
        if (definition.shaderVersion.HasValue() && definition.shaderVersion.GetId() != definition.shader.id) {
            error = "Resolved shader version belongs to a different material shader identity"; return false;
        }
        if (!State(definition, definition.renderState, error)) return false;
        try {
            std::set<std::string_view> ids;
            for (const auto& parameter : definition.parameters) {
                if (!Name(parameter.id) || !ids.insert(parameter.id).second || parameter.displayName.size() > 1024
                    || parameter.displayName.find('\0') != std::string::npos
                    || (parameter.minimum && !std::isfinite(*parameter.minimum)) || (parameter.maximum && !std::isfinite(*parameter.maximum))
                    || (parameter.minimum && parameter.maximum && *parameter.minimum > *parameter.maximum)
                    || static_cast<unsigned>(parameter.textureSemantic) > static_cast<unsigned>(MaterialTextureSemantic::Data)) {
                    error = "Material parameter schema is invalid: " + parameter.id; return false;
                }
                if ((parameter.type == MaterialParameterType::Bool || parameter.type == MaterialParameterType::Texture2D)
                    && (parameter.minimum || parameter.maximum)) { error = "Bool/texture parameters cannot declare numeric ranges"; return false; }
                if (parameter.type != MaterialParameterType::Texture2D
                    && (parameter.allowTextureFallback || parameter.textureSemantic != MaterialTextureSemantic::Data)) {
                    error = "Numeric parameters cannot declare texture policy"; return false;
                }
                if (!ValidateMaterialValue(parameter, parameter.defaultValue, error)) return false;
                if (definition.shadingModel == MaterialShadingModel::Pbr && parameter.id == "base_color") {
                    const auto* color = std::get_if<std::array<float, 4>>(&parameter.defaultValue);
                    if (!color || std::any_of(color->begin(), color->begin() + 3, [](float number) { return number > 1; })) {
                        error = "PBR base color is reflectance in [0,1]; HDR energy belongs to emissive";
                        return false;
                    }
                }
            }
            error.clear(); return true;
        } catch (const std::exception& exception) { error = exception.what(); return false; }
    }
    bool ResolveMaterialRenderState(const MaterialDefinition& definition, const MaterialRenderState& base,
        const MaterialStateOverrides& overrides, MaterialRenderState& output, std::string& error)
    {
        MaterialRenderState candidate = base;
        const auto allowed = [&](MaterialStateOverride kind) { return (definition.allowedStateOverrides & static_cast<uint32_t>(kind)) != 0; };
        if (overrides.cull) { if (!allowed(MaterialStateOverride::Cull)) { error = "Cull override is forbidden"; return false; } candidate.cull = *overrides.cull; }
        if (overrides.depthTest) { if (!allowed(MaterialStateOverride::DepthTest)) { error = "Depth-test override is forbidden"; return false; } candidate.depthTest = *overrides.depthTest; }
        if (overrides.depthWrite) { if (!allowed(MaterialStateOverride::DepthWrite)) { error = "Depth-write override is forbidden"; return false; } candidate.depthWrite = *overrides.depthWrite; }
        if (overrides.blend) { if (!allowed(MaterialStateOverride::Blend)) { error = "Blend override is forbidden"; return false; } candidate.blend = *overrides.blend; }
        if ((candidate.cull != definition.renderState.cull && !allowed(MaterialStateOverride::Cull))
            || (candidate.depthTest != definition.renderState.depthTest && !allowed(MaterialStateOverride::DepthTest))
            || (candidate.depthWrite != definition.renderState.depthWrite && !allowed(MaterialStateOverride::DepthWrite))
            || (candidate.blend != definition.renderState.blend && !allowed(MaterialStateOverride::Blend))) {
            error = "Material state differs from a non-overridable definition policy"; return false;
        }
        if (!State(definition, candidate, error)) return false;
        output = candidate; error.clear(); return true;
    }
    bool ValidateMaterialInstance(const MaterialInstance& instance, std::string& error)
    {
        if (!instance.definition.HasValue() || instance.revision == 0) { error = "Material instance has no fixed definition or revision"; return false; }
        const auto& definition = *instance.definition.Get();
        if (!ValidateMaterialDefinition(definition, error)) return false;
        if (instance.parameters.size() != definition.parameters.size()) { error = "Material instance parameter set is incomplete"; return false; }
        try {
            std::set<std::string_view> ids;
            for (const auto& value : instance.parameters) {
                if (!ids.insert(value.parameterId).second) { error = "Duplicate material instance parameter"; return false; }
                const auto schema = std::find_if(definition.parameters.begin(), definition.parameters.end(), [&](const auto& p) { return p.id == value.parameterId; });
                if (schema == definition.parameters.end()) { error = "Unknown material instance parameter"; return false; }
                if (!ValidateMaterialValue(*schema, value.value, error)) return false;
                if (definition.shadingModel == MaterialShadingModel::Pbr && value.parameterId == "base_color") {
                    const auto* color = std::get_if<std::array<float, 4>>(&value.value);
                    if (!color || std::any_of(color->begin(), color->begin() + 3, [](float number) { return number > 1; })) {
                        error = "PBR base color is reflectance in [0,1]; HDR energy belongs to emissive";
                        return false;
                    }
                }
                if (!schema->allowOverride && value.value != schema->defaultValue) { error = "Material changes a fixed parameter"; return false; }
            }
            constexpr std::array<std::string_view, 5> textureIds{"baseColor_texture", "normal_texture",
                "metallicRoughness_texture", "emissive_texture", "occlusion_texture"};
            for (size_t index = 0; index < textureIds.size(); ++index) {
                if (!instance.textureVersions[index].HasValue()) continue;
                const auto* value = FindMaterialParameter(instance, textureIds[index]);
                const auto* texture = value ? std::get_if<AssetHandle<ImageAsset>>(value) : nullptr;
                if (!texture || texture->id != instance.textureVersions[index].GetId()) {
                    error = "Fixed material texture version belongs to a different logical parameter";
                    return false;
                }
            }
            MaterialRenderState state;
            return ResolveMaterialRenderState(definition, instance.renderState, {}, state, error);
        } catch (const std::exception& exception) { error = exception.what(); return false; }
    }
    bool ValidateMaterialTexture(const MaterialParameter& parameter, const ImageAsset& image, std::string& error)
    {
        if (parameter.type != MaterialParameterType::Texture2D
            || static_cast<unsigned>(parameter.textureSemantic) > static_cast<unsigned>(MaterialTextureSemantic::Data)) {
            error = "Parameter is not a valid texture schema"; return false;
        }
        if (!ValidateImageAsset(image, error)) return false;
        if (image.image.dimension != ImageDimension::Texture2D || image.image.arrayLayers != 1) { error = "Texture2D requires a non-array 2D image"; return false; }
        const auto expected = parameter.textureSemantic == MaterialTextureSemantic::Normal ? ImageSemantic::Normal
            : (parameter.textureSemantic == MaterialTextureSemantic::BaseColor || parameter.textureSemantic == MaterialTextureSemantic::Emissive) ? ImageSemantic::Color : ImageSemantic::Data;
        if (image.image.semantic != expected) { error = "Material texture semantic conflicts with the image asset"; return false; }
        if (GetPixelFormatInfo(image.image.format).isFloat) {
            if (image.image.format != PixelFormat::RGBA32_Float) {
                error = "Ordinary floating surface textures require explicit RGBA32F value validation";
                return false;
            }
            const float maximum = expected == ImageSemantic::Color ? 1e8f : 1.0f;
            for (const auto& sub : image.image.subresources) {
                for (uint32_t y = 0; y < sub.height; ++y) {
                    for (uint32_t x = 0; x < sub.width; ++x) {
                        std::array<float, 4> texel;
                        std::memcpy(texel.data(), image.image.pixels.data() + sub.offset + uint64_t(y) * sub.rowPitch + uint64_t(x) * 16, 16);
                        for (size_t channel = 0; channel < 4; ++channel) {
                            if (!std::isfinite(texel[channel]) || texel[channel] < 0 ||
                                texel[channel] > (channel == 3 ? 1.0f : maximum)) {
                                error = "Floating surface texel exceeds its explicit color/data/alpha budget";
                                return false;
                            }
                        }
                    }
                }
            }
        }
        error.clear(); return true;
    }

    bool ParseMaterialDefinition(std::string_view text, std::shared_ptr<const MaterialDefinition>& output, std::string& error, const ArchiveLimits& limits)
    {
        try {
            std::unique_ptr<ArchiveValue> document;
            if (!Document(text, "definition", document, error, limits) || !Fields(*document, { "schemaVersion","kind","family","shadingModel","domain","alphaMode","shader","vertexEntry","fragmentEntry","parameterBlock","parameters","renderState","allowedStateOverrides" }, {}, error)) return false;
            MaterialDefinition candidate;
            std::string shader, model, domain, alpha;
            if (!Text(*document,"family",candidate.family,error) || !Text(*document,"shader",shader,error)
                || !Text(*document,"shadingModel",model,error) || !Text(*document,"domain",domain,error)
                || !Text(*document,"alphaMode",alpha,error) || !Text(*document,"vertexEntry",candidate.vertexEntry,error)
                || !Text(*document,"fragmentEntry",candidate.fragmentEntry,error) || !Text(*document,"parameterBlock",candidate.parameterBlock,error)) return false;
            if (!AssetId::TryParse(shader,candidate.shader.id) || !Enum<MaterialShadingModel>(model, {{"unlit",MaterialShadingModel::Unlit},{"pbr",MaterialShadingModel::Pbr}},candidate.shadingModel)
                || domain!="surface" || !Enum<MaterialAlphaMode>(alpha,{{"opaque",MaterialAlphaMode::Opaque},{"mask",MaterialAlphaMode::Mask},{"blend",MaterialAlphaMode::Blend}},candidate.alphaMode)) { error="Material shader/model/domain/alpha is invalid"; return false; }
            if (const auto* state=document->Find("renderState")) {
                MaterialStateOverrides parsed;
                if (!ParseStates(*state,parsed,error)) return false;
                if(parsed.cull) candidate.renderState.cull=*parsed.cull;
                if(parsed.depthTest) candidate.renderState.depthTest=*parsed.depthTest;
                if(parsed.depthWrite) candidate.renderState.depthWrite=*parsed.depthWrite;
                if(parsed.blend) candidate.renderState.blend=*parsed.blend;
            }
            if (const auto* allowed=document->Find("allowedStateOverrides")) {
                const auto* array=std::get_if<ArchiveValue::Array>(&allowed->data);
                if(!array || array->size()>4) {error="Invalid state override list";return false;}
                for(const auto& item:*array) {
                    const auto* value=std::get_if<std::string>(&item.data); MaterialStateOverride flag;
                    if(!value || !Enum<MaterialStateOverride>(*value,{{"cull",MaterialStateOverride::Cull},{"depthTest",MaterialStateOverride::DepthTest},{"depthWrite",MaterialStateOverride::DepthWrite},{"blend",MaterialStateOverride::Blend}},flag)
                        || (candidate.allowedStateOverrides & static_cast<uint32_t>(flag))) {error="Invalid or duplicate state override";return false;}
                    candidate.allowedStateOverrides|=static_cast<uint32_t>(flag);
                }
            }
            const auto* parametersValue=document->Find("parameters");
            const auto* parameters=parametersValue?std::get_if<ArchiveValue::Array>(&parametersValue->data):nullptr;
            if(!parameters || parameters->empty() || parameters->size()>1024) {error="Material parameters must be a bounded nonempty array";return false;}
            for(const auto& value:*parameters) {
                if(!Fields(value,{"id","displayName","type","default","min","max","allowOverride","textureSemantic","allowTextureFallback"},"/parameters",error)) return false;
                MaterialParameter parameter; std::string type;
                if(!Text(value,"id",parameter.id,error)||!Text(value,"displayName",parameter.displayName,error,false)||!Text(value,"type",type,error))return false;
                if(!Enum<MaterialParameterType>(type,{{"float",MaterialParameterType::Float},{"float2",MaterialParameterType::Float2},{"float3",MaterialParameterType::Float3},{"float4",MaterialParameterType::Float4},{"int",MaterialParameterType::Int},{"uint",MaterialParameterType::UInt},{"bool",MaterialParameterType::Bool},{"texture2d",MaterialParameterType::Texture2D}},parameter.type)) {error="Unknown material parameter type";return false;}
                const auto* defaultValue=value.Find("default");
                if(!defaultValue){error="Missing material default: "+parameter.id;return false;}
                if(!ParseValue(*defaultValue,parameter.type,parameter.defaultValue,error)){error=parameter.id+": "+error;return false;}
                for(const auto field:{"min","max"}) if(const auto* bound=value.Find(field)) {
                    double number=0;if(!Number(*bound,number)){error="Material range must be finite";return false;}
                    if(std::string_view(field)=="min")parameter.minimum=number;else parameter.maximum=number;
                }
                if(!Boolean(value,"allowOverride",parameter.allowOverride,error)||!Boolean(value,"allowTextureFallback",parameter.allowTextureFallback,error))return false;
                if(value.Find("textureSemantic")) {
                    std::string semantic;if(!Text(value,"textureSemantic",semantic,error)||!Enum<MaterialTextureSemantic>(semantic,{{"baseColor",MaterialTextureSemantic::BaseColor},{"normal",MaterialTextureSemantic::Normal},{"metallicRoughness",MaterialTextureSemantic::MetallicRoughness},{"emissive",MaterialTextureSemantic::Emissive},{"occlusion",MaterialTextureSemantic::Occlusion},{"data",MaterialTextureSemantic::Data}},parameter.textureSemantic)){error="Invalid texture semantic";return false;}
                }
                candidate.parameters.push_back(std::move(parameter));
            }
            if(!ValidateMaterialDefinition(candidate,error))return false;
            auto published=std::make_shared<const MaterialDefinition>(std::move(candidate));output.swap(published);error.clear();return true;
        } catch(const std::exception& exception){error=exception.what();return false;}
    }
    bool ParseMaterialInstance(std::string_view text, const AssetVersionRef<MaterialDefinition>& definition,
        std::shared_ptr<const MaterialInstance>& output, std::string& error, const ArchiveLimits& limits)
    {
        if(!definition.HasValue()){error="Material instance requires a resolved definition version";return false;}
        try {
            std::unique_ptr<ArchiveValue> document;
            if(!Document(text,"instance",document,error,limits)||!Fields(*document,{"schemaVersion","kind","definitionId","definitionVersion","revision","parameters","renderState"},{},error))return false;
            std::string textId; AssetId id; uint64_t version=0,revision=0;
            const auto* versionValue=document->Find("definitionVersion");const auto* revisionValue=document->Find("revision");
            if(!Text(*document,"definitionId",textId,error)||!AssetId::TryParse(textId,id)||id!=definition.GetId()
                ||!versionValue||!versionValue->ReadUnsigned(version)||version!=definition.GetVersion()
                ||!revisionValue||!revisionValue->ReadUnsigned(revision)||revision==0){error="Material definition identity/version or instance revision is invalid";return false;}
            std::vector<MaterialParameterOverride> overrides;
            if(const auto* parameters=document->Find("parameters")) {
                const auto* object=std::get_if<ArchiveValue::Object>(&parameters->data);
                if(!object||object->size()>1024){error="Material overrides must be a bounded object";return false;}
                for(const auto& [name,value]:*object){
                    const auto schema=std::find_if(definition.Get()->parameters.begin(),definition.Get()->parameters.end(),[&](const auto& p){return p.id==name;});
                    if(schema==definition.Get()->parameters.end()){error="Unknown material override: "+name;return false;}
                    MaterialParameterOverride change;change.parameterId=name;
                    if(!ParseValue(value,schema->type,change.value,error))return false;overrides.push_back(std::move(change));
                }
            }
            MaterialStateOverrides states;if(const auto* value=document->Find("renderState"))if(!ParseStates(*value,states,error))return false;
            return CreateMaterialInstance(definition,overrides,states,revision,output,error);
        } catch(const std::exception& exception){error=exception.what();return false;}
    }

    bool CreateBuiltinMaterialDefinition(BuiltinMaterialKind kind, AssetId shader,
        std::shared_ptr<const MaterialDefinition>& output, std::string& error)
    {
        if(kind!=BuiltinMaterialKind::Unlit&&kind!=BuiltinMaterialKind::Pbr&&kind!=BuiltinMaterialKind::Error){error="Unknown builtin material";return false;}
        try {
            MaterialDefinition definition;definition.shader=AssetHandle<ShaderAsset>{shader};
            definition.family=kind==BuiltinMaterialKind::Pbr?"catalpa.pbr":kind==BuiltinMaterialKind::Error?"catalpa.error":"catalpa.unlit";
            definition.shadingModel=kind==BuiltinMaterialKind::Pbr?MaterialShadingModel::Pbr:MaterialShadingModel::Unlit;
            definition.renderState.cull=MaterialCullMode::None;
            definition.allowedStateOverrides=static_cast<uint32_t>(MaterialStateOverride::Cull);
            MaterialParameter color;color.id="base_color";color.displayName="Base color";color.type=MaterialParameterType::Float4;
            color.defaultValue=kind==BuiltinMaterialKind::Error?std::array<float,4>{1,0,1,1}:std::array<float,4>{1,1,1,1};color.minimum=0;
            definition.parameters.push_back(std::move(color));
            if(kind==BuiltinMaterialKind::Pbr){
                MaterialParameter metallic;metallic.id="metallic";metallic.displayName="Metallic";metallic.defaultValue=0.0f;metallic.minimum=0;metallic.maximum=1;
                MaterialParameter roughness=metallic;roughness.id="roughness";roughness.displayName="Roughness";roughness.defaultValue=0.5f;
                MaterialParameter emissive;emissive.id="emissive";emissive.displayName="Emissive";emissive.type=MaterialParameterType::Float3;emissive.defaultValue=std::array<float,3>{0,0,0};emissive.minimum=0;
                definition.parameters.push_back(std::move(metallic));definition.parameters.push_back(std::move(roughness));definition.parameters.push_back(std::move(emissive));
            }
            if(!ValidateMaterialDefinition(definition,error))return false;
            auto published=std::make_shared<const MaterialDefinition>(std::move(definition));output.swap(published);error.clear();return true;
        }catch(const std::exception& exception){error=exception.what();return false;}
    }
    bool CreateBuiltinMaterialInstance(BuiltinMaterialKind kind,AssetId definitionId,uint64_t definitionVersion,
        AssetId shader,std::shared_ptr<const MaterialInstance>& output,std::string& error)
    {
        std::shared_ptr<const MaterialDefinition> value;
        if(!CreateBuiltinMaterialDefinition(kind,shader,value,error))return false;
        AssetVersionRef<MaterialDefinition> definition;
        if(!AssetVersionRef<MaterialDefinition>::TryCreate(definitionId,definitionVersion,value,definition,error))return false;
        return CreateMaterialInstance(definition,{},{},1,output,error);
    }

    bool ValidateMaterialShaderInterface(const MaterialDefinition& definition,const ShaderMetadata& metadata,
        const VertexLayout& vertices,std::string& error)
    {
        if(!ValidateMaterialDefinition(definition,error)||!ValidateShaderMetadata(metadata,error))return false;
        if(definition.shaderVersion.HasValue()&&definition.shaderVersion.Get()->metadata!=metadata){
            error="Material interface metadata is not its fixed shader dependency";return false;
        }
        const auto* vertex=FindShaderEntry(metadata,definition.vertexEntry,ShaderStage::Vertex);
        const auto* fragment=FindShaderEntry(metadata,definition.fragmentEntry,ShaderStage::Fragment);
        if(!vertex||!fragment){error="Material shader entry/stage is missing";return false;}
        if(vertices.stride==0||vertices.attributes.size()>64){error="Vertex layout is invalid";return false;}
        try {
            std::set<uint32_t> locations;
            for(const auto& attribute:vertices.attributes){const auto bytes=GetVertexFormatByteSize(attribute.format);
                if(!bytes||attribute.offset>vertices.stride||bytes>vertices.stride-attribute.offset||!locations.insert(attribute.location).second){error="Invalid or duplicate vertex attribute";return false;}}
            for(const auto& input:vertex->inputs){
                const auto found=std::find_if(vertices.attributes.begin(),vertices.attributes.end(),[&](const auto& a){return a.location==input.location;});
                if(found==vertices.attributes.end()||input.type.scalar!=ShaderScalarKind::Float||input.type.width!=32
                    ||input.type.rows!=1||input.type.arrayCount!=1||GetVertexFormatByteSize(found->format)!=input.type.columns*4){error="Shader vertex input is missing or has an incompatible format";return false;}
            }
            const ShaderBinding* block=nullptr;
            std::map<std::pair<uint32_t,uint32_t>,const ShaderBinding*> bindings;
            for(const auto* entry:{vertex,fragment})for(const auto& binding:entry->bindings){
                const auto key=std::pair{binding.set,binding.binding};const auto [found,inserted]=bindings.emplace(key,&binding);
                if(!inserted&&*found->second!=binding){error="Shader stages disagree about a material resource binding";return false;}
            }
            for(const auto& [key,binding]:bindings){(void)key;if(binding->name==definition.parameterBlock){
                if(block){error="Ambiguous material parameter block";return false;}block=binding;}}
            // 普通表面用固定五float4传输块；领域参数通过受检adapter打包，不能按作者字段猜偏移。
            // 旧的直接成员ABI仍走下面的通用严格检查，已有自定义材质不被静默重新解释。
            if (block && block->blockSize == 80 && definition.family.starts_with("catalpa.")) {
                constexpr std::array<std::string_view, 5> names{"base_color", "emissive_metallic",
                    "roughness_normal_occlusion_cutoff", "uv_scale_offset", "time_effect"};
                if (block->kind != ShaderResourceKind::UniformBuffer || block->count != 1 || block->members.size() != names.size()) {
                    error = "Surface material block has an incompatible resource shape";
                    return false;
                }
                for (size_t i = 0; i < names.size(); ++i) {
                    const auto& member = block->members[i];
                    if (member.name != names[i] || member.offset != i * 16 || member.size != 16 ||
                        member.type.scalar != ShaderScalarKind::Float || member.type.width != 32 ||
                        member.type.columns != 4 || member.type.rows != 1 || member.type.arrayCount != 1 ||
                        member.arrayStride || member.matrixStride || !member.members.empty()) {
                        error = "Surface material float4 layout does not match the final shader ABI";
                        return false;
                    }
                }
                const std::map<std::string_view, MaterialParameterType> supported{
                    {"base_color", MaterialParameterType::Float4}, {"emissive", MaterialParameterType::Float3},
                    {"metallic", MaterialParameterType::Float}, {"roughness", MaterialParameterType::Float},
                    {"normal_scale", MaterialParameterType::Float}, {"occlusion_strength", MaterialParameterType::Float},
                    {"alpha_cutoff", MaterialParameterType::Float}, {"uv_flip_v", MaterialParameterType::Bool},
                    {"uv_scale_offset", MaterialParameterType::Float4}, {"uv_cycles", MaterialParameterType::Float2},
                    {"pulse_cycles", MaterialParameterType::UInt},
                    {"baseColor_sampler", MaterialParameterType::UInt}, {"normal_sampler", MaterialParameterType::UInt},
                    {"metallicRoughness_sampler", MaterialParameterType::UInt}, {"emissive_sampler", MaterialParameterType::UInt},
                    {"occlusion_sampler", MaterialParameterType::UInt}};
                for (const auto& parameter : definition.parameters) {
                    if (parameter.type == MaterialParameterType::Texture2D) {
                        const auto found = std::find_if(bindings.begin(), bindings.end(), [&](const auto& pair) {
                            return pair.second->name == parameter.id && pair.second->count == 1 &&
                                pair.second->kind == ShaderResourceKind::SampledImage &&
                                pair.second->imageDimension == ShaderImageDimension::Texture2D;
                        });
                        if (found == bindings.end()) { error = "Surface texture binding is missing: " + parameter.id; return false; }
                    } else {
                        const auto found = supported.find(parameter.id);
                        if (found == supported.end() || found->second != parameter.type) {
                            error = "Surface adapter does not support parameter: " + parameter.id;
                            return false;
                        }
                    }
                }
                error.clear();
                return true;
            }
            size_t numbers=0;
            for(const auto& parameter:definition.parameters){
                if(parameter.type==MaterialParameterType::Texture2D){
                    const ShaderBinding* texture=nullptr;
                    for(const auto& [key,binding]:bindings){(void)key;if(binding->name==parameter.id){if(texture){error="Ambiguous material texture binding";return false;}texture=binding;}}
                    if(!texture||texture->count!=1||texture->imageDimension!=ShaderImageDimension::Texture2D||(texture->kind!=ShaderResourceKind::SampledImage&&texture->kind!=ShaderResourceKind::CombinedImageSampler)){error="Material texture binding is missing or incompatible: "+parameter.id;return false;}
                    continue;
                }
                ++numbers;
                if(!block||block->kind!=ShaderResourceKind::UniformBuffer||block->count!=1){error="Material uniform block is missing or incompatible";return false;}
                const auto member=std::find_if(block->members.begin(),block->members.end(),[&](const auto& v){return v.name==parameter.id;});
                const uint32_t columns=parameter.type==MaterialParameterType::Float2?2:parameter.type==MaterialParameterType::Float3?3:parameter.type==MaterialParameterType::Float4?4:1;
                const auto scalar=parameter.type==MaterialParameterType::Int?ShaderScalarKind::SignedInteger:parameter.type==MaterialParameterType::UInt?ShaderScalarKind::UnsignedInteger:parameter.type==MaterialParameterType::Bool?ShaderScalarKind::Boolean:ShaderScalarKind::Float;
                if(member==block->members.end()||member->type.scalar!=scalar||member->type.width!=32||member->type.rows!=1
                    ||member->type.columns!=columns||member->type.arrayCount!=1||member->arrayStride||member->matrixStride
                    ||!member->members.empty()||member->offset%4||member->size!=columns*4){error="Material parameter does not match final shader ABI: "+parameter.id;return false;}
            }
            if(block&&block->members.size()!=numbers){error="Shader material block contains undeclared schema members";return false;}
            error.clear();return true;
        }catch(const std::exception& exception){error=exception.what();return false;}
    }
}
