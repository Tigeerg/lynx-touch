VARYING float vPathT;

void MAIN()
{
    vPathT = UV0.x;
    POSITION = MODELVIEWPROJECTION_MATRIX * vec4(VERTEX, 1.0);
}