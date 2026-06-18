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
} pcs;

layout(location = 0) flat out uint outMeshletID;

void main() {
    uint instIdx;
    if (pcs.isNaniteMode == 1) {
        uint packedVal = gl_InstanceIndex;
        instIdx = packedVal >> 16;
        outMeshletID = gl_InstanceIndex;
    } else {
        instIdx = gl_InstanceIndex;
        outMeshletID = gl_InstanceIndex << 16;
    }
    
    InstanceData inst = instances[instIdx];
    mat4 modelMatrix = inst.modelMatrix;
    gl_Position = pcs.viewProj * (modelMatrix * vec4(inPosition, 1.0));
}
