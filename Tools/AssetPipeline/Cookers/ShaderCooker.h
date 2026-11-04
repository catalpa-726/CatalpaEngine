#pragma once
#include "catpch.h"
#include "Shaders/ShaderAsset.h"
#include "Assets/AssetId.h"
namespace Catalpa::Tools {
    // 把经validator/反射的不可变领域对象写成统一容器；实际文件发布由命令输出策略负责。
    class ShaderCooker final {
    public: static bool Cook(AssetId,const ShaderAsset&,std::vector<std::byte>& output,std::string& error);
    };
}
