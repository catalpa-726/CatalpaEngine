#pragma once
#include "Scripting/BehaviorGraphAsset.h"

namespace Catalpa::Tools {
    class BehaviorGraphCompiler {
      public:
        // 输出验证过的解释程序；控制流允许环，由 Runtime 的指令/队列预算约束。
        static bool Compile(std::string_view source, BehaviorGraphAsset&, std::string& error);
    };
}
