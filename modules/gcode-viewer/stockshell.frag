// stockshell.frag - цвет стенок и дна заготовки.
//
// Это тот же необработанный металл, что у нетронутого верха поля (палитра
// GcodeViewer3D.qml: stockRawColor), и та же светлота от нормали, что у верха
// (stockshell.vert). Съёма на стенках нет по построению: поле живёт внутри
// габарита, оболочка - его стенки.
VARYING float vShade;

vec3 linearToSrgb(vec3 c)
{
    return mix(c * 12.92,
               1.055 * pow(c, vec3(0.4166667)) - 0.055,
               step(vec3(0.0031308), c));
}

void MAIN()
{
    FRAGCOLOR = vec4(linearToSrgb(stockRawColor.rgb * vShade), 1.0);
}
