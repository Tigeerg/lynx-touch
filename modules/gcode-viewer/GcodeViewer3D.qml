import QtQuick
import QtQuick3D
import QtQuick3D as Q3D
import MockBackend 1.0

/* GcodeViewer3D — Pure QML 3D viewer for G-code toolpaths (Qt 6 / QtQuick3D).
 *   G0 (rapid) -> green, G1 (feed) -> blue, spindle + tool, floor grid.
 *   Tool tip is parked at the work-coordinate origin.
 *   LMB drag = orbit around the toolpath center
 *   RMB drag = pan the camera (moves the orbit center)
 *   wheel    = zoom
 *
 * Раскладка клавиш (обработчик - Keys.onPressed ниже, кнопки - в панели внизу):
 *   Пробел/P - пуск/пауза, Esc - в начало, вверх/вниз - скорость;
 *   S - заготовка (габарит), M - вид детали по кругу (сплошная -> прозрачная ->
 *   скрыта), K - кожа детали (тело без рёбер), G - линии УП, B - прежняя коробка
 *   вместо поверхности съёма, V - заготовка полупрозрачной;
 *   C - палитра остатка материала (пресеты), R - вид остатка по кругу
 *   (точный -> гладкий -> скруглённый -> точный контур);
 *   L - длина, T - этапы, PgUp/PgDn - кадры этапов;
 *   E - рёбра, D - касательные, Q - качество, F - fps.
 * Вид детали - один переключатель на три положения, и прозрачная кожа на нём же
 * и есть прежний рентген-вид: отдельной клавиши X у вьювера больше нет
 * (разбор - у свойства partMode).
 * Те же переключатели чистого вида доступны до старта переменными окружения
 * (cpp/viewer/main.cpp): CNC_STOCK_BOX, CNC_NO_TOOLPATH, CNC_NO_PART_SKIN,
 * CNC_STOCK_GHOST (заготовка полупрозрачной, умолчание; =0 или false - сплошная),
 * CNC_PART_MODE (solid/transparent/hidden - тот же один переключатель вида детали),
 * вид остатка - CNC_STOCK_VIEW (exact/smooth/rounded/contour, умолчание smooth) и
 * прежнее сокращение CNC_STOCK_ROUND=1 для rounded, а цвета палитры -
 * CNC_STOCK_RAW и CNC_STOCK_CUT (по "#RRGGBB"), цвет точного контура реза -
 * CNC_STOCK_CONTOUR.
 */
