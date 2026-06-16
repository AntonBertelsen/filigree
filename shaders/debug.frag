#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform DebugPushConstants {
    uint mipLevel;
    float nearPlane;
    float farPlane;
} pcs;

layout(set = 0, binding = 0) uniform sampler2D hzbTex;

vec3 heatmap(float t) {
    // Matplotlib Viridis colormap: Purple -> Blue -> Teal -> Green -> Yellow
    vec3 c1 = vec3(0.267, 0.005, 0.329); // Purple
    vec3 c2 = vec3(0.230, 0.322, 0.545); // Blue
    vec3 c3 = vec3(0.128, 0.567, 0.551); // Teal
    vec3 c4 = vec3(0.369, 0.788, 0.383); // Green
    vec3 c5 = vec3(0.993, 0.906, 0.144); // Yellow
    
    if (t < 0.25) return mix(c1, c2, t / 0.25);
    if (t < 0.5)  return mix(c2, c3, (t - 0.25) / 0.25);
    if (t < 0.75) return mix(c3, c4, (t - 0.5) / 0.25);
    return mix(c4, c5, (t - 0.75) / 0.25);
}

void main() {
    // Sample selected mip level of HZB texture
    float depth = textureLod(hzbTex, inUV, float(pcs.mipLevel)).r;
    
    // Linearize depth to physical distance
    // z_dist = (near * far) / (far - depth * (far - near))
    float z_dist = (pcs.nearPlane * pcs.farPlane) / (pcs.farPlane - depth * (pcs.farPlane - pcs.nearPlane));
    
    // Map distance to a tight range [nearPlane, 6.0] for high-contrast visualization
    float maxVisualDistance = 6.0;
    float t = clamp((z_dist - pcs.nearPlane) / (maxVisualDistance - pcs.nearPlane), 0.0, 1.0);
    
    // Map to heatmap color (inverted Viridis so near = yellow, far = purple)
    outColor = vec4(heatmap(1.0 - t), 1.0);
}
