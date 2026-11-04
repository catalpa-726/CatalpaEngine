import Common.Mesh;

// 完整深度入口使用相同共享变换；当前Equal预通道实际复用Unlit VS字节码，避免重编译重关联。
[shader("vertex")]
MeshVertexOutput vertexMain(MeshVertexInput input)
{
    return transformMeshVertex(input);
}
// 无颜色输出的合法fragment入口，S07再接与颜色pass一致的Mask语义。
[shader("fragment")]
void fragmentMain()
{
}