Item {
    id: root
    focus: true

    property color backgroundColor: "#FCFDFF"
    property string gcode: ""
    property bool loading: false     // show the "loading" overlay while a program loads
    readonly property string viewerVersion: "0.58.1+mesh.6"   // keep in sync with modules/gcode-viewer/VERSION

    // ---- orbit / pan camera state ----
    property real yaw: 35          // degrees (eulerRotation.y)
    property real pitch: -32       // degrees (eulerRotation.x)
    property real dist: 240        // distance from the pivot
    property real panX: 0          // pan offset along camera-right
    property real panY: 0          // pan offset along camera-up
    property bool invertX: false   // invert horizontal orbit (false = natural)

    // ---- качество рендера: уровень low/med/high/safe (DESIGN.md, разд. 13) ----
    // Уровень задаёт точку входа (CNC_QUALITY) или клавиша Q. Числа уровня med
    // повторяют значения, действовавшие до пресетов, поэтому без CNC_QUALITY
    // картинка та же; уровень влияет на рендер, геометрию разбирает ядро.
    property string quality: "med"
    readonly property bool qualityReduced: root.quality === "low" || root.quality === "safe"
    readonly property bool qualitySafe: root.quality === "safe"
    // Сглаживание сцены - СУПЕРВЫБОРКОЙ, а не MSAA. Кромка реза (переход
    // нетронутого/снятого металла) и линия границы считаются во фрагментном
    // шейдере по экранной производной (stocksurface.frag) - это НЕ геометрическая
    // кромка. MSAA её не сглаживает: все подвыборки пикселя получают один цвет
    // фрагмента, поэтому тонкая линия на кромке ложилась на сетку подпиксельной
    // фазой и читалась лестницей/точками (замер: кадр 1024x600, вид «точный
    // контур», облёт камерой - линия шла рваной). Сцена рендерится в 2x по каждой
    // оси (renderTextureWidth ниже) и сводится к элементу: и линия, и кромка
    // ложатся полутоном. MSAA на med/high снят - супервыборка сглаживает и
    // геометрические силуэты, а вдвоём они стоили бы вчетверо. Цена: вчетверо
    // фрагментов и памяти цели на med/high (см. журнал).
    readonly property int msaaMode: SceneEnvironment.NoAA
    // Обводка: safe тоньше (0.8 px против 1.5) и смещение глубже - дешевле на
    // программном растеризаторе; med/high/low держат сегодняшние числа.
    // Ширина снижена с 2.0/1.0 (замер: рёбра детали, касательные и рёбра
    // инструмента с оправкой читаются 1.5 px, а на safe 0.8 px - лента с
    // distance-field AA остаётся резкой). На safe картинка к тому же дороже
    // читается толще: сцена там рисуется в логических пикселях (DPR 1,
    // renderTextureWidth ниже) и растягивается на экран, поэтому одна и та же
    // ширина ложится на больше физических пикселей и выглядит грубее.
    readonly property real edgeLineWidthPx: root.qualitySafe ? 0.8 : 1.5
    // Смещение обводки к глазу в clip z (stepledges.vert, polyOffset): оно
    // держит ленту перед кожей, но теперь под обводкой идёт ещё и сдвинутый
    // к глазу остаток материала (stockDepthOffset, 3e-5 NDC). Пока смещение
    // обводки было равно сдвигу остатка (3e-5), сдвинутая поверхность съедала
    // половину обводки: замер на кадре 1024x600 (Test_model, область слитка
    // 66126 px) - чёрных пикселей лент 1347 против 2866 без сдвига остатка.
    // 6e-5 - вдвое больше сдвига остатка, и потому одно на всех уровнях: прежнее
    // 6e-5 было у safe, теперь оно у всех. Обводка остаётся на месте по экрану
    // (сдвиг только по глубине) и читается теми же 2900 пикселями, что и до
    // сдвига остатка.
    readonly property real edgePolyOffset: 0.00006
    readonly property real edgeDepthBias: root.qualitySafe ? -1500 : -1000
    // Шаг пересчёта силуэта: low/safe - каждый 4-й кадр, med/high - каждый.
    readonly property int silhouetteStep: root.qualityReduced ? 4 : 1
    // Сетка пола: low/safe грубее - реже линии и шире затухание.
    readonly property int gridStep: root.qualityReduced ? 40 : 20
    readonly property int gridFeather: root.qualityReduced ? 48 : 24
    // Масштаб рендера: med/high - супервыборка 2x по каждой оси (вчетверо
    // фрагментов и памяти цели), low/safe - логические пиксели (дешёвые уровни;
    // safe поля не рисует вовсе, поэтому контура там нет). Отсчёт - от физического
    // размера: Math.max(DPR, 2), то есть при DPR>=2 супервыборка к логическому и
    // так совпадает с физическим разрешением и памяти не добавляет.
    readonly property int renderTextureWidth: root.qualityReduced
        ? Math.round(view3d.width)
        : Math.round(view3d.width * Math.max(Screen.devicePixelRatio, 2))
    readonly property int renderTextureHeight: root.qualityReduced
        ? Math.round(view3d.height)
        : Math.round(view3d.height * Math.max(Screen.devicePixelRatio, 2))

    // ---- edge layer toggles (diagnostics: E / D) ----
    property bool showBrepEdges: true      // static STEP B-rep outline
    property bool showTangentLines: true   // view-dependent tangent contour

    // ---- FPS counter (F) ----
    // Экспоненциальное среднее кадровой частоты: мгновенное 1/frameTime на
    // vsync-мониторах стоит ровно и читать нечего, единичный дроп кадра
    // не должен бросать цифру. Коэффициент 0.9 -> скачок виден за ~10 кадров.
    property bool showFps: false
    property real fpsSmoothed: 0            // среднее, кадр/с
    FrameAnimation {
        running: root.showFps
        onTriggered: {
            if (frameTime <= 0)
                return
            var fps = 1.0 / frameTime
            root.fpsSmoothed = root.fpsSmoothed > 0
                ? root.fpsSmoothed * 0.9 + fps * 0.1 : fps
        }
    }

    // ---- screen-space outline ----
    // The silhouette rim is drawn in world units, so its on-screen width would
    // change with zoom (too thin / invisible when zoomed out). We instead scale
    // the outline radially so the rim stays ~outlinePx pixels at any distance.
    // Ширина снижена с 1.0: контур детали, резца и оправки читается 0.8 px.
    property real outlinePx: 0.8
    readonly property real outlineMmPerPx: (2.0 * root.dist * 0.5773503) / Math.max(240, view3d.height)
    function outlineScale(radius) {
        if (!radius || radius <= 0.001) return 1.0
        return Math.max(1.004, 1.0 + (root.outlinePx * root.outlineMmPerPx) / radius)
    }

    // ---- tool animation ----
    property bool playing: false       // Space toggles play / pause
    property real pathDist: 0          // travelled distance along the path (scene units)
    property real speed: 1.0           // animation speed multiplier (1.0 = 1:1 with the machine)
    readonly property real moveLength: path ? path.moveLength : 0

    // ---- draw-distance limiting ----
    // maxVisibleT: how far ahead of the tool (fraction of the whole path) may be
    // drawn. 1.0 = the entire code is shown at once (limit mode off); a smaller
    // value only draws the part near the tool and fades the rest out smoothly.
    property real maxVisibleT: 1.0
    // Smooth-fade window (fraction of the path): lines within this band after
    // the visible edge fade to transparent, so the tool "chases" the drawing.
    property real fadeBandT: 0.1
    // Simulation progress (0..1): path below this is hidden.
    readonly property real cutoffT: (path && path.moveLength > 0)
                                    ? Math.min(1.0, root.pathDist / path.moveLength) : 0.0
    // Visible window around the tool when length limiting is on.
    readonly property real visibleWindow: Math.min(1.0, root.maxVisibleT + root.fadeBandT)
    readonly property real pathEndT: (path && path.moveLength > 0)
                                     ? Math.min(1.0, root.pathDist / path.moveLength + root.visibleWindow) : 1.0
    // Полоса затухания - принадлежность режима ограничения длины: когда показан
    // весь код, затухать нечему, и хвост пути рисовался полупрозрачными линиями.
    // На слитке это читалось светлыми потёками поверх кармана: замер на кадре
    // 1024x600 (Test_model, сплошная кожа, стекло снято, камера yaw 35 pitch -32
    // dist 341) - точки 484,290 и 476,294 в кармане давали #1576CF и #3192EB
    // (синий #0162BB, разбавленный фоном) при полосе 0.1 и чистый #0162BB при
    // полосе 0.0001, то есть бледные полосы были хвостом пути, а не спором
    // поверхностей. Видны они были на глубине кармана, где хвост программы
    // (сверление) ложится плотными кадрами. Условие то же, что у pathEndT:
    // видимое окно меньше целого - полоса есть, окно целое - её нет.
    readonly property real fadeStartT: root.visibleWindow < 1.0
                                       ? Math.max(root.pathEndT - root.fadeBandT, 0.0) : 1.0

    // ---- stages (operation blocks) ----
    // Every UI_TOOL block of the program is a stage. In stage mode only the
    // selected stage is drawn; the prev/next buttons switch stages.
    property bool stageMode: false
    property int stageIndex: 0
    readonly property var stages: path ? path.stages : []
    readonly property int stageCount: stages ? stages.length : 0
    // Holder-vs-stock collisions found by the background scan.
    readonly property int collisionCount: path ? path.collisionCount : 0
    // NC-vs-machine tool mismatches (skeleton; needs the controller table).
    readonly property int toolIssueCount: path ? path.toolIssueCount : 0
    // Effective current operation: the selected one in stage mode, otherwise
    // the operation the tool is currently inside (follows the timeline).
    readonly property int stageCur: (root.stageCount > 0)
        ? (root.stageMode
           ? Math.max(0, Math.min(root.stageIndex, root.stageCount - 1))
           : root.stageAtT(root.cutoffT))
        : 0
    readonly property real stageStartT: (stageMode && stageCount > 0) ? stages[stageCur].start : 0.0
    readonly property real stageEndT: (stageMode && stageCount > 0) ? stages[stageCur].end : 1.0
    readonly property string stageLabel: (stageCount > 0)
        ? (stages[stageCur].label + "   (" + (stageCur + 1) + "/" + stageCount + ")") : ""

    // Index of the stage the given path parameter (0..1) falls into.
    function stageAtT(t) {
        for (var i = root.stageCount - 1; i >= 0; i--)
            if (t >= stages[i].start - 1e-9) return i
        return 0
    }
    // Cutter for the current operation (backend swaps its geometry per stage).
    readonly property int activeToolStage: root.stageCur
    onActiveToolStageChanged: if (path) path.setToolStage(root.activeToolStage)

    function jumpToStage() {
        if (stageCount > 0 && root.moveLength > 0) {
            var i = Math.max(0, Math.min(root.stageIndex, root.stageCount - 1))
            root.pathDist = Math.max(0.0, Math.min(root.moveLength, stages[i].start * root.moveLength))
        }
    }
    // Stage mode ON: isolate the selected operation (prev/next wrap around).
    function setStage(i) {
        if (stageCount <= 0) return
        root.stageIndex = ((i % stageCount) + stageCount) % stageCount
        if (root.stageMode) { root.playing = false; root.jumpToStage() }
    }
    // Stage mode OFF: the same buttons scrub the timeline to the neighbouring
    // operation boundary (no wrap; clamps to the start / end of the program).
    function seekStage(dir) {
        if (stageCount <= 0 || root.moveLength <= 0) return
        var t = root.stageAtT(root.cutoffT) + dir
        root.playing = false
        if (t < 0) root.pathDist = 0.0
        else if (t >= stageCount) root.pathDist = root.moveLength
        else root.pathDist = Math.max(0.0, Math.min(root.moveLength, stages[t].start * root.moveLength))
    }
    function prevStage() { if (root.stageMode) root.setStage(root.stageCur - 1); else root.seekStage(-1) }
    function nextStage() { if (root.stageMode) root.setStage(root.stageCur + 1); else root.seekStage(1) }
    function toggleStageMode() {
        root.stageMode = !root.stageMode
        if (root.stageMode) { root.playing = false; root.jumpToStage() }
    }

    // Jump the timeline to a collision point (default: the first one).
    function goToCollision(i) {
        var list = path ? path.collisions : []
        if (!list || list.length === 0) return
        var c = list[Math.max(0, Math.min(list.length - 1, i))]
        root.playing = false
        root.pathDist = Math.max(0.0, Math.min(root.moveLength, c.d))
    }

    readonly property real pitchRad: pitch * Math.PI / 180
    readonly property real yawRad: yaw * Math.PI / 180
    // Camera forward vector for Qt's euler order (X applied first, then Y).
    readonly property vector3d fwd: Qt.vector3d(
        -Math.sin(yawRad) * Math.cos(pitchRad),
        Math.sin(pitchRad),
        -Math.cos(yawRad) * Math.cos(pitchRad))

    // Camera basis for panning
    readonly property vector3d rightVec: {
        var c = Qt.vector3d(-fwd.z, 0, fwd.x)          // fwd x worldUp
        var l = Math.sqrt(c.x * c.x + c.y * c.y + c.z * c.z) || 1
        return Qt.vector3d(c.x / l, c.y / l, c.z / l)
    }
    readonly property vector3d upVec: {
        var r = rightVec
        return Qt.vector3d(r.y * fwd.z - r.z * fwd.y,
                           r.z * fwd.x - r.x * fwd.z,
                           r.x * fwd.y - r.y * fwd.x)   // right x fwd
    }

    // Orbit center = toolpath center + pan offset
    readonly property vector3d pivot: path
        ? Qt.vector3d(path.center.x + rightVec.x * panX + upVec.x * panY,
                      path.center.y + rightVec.y * panX + upVec.y * panY,
                      path.center.z + rightVec.z * panX + upVec.z * panY)
        : Qt.vector3d(0, 0, 0)

    GcodeToolpath {
        id: path
        objectName: "gcodeToolpath"
        Component.onCompleted: if (root.gcode !== "") load_string(root.gcode)
    }
    onGcodeChanged: if (gcode !== "") path.load_string(gcode)

    // Re-frame the camera whenever a new toolpath is loaded.
    // The animation does NOT start automatically: the tool stays at the start,
    // the whole path is drawn and the operator starts playback (Space / button).
    Connections {
        target: path
        function onPathChanged() {
            root.dist = Math.max(90, path.radius * 3.0 + 60)
            root.panX = 0
            root.panY = 0
            root.pathDist = 0
            root.playing = false          // never auto-play on load
            root.scheduleLabels()         // axis size/center changed
        }
    }

    // Advance the tool along the path (called while playing).
    // Real 1:1 speed: every segment is traversed at its programmed speed
    // (feed F mm/min -> mm/s, rapids at the machine rapid rate); `speed` is a
    // playback multiplier (1.0 = real time), NOT a scene speed.
    // FrameAnimation fires once per rendered frame and reports the real frame
    // time, so playback speed does not depend on the FPS (slow CPUs).
    FrameAnimation {
        running: root.playing && root.moveLength > 0
        onTriggered: {
            var v = path.speedAt(root.pathDist)
            if (v <= 0.0) v = 10.0                    // safety fallback
            // In stage mode playback stops at the end of the selected stage.
            var limit = root.moveLength
            if (root.stageMode && root.stageCount > 0)
                limit = root.stageEndT * root.moveLength
            var d = root.pathDist + root.speed * v * Math.min(frameTime, 0.1)
            if (d >= limit) {
                root.pathDist = limit
                root.playing = false
            } else {
                root.pathDist = d
            }
        }
    }

    // Живая публикация поля: пока программа играет, поле догоняет pathDist раз
    // в 100 мс - каденция из DESIGN.md (чаще нельзя: обновление текстуры - целый
    // буфер; на заготовке Test_model 150x100 мм это 242 КиБ на тик при узле
    // 0.5 мм и 1.44 МиБ при 0.2 мм). Вне проигрывания таймера нет: остановка и
    // перемотка догоняются одним вызовом на изменение pathDist/playing, а первый
    // проход считается и без них.
    Timer {
        interval: 100
        repeat: true
        running: root.playing && root.moveLength > 0
        onTriggered: if (path) path.stockFollow(root.pathDist)
    }
    Connections {
        target: root
        function onPathDistChanged() {
            if (!root.playing && path) path.stockFollow(root.pathDist)
        }
        function onPlayingChanged() {
            if (!root.playing && path) path.stockFollow(root.pathDist)
        }
    }

    // Orientation floor: centered on the STOCK bottom face (falls back to the toolpath)
    readonly property bool stockFloor: (path !== null) && path.hasStock
    readonly property real floorLevel: stockFloor ? (path.stockCenter.y - path.stockSize.y / 2)
                                                  : (path ? path.minY : -8)
    readonly property real floorX: stockFloor ? path.stockCenter.x : (path ? path.center.x : 0)
    readonly property real floorZ: stockFloor ? path.stockCenter.z : (path ? path.center.z : 0)
    readonly property real floorSize: stockFloor
        ? Math.max(300, Math.max(path.stockSize.x, path.stockSize.z) * 2.6)
        : (path ? Math.max(240, path.radius * 3.2) : 300)

    GridGeometry {
        id: floorGrid
        size: root.floorSize
        // Fixed cell/feather like the original layout (the field got wider,
        // the fade band must not grow with it - it was shrinking the visible
        // area at the edges). low/safe - грубее (quality).
        step: root.gridStep
        // Dipped ~1 px below the true floor: the part bottom and the billet
        // bottom lie exactly in this plane; coplanar surfaces z-fight.
        level: root.floorLevel - 1.0 * root.outlineMmPerPx
        centerX: root.floorX
        centerZ: root.floorZ
        feather: root.gridFeather
    }

    // Compact machine axes: X red (+X), Y green (-Z), Z blue (+Y, up)
    readonly property real axisLen: path ? Math.max(8, path.radius * 0.175) : 12
    readonly property real arrowLen: Math.max(2.5, Math.min(5, root.axisLen * 0.3))
    readonly property real arrowW: root.arrowLen * 0.6
    AxisArrowGeometry {
        id: axisArrow
        length: root.axisLen
        headLength: root.arrowLen
        headWidth: root.arrowW
        shaftWidth: 0.7
    }

    // ---- Остаток материала: сетка верха поля и оболочка коробки ----
    // Обе строятся в МАШИННЫХ миллиметрах, из тех же чисел, что публикует
    // контроллер о поле (stockBounds/stockCellMm/stockCountX/Y): сетка верха -
    // вершина на узел поля, поэтому второго уровня сэмплирования нет, а высоту
    // вершины вычитает вершинный шейдер stocksurface.vert по текстуре поля.
    // Оболочка - та же коробка без верхней грани (верх рисует поле), с прежним
    // микро-сдвигом против z-fighting: он двигает только стенки, физический
    // габарит заготовки не меняется.
    // Границы стенок (габарит плюс зазоры) стоят здесь, а не у материала: ими
    // собрана геометрия оболочки, и из них же вершинный шейдер стенок
    // (stockshell.vert) выбирает, на какой грани стоит вершина - одно число,
    // один источник.
    // Боковой зазор стенок - НОЛЬ: стенка стоит на габарите поля, а не сдвинута
    // наружу. Прежние 1.5 пикселя были сдвигом коробки против z-fighting, когда
    // коробка несла и верх; с полем верх рисует сетка, а сдвинутая наружу стенка
    // оставляла вдоль всего верхнего ребра горизонтальную полку шириной 1.5 px
    // (замер: 0.64 мм на дистанции 220 мм при 0.42 мм/px, 0.87 мм на 300 мм) -
    // сквозь неё на ребре читались фон и нутро слитка. Верх сетки доходит ровно
    // до габарита (stockSurfaceMesh прижимает крайний узел), поэтому при нулевом
    // зазоре верх и стенка сходятся в том же ребре, что обещал комментарий ядра
    // (core/stock_sim, stockShellVertices).
    readonly property real shellGapSideMm: 0
    readonly property real shellGapTopMm: 1.0 * root.outlineMmPerPx
    readonly property real shellGapBottomMm: 1.0 * root.outlineMmPerPx
    // Габарит поля в машинных мм [xmin, xmax, ymin, ymax, zmin, zmax] для
    // материалов остатка (fieldMin/fieldTopMm/fieldCount у стенок). Пустой список
    // (поля ещё нет) даёт нули, а не undefined: на undefined QML отвечает
    // предупреждением, а прогон обязан быть без "[QML warning]". Один источник и
    // на верх поля, и на стенки.
    readonly property var stockFieldBounds: (path && path.stockBounds.length >= 6)
        ? path.stockBounds : [0, 0, 0, 0, 0, 0]
    readonly property vector3d shellCenter: path
        ? Qt.vector3d((path.stockBounds[0] + path.stockBounds[1]) / 2,
                      (path.stockBounds[2] + path.stockBounds[3]) / 2,
                      (path.stockBounds[4] - root.shellGapBottomMm
                       + path.stockBounds[5] + root.shellGapTopMm) / 2)
        : Qt.vector3d(0, 0, 0)
    readonly property vector3d shellHalf: path
        ? Qt.vector3d((path.stockBounds[1] - path.stockBounds[0]) / 2 + root.shellGapSideMm,
                      (path.stockBounds[3] - path.stockBounds[2]) / 2 + root.shellGapSideMm,
                      (path.stockBounds[5] - path.stockBounds[4]
                       + root.shellGapTopMm + root.shellGapBottomMm) / 2)
        : Qt.vector3d(1, 1, 1)
    StockSurfaceGeometry {
        id: stockSurface
        boundList: path ? path.stockBounds : []
        cellMm: path ? path.stockCellMm : 0.5
        countX: path ? path.stockCountX : 0
        countY: path ? path.stockCountY : 0
        // Дробление клетки поля в рисуемом меше (гладкий вид). Поле, его текстура
        // и сверки считаются по узлам: дробление живёт только здесь.
        subdiv: root.stockSubdiv
    }
    StockShellGeometry {
        id: stockShell
        boundList: path ? path.stockBounds : []
        cellMm: path ? path.stockCellMm : 0.5
        countX: path ? path.stockCountX : 0
        countY: path ? path.stockCountY : 0
        gapSideMm: root.shellGapSideMm
        gapTopMm: root.shellGapTopMm
        gapBottomMm: root.shellGapBottomMm
    }
    // Точные слои реза (PLAN-STOCK.md, 3.1/1.5): стенка по ТОЧНОЙ границе реза и
    // сама граница полилинией. Границу считает ядро по ходам (stockCutBoundary),
    // независимо от поля: она приходит готовыми отрезками (path.stockBoundary), а
    // поле лишь даёт низ стенке (path.stockDepth, глубина дна в точке контура).
    // Данные подаются там, где слой нужен: стенка - виду «точный контур»,
    // полилиния - точному виду (там она сверяет границу со ступенями узлов) и
    // виду «точный контур» (там её строит проверка слоя, keyprobe: отрезков
    // границы столько же, сколько у стенки). Рисуется полилиния только у точного
    // вида: у вида «точный контур» границу рисует шейдер поверхности по знаковому
    // расстоянию до точной границы (stocksurface.frag), и вторая линия там
    // спорила бы с ней.
    StockCutWallGeometry {
        id: stockCutWall
        boundList: path ? path.stockBounds : []
        cellMm: path ? path.stockCellMm : 0.5
        countX: path ? path.stockCountX : 0
        countY: path ? path.stockCountY : 0
        cutMm: 0.05
        boundaryData: (root.stockLayersDrawn && path) ? path.stockBoundary : ""
        depthData: (root.stockLayersDrawn && path) ? path.stockDepth : ""
    }
    // Слоистая поверхность реза (PLAN-STOCK.md, 3.1; этап 1.3): верх нетронутого
    // материала и дно реза, разрезанные ТОЧНОЙ границей реза (path.stockBoundary),
    // а не порогом поля. Ею рисуются виды с гладким верхом (гладкий и точный
    // контур): полосы перехода между уровнями нет вовсе, а отвесную грань между
    // половинами несёт слой стенки (stockCutWall) на той же границе. Высота - тот
    // же материал и тот же шейдер, что у прежнего верха поля: меняется только меш.
    StockCutLayerGeometry {
        id: stockCutLayer
        boundList: path ? path.stockBounds : []
        cellMm: path ? path.stockCellMm : 0.5
        countX: path ? path.stockCountX : 0
        countY: path ? path.stockCountY : 0
        cutMm: 0.05
        boundaryData: (root.stockLayersDrawn && path) ? path.stockBoundary : ""
        depthData: (root.stockLayersDrawn && path) ? path.stockDepth : ""
    }
    StockCutContourGeometry {
        id: stockCutContour
        boundList: path ? path.stockBounds : []
        boundaryData: ((root.stockCutExact || root.stockContourPolyline) && path)
                      ? path.stockBoundary : ""
    }

    // Project a 3D point to View3D screen coordinates (manual perspective)
    function projectToScreen(p) {
        var camX = root.pivot.x - root.dist * root.fwd.x
        var camY = root.pivot.y - root.dist * root.fwd.y
        var camZ = root.pivot.z - root.dist * root.fwd.z
        var dx = p.x - camX
        var dy = p.y - camY
        var dz = p.z - camZ
        var zc = dx * root.fwd.x + dy * root.fwd.y + dz * root.fwd.z   // depth (front > 0)
        if (zc <= 0.001) return Qt.vector2d(-10000, -10000)
        var xc = dx * root.rightVec.x + dy * root.rightVec.y + dz * root.rightVec.z
        var yc = dx * root.upVec.x + dy * root.upVec.y + dz * root.upVec.z
        var f = Math.tan(30 * Math.PI / 180)          // default vertical FOV = 60°
        var aspect = view3d.width / Math.max(1, view3d.height)
        var ndcX = (xc / zc) / (f * aspect)
        var ndcY = (yc / zc) / f
        return Qt.vector2d((ndcX * 0.5 + 0.5) * view3d.width,
                           (1 - (ndcY * 0.5 + 0.5)) * view3d.height)
    }

    function updateAxisLabels() {
        var d = root.axisLen + 12
        var px = projectToScreen(Qt.vector3d(d, 0, 0))
        var py = projectToScreen(Qt.vector3d(0, 0, -d))
        var pz = projectToScreen(Qt.vector3d(0, d, 0))
        labelX.x = px.x - labelX.width / 2
        labelX.y = px.y - labelX.height / 2
        labelY.x = py.x - labelY.width / 2
        labelY.y = py.y - labelY.height / 2
        labelZ.x = pz.x - labelZ.width / 2
        labelZ.y = pz.y - labelZ.height / 2
    }

    View3D {
        id: view3d
        anchors.fill: parent
        // Масштаб рендера (quality): med/high - супервыборка 2x, low/safe -
        // логические пиксели. Разбор и цена - у свойства renderTextureWidth.
        explicitTextureWidth: root.renderTextureWidth
        explicitTextureHeight: root.renderTextureHeight

        environment: SceneEnvironment {
            clearColor: root.backgroundColor
            backgroundMode: SceneEnvironment.Color
            // Сглаживание - супервыборкой (renderTextureWidth), а не MSAA:
            // кромка и линия контура реза считаются во фрагментном шейдере, и
            // MSAA их не берёт - подвыборки MSAA получают один цвет фрагмента.
            // Разбор и цена - у свойства msaaMode.
            antialiasingMode: root.msaaMode
        }

        PerspectiveCamera {
            id: camera
            // Look-at by construction: position = pivot - dist * forward
            position: Qt.vector3d(root.pivot.x - root.dist * root.fwd.x,
                                  root.pivot.y - root.dist * root.fwd.y,
                                  root.pivot.z - root.dist * root.fwd.z)
            eulerRotation: Qt.vector3d(root.pitch, root.yaw, 0)
            // Distance-proportional near plane: an integer depth buffer
            // resolves ~ulp*z^2/near mm at the far end; a fixed near=1
            // spends the budget on empty space, so the ~0.2 mm line float
            // dropped under one ulp on close/grazing views (side-wall
            // outlines vanished on zoom). Scaling near with dist keeps the
            // float margin resolvable everywhere.
            clipNear: Math.max(1, root.dist * 0.01)
            clipFar: root.dist + 8000
        }

        // ---- Flat CAD lighting (SolidWorks-style, no shadow maps) ----
        // The part skin is the only PBR-lit material; everything else stays
        // NoLighting/Unshaded. A black-colored light contributes ONLY its
        // ambientColor (QtQuick3D has no AmbientLight type): a warm, high
        // ambient keeps the base beige bright on shadowed facets, while the
        // key adds just enough top/side separation to read the form.
        DirectionalLight {
            color: "#000000"
            ambientColor: "#E4DECD"
        }
        DirectionalLight {
            eulerRotation: Qt.vector3d(60, 25, 0)
            color: "#FFFFFF"
            brightness: 0.50
        }
        DirectionalLight {
            eulerRotation: Qt.vector3d(30, -150, 0)
            color: "#F0F2F5"
            brightness: 0.30
        }

        // ---- Floor grid (theme --border); alpha-feathered toward the edges ----
        Model {
            geometry: floorGrid
            materials: [
                PrincipledMaterial {
                    lighting: PrincipledMaterial.NoLighting
                    baseColor: "#C9CED4"
                    alphaMode: PrincipledMaterial.Blend
                    opacity: 1.0
                }
            ]
        }

        // ---- Machine axes: flat glyphs, X red (+X), Y green (-Z), Z blue (+Y) ----
        Model {
            geometry: axisArrow
            materials: [
                DefaultMaterial { lighting: DefaultMaterial.NoLighting; diffuseColor: "#D42125" }
            ]
        }
        Model {
            geometry: axisArrow
            eulerRotation: Qt.vector3d(0, 90, 0)
            materials: [
                DefaultMaterial { lighting: DefaultMaterial.NoLighting; diffuseColor: "#1D9247" }
            ]
        }
        Model {
            geometry: axisArrow
            eulerRotation: Qt.vector3d(0, 0, 90)
            materials: [
                DefaultMaterial { lighting: DefaultMaterial.NoLighting; diffuseColor: "#0362BB" }
            ]
        }

        // ---- Остаток материала: поле высот (верх) плюс стенки и дно ----
        // Кадр. Меш поля и текстура - в МАШИННЫХ миллиметрах, а сцена - (X, Z,
        // -Y): поворот узла на -90 по X переводит машинные (x, y, z) в (x, z,
        // -y) - та же замена, что делает бэкенд, обходя путь
        // (core/stock_sim.cpp, simulateStock). Ось выдавливания - машинный Z,
        // в кадре он уходит вверх; узел поля (x, y) с глубиной d стоит в (x,
        // zMax - d, -y), и это же число сверяет случай stocksurface пробника
        // stock_sim_probe.
        // Правило «всегда вперёд» (depthBias -50000) у поверхности нет: этот
        // bias есть только у резца, который стоит ВНУТРИ материала; поверхность
        // съёма и есть материал, и с ним она перестала бы прятать путь под
        // собой, а резаная стенка врала бы о геометрии.
        // За кем право при совпадении: у готовой программы снятая поверхность и
        // есть кожа детали, и в местах касания они спорят за один пиксель.
        // Право отдано ПОВЕРХНОСТИ СЪЁМА: во время симуляции предмет - снятый
        // металл, а кожа детали - эталон (в том числе сплошной, partMode 0), и
        // совпавший пиксель она уступает. Спор решается сдвигом ТОЛЬКО ГЛУБИНЫ в
        // вершинном шейдере (stocksurface.vert и stockshell.vert, свойство
        // stockDepthOffset): позиции вершин не двигаются, поэтому габарит и
        // глубина реза не врут, а совпавшие поверхности выигрывает снятый металл.
        // Сдвиг назад (как было до правки) давал обратное: на совпавшем верху
        // была видна кожа, а не поверхность.
        // Сплошная кожа глубину пишет, поэтому право на совпавшем пикселе решает
        // не она, а поле со знаком своего сдвига (замеры - у partMode и у
        // stockDepthOffset: у знака обратного съём уходит под кожу).
        // Где заготовка ЕЩЁ ВЫШЕ детали, спора нет вовсе: снятый металл закрывает
        // кожу собой, и это правильный вид - материал над деталью не снят.
        Node {
            visible: root.showStockVisible && root.stockFieldDrawn
            eulerRotation: Qt.vector3d(-90, 0, 0)

            // Верх поля: непрозрачный и пишет глубину - он и есть материал.
            // Светлота - своя, от нормали (stockTone/stockLamp, разбор -
            // stocksurface.vert): свет сцены давал дну кармана светлоту меньше
            // стенки, и снятый металл читался конусом, а стенки - почти одним
            // цветом.
            // Цвет - палитра пресетов (клавиша C, CNC_STOCK_RAW/CNC_STOCK_CUT):
            // нетронутый верх (глубина 0) одним цветом, снятый металл другим.
            // Порог 0.05 мм отсекает задиры на десятые доли узла 0.5 мм.
            // Текстура поля - nearest: узел сетки стоит в центре своего текселя,
            // линейная выборка R32F на ветке OpenGL 3.3 не гарантирована и
            // смазала бы стенку реза на полклетки. Гладкий вид интерполирует
            // поле сам, четырьмя nearest-выборками (stocksurface.vert): своей
            // интерполяции фильтр драйвера не касается.
            // Полупрозрачный вид (клавиша V, CNC_STOCK_GHOST) - тот же материал с
            // плотностью stockAlpha и со смешением: снятый металл читается сквозь
            // стенку, а сама стенка не бросает тень на то, что за ней. Пишет
            // глубину и при смешении (AlwaysDepthDraw): без записи дальние грани
            // и справочная кожа шли бы поверх слитка, и вместо полупрозрачной
            // заготовки была бы мешанина скрытых граней - та самая жалоба, которую
            // вылечила обратная сторона отсечения у сплошной оболочки.
            // Смешение - состояние растеризатора, а не код шейдера, поэтому вид
            // переключается свойствами материала (у alphaMode кожи так не выходит:
            // он вшит в шейдер, и там режимы разведены двумя моделями).
            Model {
                // Виды с гладким верхом (гладкий и точный контур) рисуются
                // слоями реза: верх нетронутого материала и дно, разрезанные по
                // ТОЧНОЙ границе (stockCutLayer). Прежний меш поля остаётся
                // точному виду (его предмет - ступени узлов) и скруглённому.
                geometry: root.stockLayersDrawn ? stockCutLayer : stockSurface
                materials: CustomMaterial {
                    shadingMode: CustomMaterial.Unshaded
                    property real cellMm: path ? path.stockCellMm : 0.5
                    // Сдвиг глубины к глазу от кожи детали: величину, знак и
                    // замеры держит свойство узла (stockDepthOffset, там же -
                    // почему сдвиг один на верх и стенки). Сдвигается только clip
                    // z, позиции вершин не двигаются, поэтому габарит заготовки и
                    // глубина реза не врут (ср. stocksurface пробника:
                    // высота вершины - верх минус поле).
                    property real depthOffset: root.stockDepthOffset
                    property color stockRawColor: root.stockRawColor
                    property color stockCutColor: root.stockCutColor
                    property real cutMm: 0.05
                    property real stockAlpha: root.stockGhost ? root.stockGhostAlpha : 1.0
                    // Вид остатка: свойство узла (клавиша R, CNC_STOCK_VIEW,
                    // CNC_STOCK_ROUND). Разбор видов и их цена - в
                    // stocksurface.vert; вид «точный контур» рисует верх гладким
                    // (stockShadeView), а границу реза добавляет линией.
                    property int stockView: root.stockShadeView
                    // Граница реза (вид «точный контур»): полутолщина в пикселях и
                    // цвет. 0 - линии нет. Толщина в пикселях, а не в мм: линия
                    // меры не должна толстеть и пропадать при зуме.
                    property real contourHalfPx: root.stockCutExact ? root.stockContourHalfPx : 0.0
                    property color contourColor: root.stockContourColor
                    property vector3d stockLamp: root.stockLamp
                    property real stockTone: root.stockTone
                    property TextureInput stockField: TextureInput {
                        texture: Texture {
                            textureData: path ? path.stockField : null
                            minFilter: Texture.Nearest
                            magFilter: Texture.Nearest
                            mipFilter: Texture.None
                        }
                    }
                    // Знаковое расстояние до УТОЧНЁННОЙ цепочки реза (ядро,
                    // stockCutChainDistance), мелкой сеткой - в stockCutRefine раз
                    // чаще поля: им фрагмент рисует цвет и линию границы у вида
                    // «точный контур» (cutExactBoundary), а не глубиной узла поля -
                    // поэтому кромка цвета совпадает с кромкой геометрии, а не идёт
                    // хордой клетки.
                    property TextureInput stockCutField: TextureInput {
                        texture: Texture {
                            textureData: path ? path.stockCutField : null
                            minFilter: Texture.Nearest
                            magFilter: Texture.Nearest
                            mipFilter: Texture.None
                        }
                    }
                    property bool cutExactBoundary: root.stockCutExact
                    sourceBlend: root.stockGhost ? CustomMaterial.SrcAlpha : CustomMaterial.NoBlend
                    destinationBlend: root.stockGhost ? CustomMaterial.OneMinusSrcAlpha
                                                       : CustomMaterial.NoBlend
                    depthDrawMode: root.stockGhost ? Material.AlwaysDepthDraw
                                                   : Material.OpaqueOnlyDepthDraw
                    cullMode: Material.NoCulling
                    vertexShader: "stocksurface.vert"
                    fragmentShader: "stocksurface.frag"
                }
            }

            // Стенки и дно: СПЛОШНОЕ тело - непрозрачные и пишут глубину, поэтому
            // сквозь них не видно ни дальней стенки, ни дна, ни детали внутри:
            // заготовка читается слитком, как в симуляторе обработки. Верхней
            // грани у них нет - её рисует поле.
            //
            // Сторона отсечения здесь ОБРАТНАЯ обычной. Оболочку собирает ядро
            // (core/stock_sim, stockShellVertices), и намотка её пятиугольников
            // такова, что правая нормаль смотрит ВНУТРЬ коробки; этот буфер
            // сверяет случай stockshell пробника stock_sim_probe и менять его
            // нельзя. При BackFaceCulling отсекались не дальние стенки, а
            // БЛИЖНИЕ: три грани, смотрящие на камеру, исчезали, и сквозь них
            // читались пол, справочная кожа детали и линии УП - та самая жалоба
            // на просвет боковой грани слитка. Замер по кадрам 1024x600
            // (Test_model, вид с полем съёма, ни линий УП, ни кожи):
            // BackFaceCulling против FrontFaceCulling - 13990 пикселей по стенке
            // слитка, то есть ближняя стенка была снята целиком, и на её месте
            // читались пол и справочная кожа детали. FrontFaceCulling снимает
            // дальние грани (их и так прячет буфер глубины) и оставляет ближние.
            // Если намотку в ядре однажды поправят, эту строку надо вернуть на
            // BackFaceCulling.
            //
            // Цвет и светлота - те же, что у верха поля: тот же необработанный
            // металл (stockRawColor) и та же лампа слитка, но нормаль грани
            // считается здесь, в вершинном шейдере, из положения вершины
            // (stockshell.vert): буфер оболочки - одни позиции, его сверяет
            // случай stockshell пробника (30 вершин по три числа), менять его
            // нельзя. Прежде оболочка шла совсем без света (у материала не было
            // нормали) и все стенки были залиты одним #B79A55 - отсюда и жалоба
            // на странный цвет боковых граней.
            // Верх стенки обрезается по полю высот (stockshell.frag): стенка
            // стоит на габарите и не знала бы, что инструмент снял металл через
            // кромку. Поле и его габарит приходят сюда теми же числами, что у
            // верха: габарит (fieldMin - низ угла, fieldTopMm - верх заготовки),
            // шаг (fieldCellMm), узлы (fieldCount) и сама текстура (stockField).
            // Полупрозрачный вид (V, CNC_STOCK_GHOST) - как у верха поля
            // (комментарий там): та же плотность и та же запись глубины.
            // Инструмент внутри виден и так: резец, оправка и линии УП идут
            // «всегда вперёд» (depthBias -50000) и рисуются поверх слитка.
            Model {
                geometry: stockShell
                materials: CustomMaterial {
                    shadingMode: CustomMaterial.Unshaded
                    property color stockRawColor: root.stockRawColor
                    property vector3d stockLamp: root.stockLamp
                    property real stockTone: root.stockTone
                    property real stockAlpha: root.stockGhost ? root.stockGhostAlpha : 1.0
                    // Тот же сдвиг глубины к глазу, что у верха поля: наружные
                    // стенки слитка совпадают с наружными гранями детали, и без
                    // сдвига совпавший пиксель решал шум растеризатора - стенка
                    // уходила в кожу детали, а вдоль её верха шла полоса
                    // (разбор и замеры - у свойства stockDepthOffset).
                    property real depthOffset: root.stockDepthOffset
                    property vector3d fieldMin: Qt.vector3d(root.stockFieldBounds[0],
                                                            root.stockFieldBounds[2],
                                                            root.stockFieldBounds[4])
                    property real fieldTopMm: root.stockFieldBounds[5]
                    property real fieldCellMm: path ? path.stockCellMm : 0.5
                    property vector2d fieldCount: Qt.vector2d(path ? path.stockCountX : 0,
                                                             path ? path.stockCountY : 0)
                    // Вид остатка: обрезка верха стенки берёт глубину той же
                    // выборкой, что и нарисованный верх (stockshell.frag).
                    property int stockView: root.stockShadeView
                    property TextureInput stockField: TextureInput {
                        texture: Texture {
                            textureData: path ? path.stockField : null
                            minFilter: Texture.Nearest
                            magFilter: Texture.Nearest
                            mipFilter: Texture.None
                        }
                    }
                    // Границы стенок в машинных мм (габарит плюс зазоры): те же
                    // числа, из которых собрана геометрия оболочки, стоят рядом
                    // с ней - у узла остатка материала.
                    property vector3d shellCenter: root.shellCenter
                    property vector3d shellHalf: root.shellHalf
                    sourceBlend: root.stockGhost ? CustomMaterial.SrcAlpha : CustomMaterial.NoBlend
                    destinationBlend: root.stockGhost ? CustomMaterial.OneMinusSrcAlpha
                                                       : CustomMaterial.NoBlend
                    depthDrawMode: root.stockGhost ? Material.AlwaysDepthDraw
                                                   : Material.OpaqueOnlyDepthDraw
                    cullMode: Material.FrontFaceCulling
                    vertexShader: "stockshell.vert"
                    fragmentShader: "stockshell.frag"
                }
            }

            // Точная стенка реза (PLAN-STOCK.md, 3.1): вертикальные квады по
            // ТОЧНОЙ границе реза, а не по ступеням узлов поля. Верх на
            // плоскости нетронутого верха, низ - на кромке отрезанного дна
            // (stockLayerDepthMm, у уступа - на уровне дна, а не на билинейной
            // глубине поля), поэтому стык верх/стенка/дно идёт по одной границе,
            // без щели. Буфер собирает ядро (core/stock_sim,
            // stockCutWallFromLayer), геометрия - qmlbackend/gv_stockgeom.
            // Видна там же, где рисуется слоистая поверхность: у гладкого вида и
            // вида «точный контур» (stockLayersDrawn). Она и несёт ту отвесную
            // грань, которой между верхом и дном больше нет - верх разрезан по
            // границе.
            Model {
                visible: root.showStockVisible && root.stockFieldDrawn
                         && root.stockLayersDrawn
                geometry: stockCutWall
                materials: CustomMaterial {
                    shadingMode: CustomMaterial.Unshaded
                    property color stockCutColor: root.stockCutColor
                    property real stockTone: root.stockTone
                    property real stockAlpha: root.stockGhost ? root.stockGhostAlpha : 1.0
                    // Тот же сдвиг глубины к глазу, что у верха поля: стенка
                    // стоит в плоскости нетронутого верха и спорит с ним за
                    // совпавший пиксель.
                    property real depthOffset: root.stockDepthOffset
                    sourceBlend: root.stockGhost ? CustomMaterial.SrcAlpha : CustomMaterial.NoBlend
                    destinationBlend: root.stockGhost ? CustomMaterial.OneMinusSrcAlpha
                                                       : CustomMaterial.NoBlend
                    depthDrawMode: root.stockGhost ? Material.AlwaysDepthDraw
                                                   : Material.OpaqueOnlyDepthDraw
                    cullMode: Material.NoCulling
                    vertexShader: "stockcutwall.vert"
                    fragmentShader: "stockcutwall.frag"
                }
            }

            // Точный контур реза полилинией: та же граница, что у стенки, но
            // нарисованная линией на плоскости нетронутого верха. Здесь она -
            // сверка точного вида: узел рисует ступенями, а полилиния показывает,
            // где граница реза на самом деле. Граница вида «точный контур»
            // нарисована иначе - по знаковому расстоянию поля во фрагменте
            // (stocksurface.frag), поэтому полилиния отдана точному виду, где
            // сравнивать есть с чем. Цвет - свой (stockContourColor): линия меры,
            // а не материал, и обязана читаться на обоих тонах палитры.
            Model {
                visible: root.showStockVisible && root.stockFieldDrawn
                         && root.stockContourPolyline
                geometry: stockCutContour
                materials: CustomMaterial {
                    shadingMode: CustomMaterial.Unshaded
                    property color contourColor: root.stockContourColor
                    // Сдвиг чуть сильнее, чем у верха поля (stockDepthOffset):
                    // контур лежит в плоскости нетронутого верха, и равный сдвиг
                    // оставил бы совпавший пиксель на спор растеризатору.
                    property real depthOffset: root.stockContourOffset
                    vertexShader: "stockcutcontour.vert"
                    fragmentShader: "stockcutcontour.frag"
                }
            }
        }

        // ---- Прежняя коробка: сравнение с полем и вид, пока поля нет ----
        // Видна, когда поля нет (состояние не ready или уровень safe) либо её
        // включили нарочно (клавиша B, CNC_STOCK_BOX). Прежний вид: приподнята
        // против детали на доли пикселя (иначе совпадающие грани мерцают при
        // облёте), полупрозрачная (opacity 0.2) и глубину не пишет, поэтому по
        // ней видно траекторию внутри.
        Model {
            visible: root.showStockVisible && (path !== null) && path.hasStock
                     && !root.stockFieldDrawn
            source: "#Cube"
        // Микро-сдвиг против z-fighting (только порядок глубины, около 1.5 px):
        // грани заготовки совпадают с необработанными гранями детали, а верх и
        // низ лежат в её плоскостях - совпадающие поверхности мерцают при облёте.
        // Габарит заготовки (коллизии, зазоры) от этого не меняется, он точный.
        readonly property real gapMm: 1.5 * root.outlineMmPerPx
        readonly property real topGapMm: 1.0 * root.outlineMmPerPx
        readonly property real botGapMm: 1.0 * root.outlineMmPerPx
        position: path
                ? Qt.vector3d(path.stockCenter.x,
                              path.stockCenter.y + (topGapMm + botGapMm) / 2,
                              path.stockCenter.z)
                : Qt.vector3d(0, 0, 0)
        scale: path && path.stockSize.x > 0
               ? Qt.vector3d((path.stockSize.x + 2 * gapMm) / 100,
                             (path.stockSize.y + topGapMm - botGapMm) / 100,
                             (path.stockSize.z + 2 * gapMm) / 100)
               : Qt.vector3d(0.001, 0.001, 0.001)
            opacity: 0.2
            materials: [
                // Габаритная оболочка, а не геометрия: коробка охватывает
                // деталь, поэтому с непрозрачной кожей её дальние грани теряли
                // тест глубины и обводка тускнела. Как и линии УП, она
                // собирается всегда вперёд (глубину не пишет).
                DefaultMaterial {
                    lighting: DefaultMaterial.NoLighting
                    diffuseColor: "#8A93A0"
                    depthDrawMode: Material.NeverDepthDraw
                }
            ]
        }

        // ---- Part model: opaque CAD skin + depth-tested edge layers ----
        // Classic shaded-with-edges: the skin writes the depth buffer and
        // the two line layers (B-rep outline, analytic tangents) render in
        // the SAME scene - the rasterizer hides line parts behind the body
        // per fragment, live while orbiting; the lines are floated ~0.05 mm
        // outward from the model center (geometric, zoom-proof) so they
        // never z-fight with their own grazing surface.
        Node {
            visible: root.showPartVisible
            rotation: path ? path.partRotation : Qt.quaternion(1, 0, 0, 0)
            position: path ? path.partPosition : Qt.vector3d(0, 0, 0)
            // Сплошная кожа - вид детали по умолчанию (partMode 0). Материал
            // строго Opaque и пишет глубину: только тогда растеризатор закрывает
            // СКРЫТЫЕ рёбра и задние грани. Прежний Blend с opacity 1.0 шёл в
            // прозрачную очередь, глубину не писал - задние грани и скрытые
            // кромки просвечивали серым. alphaMode в рантайме не
            // перекомпилируется, поэтому режимы разведены двумя моделями
            // (visible), а не сменой свойства на одном материале.
            // Клавиша K снимает кожу целиком (тело), оставляя рёбра и
            // касательные: так на снятом металле остаётся только справочный
            // каркас детали.
            Model {
                visible: root.partSkinSolid
                geometry: path ? path.partGeometry : null
                materials: [
                    PrincipledMaterial {
                        baseColor: "#B9BEC4"
                        roughness: 0.9
                        metalness: 0.0
                        specularAmount: 0.2
                        cullMode: Material.NoCulling
                        alphaMode: PrincipledMaterial.Opaque
                    }
                ]
            }
            // Прозрачная деталь (partMode 1): та же кожа полупрозрачной, глубину
            // не пишет - сквозь неё видны снятый металл, скрытые кромки и то, что
            // за деталью. Плотность - свойство partSkinOpacity. Прежний рентген
            // (клавиша X) и справочная кожа симуляции слились сюда: тот же вид,
            // одна клавиша, одна плотность (разбор - у partMode). Цена названа
            // прямо: смешение идёт вне очереди по глубине и стоит кадра, поэтому
            // это вид «посмотреть внутрь», а не рабочий.
            Model {
                visible: root.partSkinClear
                geometry: path ? path.partGeometry : null
                materials: [
                    PrincipledMaterial {
                        baseColor: "#B9BEC4"
                        roughness: 0.9
                        metalness: 0.0
                        specularAmount: 0.2
                        cullMode: Material.NoCulling
                        alphaMode: PrincipledMaterial.Blend
                        opacity: root.partSkinOpacity
                    }
                ]
            }
            Model {
                visible: root.showBrepEdges
                geometry: path ? path.hlrEdges : null
                // Малый bias + cam-float (смещение к глазу в set_segments):
                // видимые рёбра чёткие, скрытые отсекаются кожей.
                // -50000 (передний слой) давал ложный «рентген» детали.
                // safe - глубже (quality): coarse-точность программного растра.
                depthBias: root.edgeDepthBias
                // Constant-width screen-space ribbons (1.5 px, distance-field AA):
                // crisp at any zoom (three.js LineMaterial style: alpha falls off
                // across fwidth() borders, not by MSAA coverage), depth-tested.
                materials: CustomMaterial {
                    shadingMode: CustomMaterial.Unshaded
                    property real lineWidthPx: root.edgeLineWidthPx
                    property real polyOffset: root.edgePolyOffset
                    property real viewportW: view3d.width
                    property real viewportH: view3d.height
                    property color edgeColor: "#000000"
                    property vector3d eyeRaw: path ? path.eyeRaw : Qt.vector3d(0, 0, 0)
                    property real camFloatMm: 0.0   // сдвиг к глазу в stepledges.vert
                    cullMode: Material.NoCulling
                    vertexShader: "stepledges.vert"
                    fragmentShader: "stepledges.frag"
                }
            }
            Model {
                visible: root.showTangentLines
                geometry: path ? path.hlrTangents : null
                // Силуэт по мешу (ребро на горизонте поверхности): нужен тест
                // глубины, иначе дальние участки горизонта рисуются сквозь тело
                // и сдваиваются со швами. Малый bias + cam-float держат их
                // чёткими на видимых стенках; safe - глубже (quality).
                depthBias: root.edgeDepthBias
                // Constant-width screen-space ribbons (1.5 px, distance-field AA):
                // crisp at any zoom (three.js LineMaterial style: alpha falls off
                // across fwidth() borders, not by MSAA coverage), depth-tested.
                materials: CustomMaterial {
                    shadingMode: CustomMaterial.Unshaded
                    property real lineWidthPx: root.edgeLineWidthPx
                    property real polyOffset: root.edgePolyOffset
                    property real viewportW: view3d.width
                    property real viewportH: view3d.height
                    property color edgeColor: "#000000"
                    property vector3d eyeRaw: path ? path.eyeRaw : Qt.vector3d(0, 0, 0)
                    property real camFloatMm: 0.0   // сдвиг к глазу в stepledges.vert
                    cullMode: Material.NoCulling
                    vertexShader: "stepledges.vert"
                    fragmentShader: "stepledges.frag"
                }
            }
        }

        // ---- Holder-vs-stock collision markers (from the background scan) ----
        Repeater3D {
            model: path ? path.collisions : []
            delegate: Model {
                required property var modelData
                visible: root.showCollisions
                source: "#Sphere"
                position: (modelData && modelData.scene_hb)
                          ? Qt.vector3d(modelData.scene_hb[0], modelData.scene_hb[1], modelData.scene_hb[2])
                          : Qt.vector3d(0, 0, 0)
                scale: Qt.vector3d(0.14, 0.14, 0.14)
                materials: [
                    DefaultMaterial { lighting: DefaultMaterial.NoLighting; diffuseColor: "#D32F2F" }
                ]
            }
        }

        // ---- G0 rapids (green); traversed part hidden during playback, far
        // part clipped and faded out when length limiting is on.
        // depthBias -50000: composite last, above the translucent part ----
        // Ширины у линий УП нет: геометрия - аппаратные GL_LINES (cpp/qt/gv_geometry,
        // PrimitiveType::Lines), то есть ровно 1 px, и ручки вроде lineWidthPx у
        // них не завести, не переведя траекторию в ленты. Толще они читаются на
        // low/safe: там сцена рисуется в логических пикселях (renderTextureWidth)
        // и растягивается на экран, и один пиксель ложится на больше физических.
        Model {
            visible: root.showToolpath
            geometry: path ? path.rapidGeometry : null
            depthBias: -50000
            materials: [
                CustomMaterial {
                    shadingMode: CustomMaterial.Unshaded
                    property real cutoff: root.cutoffT
                    property real pathEnd: root.pathEndT
                    property real fadeStart: root.fadeStartT
                    property real stageMode: root.stageMode ? 1.0 : 0.0
                    property real stageStart: root.stageStartT
                    property real stageEnd: root.stageEndT
                    property color lineColor: "#1D9247"
                    sourceBlend: CustomMaterial.SrcAlpha
                    destinationBlend: CustomMaterial.OneMinusSrcAlpha
                    cullMode: Material.NoCulling
                    depthDrawMode: Material.NeverDepthDraw
                    vertexShader: "toolpath.vert"
                    fragmentShader: "toolpath.frag"
                }
            ]
        }

        // ---- G1 feeds (blue); traversed part hidden during playback, far
        // part clipped and faded out when length limiting is on.
        // depthBias -50000: composite last, above the translucent part ----
        Model {
            visible: root.showToolpath
            geometry: path ? path.feedGeometry : null
            depthBias: -50000
            materials: [
                CustomMaterial {
                    shadingMode: CustomMaterial.Unshaded
                    property real cutoff: root.cutoffT
                    property real pathEnd: root.pathEndT
                    property real fadeStart: root.fadeStartT
                    property real stageMode: root.stageMode ? 1.0 : 0.0
                    property real stageStart: root.stageStartT
                    property real stageEnd: root.stageEndT
                    property color lineColor: "#0362BB"
                    sourceBlend: CustomMaterial.SrcAlpha
                    destinationBlend: CustomMaterial.OneMinusSrcAlpha
                    cullMode: Material.NoCulling
                    depthDrawMode: Material.NeverDepthDraw
                    vertexShader: "toolpath.vert"
                    fragmentShader: "toolpath.frag"
                }
            ]
        }

        // ---- Spindle + tool (tip follows the animated path point) ----
        Node {
            id: toolNode
            position: path ? path.sampleAt(root.pathDist) : Qt.vector3d(0, 0, 0)
            readonly property real toolLen: (path && path.toolLength > 0) ? path.toolLength : 24
            // Holder bottom: the gauge length from UI_HOLDER when exported, else
            // the cutter stick-out (toolLen).
            readonly property real holderBase: (path && path.holderGauge > 0) ? path.holderGauge : toolLen

            // Резец собран из UI_TOOL (плоский / сферический / радиусный / фаска / сверло...)
            Model {
                // depthBias -50000 (всегда вперёд) оставлен намеренно: резец стоит
                // ВНУТРИ заготовки и детали, и тест глубины спрятал бы его за их
                // кожей. Уровень материала (PrincipledMaterial) при этом тот же,
                // что у детали, - резец читается её металлом под тем же светом.
                depthBias: -50000
                geometry: path ? path.toolGeometry : null
                materials: [
                    PrincipledMaterial {
                        baseColor: "#B9BEC4"
                        roughness: 0.9
                        metalness: 0.0
                        specularAmount: 0.2
                        cullMode: Material.NoCulling
                        alphaMode: PrincipledMaterial.Opaque
                    }
                ]
            }
            // Outer contour (silhouette) via inverted hull: an enlarged copy of
            // the cutter. The cutter mesh is wound inward (outward faces are the
            // "back" faces), so back-face culling keeps only the far side of the
            // enlarged copy -> just a dark rim around the silhouette (no fill,
            // no rings, no longitudinal seams).
            Model {
                depthBias: -50000
                geometry: path ? path.toolOutline : null
                scale: {
                    var s = root.outlineScale(path ? path.toolRadius : 0)
                    return Qt.vector3d(s, 1.0, s)
                }
                materials: [
                    DefaultMaterial {
                        lighting: DefaultMaterial.NoLighting
                        diffuseColor: "#141B24"
                        cullMode: Material.BackFaceCulling
                    }
                ]
            }
            // Рёбра резца: кольца профиля и продольные образующие - те же ленты
            // stepledges, что у детали (постоянная ширина в px, AA, depth-тест).
            // depthBias -50000: резец внутри заготовки и детали, depth-тест его
            // скрыл бы (см. комментарий у тела).
            Model {
                depthBias: -50000
                geometry: path ? path.toolProfileEdges : null
                materials: CustomMaterial {
                    shadingMode: CustomMaterial.Unshaded
                    property real lineWidthPx: root.edgeLineWidthPx
                    property real polyOffset: root.edgePolyOffset
                    property real viewportW: view3d.width
                    property real viewportH: view3d.height
                    property color edgeColor: "#000000"
                    property vector3d eyeRaw: path ? path.eyeRaw : Qt.vector3d(0, 0, 0)
                    property real camFloatMm: 0.0
                    cullMode: Material.NoCulling
                    vertexShader: "stepledges.vert"
                    fragmentShader: "stepledges.frag"
                }
            }
            // ---- Holder: primitive ER20 collet nut + spindle body ----
            // Solid of revolution from the backend (path.holderGeometry), bottom
            // at the stick-out. Silhouette outline = inverted hull of an OPEN
            // shell (no flat bottom/top caps -> no dark disks), scaled radially
            // so the rim stays ~outlinePx px at any zoom.
            Model {
                depthBias: -50000
                geometry: path ? path.holderOutline : null
                position: Qt.vector3d(0, toolNode.holderBase, 0)
                scale: {
                    var s = root.outlineScale(path ? path.holderRadius : 0)
                    return Qt.vector3d(s, 1.0, s)
                }
                materials: [
                    DefaultMaterial {
                        lighting: DefaultMaterial.NoLighting
                        diffuseColor: "#141B24"
                        cullMode: Material.BackFaceCulling
                    }
                ]
            }
            // Рёбра оправки: кольца профиля и продольные образующие - лентами
            // stepledges, как у детали и резца (см. комментарий у toolProfileEdges).
            Model {
                depthBias: -50000
                geometry: path ? path.holderProfileEdges : null
                position: Qt.vector3d(0, toolNode.holderBase, 0)
                materials: CustomMaterial {
                    shadingMode: CustomMaterial.Unshaded
                    property real lineWidthPx: root.edgeLineWidthPx
                    property real polyOffset: root.edgePolyOffset
                    property real viewportW: view3d.width
                    property real viewportH: view3d.height
                    property color edgeColor: "#000000"
                    property vector3d eyeRaw: path ? path.eyeRaw : Qt.vector3d(0, 0, 0)
                    property real camFloatMm: 0.0
                    cullMode: Material.NoCulling
                    vertexShader: "stepledges.vert"
                    fragmentShader: "stepledges.frag"
                }
            }
            Model {
                // depthBias -50000 (всегда вперёд): оправка стоит внутри
                // заготовки; тест глубины скрыл бы её за кожей. Материал - как у
                // детали, тот же свет сцены (PrincipledMaterial сам берёт
                // DirectionalLight'ы сцены).
                depthBias: -50000
                geometry: path ? path.holderGeometry : null
                position: Qt.vector3d(0, toolNode.holderBase, 0)
                materials: [
                    PrincipledMaterial {
                        baseColor: "#B9BEC4"
                        roughness: 0.9
                        metalness: 0.0
                        specularAmount: 0.2
                        cullMode: Material.NoCulling
                        alphaMode: PrincipledMaterial.Opaque
                    }
                ]
            }
        }
    }


    // ---- Axis labels (2D overlay projected onto the axis tips) ----
    Text {
        id: labelX
        objectName: "labelX"
        visible: path !== null
        text: "X"
        color: "#D42125"
        font.bold: true
        font.pixelSize: 15
    }
    Text {
        id: labelY
        objectName: "labelY"
        visible: path !== null
        text: "Y"
        color: "#1D9247"
        font.bold: true
        font.pixelSize: 15
    }
    Text {
        id: labelZ
        objectName: "labelZ"
        visible: path !== null
        text: "Z"
        color: "#0362BB"
        font.bold: true
        font.pixelSize: 15
    }
    // Re-project the axis labels only when an input actually changes.
    // Qt.callLater coalesces several changes in one frame into a single call,
    // so there is no polling timer and zero work while the view is idle.
    function scheduleLabels() { Qt.callLater(root.updateAxisLabels) }
    onYawChanged: scheduleLabels()
    onPitchChanged: scheduleLabels()
    onDistChanged: scheduleLabels()
    onPanXChanged: scheduleLabels()
    onPanYChanged: scheduleLabels()
    Component.onCompleted: scheduleLabels()
    Connections {
        target: view3d
        function onWidthChanged() { root.scheduleLabels() }
        function onHeightChanged() { root.scheduleLabels() }
    }

    // ---- Orbit (LMB) / pan (RMB) / zoom (wheel) ----
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        property real lastX: 0
        property real lastY: 0
        property bool panning: false

        onPressed: (mouse) => {
            root.forceActiveFocus()
            lastX = mouse.x
            lastY = mouse.y
            panning = (mouse.button === Qt.RightButton)
        }

        onReleased: panning = false

        onPositionChanged: (mouse) => {
            var dx = mouse.x - lastX
            var dy = mouse.y - lastY
            lastX = mouse.x
            lastY = mouse.y
            if (panning) {
                var s = root.dist * 0.0015
                root.panX -= dx * s
                root.panY += dy * s
            } else {
                root.yaw += (root.invertX ? dx : -dx) * 0.4
                root.pitch = Math.max(-89, Math.min(89, root.pitch - dy * 0.4))
            }
        }

        onWheel: (wheel) => {
            var f = Math.exp(-wheel.angleDelta.y / 600.0)
            root.dist = Math.max(25, Math.min(4000, root.dist * f))
        }
    }

    // ---- display modes ----
    property bool showStockVisible: true     // показывать заготовку
    // Чем рисуется заготовка. Поле - когда контроллер опубликовал готовое поле
    // (stockHasField) и уровень не safe: у safe поле рисуется грубейшим узлом и
    // дороже всего кадра (DESIGN.md, разд. 13). Признак поля, а не состояние
    // stockSimState: пока считается новое поле (тик, перемотка), на экране
    // остаётся прежнее целое, и рисование не должно на это время пропадать.
    // Коробка - пока поля нет (первый проход), после отказа (инструмент картой
    // не выражается) и по клавише B / CNC_STOCK_BOX=1 для сравнения.
    property bool stockPlainBox: false       // коробка вместо поля (B, CNC_STOCK_BOX)
    // Заготовка полупрозрачной: клавиша V, CNC_STOCK_GHOST до старта (те же две
    // ручки, что у прочих видов). По умолчанию ВКЛЮЧЕНО - так просил оператор
    // (дважды): стеклянный слиток читается как материал, сквозь него видны
    // карман, деталь и инструмент. Плата: по цвету слитка снятое и нетронутое
    // металл различают хуже, чем у сплошного (смешение с фоном), поэтому
    // плотность поднята с 0.35 до 0.90 - это доля цвета материала в пикселе;
    // замер на кадре 1024x600 (Test_model, кожа и линии сняты): медный съём
    // #BD5215 под стеклом читается как #DA7A44, нетронутая охра #BEA05C как
    // #D3B87C при 0.90, а при 0.62 те же точки давали #FFCFAA и #F2DFB5 -
    // почти фон. Сквозь 0.90 видно нутро и инструмент, то есть вид остался
    // стеклом, а не сплошным. Сплошной вид - на той же клавише V и через
    // CNC_STOCK_GHOST=0 до старта.
    // Плотность одна на верх поля и на стенки: слиток читается одним телом.
    // Сравнение с коробкой (B, CNC_STOCK_BOX) не путать: там материал уходил в
    // коробку без поля, здесь - тот же снятый металл, но сквозь него видно.
    property bool stockGhost: true
    readonly property real stockGhostAlpha: 0.90
    // Вид остатка заготовки: 0 - точный по узлам поля, 1 - гладкий (меш
    // дроблённый, высота билинейная), 2 - скруглённый (глубина узла - среднее по
    // окрестности), 3 - точный контур (гладкий верх и точные слои реза по границе).
    // Один переключатель на все четыре: клавиша R листает по кругу,
    // CNC_STOCK_VIEW ставит вид до старта, а CNC_STOCK_ROUND=1 остаётся
    // сокращением для третьего (прежняя ручка скругления).
    // По умолчанию гладкий: им сняты обе жалобы на рваность - полоса вдоль
    // кромки ступени и мозаика мелких граней на стенке. Цены: у точного вида
    // полоса в клетку на верху у кромки и грани меша; у скруглённого кромка реза
    // уезжает вглубь до узла (0.5 мм) и мелкое окно теряет до половины глубины;
    // у гладкого нарисованная поверхность расходится с полем до четверти
    // кручения клетки, а меш крупнее в subdiv^2 раз. Разбор - в stocksurface.vert.
    property int stockView: 1
    // Вид выборки для ШЕЙДЕРА у вида «точный контур»: он рисует гладкий верх
    // (1), а точную границу добавляет линией по знаковому расстоянию поля. Иначе
    // ступени меша спорили бы с той самой границей, ради которой вид и заведён.
    readonly property int stockShadeView: root.stockView === 3 ? 1 : root.stockView
    // Дробление клетки поля в прежнем меше (StockSurfaceGeometry): 1 у точного
    // вида - он и есть показ ступеней узла, там дробить нечего; 2 у скруглённого.
    // Виды с гладким верхом меш берут у слоистой поверхности
    // (StockCutLayerGeometry): там билинейная высота на подклетке - та же
    // поверхность, а ступень уступа режет геометрия, поэтому дробление не нужно.
    // Цена дробления - вчетверо больше вершин и треугольников (замер: 60501 узел
    // поля и 120000 треугольников против 241001 вершины и 480000 треугольников;
    // поле, его текстура и сверки не меняются).
    readonly property int stockSubdiv: root.stockView === 0 ? 1 : 2
    // Вид «точный контур» (четвёртый в списке): гладкий верх плюс точные слои
    // реза - верх и дно, разрезанные по ТОЧНОЙ границе реза
    // (StockCutLayerGeometry, core/stock_sim, stockLayerSurface), вертикальная
    // стенка на той же границе (StockCutWallGeometry) и линия границы по
    // знаковому расстоянию поля (stocksurface.frag). Верх здесь гладкий нарочно:
    // граница реза считается по порогу поля, а не по ступеням узлов, и
    // ступенчатый верх спорил бы с ней на экране. Листая вид (R), ровную границу
    // сравнивают со ступенчатой у точного вида.
    readonly property bool stockCutExact: root.stockView === 3
    // Слоистая поверхность реза (PLAN-STOCK.md, 3.1; этап 1.3): виды с гладким
    // верхом - гладкий и точный контур (stockShadeView === 1) - рисуют верх
    // нетронутого материала и дно, разрезанные по ТОЧНОЙ границе реза
    // (StockCutLayerGeometry, ядро stockLayerSurface), а отвесную грань между
    // ними несёт слой стенки на той же границе. Полосы перехода между уровнями
    // нет вовсе: кромка острова отвесна, а не наклонена. Точный вид (его предмет -
    // ступени узлов) и скруглённый остаются прежним мешем поля.
    readonly property bool stockLayersDrawn: root.stockShadeView === 1
    // Полилиния контура (marching squares) у точного вида: ступени узлов там есть
    // с чем сверить, у видов с гладким верхом - нет, там граница нарисована
    // линией по знаковому расстоянию поля (stocksurface.frag).
    readonly property bool stockContourPolyline: root.stockView === 0
    readonly property var stockViewNames: ["точный", "гладкий", "скруглённый", "точный контур"]
    readonly property string stockViewName: root.stockViewNames[root.stockView]
    function cycleStockView() { root.stockView = (root.stockView + 1) % root.stockViewNames.length }
    readonly property bool stockFieldDrawn: !root.stockPlainBox && !root.qualitySafe
        && path !== null && path.hasStock && path.stockHasField

    // Сдвиг глубины остатка к глазу: и верх поля (stocksurface.vert), и стенки с
    // дном (stockshell.vert) уводятся к камере на эту долю NDC
    // (clip.z += offset * clip.w). Знак минус - снятый металл выигрывает
    // совпавший пиксель у кожи детали: у готовой программы снятая поверхность и
    // есть кожа, а кожа - эталон, по которому читают съём.
    // Одна ручка на верх и стенки, потому что у заготовки габарит равен габариту
    // детали: совпадают не только дно кармана с кожей, но и наружные стенки
    // слитка с наружными гранями детали. Сдвиг был только у верха, и стенки
    // решал шум растеризатора - наружная стенка уходила в кожу детали целиком, а
    // полоса вдоль её верха шла полосами.
    // Величина 3e-5 NDC. Замер на кадре 1024x600 (Test_model, сплошная кожа,
    // линии УП сняты, камера yaw 35 pitch -32), область слитка 66126 px,
    // пиксели спора (кожа видна там, где её нет без кожи): 1e-6 - 803, 1e-5 -
    // 365, 3e-5 - 7, 1e-4 - 7. То есть спор кончается между 1e-5 и 3e-5: это и
    // есть его размах. Размах не в шуме float32, а в геометрии: стенка реза в
    // поле - наклон в полклетки (0.5 мм при узле 0.5 мм), а стенка детали
    // вертикальна, и они пересекаются. 3e-5 на дистанции облёта 341 мм - около
    // 1.0 мм (341^2 / 3.41 * 3e-5), то есть 1 % габарита слитка 100x160x30 мм.
    // Больше не берут, потому что сдвиг здесь постоянен в NDC, а значит в
    // миллиметрах растёт с дистанцией: 3e-5 - 1.0 мм на 341 и 2.7 мм на 900, а
    // 1e-4 - 3.4 мм на 341 без выигрыша в споре (те же 7 px). Сдвиг экранную
    // позицию не меняет (только clip z), поэтому видно его лишь там, где он
    // решает спор, - обводка детали под сдвинутый остаток не уходит (её ленты
    // сдвинуты сильнее, edgePolyOffset 6e-5).
    readonly property real stockDepthOffset: -3e-5
    // Сдвиг глубины точного контура реза: тот же знак (к глазу), вдвое больше -
    // контур лежит в плоскости нетронутого верха, а равный сдвиг оставлял бы
    // совпавший пиксель на спор растеризатору. Двойка - не замер, а заведомый запас
    // на шум: сдвиг экранную позицию не меняет (только clip z), а контур - линия
    // поверх поля, и его задача - читаться. Нужен только полилинии точного вида
    // (StockCutContourGeometry): граница вида «точный контур» идёт во фрагменте
    // поверхности, а там сдвиг не нужен - она лежит на самой поверхности.
    readonly property real stockContourOffset: -6e-5
    // Полутолщина границы реза вида «точный контур», пиксели (stocksurface.frag).
    // 1.1 - линия в две с лишним полосы: перепад глубины на стенке фрезы умещается
    // в пиксель, поэтому центр линии редко попадает между пикселями, и при 0.7
    // ближайший пиксель получал полтона (замер 0.58.1+alt.3: L=70 при металле 123, то
    // есть 0.5 линии). Толщина в пикселях, а не в мм: граница - мера, и при зуме
    // она не должна толстеть или пропадать.
    readonly property real stockContourHalfPx: 1.1

    // ---- Палитра остатка материала: два цвета и пресеты для подбора контраста ----
    // Поле отвечает на один вопрос - "где уже снято": нетронутый верх одним
    // цветом, снятый металл другим. Пресеты подобраны под соседей по экрану -
    // кожу детали #B9BEC4 (светлеет светом до #E5E6E5), зелёный быстрый ход
    // #1D9247, синий рабочий #0362BB, чёрные рёбра, серо-синюю сетку пола
    // #C9CED4 и белый фон #FCFDFF, - и друг под друга: у "светлое-тёмное"
    // ступени разведены по светлоте (верх почти белый, снятый металл почти
    // чёрный), у "жёлтый-фиолетовый" и "бирюза-киноварь" - по дополнительному
    // тону (верх и снятый металл на противоположных сторонах круга).
    // Клавиша C листает пресеты вживую; CNC_STOCK_RAW и CNC_STOCK_CUT ставят
    // свои цвета до старта (cpp/viewer/main.cpp): цвета тогда стоят как заданы,
    // а пресет остаётся тем, что был.
    property int stockPreset: 0
    readonly property var stockPresets: [
        { name: "охра-медь",          raw: "#C3A45E", cut: "#C9571A" },
        { name: "светлое-тёмное",     raw: "#F7ECC0", cut: "#3A332A" },
        { name: "жёлтый-фиолетовый",  raw: "#F5D31F", cut: "#6B34C8" },
        { name: "бирюза-киноварь",    raw: "#35C4C0", cut: "#D6391C" }
    ]
    property color stockRawColor: stockPresets[stockPreset].raw
    property color stockCutColor: stockPresets[stockPreset].cut
    // Цвет точного контура реза (вид «точный контур», клавиша R): линия меры, а
    // не материал, поэтому он один на все пресеты и не входит в палитру - обязан
    // читаться и на нетронутом верху, и на снятом металле. Тёмный тон выбран как
    // у рёбер B-Rep: контур читается нарисованной границей, а не ещё одним тоном
    // металла. Ставится до старта переменной CNC_STOCK_CONTOUR (cpp/viewer/main.cpp).
    property color stockContourColor: "#101418"

    // ---- Лампа слитка: своя светлота вместо света сцены ----
    // Свет сцены настроен под кожу детали (грани почти вертикальные), и на
    // слитке дно кармана выходило темнее стенки (замер в stocksurface.vert:
    // #B34B10 против #C75414, 0.88) - карман читался конусом. Поэтому светлота
    // слитка (верх поля и стенки - stocksurface.vert и stockshell.vert) берётся
    // от нормали: ступень тона плюс одно направление. Лампа - в машинных осях
    // (X, Y, Z): сверху и со стороны камеры (машинные +X, -Y), поэтому видимые
    // стенки светлее невидимых; тон 0.35 - ступень невидимой стенки, верх
    // получает 0.95. Замер по кадру 1024x600 (Test_model, линии УП и кожа
    // сняты, пресет по умолчанию): нетронутый верх #BEA05C, дно кармана
    // #C45516, ближняя стенка кармана #7D330A, стенки слитка #907944 и #967E47
    // - дно светлее ближней стенки в 1.57 раза. Плоский верх и плоское дно
    // кармана получают ровно одну светлоту: их различает цвет палитры.
    property vector3d stockLamp: Qt.vector3d(0.9, -0.55, 2.4)
    property real stockTone: 0.35

    // Пресет задаёт оба цвета сразу.
    function applyStockPreset(index) {
        var count = root.stockPresets.length
        root.stockPreset = ((index % count) + count) % count
        root.stockRawColor = root.stockPresets[root.stockPreset].raw
        root.stockCutColor = root.stockPresets[root.stockPreset].cut
    }
    function cycleStockPreset() { root.applyStockPreset(root.stockPreset + 1) }

    // ---- Деталь: сплошная -> прозрачная -> скрыта (клавиша M) ----
    // Вид детали: 0 - сплошная кожа, 1 - прозрачная кожа, 2 - деталь скрыта
    // целиком (и кожа, и рёбра, и касательные). Один переключатель на три
    // положения: клавиша M листает по кругу, CNC_PART_MODE ставит вид до старта
    // (cpp/viewer/main.cpp). Это ЕДИНСТВЕННЫЙ переключатель вида детали: прежний
    // рентген (клавиша X, свойство xray) и справочная кожа симуляции
    // (partSkinGhost) сведены сюда - по ним нельзя было понять, какой вид сейчас
    // и чем он отличается от соседнего, а прозрачная кожа делает то же, что делал
    // рентген.
    // По умолчанию сплошная - и это не вкус: деталь - эталон, по которому читают
    // снятый металл, а прозрачная кожа смешивает его цвет со своим и глубину реза
    // по картинке уже не измерить. Вид детали не зависит ни от заготовки, ни от
    // поля съёма: правило «кожа уходит в призрак, пока на экране поле» отменено,
    // потому что оно делало деталь полупрозрачной без спроса, а снятый металл и без
    // него читается - поверхность съёма идёт к глазу на 3e-5 NDC
    // (stocksurface.vert, свойство stockDepthOffset): совпавший пиксель
    // выигрывает снятый металл, а не кожа. Замер при прежней величине сдвига
    // 1e-6 (кадр 1024x600, Test_model, стеклянная заготовка, линии УП сняты,
    // камера yaw 35 pitch -32 dist 341): медного съёма 11967 пикселей со
    // сплошной кожей против 12323 без кожи вовсе, нетронутого верха 9924 против
    // 10678 - призрак для читаемости съёма не нужен. Знак сдвига при сплошной
    // коже решает всё: с обратным знаком кожа выигрывает совпавший пиксель, верх
    // уходит в неё, и медного съёма остаётся 4578 пикселей - поэтому сдвиг
    // остаётся минусом (числа и их разбор - у stockDepthOffset).
    // Прежний запрет рентгена на уровнях low/safe снят вместе с ним: прозрачную
    // деталь ставит оператор, и молчащее нажатие читалось бы как поломка. Цена
    // названа: полупрозрачная кожа - Blend-overdraw, на слабом GPU кадр дороже
    // сплошного, поэтому это вид «посмотреть внутрь», а не рабочий.
    property int partMode: 0
    readonly property var partModeNames: ["сплошная", "прозрачная", "скрыта"]
    readonly property string partModeName: root.partModeNames[root.partMode]
    function cyclePartMode() { root.partMode = (root.partMode + 1) % root.partModeNames.length }
    // Прежний тумблер видимости детали: производное от вида, а не отдельное
    // свойство - иначе два источника одного состояния могли бы разойтись.
    readonly property bool showPartVisible: root.partMode < 2
    // Кожа детали отдельно от рёбер: клавиша K (CNC_NO_PART_SKIN) снимает тело
    // детали, оставляя справочный каркас - на снятом металле видно только его.
    property bool showPartSkin: true
    // Какую из двух моделей кожи рисует узел детали. Правило одно - вид детали
    // (partMode) и клавиша K: заготовка, поле съёма и уровень качества на него не
    // влияют. Свойства вынесены наружу, потому что по ним вид и проверяют
    // (keyprobe): сцена и проверка смотрят на одно, а не на два похожих условия.
    readonly property bool partSkinSolid: root.showPartSkin && root.partMode === 0
    readonly property bool partSkinClear: root.showPartSkin && root.partMode === 1
    // Плотность кожи: сплошная непрозрачна (1.0), прозрачная - 0.35. Замер по
    // кадру 1024x600 (Test_model, медный съём без кожи #CA5614): при 0.22 он
    // читается как #C37E53 - серый налёт съедал цвет, при 0.35 как #C57241;
    // прежняя плотность призрака 0.15 осталась бы незамеченной нажатием, и вид
    // выглядел бы молчащим.
    readonly property real partSkinOpacity: root.partMode === 1 ? 0.35 : 1.0
    property bool showAllCode: true          // true = whole code at once, false = limit draw distance
    property bool showCollisions: true    // красные диски столкновений;
                                          // off в render_probe - не
                                          // заслоняют контур
    // Линии УП отдельно от материала: клавиша G (CNC_NO_TOOLPATH) и кнопка
    // «Линии УП» - чистый вид слитка, без траектории поверх него. Линии идут
    // «всегда вперёд» (depthBias -50000), поэтому на сплошной заготовке они
    // видны и сквозь неё; снятие линий и есть ответ на это.
    property bool showToolpath: true

    function togglePlay() {
        if (root.moveLength > 0) {
            root.playing = !root.playing
            if (root.playing && root.pathDist >= root.moveLength)
                root.pathDist = 0             // replay from the start
        }
    }
    function resetPath() { root.playing = false; root.pathDist = 0 }

    // Силуэт детали и тела вращения (инструмент, оправка) пересчитывается по
    // готовности детали и раз в отрисованный кадр при движении камеры (события
    // копятся); скрытую деталь отсекает тест глубины GPU, а не этот слой.
    // low/safe пересчитывают силуэт каждый silhouetteStep-й кадр (quality).
    property bool tangentsPending: false
    property int silhouetteFrame: 0
    function refreshTangents() {
        if (!path)
            return
        // Острие инструмента в кадре сцены: от него отсчитывается глаз силуэта
        // тела вращения, поэтому оно идёт вместе с глазом камеры.
        path.setToolTip(toolNode.position)
        path.refreshTangents(camera.position)
    }
    FrameAnimation {
        running: root.tangentsPending
        onTriggered: {
            if (!root.tangentsPending)
                return
            root.tangentsPending = false
            root.silhouetteFrame++
            if (root.silhouetteFrame % root.silhouetteStep !== 0)
                return
            root.refreshTangents()
        }
    }
    Connections {
        target: camera
        function onPositionChanged() { root.tangentsPending = true }
    }
    Connections {
        target: path
        enabled: path !== null
        function onMeshChanged() { root.refreshTangents() }
    }

    // ---- Playback: Space = play/pause, Esc = reset to start ----
    // ---- Hotkeys: S stock, M part view - один переключатель на три положения
    // ----          (сплошная -> прозрачная -> скрыта; он же прежний рентген),
    // ----          K part skin (тело детали без рёбер),
    // ----          G toolpath lines (линии УП), L limit length, T stages,
    // ----          PgUp/PgDn switch stage, P play, E edges, D tangents,
    // ----          F fps, Q - цикл качества (med -> low -> high -> safe),
    // ----          B - прежняя коробка заготовки вместо поверхности съёма,
    // ----          C - палитра слитка, R - вид остатка (точный/гладкий/скруглённый),
    // ----          V - заготовка полупрозрачной (CNC_STOCK_GHOST).
    // Раскладка не должна гасить горячие клавиши: на русской раскладке Qt сообщает
    // для физической X код Key_Ch, а не Key_X, и буквенные ветки ниже не работают.
    // Буквы сверяются по nativeScanCode - скан-коду физической клавиши, он от
    // раскладки не зависит. nativeVirtualKey для этого не годится: QML-тип KeyEvent
    // (QtQuick, qquickevents_p_p.h, класс QQuickKeyEvent) его не отдаёт вовсе, в
    // обработчике Keys он всегда undefined.
    // Измерено при CNC_KEY_DEBUG=1 (живой вьювер и проба build/viewer/keyprobe.exe),
    // клавиши P S M L T E D F (X вьювер больше не обрабатывает - вид детали один):
    //   русская раскладка: sc 25 31 50 38 20 18 32 33, key 1047 1067 1068 1044
    //                      1045 1059 1042 1040 (символы з ы ь д е у в а);
    //   латиница:          те же sc, key 80 83 77 76 84 69 68 70.
    // Клавиши G и K сверены той же пробой (она печатает sc/key/cp/vk каждой) и
    // берутся по физическому скан-коду, как остальные буквы: 0x22 и 0x25.
    // Второе слагаемое - Qt.Key: клавиша без скан-кода и раскладки, где буква стоит
    // на другом месте. Сочетания с Ctrl/Alt/Meta пропускаются только у букв: для них
    // это команды приложения и переключатель раскладки, а не горячая клавиша вьювера;
    // служебные клавиши (Space, Esc, стрелки, PgUp/PgDn) остаются как были - от
    // раскладки они не зависят и сверяются по Qt.Key.
    function keyIs(ev, scan, qtkey) {
        if (ev.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))
            return false
        return ev.nativeScanCode === scan || ev.key === qtkey
    }

    // Клавиша Q: цикл уровней med -> low -> high -> safe -> med. Механизм тот
    // же, что у прочих букв: физический скан-код 0x10 или Qt.Key_Q на раскладке,
    // где Q стоит в другом месте.
    function cycleQuality() {
        var order = ["med", "low", "high", "safe"]
        var i = order.indexOf(root.quality)
        root.quality = order[(i + 1) % order.length]
    }

    // Диагностика клавиатуры: печатает признаки события и состояние переключателей
    // после обработки. По умолчанию молчит (строка на каждое нажатие - шум), включает
    // точка входа переменной окружения CNC_KEY_DEBUG (cpp/viewer/main.cpp).
    property bool keyDebug: false
    function keyLogRaw(ev) {
        var cp = ev.text.length > 0 ? ev.text.codePointAt(0) : -1
        console.log("[key] key=" + ev.key + " cp=" + cp
            + " vk=" + ev.nativeVirtualKey + " sc=" + ev.nativeScanCode
            + " mods=" + ev.modifiers + " focus=" + root.activeFocus)
    }
    function keyLogState() {
        console.log("[key] state stock=" + root.showStockVisible + " ghost=" + root.stockGhost
            + " part=" + root.showPartVisible + " partMode=" + root.partModeName
            + " skin=" + root.showPartSkin + " path=" + root.showToolpath
            + " all=" + root.showAllCode + " stages=" + root.stageMode
            + " edges=" + root.showBrepEdges + " tangents=" + root.showTangentLines
            + " skinSolid=" + root.partSkinSolid + " skinClear=" + root.partSkinClear
            + " skinAlpha=" + root.partSkinOpacity + " fps=" + root.showFps
            + " quality=" + root.quality
            + " box=" + root.stockPlainBox + " field=" + root.stockFieldDrawn
            + " view=" + root.stockViewName + " subdiv=" + root.stockSubdiv
            + " palette=" + root.stockPresets[root.stockPreset].name
            + " raw=" + root.stockRawColor + " cut=" + root.stockCutColor
            + " playing=" + root.playing + " dist=" + Math.round(root.pathDist))
    }

    Keys.onPressed: (event) => {
        if (root.keyDebug)
            root.keyLogRaw(event)
        if (event.key === Qt.Key_Space || root.keyIs(event, 0x19, Qt.Key_P)) {
            root.togglePlay()
            event.accepted = true
        } else if (event.key === Qt.Key_Escape) {
            root.resetPath()
            event.accepted = true
        } else if (event.key === Qt.Key_Up) {
            root.speed = Math.min(50.0, root.speed * 1.5)
            event.accepted = true
        } else if (event.key === Qt.Key_Down) {
            root.speed = Math.max(0.05, root.speed / 1.5)
            event.accepted = true
        } else if (root.keyIs(event, 0x1F, Qt.Key_S)) {
            root.showStockVisible = !root.showStockVisible
            event.accepted = true
        } else if (root.keyIs(event, 0x32, Qt.Key_M)) {
            // Вид детали по кругу: сплошная -> прозрачная -> скрыта. Тот же
            // переключатель, что CNC_PART_MODE во входе (viewer/main.cpp), и
            // единственный: прозрачная кожа и есть прежний рентген (клавиши X
            // у вьювера больше нет).
            root.cyclePartMode()
            event.accepted = true
        } else if (root.keyIs(event, 0x2F, Qt.Key_V)) {
            // Заготовка полупрозрачной: тот же переключатель, что CNC_STOCK_GHOST
            // во входе (viewer/main.cpp).
            root.stockGhost = !root.stockGhost
            event.accepted = true
        } else if (root.keyIs(event, 0x25, Qt.Key_K)) {
            // Кожа детали отдельно от рёбер: снятое тело оставляет на снятом
            // металле один справочный каркас. Тот же переключатель, что
            // CNC_NO_PART_SKIN во входе (viewer/main.cpp).
            root.showPartSkin = !root.showPartSkin
            event.accepted = true
        } else if (root.keyIs(event, 0x22, Qt.Key_G)) {
            // Линии УП поверх слитка: снять их - и остаётся только снятый металл.
            // Тот же переключатель, что CNC_NO_TOOLPATH во входе (viewer/main.cpp).
            root.showToolpath = !root.showToolpath
            event.accepted = true
        } else if (root.keyIs(event, 0x26, Qt.Key_L)) {
            root.showAllCode = !root.showAllCode
            event.accepted = true
        } else if (root.keyIs(event, 0x14, Qt.Key_T)) {
            root.toggleStageMode()
            event.accepted = true
        } else if (event.key === Qt.Key_PageUp) {
            root.prevStage()
            event.accepted = true
        } else if (event.key === Qt.Key_PageDown) {
            root.nextStage()
            event.accepted = true
        } else if (root.keyIs(event, 0x12, Qt.Key_E)) {
            root.showBrepEdges = !root.showBrepEdges
            event.accepted = true
        } else if (root.keyIs(event, 0x20, Qt.Key_D)) {
            root.showTangentLines = !root.showTangentLines
            event.accepted = true
        } else if (root.keyIs(event, 0x10, Qt.Key_Q)) {
            root.cycleQuality()
            event.accepted = true
        } else if (root.keyIs(event, 0x21, Qt.Key_F)) {
            root.showFps = !root.showFps
            if (!root.showFps) root.fpsSmoothed = 0
            event.accepted = true
        } else if (root.keyIs(event, 0x30, Qt.Key_B)) {
            // Сравнение: прежняя коробка вместо поверхности съёма. Тот же
            // переключатель, что CNC_STOCK_BOX во входе (viewer/main.cpp).
            root.stockPlainBox = !root.stockPlainBox
            event.accepted = true
        } else if (root.keyIs(event, 0x2E, Qt.Key_C)) {
            // Палитра остатка материала: пресеты один за другим (подбор контраста
            // на живом кадре). Цвета до старта ставят CNC_STOCK_RAW/CNC_STOCK_CUT
            // (cpp/viewer/main.cpp).
            root.cycleStockPreset()
            event.accepted = true
        } else if (root.keyIs(event, 0x13, Qt.Key_R)) {
            // Вид остатка заготовки: точный -> гладкий -> скруглённый (цена
            // каждого - в комментарии свойства). Тот же переключатель, что
            // CNC_STOCK_VIEW и CNC_STOCK_ROUND во входе (cpp/viewer/main.cpp).
            root.cycleStockView()
            event.accepted = true
        }
        if (root.keyDebug)
            root.keyLogState()
    }

    // Счётчик кадров: правый верхний угол. Строки подсказок во всю ширину окна
    // больше нет (подсказка - колонка в левом углу), поэтому счётчик вернулся на
    // верхний отступ, а z держит его поверх сцены.
    Text {
        visible: root.showFps
        z: 1
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.top: parent.top
        anchors.topMargin: 10
        text: Math.round(root.fpsSmoothed) + " FPS"
        color: "#535C66"
        font.pixelSize: 12
        font.bold: true
    }

    // ---- Подсказка горячих клавиш: компактная колонка в левом верхнем углу ----
    // Прежде это была одна строка во всю ширину окна: она длиннее 1024 px, лезла
    // через весь верх сцены и мешала смотреть слиток. Колонка занимает ~210x150 px
    // в углу, где сцена пуста, и не задевает ни заготовку, ни счётчик кадров.
    // Клавиши названы латиницей, как на клавиатуре: обработчик Keys сверяет буквы
    // по физическому скан-коду, поэтому они работают и на русской раскладке
    // (keyprobe гоняет обе). Состояния (деталь, палитра, скорость, процент и
    // переключатели) стоят там же, в тех же словах, что у кнопок панели внизу.
    Rectangle {
        id: hintPanel
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 8
        z: 1
        width: hintCol.implicitWidth + 16
        height: hintCol.implicitHeight + 12
        radius: 6
        // Фон полупрозрачный, а текст - нет: opacity на прямоугольнике погасил бы
        // и глифы, и на белом фоне подсказка стала бы бледной.
        color: Qt.rgba(0.96, 0.97, 0.98, 0.90)
        border.color: "#D7DBE0"
        border.width: 1

        Column {
            id: hintCol
            x: 8
            y: 6
            spacing: 2
            // Строки - по группам: сверху ход и состояние, ниже клавиши по одной.
            // Глиф паузы (\u23F8) шрифт не рисует - в кадре на его месте пустой
            // квадрат, поэтому ход назван словом, как у кнопки панели внизу.
            Text {
                text: "v" + root.viewerVersion + "   "
                      + (root.playing ? "идёт" : "пауза") + " "
                      + Math.round(root.moveLength > 0 ? 100 * root.pathDist / root.moveLength : 0) + "%"
                      + "   \u00D7" + root.speed.toFixed(1)
                color: "#3D454E"
                font.pixelSize: 10
                font.bold: true
            }
            Text {
                text: "Пробел пуск/пауза · Esc в начало"
                color: "#535C66"
                font.pixelSize: 10
            }
            Text {
                text: "\u2191/\u2193 скорость · Q качество: " + root.quality
                color: "#535C66"
                font.pixelSize: 10
            }
            Text {
                text: "S заготовка · M деталь: " + root.partModeName
                color: "#535C66"
                font.pixelSize: 10
            }
            Text {
                text: "K кожа: " + (root.showPartSkin ? "вкл" : "выкл")
                      + " · G линии УП: " + (root.showToolpath ? "вкл" : "выкл")
                color: "#535C66"
                font.pixelSize: 10
            }
            Text {
                text: "L длина · T этапы · PgUp/PgDn кадры"
                color: "#535C66"
                font.pixelSize: 10
            }
            Text {
                text: "E рёбра: " + (root.showBrepEdges ? "вкл" : "выкл")
                      + " · D касательные: " + (root.showTangentLines ? "вкл" : "выкл")
                color: "#535C66"
                font.pixelSize: 10
            }
            Text {
                text: "F fps: " + (root.showFps ? "вкл" : "выкл")
                      + " · B коробка: " + (root.stockPlainBox ? "вкл" : "выкл")
                color: "#535C66"
                font.pixelSize: 10
            }
            Text {
                text: "V стекло: " + (root.stockGhost ? "вкл" : "выкл")
                      + " · R вид: " + root.stockViewName
                color: "#535C66"
                font.pixelSize: 10
            }
            Text {
                text: "C палитра: " + root.stockPresets[root.stockPreset].name
                color: "#535C66"
                font.pixelSize: 10
            }
        }
    }

    // ---- Mode / playback toolbar (bottom center, two rows) ----
    Column {
        id: toolbar
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 8
        spacing: 5
        z: 10

        // Row 1: playback + what is displayed
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 6

            // "Играть / Пауза" — playback is always manual now (no auto-play)
            ViewButton {
                checked: root.playing
                text: root.playing ? "\u23F8 Пауза" : "\u25B6 Играть"
                onClicked: root.togglePlay()
            }

            // "Габарит заготовки" toggle
            ViewButton {
                checked: root.showStockVisible
                text: "Габарит заготовки"
                onClicked: root.showStockVisible = !root.showStockVisible
            }

            // Вид детали (клавиша M): по кругу - сплошная, прозрачная, скрыта.
            // Кнопка называет ТЕКУЩИЙ вид словами, а не действие: состояний три,
            // и по надписи «Модель детали» не понять, какое следующее, а слово
            // «прозрачная» рядом с прежним рентгеном не говорило, чем эти два
            // вида отличаются. Имя положения - общее с подсказкой на экране
            // (partModeNames), поэтому надписи не расходятся.
            ViewButton {
                checked: root.partMode !== 2
                text: "Деталь: " + root.partModeName
                onClicked: root.cyclePartMode()
            }

            // "Кожа детали" toggle (клавиша K): тело детали без рёбер - чистый
            // вид снятого металла, поверх него только справочный каркас.
            ViewButton {
                checked: root.showPartSkin
                text: "Кожа детали"
                onClicked: root.showPartSkin = !root.showPartSkin
            }

            // "Линии УП" toggle (клавиша G): линии идут всегда вперёд и видны
            // поверх сплошной заготовки; снятие линий оставляет голый слиток.
            ViewButton {
                checked: root.showToolpath
                text: "Линии УП"
                onClicked: root.showToolpath = !root.showToolpath
            }

            // "Весь код" vs "Ограничить длину": limit mode draws only the part
            // near the tool and fades the far part smoothly.
            ViewButton {
                checked: !root.showAllCode
                text: root.showAllCode ? "Ограничить длину" : "Весь код"
                onClicked: root.showAllCode = !root.showAllCode
            }

            // Collision indicator (red) — click to jump to the first collision
            Rectangle {
                visible: root.collisionCount > 0
                height: 30
                radius: 6
                width: collLbl.implicitWidth + 22
                color: "#D32F2F"
                border.color: "#B71C1C"
                border.width: 1
                Text {
                    id: collLbl
                    anchors.centerIn: parent
                    text: "\u26A0 " + root.collisionCount + " столкн."
                    color: "#FFFFFF"
                    font.pixelSize: 12
                    font.bold: true
                }
                MouseArea { anchors.fill: parent; onClicked: root.goToCollision(0) }
            }

            // Tool-check indicator (orange) — NC tool vs machine tool table
            Rectangle {
                visible: root.toolIssueCount > 0
                height: 30
                radius: 6
                width: toolChkLbl.implicitWidth + 22
                color: "#E08A00"
                border.color: "#B36B00"
                border.width: 1
                Text {
                    id: toolChkLbl
                    anchors.centerIn: parent
                    text: "\u26A0 Инструмент: " + root.toolIssueCount
                    color: "#FFFFFF"
                    font.pixelSize: 12
                    font.bold: true
                }
            }
        }

        // Row 2: stages ("по этапам") + frame (stage) switching
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 6
            visible: root.stageCount > 1

            ViewButton {
                checked: root.stageMode
                text: "По этапам"
                onClicked: root.toggleStageMode()
            }
            ViewButton { text: "\u25C0"; onClicked: root.prevStage() }

            // Current stage label (e.g. "T1 фреза D4   (2/4)")
            Rectangle {
                height: 30
                radius: 6
                width: stageLbl.implicitWidth + 22
                color: "#FCFDFF"
                border.color: "#C9CED4"
                border.width: 1
                Text {
                    id: stageLbl
                    anchors.centerIn: parent
                    text: root.stageLabel
                    color: "#141B24"
                    font.pixelSize: 12
                    font.bold: true
                }
            }

            ViewButton { text: "\u25B6"; onClicked: root.nextStage() }
        }
    }

    // Reusable toolbar button (checked = active / lit)
    component ViewButton: Rectangle {
        id: btn
        property bool checked: false
        property string text: ""
        signal clicked()
        width: btnLabel.implicitWidth + 22
        height: 30
        radius: 6
        color: btn.checked ? "#0362BB" : "#FCFDFF"
        border.color: btn.checked ? "#0362BB" : "#C9CED4"
        border.width: 1

        Text {
            id: btnLabel
            anchors.centerIn: parent
            text: btn.text
            color: btn.checked ? "#FFFFFF" : "#141B24"
            font.pixelSize: 12
            font.bold: true
        }
        MouseArea {
            anchors.fill: parent
            onClicked: btn.clicked()
        }
    }

    // ---- Loading overlay ----
    Rectangle {
        anchors.fill: parent
        visible: root.loading
        color: Qt.rgba(0.99, 0.99, 1.0, 0.88)
        z: 1000

        Column {
            anchors.centerIn: parent
            spacing: 16

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Загрузка УП, ожидайте\u2026"
                color: "#141B24"
                font.pixelSize: 16
                font.bold: true
            }

            Rectangle {
                id: loadTrack
                width: 280
                height: 8
                radius: 4
                color: "#E3E7EC"

                Rectangle {
                    id: loadBar
                    width: 96
                    height: parent.height
                    radius: 4
                    color: "#0362BB"
                    NumberAnimation on x {
                        running: root.loading
                        from: 0
                        to: loadTrack.width - loadBar.width
                        duration: 900
                        loops: Animation.Infinite
                    }
                }
            }
        }
    }
}
