#version 440

layout(location = 0) in vec2 pos; // metres

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    vec4 color;
    vec4 xform; // px per metre, centre x, centre y, point size in device pixels
    float opacity;
};

out gl_PerVertex { vec4 gl_Position; float gl_PointSize; };

void main()
{
    vec2 p = vec2(xform.y + pos.x * xform.x, xform.z - pos.y * xform.x);
    gl_Position = mvp * vec4(p, 0.0, 1.0);
    gl_PointSize = xform.w;
}
