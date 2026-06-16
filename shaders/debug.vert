#version 450

layout(location = 0) out vec2 outUV;

void main() {
    // Generates a full-screen triangle
    // gl_VertexIndex = 0 => coords: (-1, -1), UV: (0, 0)
    // gl_VertexIndex = 1 => coords: ( 3, -1), UV: (2, 0)
    // gl_VertexIndex = 2 => coords: (-1,  3), UV: (0, 2)
    float x = -1.0 + float((gl_VertexIndex & 1) << 2);
    float y = -1.0 + float((gl_VertexIndex & 2) << 1);
    
    outUV.x = (x + 1.0) * 0.5;
    outUV.y = (y + 1.0) * 0.5;
    
    gl_Position = vec4(x, y, 0.0, 1.0);
}
