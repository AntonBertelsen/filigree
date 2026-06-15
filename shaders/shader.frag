#version 450

// Input color interpolated from the vertex shader
layout(location = 0) in vec3 fragColor;

// Output color for the framebuffer (RGBA)
layout(location = 0) out vec4 outColor;

void main() {
    // Write out the interpolated color with an alpha of 1.0 (opaque)
    outColor = vec4(fragColor, 1.0);
}
