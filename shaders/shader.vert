#version 450

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
} pcs;

// Output color to be passed to the Fragment Shader
layout(location = 0) out vec3 fragColor;

// Positions for our 3 vertices in 3D space
vec3 positions[3] = vec3[](
    vec3(0.0, -0.5, 0.0),   // Top
    vec3(0.5, 0.5, 0.0),    // Bottom Right
    vec3(-0.5, 0.5, 0.0)    // Bottom Left
);

// Hardcoded RGB colors for each vertex
vec3 colors[3] = vec3[](
    vec3(1.0, 0.0, 0.0), // Red
    vec3(0.0, 1.0, 0.0), // Green
    vec3(0.0, 0.0, 1.0)  // Blue
);

void main() {
    // gl_Position is a built-in output variable representing the final screen position of the vertex
    gl_Position = pcs.viewProj * vec4(positions[gl_VertexIndex], 1.0);
    
    // Pass the vertex color to the fragment shader
    fragColor = colors[gl_VertexIndex];
}

