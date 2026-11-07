#pragma once
#include "Navigation/NavMeshAsset.h"

namespace Catalpa::Tools {
    // profile是构建输入，调用者必须把配置字节计入DDC依赖；作者settings逐项显式覆盖。
    bool ResolveNavigationBuildSource(std::string_view source, std::string_view profiles,
                                      NavigationBuildInput&, std::string& error);
} // namespace Catalpa::Tools
