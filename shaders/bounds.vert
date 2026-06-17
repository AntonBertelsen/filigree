#version 450

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
} pcs;

struct MeshletBounds {
    vec4 sphereCenterRadius;          // xyz = center, w = radius (individual bounds)
    vec4 coneAxisCutoff;              // xyz = axis, w = cutoff
    vec4 lodSphereCenterRadius;       // xyz = center, w = radius (LOD group bounds)
    vec4 parentLodSphereCenterRadius; // xyz = center, w = radius (LOD parent group bounds)
    vec4 lodParams;                   // x = selfError, y = parentError, zw = padding
};

layout(std430, set = 0, binding = 3) readonly buffer Bounds {
    MeshletBounds bounds[];
};

layout(std430, set = 0, binding = 5) readonly buffer Visibility {
    uint visibilities[];
};

layout(location = 0) out vec3 outColor;

void main() {
    uint vertexID = gl_VertexIndex;      // 0 to 95 for 3 circles of 16 segments
    uint instanceID = gl_InstanceIndex;  // meshlet index
    
    vec4 centerRadius = bounds[instanceID].sphereCenterRadius;
    vec3 center = centerRadius.xyz;
    float radius = centerRadius.w;
    uint visible = visibilities[instanceID];
    
    // Discard LOD-culled bounding spheres to keep the debug view clean
    if (visible == 5 || visible == 0) {
        gl_Position = vec4(0.0);
        outColor = vec3(0.0);
        return;
    }
    
    // 3 circles: XY, YZ, ZX
    // Each circle has 16 segments (32 vertices)
    uint circleID = vertexID / 32;
    uint segmentVertexID = vertexID % 32;
    uint segmentID = segmentVertexID / 2;
    uint isEnd = segmentVertexID % 2;
    
    float angle = 2.0 * 3.14159265 * float(segmentID + isEnd) / 16.0;
    float c = cos(angle);
    float s = sin(angle);
    
    vec3 localPos = vec3(0.0);
    if (circleID == 0) {
        localPos = vec3(c, s, 0.0);
    } else if (circleID == 1) {
        localPos = vec3(0.0, c, s);
    } else {
        localPos = vec3(s, 0.0, c);
    }
    
    vec3 worldPos = center + localPos * radius;
    gl_Position = pcs.viewProj * vec4(worldPos, 1.0);
    
    // Color coding based on culling status:
    // 1 = Visible (Green)
    // 2 = Frustum Culled (Yellow)
    // 3 = Backface Culled (Blue)
    // 4 = HZB Occlusion Culled (Red)
    // 5 = LOD Culled (Purple)
    if (visible == 1) {
        outColor = vec3(0.0, 1.0, 0.0);       // Green
    } else if (visible == 2) {
        outColor = vec3(1.0, 1.0, 0.0);       // Yellow
    } else if (visible == 3) {
        outColor = vec3(0.0, 0.5, 1.0);       // Blue
    } else if (visible == 4) {
        outColor = vec3(1.0, 0.0, 0.0);       // Red
    } else if (visible == 5) {
        outColor = vec3(0.6, 0.1, 0.8);       // Purple (LOD Culled)
    } else {
        outColor = vec3(0.5, 0.5, 0.5);       // Grey fallback
    }
}
