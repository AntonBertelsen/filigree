#version 450

struct InstanceData {
    mat4 modelMatrix;
    uint baseVertexOffset;
    uint baseIndexOffset;
    uint firstMeshletCommandOffset;
    uint isNanite;
};

struct VkDrawIndexedIndirectCommand {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int  vertexOffset;
    uint firstInstance;
};

struct MeshVertex {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
};

// Bindings
layout(std430, set = 0, binding = 0) readonly buffer InputCommands {
    VkDrawIndexedIndirectCommand inputs[];
};

layout(std430, set = 0, binding = 6) readonly buffer InstanceBuffer {
    InstanceData instances[];
};

layout(std430, set = 0, binding = 8) readonly buffer Vertices {
    MeshVertex vertices[];
};

layout(std430, set = 0, binding = 9) readonly buffer NaniteIndices {
    uint packedNaniteIndices[];
};

layout(std430, set = 0, binding = 15) readonly buffer DrawnMeshlets {
    uint drawnMeshlets[];
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

// Helper to decode 16-bit indices
uint getIndex(uint indexOffset) {
    uint wordIdx = indexOffset / 2;
    uint pair = packedNaniteIndices[wordIdx];
    if (indexOffset % 2 == 0) {
        return pair & 0xFFFFu;
    } else {
        return (pair >> 16) & 0xFFFFu;
    }
}

void main() {
    uint instIdx;
    uint meshletID;
    
    if (pcs.isNaniteMode == 1) {
        uint packedVal = gl_InstanceIndex;
        meshletID = packedVal & 0xFFFFu;
        instIdx = packedVal >> 16;
    } else if (pcs.isNaniteMode == 2) {
        uint packedVal = drawnMeshlets[gl_InstanceIndex];
        instIdx = packedVal >> 16;
        meshletID = packedVal & 0xFFFFu;
    } else {
        meshletID = 0;
        instIdx = gl_InstanceIndex;
    }
    
    InstanceData inst = instances[instIdx];
    
    if (pcs.isNaniteMode == 2) {
        VkDrawIndexedIndirectCommand cmd = inputs[inst.firstMeshletCommandOffset + meshletID];
        
        uint localVertexID = gl_VertexIndex;
        if (localVertexID >= cmd.indexCount) {
            gl_Position = vec4(0.0);
            fragColor = vec3(0.0);
            fragNormal = vec3(0.0, 0.0, 1.0);
            return;
        }
        
        uint indexOffset = cmd.firstIndex + localVertexID;
        uint localIdx = getIndex(indexOffset);
        uint globalVertexIdx = uint(cmd.vertexOffset) + localIdx;
        
        MeshVertex v = vertices[globalVertexIdx];
        vec3 pos = vec3(v.px, v.py, v.pz);
        vec3 normal = vec3(v.nx, v.ny, v.nz);
        
        gl_Position = pcs.viewProj * (inst.modelMatrix * vec4(pos, 1.0));
        fragNormal = normalize(mat3(inst.modelMatrix) * normal);
    } else {
        gl_Position = pcs.viewProj * (inst.modelMatrix * vec4(inPosition, 1.0));
        fragNormal = normalize(mat3(inst.modelMatrix) * inNormal);
    }
    
    if (pcs.isNaniteMode >= 1) {
        uint visualID = meshletID + 1;
        float r = float((visualID * 17) % 255) / 255.0;
        float g = float((visualID * 59) % 255) / 255.0;
        float b = float((visualID * 97) % 255) / 255.0;
        fragColor = vec3(r, g, b);
    } else {
        fragColor = vec3(0.0);
    }
}
