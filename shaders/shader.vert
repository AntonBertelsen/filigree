#version 450

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

// Vertex inputs from vertex buffer
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

// Output color and normal to be passed to the Fragment Shader
layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragNormal;

void main() {
    uint instIdx;
    uint meshletID;
    if (pcs.isNaniteMode == 1) {
        uint packedVal = gl_InstanceIndex;
        meshletID = packedVal & 0xFFFFu;
        instIdx = packedVal >> 16;
    } else {
        meshletID = 0;
        instIdx = gl_InstanceIndex;
    }
    
    InstanceData inst = instances[instIdx];
    mat4 modelMatrix = inst.modelMatrix;

    // Transform vertex position to clip space
    gl_Position = pcs.viewProj * (modelMatrix * vec4(inPosition, 1.0));
    
    if (pcs.isNaniteMode == 1) {
        // Calculate color based on cluster index (offset by 1 so index 0 is not pure black)
        uint visualID = meshletID + 1;
        float r = float((visualID * 17) % 255) / 255.0;
        float g = float((visualID * 59) % 255) / 255.0;
        float b = float((visualID * 97) % 255) / 255.0;
        fragColor = vec3(r, g, b);
    } else {
        fragColor = vec3(0.0); // 0.0 triggers fallback to grey in fragment shader
    }
    
    // Transform normals by modelMatrix (upper 3x3)
    fragNormal = normalize(mat3(modelMatrix) * inNormal);
}
