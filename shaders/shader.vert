#version 450

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
} pcs;

// Vertex inputs from vertex buffer
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

// Output color and normal to be passed to the Fragment Shader
layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragNormal;

void main() {
    // Transform vertex position to clip space
    gl_Position = pcs.viewProj * vec4(inPosition, 1.0);
    
    // Calculate color based on gl_InstanceIndex (offset by 1 so index 0 is not pure black)
    uint meshletID = uint(gl_InstanceIndex) + 1;
    float r = float((meshletID * 17) % 255) / 255.0;
    float g = float((meshletID * 59) % 255) / 255.0;
    float b = float((meshletID * 97) % 255) / 255.0;
    
    fragColor = vec3(r, g, b);
    fragNormal = inNormal;
}

