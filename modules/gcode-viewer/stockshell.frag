// stockshell.frag - цвет стенок и дна заготовки и обрезка верха стенки по полю.
//
// Цвет - тот же необработанный металл, что у нетронутого верха поля (палитра
// GcodeViewer3D.qml: stockRawColor), и та же светлота от нормали, что у верха
// (stockshell.vert).
//
// Обрезка верха. Оболочка статична: её край - габарит заготовки, а верх стоит на
// верху габарита (core/stock_sim, stockShellVertices), тогда как узел поля на
// краю опускается там, где инструмент снял металл через кромку (stocksurface.vert
// вычитает глубину поля из вершины). Пока верх стенки не следовал за полем, на
// внешнем контуре поверхность и стенка расходились: вдоль кромки читалась щель, а
// сама стенка показывала полный материал там, где его уже нет - замер и правило
// сверяет случай stockshell_trim пробника stock_sim_probe.
//
// Обрезка идёт во фрагменте, а не вершиной: геометрия стенки - один
// прямоугольник на грань (четыре угла, буфер сверяет случай stockshell), и
// опускание только углов дало бы вдоль кромки прямую между двумя углами, тогда
// как поле на кромке меняется по узлам. Поэтому глубина берётся по машинным
// координатам фрагмента (vMachine, stockshell.vert), а фрагмент выше материала
// (z выше верха заготовки минус глубина поля в своём месте) отбрасывается.
//
// Глубина берётся ТОЙ ЖЕ реконструкцией, что у нарисованного верха: билинейно по
// четырём узлам клетки (ниже edgeDepth, копия fieldSample из stocksurface.vert).
// Прежде здесь стоял ближайший узел (nearest), и верх стенки шёл ступенькой по
// узлам, тогда как нарисованный верх между узлами линеен: там, где верх выходил
// выше обрезанной стенки, между ними читался фон. На Test_model это не видно -
// поле на габарите не срезано, обрезка не работает вовсе, и кадры до и после
// правки совпадают пиксель в пиксель (0 px, узел 0.5 мм и грубый 2 мм); меру
// расхождения держит синтетический случай stockshell_trim_interp пробника
// stock_sim_probe - кромка, срезанная на разную глубину, у ближайшего узла
// расходится с нарисованным краем до половины перепада соседних узлов.
// Копия неизбежна: у CustomMaterial нет общего include, а буфер оболочки сверяет
// пробник - правило обрезки живёт только во фрагментном шейдере. Меняя выборку
// здесь, правь и stocksurface.vert (fieldSample/nodeDepth), и наоборот.
//
// Машинные координаты приходят свойствами QML: габарит поля (fieldMin -
// [xmin, ymin, zmin], fieldTopMm - zmax), шаг узла (fieldCellMm), число узлов
// (fieldCount), вид остатка (stockView) и само поле (stockField) - те же числа,
// что у поверхности съёма.
VARYING float vShade;
VARYING vec3 vMachine;

vec3 linearToSrgb(vec3 c)
{
    return mix(c * 12.92,
               1.055 * pow(c, vec3(0.4166667)) - 0.055,
               step(vec3(0.0031308), c));
}

// Вид остатка: числа те же, что у свойства stockView в QML и у stocksurface.vert
// (0 - точный, 1 - гладкий, 2 - скруглённый); выборка различается только
// скруглённым видом.
const int STOCK_VIEW_ROUNDED = 2;

// Значение узла поля, мм: в скруглённом виде - среднее по плюс-окрестности, как
// nodeDepth в stocksurface.vert (соседи за полем повторяют его же: wrapMode).
float nodeValue(vec2 node, vec2 count)
{
    vec2 uv = (clamp(node, vec2(0.0), count - vec2(1.0)) + vec2(0.5)) / count;
    float d = texture(stockField, uv).r;
    if (stockView != STOCK_VIEW_ROUNDED)
        return d;
    vec2 texel = vec2(1.0) / count;
    float around = texture(stockField, uv + vec2(texel.x, 0.0)).r
                 + texture(stockField, uv - vec2(texel.x, 0.0)).r
                 + texture(stockField, uv + vec2(0.0, texel.y)).r
                 + texture(stockField, uv - vec2(0.0, texel.y)).r;
    return (4.0 * d + around) / 8.0;
}

// Глубина поля в точке кромки, мм, в узлах поля: билинейно по четырём узлам
// клетки - та же реконструкция, что у нарисованного верха (fieldSample). На
// габарите вес второго ряда нулевой, поэтому получается ровно та линейная
// интерполяция между узлами кромки, какой нарисован край поверхности.
float edgeDepth(vec2 p, vec2 count)
{
    vec2 hi = max(count - vec2(1.0), vec2(0.0));
    vec2 q = clamp(p, vec2(0.0), hi);
    vec2 base = floor(q);
    vec2 f = q - base;
    float d00 = nodeValue(base, count);
    float d10 = nodeValue(min(base + vec2(1.0, 0.0), hi), count);
    float d01 = nodeValue(min(base + vec2(0.0, 1.0), hi), count);
    float d11 = nodeValue(min(base + vec2(1.0), hi), count);
    return mix(mix(d00, d10, f.x), mix(d01, d11, f.x), f.y);
}

void MAIN()
{
    vec2 count = max(fieldCount, vec2(1.0));
    vec2 node = (vMachine.xy - fieldMin.xy) / max(fieldCellMm, 1e-6);
    float depth = edgeDepth(node, count);
    // 1e-4 мм - запас на двоичное округление: он на три порядка ниже узла поля
    // 0.5 мм и не даёт кромке дрожать от шума float32.
    if (vMachine.z > fieldTopMm - depth + 1e-4)
        discard;
    FRAGCOLOR = vec4(linearToSrgb(stockRawColor.rgb * vShade), stockAlpha);
}
