// stockcutcontour.vert - точный контур реза: отрезки линий на плоскости
// нетронутого верха (буфер core/stock_sim, stockCutContour). Позиции уже в
// машинных мм, высота - верх заготовки.
//
// Сдвиг глубины к глазу (depthOffset) уводит контур от нарисованного верха поля:
// они лежат в одной плоскости, и без сдвига совпавший пиксель решал бы шум
// растеризатора. Сдвигается только clip z, позиции вершин не двигаются.
void MAIN()
{
    POSITION = MODELVIEWPROJECTION_MATRIX * vec4(VERTEX, 1.0);
    POSITION.z += depthOffset * POSITION.w;
}
