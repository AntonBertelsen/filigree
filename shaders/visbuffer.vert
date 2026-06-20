#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

struct InstanceData {
    mat4 modelMatrix;
    uint baseVertexOffset;
    uint baseIndexOffset;
    uint firstMeshletCommandOffset;
    uint isNanite;
};

layout(std430, set = 0, binding = 6) readonly buffer InstanceBuffer {
    InstanceData instances[];
};

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    uint isNaniteMode;
    float viewportWidth;
    float viewportHeight;
} pcs;

layout(location = 0) flat out uint outVisPayload;

void main() {
    uint instIdx;
    uint clustIdx;
    if (pcs.isNaniteMode == 1) {
        instIdx = gl_InstanceIndex >> 16;
        clustIdx = gl_InstanceIndex & 0xFFFFu;
        outVisPayload = (instIdx << 22) | (clustIdx << 8);
    } else {
        instIdx = gl_InstanceIndex;
        outVisPayload = (instIdx << 20);
    }
    
    InstanceData inst = instances[instIdx];
    mat4 modelMatrix = inst.modelMatrix;
    gl_Position = pcs.viewProj * (modelMatrix * vec4(inPosition, 1.0));
}
