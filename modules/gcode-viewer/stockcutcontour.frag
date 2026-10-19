// stockcutcontour.frag - цвет точного контура реза.
//
// Один цвет (contourColor, свойство QML): контур - это мера, а не материал, и
// его задача - читаться на обоих тонах палитры (нетронутый верх и снятый металл).
// Плотность постоянная: линия меры не должна зависеть от стекла заготовки.
vec3 linearToSrgb(vec3 c)
{
    return mix(c * 12.92,
               1.055 * pow(c, vec3(0.4166667)) - 0.055,
               step(vec3(0.0031308), c));
}

void MAIN()
{
    FRAGCOLOR = vec4(linearToSrgb(contourColor.rgb), 1.0);
}
