#pragma once

#include "catpch.h"

#include "Geometry/MeshData.h"

namespace Catalpa {

	/**
	 * @brief 生成中心在原点且各面拥有独立法线与纹理坐标的立方体。
	 * 单位为米；中心在原点，右手系，从外侧看逆时针，法线向外且每面独立UV[0,1]。
	 * 创建新 CPU 数据，不访问设备；非法尺寸或分配失败保留 output。
	 *
	 * @param[in] size 立方体完整边长，须为可表示的正有限值。
	 * @param[out] output 成功时接收生成的立方体网格。
	 *
	 * @return 网格生成成功时返回正常状态；无效边长、分配或校验失败时返回网格错误。
	 */
	[[nodiscard]] CATALPA_API MeshStatus CreateCubeMesh(float size, MeshData& output);
	/**
	 * @brief 为立方体网格的各面生成沿纹理 U 方向的切线。
	 * 同一几何/UV的独立48字节派生流，附每面+U单位切线和+1手性，供普通材质法线预览。
	 * 旧CreateCubeMesh的32字节输出协议保持原样；失败保留output，不依赖Tools或设备。
	 *
	 * @param[in] size 立方体完整边长，须为可表示的正有限值。
	 * @param[out] output 成功时接收立方体网格，每个顶点带有沿该面纹理 U 方向的四分量切线。
	 *
	 * @return 切线及顶点流全部生成并校验通过时返回正常状态，否则返回具体网格错误。
	 */
	[[nodiscard]] CATALPA_API MeshStatus CreateCubeMeshWithTangents(float size, MeshData& output);
	/**
	 * @brief 创建球体网格含切线。
	 *
	 * @param[in] radius 球心到球面的距离，采用网格局部空间单位。
	 * @param[out] output 接收 MeshData 结果。
	 *
	 * @return 网格处理状态；失败时可据此定位数据或布局问题。
	 */
	[[nodiscard]] CATALPA_API MeshStatus CreateSphereMeshWithTangents(float radius, MeshData& output);
	/**
	 * @brief 在 XZ 平面生成法线沿正 Y 轴的矩形网格。
	 * XZ 平面，+Y 法线；width沿X、depth沿Z，UV从(-X,+Z)到(+X,-Z)。
	 *
	 * @param[in] width 平面沿局部 X 轴的完整宽度。
	 * @param[in] depth 平面沿局部 Z 轴的完整长度。
	 * @param[out] output 成功时接收位于 XZ 平面的网格。
	 *
	 * @return 网格生成成功时返回正常状态，否则返回尺寸、分配或网格校验错误。
	 */
	[[nodiscard]] CATALPA_API MeshStatus CreatePlaneMesh(float width, float depth, MeshData& output);
}
