import Common.Mesh;
import Common.Surface;

// 深度与颜色共享变换、UV时间和Mask采样，禁止纯实心预通道填满树叶孔洞。
[shader("vertex")]
MeshVertexOutput vertexMain(MeshVertexInput input)
{
    return transformMeshVertex(input);
}
[shader("fragment")]
void fragmentMain(MeshVertexOutput input)
{
    applySurfaceMask(surfaceBase(surfaceUV(input.uv), input.vertexColor).a);
}
