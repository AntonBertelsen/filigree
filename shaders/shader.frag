#version 450

// Input color and normal interpolated from the vertex shader
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;

// Output color for the framebuffer (RGBA)
layout(location = 0) out vec4 outColor;

void main() {
    // Re-normalize the interpolated normal vector
    vec3 N = normalize(fragNormal);
    
    // Set a main directional light source (top-front-right)
    vec3 L1 = normalize(vec3(0.5, 1.0, 0.4));
    float diffuse1 = max(dot(N, L1), 0.0);
    
    // Set a secondary weak directional light source (bottom-left-back)
    // to act as a fill/bounce light to add definition to shadowed areas
    vec3 L2 = normalize(vec3(-0.8, -0.2, -0.5));
    float diffuse2 = max(dot(N, L2), 0.0);
    
    // Ambient light term
    float ambient = 0.05;
    
    // Use the input color (if valid/non-black), otherwise default to a nice neutral grey
    vec3 baseColor = fragColor;
    if (length(baseColor) < 0.01) {
        baseColor = vec3(0.8, 0.8, 0.8);
    }
    
    // Calculate final shaded color
    float lighting = diffuse1 * 0.9 + diffuse2 * 0.05 + ambient;
    vec3 shadedColor = baseColor * lighting;
    
    outColor = vec4(shadedColor, 1.0);
}
