#version 450

layout(location = 0) flat in uint inMeshletID;

// VisBuffer output: uvec2(MeshletID, TriangleID)
layout(location = 0) out uvec2 outVisData;

void main() {
    outVisData = uvec2(inMeshletID, gl_PrimitiveID);
}
