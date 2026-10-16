VARYING float vPathT;

vec3 linearToSrgb(vec3 c)
{
    return mix(c * 12.92,
               1.055 * pow(c, vec3(0.4166667)) - 0.055,
               step(vec3(0.0031308), c));
}

void MAIN()
{
    float alpha = 1.0;
    if (stageMode > 0.5) {
        // Stage view: draw only the selected operation block [stageStart..stageEnd]
        // (the whole stage is shown; the travelled part inside it still hides).
        if (vPathT < stageStart || vPathT > stageEnd)
            discard;
        if (cutoff > 0.0 && vPathT < cutoff)
            discard;
    } else {
        // Hide the part of the path the tool has already travelled.
        if (cutoff > 0.0 && vPathT < cutoff)
            discard;
        // Limit mode: clip the path at the visible window (pathEnd) and smoothly
        // fade the lines within the band [fadeStart .. pathEnd] to transparency,
        // so far-away toolpaths dissolve into the background instead of popping.
        if (vPathT > pathEnd)
            discard;
        if (pathEnd > fadeStart && vPathT > fadeStart)
            alpha = 1.0 - (vPathT - fadeStart) / (pathEnd - fadeStart);
    }
    // Custom-material colors arrive linearised; encode back to sRGB so the
    // lines keep the same brightness as the standard materials.
    FRAGCOLOR = vec4(linearToSrgb(lineColor.rgb), lineColor.a * alpha);
}