// Flat CAD edge color for the ribbon material (CustomMaterial fragment
// stage), anti-aliased the way three.js LineMaterial does it: the vertex
// carries the signed side vSide in [-1,+1], so |vSide|*halfWidth is the
// pixel distance to the line center. Alpha falls off across one fwidth()
// band at each border - crisp AA independent of MSAA coverage (MSAA was
// thinning the 1.5 px quads into a faint hatch). linear->sRGB encoded like
// the toolpath material so outline and path lines keep the same brightness
// under ViewportEffect color management.

VARYING float vSide;

vec3 linearToSrgb(vec3 c)
{
    return mix(c * 12.92,
               1.055 * pow(c, vec3(0.4166667)) - 0.055,
               step(vec3(0.0031308), c));
}

void MAIN()
{
    float halfW = lineWidthPx * 0.5;
    float dist = abs(vSide) * halfW;
    float aa = max(fwidth(dist), 1e-6);
    float alpha = 1.0 - smoothstep(halfW - aa, halfW + aa, dist);
    FRAGCOLOR = vec4(linearToSrgb(edgeColor.rgb), edgeColor.a * alpha);
}
