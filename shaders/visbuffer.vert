#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
} pcs;

layout(location = 0) flat out uint outMeshletID;

void main() {
    gl_Position = pcs.viewProj * vec4(inPosition, 1.0);
    outMeshletID = gl_InstanceIndex;
}
