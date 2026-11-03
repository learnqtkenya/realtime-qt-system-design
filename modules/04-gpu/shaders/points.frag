#version 440

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    vec4 color;
    vec4 xform;
    float opacity;
};

void main()
{
    fragColor = vec4(color.rgb * color.a, color.a) * opacity;
}
