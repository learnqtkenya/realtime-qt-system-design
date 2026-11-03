VARYING float vHeight;

void MAIN()
{
    vHeight = VERTEX.z;
    // Written explicitly: without it, no geometry appeared (6.12, with POINT_SIZE written).
    POSITION = MODELVIEWPROJECTION_MATRIX * vec4(VERTEX, 1.0);
    POINT_SIZE = pointSize;
}
