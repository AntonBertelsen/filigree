#version 450
#extension GL_EXT_shader_atomic_int64 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

// Bindings
layout(std430, set = 0, binding = 10) readonly buffer VisBuffer {
    uint64_t visBuffer[];
};

struct MeshVertex {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
};

layout(std430, set = 0, binding = 8) readonly buffer Vertices {
    MeshVertex vertices[];
};

layout(std430, set = 0, binding = 9) readonly buffer NaniteIndices {
    uint packedNaniteIndices[];
};

struct DrawCommand {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int  vertexOffset;
    uint firstInstance;
};

layout(std430, set = 0, binding = 0) readonly buffer IndirectCommands {
    DrawCommand commands[];
};

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

layout(std430, set = 0, binding = 11) readonly buffer TraditionalIndices {
    uint traditionalIndices[];
};

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    vec2 viewportSize;
    uint isNaniteMode;
    uint debugMode;
} pcs;

// Unpacks 16-bit indices from a uint array (or 32-bit indices if in traditional mode)
uint getIndex(uint indexOffset) {
    if (pcs.isNaniteMode == 0) {
        return traditionalIndices[indexOffset];
    }
    uint wordIdx = indexOffset / 2;
    uint pair = packedNaniteIndices[wordIdx];
    if (indexOffset % 2 == 0) {
        return pair & 0xFFFFu;
    } else {
        return (pair >> 16) & 0xFFFFu;
    }
}

