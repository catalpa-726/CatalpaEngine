import Common.Mesh;
import Common.Surface;

/**
 * 深度预通道与颜色通道使用同一顶点变换，以便片元阶段在相同位置测试材质遮罩。
 * @param input 网格的局部顶点属性，包括位置、纹理坐标和顶点色。
 */
[shader("vertex")]
MeshVertexOutput vertexMain(MeshVertexInput input) {
    return transformMeshVertex(input);
}
[shader("fragment")]
void fragmentMain(MeshVertexOutput input) {
    applySurfaceMask(surfaceBase(surfaceUV(input.uv), input.vertexColor).a);
}
