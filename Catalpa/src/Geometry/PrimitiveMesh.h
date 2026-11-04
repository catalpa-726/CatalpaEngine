#pragma once

#include "catpch.h"
#include "Geometry/MeshData.h"

namespace Catalpa {

    // 单位为米；中心在原点，右手系，从外侧看逆时针，法线向外且每面独立UV[0,1]。
    // 创建新 CPU 数据，不访问设备；非法尺寸或分配失败保留 output。
    [[nodiscard]] CATALPA_API MeshStatus CreateCubeMesh(float size, MeshData& output);
    // XZ 平面，+Y 法线；width沿X、depth沿Z，UV从(-X,+Z)到(+X,-Z)。
    [[nodiscard]] CATALPA_API MeshStatus CreatePlaneMesh(float width, float depth, MeshData& output);
}
