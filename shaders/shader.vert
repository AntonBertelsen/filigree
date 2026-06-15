#version 450

// Output color to be passed to the Fragment Shader
layout(location = 0) out vec3 fragColor;

// Hardcoded positions for our 3 vertices in Normalized Device Coordinates (NDC)
// In Vulkan:
// - X ranges from -1.0 (left) to 1.0 (right)
// - Y ranges from -1.0 (top) to 1.0 (bottom) (Note: Y is inverted compared to OpenGL!)
vec2 positions[3] = vec2[](
    vec2(0.0, -0.5),   // Top
    vec2(0.5, 0.5),    // Bottom Right
    vec2(-0.5, 0.5)    // Bottom Left
);

// Hardcoded RGB colors for each vertex
vec3 colors[3] = vec3[](
    vec3(1.0, 0.0, 0.0), // Red
    vec3(0.0, 1.0, 0.0), // Green
    vec3(0.0, 0.0, 1.0)  // Blue
);

void main() {
    // gl_Position is a built-in output variable representing the final screen position of the vertex
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    
    // Pass the vertex color to the fragment shader
    fragColor = colors[gl_VertexIndex];
}
