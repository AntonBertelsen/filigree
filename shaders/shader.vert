#version 450

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
} pcs;

// Vertex inputs from vertex buffer
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inColor;

// Output color to be passed to the Fragment Shader
layout(location = 0) out vec3 fragColor;

void main() {
    // Transform vertex position to clip space
    gl_Position = pcs.viewProj * vec4(inPosition, 1.0);
    
    // Pass the vertex color to the fragment shader
    fragColor = inColor;
}