void main() {
    // 1. Read VisBuffer value at current pixel coordinate
    uint pixelIndex = uint(gl_FragCoord.y) * uint(pcs.viewportSize.x) + uint(gl_FragCoord.x);
    uint64_t packedVal64 = visBuffer[pixelIndex];
    
    // Check if background (clear value 0xFFFFFFFFFFFFFFFF)
    if (packedVal64 == 0xFFFFFFFFFFFFFFFFul) {
        outColor = vec4(0.15, 0.15, 0.18, 1.0); // Nice dark grey background
        return;
    }
    
    // Unpack payload from low 32 bits of packedVal64
    uint payload = uint(packedVal64 & 0xFFFFFFFFu);
    
    uint instIdx;
    uint meshletID;
    uint triangleID;
    
    if (pcs.isNaniteMode == 1) {
        instIdx = payload >> 22;
        meshletID = (payload >> 8) & 0x3FFFu;
        triangleID = payload & 0xFFu;
    } else {
        instIdx = payload >> 20;
        meshletID = 0;
        triangleID = payload & 0xFFFFFu;
    }
    
    InstanceData inst = instances[instIdx];
    mat4 modelMatrix = inst.modelMatrix;
    
    // 2. Fetch Draw Command parameters for this meshlet
    uint baseIndex = 0;
    int vertexOffset = 0;
    if (pcs.isNaniteMode == 1) {
        DrawCommand cmd = commands[inst.firstMeshletCommandOffset + meshletID];
        baseIndex = cmd.firstIndex;
        vertexOffset = cmd.vertexOffset;
    } else {
        baseIndex = inst.baseIndexOffset;
        vertexOffset = int(inst.baseVertexOffset);
    }
    
    // 3. Load indices for the 3 triangle vertices
    uint i0 = getIndex(baseIndex + triangleID * 3 + 0) + vertexOffset;
    uint i1 = getIndex(baseIndex + triangleID * 3 + 1) + vertexOffset;
    uint i2 = getIndex(baseIndex + triangleID * 3 + 2) + vertexOffset;
    
    // 4. Load vertex structures
    MeshVertex v0 = vertices[i0];
    MeshVertex v1 = vertices[i1];
    MeshVertex v2 = vertices[i2];
    
    vec3 v0_pos = vec3(v0.px, v0.py, v0.pz);
    vec3 v1_pos = vec3(v1.px, v1.py, v1.pz);
    vec3 v2_pos = vec3(v2.px, v2.py, v2.pz);
    
    vec3 v0_normal = vec3(v0.nx, v0.ny, v0.nz);
    vec3 v1_normal = vec3(v1.nx, v1.ny, v1.nz);
    vec3 v2_normal = vec3(v2.nx, v2.ny, v2.nz);
    
    // 5. Project vertices to clip space & screen space
    vec4 p0 = pcs.viewProj * (modelMatrix * vec4(v0_pos, 1.0));
    vec4 p1 = pcs.viewProj * (modelMatrix * vec4(v1_pos, 1.0));
    vec4 p2 = pcs.viewProj * (modelMatrix * vec4(v2_pos, 1.0));
    
    vec2 v0_screen = (p0.xy / p0.w * 0.5 + 0.5) * pcs.viewportSize;
    vec2 v1_screen = (p1.xy / p1.w * 0.5 + 0.5) * pcs.viewportSize;
    vec2 v2_screen = (p2.xy / p2.w * 0.5 + 0.5) * pcs.viewportSize;
    
    // 6. Compute 2D Screen-space barycentric coordinates
    vec2 P = gl_FragCoord.xy;
    vec2 e0 = v1_screen - v0_screen;
    vec2 e1 = v2_screen - v0_screen;
    vec2 e2 = P - v0_screen;
    
    float den = e0.x * e1.y - e1.x * e0.y;
    if (abs(den) < 1e-6) {
        discard;
    }
    
    float v = (e2.x * e1.y - e1.x * e2.y) / den;
    float w = (e0.x * e2.y - e2.x * e0.y) / den;
    float u = 1.0 - v - w;
    
    // 7. Calculate perspective-correct weights
    float w0 = u / p0.w;
    float w1 = v / p1.w;
    float w2 = w / p2.w;
    float W = w0 + w1 + w2;
    if (W < 1e-6) {
        discard;
    }
    
    float b0 = w0 / W;
    float b1 = w1 / W;
    float b2 = w2 / W;
    
    // 8. Interpolate Normal
    vec3 interpolatedNormal = b0 * v0_normal + b1 * v1_normal + b2 * v2_normal;
    vec3 N = normalize(mat3(modelMatrix) * interpolatedNormal);
    
    // 9. Standard lighting calculation
    vec3 L1 = normalize(vec3(0.5, 1.0, 0.4));
    float diffuse1 = max(dot(N, L1), 0.0);
    
    vec3 L2 = normalize(vec3(-0.8, -0.2, -0.5));
    float diffuse2 = max(dot(N, L2), 0.0);
    
    float ambient = 0.05;
    
    if (pcs.debugMode == 0) {
        // Mode 0: Shaded View (colored by Meshlet ID if in Nanite mode)
        vec3 baseColor;
        if (pcs.isNaniteMode == 1) {
            uint visualID = meshletID + 1;
            float r = float((visualID * 17) % 255) / 255.0;
            float g = float((visualID * 59) % 255) / 255.0;
            float b = float((visualID * 97) % 255) / 255.0;
            baseColor = vec3(r, g, b);
        } else {
            baseColor = vec3(0.8, 0.8, 0.8);
        }
        float lighting = diffuse1 * 0.9 + diffuse2 * 0.05 + ambient;
        outColor = vec4(baseColor * lighting, 1.0);
    }
    else if (pcs.debugMode == 1) {
        // Mode 1: Neutral Shaded View (plain grey shading, matches traditional renderer)
        vec3 baseColor = vec3(0.8, 0.8, 0.8);
        float lighting = diffuse1 * 0.9 + diffuse2 * 0.05 + ambient;
        outColor = vec4(baseColor * lighting, 1.0);
    }
    else if (pcs.debugMode == 2) {
        // Mode 2: Triangle ID View
        uint visualID = triangleID + 1;
        float r = float((visualID * 47) % 255) / 255.0;
        float g = float((visualID * 103) % 255) / 255.0;
        float b = float((visualID * 163) % 255) / 255.0;
        outColor = vec4(r, g, b, 1.0);
    }
    else if (pcs.debugMode == 3) {
        // Mode 3: Barycentric Coordinates
        outColor = vec4(u, v, w, 1.0);
    }
    else if (pcs.debugMode == 4) {
        // Mode 4: Meshlet ID (flat colors, no shading)
        uint visualID = meshletID + 1;
        float r = float((visualID * 17) % 255) / 255.0;
        float g = float((visualID * 59) % 255) / 255.0;
        float b = float((visualID * 97) % 255) / 255.0;
        outColor = vec4(r, g, b, 1.0);
    }
}
