// stockcutwall.frag - цвет стенки точного контура реза.
//
// Стенка целиком снятый металл: её цвет - цвет съёма палитры (stockCutColor,
// свойство QML; тот же, что у дна кармана на верху поля), а светлота приходит из
// вершинного шейдера (stockcutwall.vert). Плотность и смешение - те же, что у
// верха поля и оболочки (свойство stockAlpha, клавиша V, CNC_STOCK_GHOST).
VARYING float vShade;

vec3 linearToSrgb(vec3 c)
{
    return mix(c * 12.92,
               1.055 * pow(c, vec3(0.4166667)) - 0.055,
               step(vec3(0.0031308), c));
}

void MAIN()
{
    FRAGCOLOR = vec4(linearToSrgb(stockCutColor.rgb * vShade), stockAlpha);
}
