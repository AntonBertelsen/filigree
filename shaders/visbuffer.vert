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
    float viewportWidth;
    float viewportHeight;
} pcs;

layout(location = 0) flat out uint outVisPayload;

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
    uint clustIdx;
    
    if (pcs.isNaniteMode == 1) {
        // Native Nanite MDI (using drawIndirectCount)
        instIdx = gl_InstanceIndex >> 16;
        clustIdx = gl_InstanceIndex & 0xFFFFu;
    } else if (pcs.isNaniteMode == 2) {
        // Instanced Fallback MDI (using manual vertex fetch)
        uint packedVal = drawnMeshlets[gl_InstanceIndex];
        instIdx = packedVal >> 16;
        clustIdx = packedVal & 0xFFFFu;
    } else {
        // Traditional mode
        instIdx = gl_InstanceIndex;
        clustIdx = 0;
    }
    
    if (pcs.isNaniteMode >= 1) {
        outVisPayload = (instIdx << 22) | (clustIdx << 8);
    } else {
        outVisPayload = (instIdx << 20);
    }
    
    InstanceData inst = instances[instIdx];
    
    if (pcs.isNaniteMode == 2) {
        VkDrawIndexedIndirectCommand cmd = inputs[inst.firstMeshletCommandOffset + clustIdx];
        
        uint localVertexID = gl_VertexIndex;
        if (localVertexID >= cmd.indexCount) {
            gl_Position = vec4(0.0);
            return;
        }
        
        uint indexOffset = cmd.firstIndex + localVertexID;
        uint localIdx = getIndex(indexOffset);
        uint globalVertexIdx = uint(cmd.vertexOffset) + localIdx;
        
        MeshVertex v = vertices[globalVertexIdx];
        vec3 pos = vec3(v.px, v.py, v.pz);
        gl_Position = pcs.viewProj * (inst.modelMatrix * vec4(pos, 1.0));
    } else {
        gl_Position = pcs.viewProj * (inst.modelMatrix * vec4(inPosition, 1.0));
    }
}
