VARYING float vHeight;

// Ground (-1.8 m) is blue; 2.2 m and above is red. The ramp starts at 0.2 so the ground stays visible.
vec3 ramp(float t)
{
    return clamp(vec3(1.5 - abs(4.0 * t - 3.0), 1.5 - abs(4.0 * t - 2.0), 1.5 - abs(4.0 * t - 1.0)), 0.0, 1.0);
}

void MAIN()
{
    FRAGCOLOR = vec4(ramp(0.2 + 0.8 * clamp((vHeight + 1.8) / 4.0, 0.0, 1.0)), 1.0);
}
