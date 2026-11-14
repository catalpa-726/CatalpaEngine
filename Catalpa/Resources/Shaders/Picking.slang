import Common.Bindings;
import Common.Mesh;
import Common.Surface;

[shader("vertex")]
MeshVertexOutput vertexMain(MeshVertexInput input) {
    return transformMeshVertex(input);
}

[shader("fragment")]
uint fragmentMain(MeshVertexOutput input) : SV_Target0 {
    float alpha = surfaceBase(surfaceUV(input.uv), input.vertexColor).a;
    applySurfaceMask(alpha);
    if (surfaceFlag(64u))
        clip(alpha - frameData.options.y);
    return drawData.ids.x;
}
