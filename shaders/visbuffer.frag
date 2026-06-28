#version 450

#if !VISBUFFER_32BIT
#extension GL_EXT_shader_atomic_int64 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#endif

#if EARLY_Z
layout(early_fragment_tests) in;
#endif

layout(location = 0) flat in uint inVisPayload;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    uint isNaniteMode;
    float viewportWidth;
    float viewportHeight;
    uint passIndex;
} pcs;

#if VISBUFFER_32BIT
layout(std430, set = 0, binding = 10) writeonly buffer VisBuffer {
    uint visBuffer[];
};
layout(std430, set = 0, binding = 14) buffer DepthBuffer {
    uint depthBuffer[];
};
#else
layout(std430, set = 0, binding = 10) writeonly buffer VisBuffer {
    uint64_t visBuffer[];
};
#endif

void main() {
    uint payload;
    if (pcs.isNaniteMode >= 1) {
        payload = inVisPayload | (gl_PrimitiveID & 0xFFu);
    } else {
        payload = inVisPayload | (gl_PrimitiveID & 0xFFFFFu);
    }
    
    uint pixelIndex = uint(gl_FragCoord.y) * uint(pcs.viewportWidth) + uint(gl_FragCoord.x);

#if VISBUFFER_32BIT
    uint depthInt = uint(gl_FragCoord.z * 4294967295.0);
    if (pcs.passIndex == 0) {
        atomicMin(depthBuffer[pixelIndex], depthInt);
    } else {
        if (depthBuffer[pixelIndex] == depthInt) {
            visBuffer[pixelIndex] = payload;
        }
    }
#else
    uint depthInt = uint(gl_FragCoord.z * 4294967295.0);
    uint64_t packedVal = (uint64_t(depthInt) << 32) | uint64_t(payload);
    atomicMin(visBuffer[pixelIndex], packedVal);
#endif
}
