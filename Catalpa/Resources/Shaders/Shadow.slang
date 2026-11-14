import Common.Math;
import Common.Mesh;
import Common.Surface;
import Common.Lighting;

[shader("vertex")]
MeshVertexOutput vertexMain(MeshVertexInput input) {
    MeshVertexOutput result = transformMeshVertex(input);
    result.position =
        multiplyColumns(lightingData.shadowViewProjection[0], lightingData.shadowViewProjection[1],
                        lightingData.shadowViewProjection[2], lightingData.shadowViewProjection[3],
                        float4(result.worldPosition, 1));
    return result;
}

[shader("fragment")]
void fragmentMain(MeshVertexOutput input) {
    applySurfaceMask(surfaceBase(surfaceUV(input.uv), input.vertexColor).a);
}
