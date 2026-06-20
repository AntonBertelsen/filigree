#version 450
#extension GL_EXT_shader_atomic_int64 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#if EARLY_Z
layout(early_fragment_tests) in;
#endif

layout(location = 0) flat in uint inVisPayload;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    uint isNaniteMode;
    float viewportWidth;
    float viewportHeight;
} pcs;

layout(std430, set = 0, binding = 10) writeonly buffer VisBuffer {
    uint64_t visBuffer[];
};

void main() {
    uint payload;
    if (pcs.isNaniteMode == 1) {
        payload = inVisPayload | (gl_PrimitiveID & 0xFFu);
    } else {
        payload = inVisPayload | (gl_PrimitiveID & 0xFFFFFu);
    }
    
    uint depthInt = uint(gl_FragCoord.z * 4294967295.0);
    uint64_t packedVal = (uint64_t(depthInt) << 32) | uint64_t(payload);
    
    uint pixelIndex = uint(gl_FragCoord.y) * uint(pcs.viewportWidth) + uint(gl_FragCoord.x);
    atomicMin(visBuffer[pixelIndex], packedVal);
}
