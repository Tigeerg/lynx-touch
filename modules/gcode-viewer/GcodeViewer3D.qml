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
 */
Item {
    id: root
    focus: true

    property color backgroundColor: "#FCFDFF"
    property string gcode: ""
    property bool loading: false     // show the "loading" overlay while a program loads
    readonly property string viewerVersion: "0.16.0"   // keep in sync with modules/gcode-viewer/VERSION

    // ---- orbit / pan camera state ----
    property real yaw: 35          // degrees (eulerRotation.y)
    property real pitch: -32       // degrees (eulerRotation.x)
    property real dist: 240        // distance from the pivot
    property real panX: 0          // pan offset along camera-right
    property real panY: 0          // pan offset along camera-up
    property bool invertX: false   // invert horizontal orbit (false = natural)

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
    property real outlinePx: 1.0
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
    readonly property real fadeStartT: Math.max(root.pathEndT - root.fadeBandT, 0.0)

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
        // area at the edges).
        step: 20
        // Dipped ~1 px below the true floor: the part bottom and the billet
        // bottom lie exactly in this plane; coplanar surfaces z-fight.
        level: root.floorLevel - 1.0 * root.outlineMmPerPx
        centerX: root.floorX
        centerZ: root.floorZ
        feather: 24
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

        environment: SceneEnvironment {
            clearColor: root.backgroundColor
            backgroundMode: SceneEnvironment.Color
            // MSAA держит сплошными тонкие рёбра и линии УП (иначе пунктир).
            // High (8x) стоит почти вдвое дороже кадра; на слабом GPU это
            // главный тормоз (см. DESIGN.md, разд. 12). Линиям лент он не
            // нужен - у них свой distance-field AA в stepledges.frag, Medium
            // (4x) их держит так же (замер render_probe: тёмная обводка
            // 10062 px при Medium против 11324 px при High - один порядок,
            // разница в пределах семантики режима кожи, не разрывов нет).
            // На быстрой карте кадровая цена не видна: QSG_RENDER_TIMING
            // даёт render=0 мс при swap=16 мс - упор в vsync, не в растер.
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.Medium
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

        // ---- Stock (billet) box from UI_STOCK: semi-transparent, toolpath inside ----
        // See the anti-coplanar clearance comment on the properties below -
        // display-only micro-offsets, physical stockSize untouched.
        Model {
            visible: root.showStockVisible && (path !== null) && path.hasStock
            source: "#Cube"
        // Minimal anti-coplanar clearance (depth-ordering only, ~1.5 px): the
        // billet faces are flush with the part's un-machined faces and its top
        // and bottom lie in the part top/bottom planes; coplanar surfaces
        // z-fight while orbiting. stockSize (collisions, clearances) is exact.
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
                // Reference envelope, not geometry: the stock box spans the
                // part, so with the now-opaque skin its far edges lost the
                // depth test and the outlines dimmed. Like the toolpath
                // lines, it composites always-forward.
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
            // Сплошная кожа (X выключен). Материал строго Opaque и пишет
            // глубину: только тогда растеризатор закрывает СКРЫТЫЕ рёбра и
            // задние грани. Прежний Blend с opacity 1.0 шёл в прозрачную
            // очередь, глубину не писал - задние грани и скрытые кромки
            // просвечивали серым. alphaMode в рантайме не перекомпилируется,
            // поэтому режимы разведены двумя моделями (visible), а не сменой
            // свойства на одном материале.
            Model {
                visible: !root.xray
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
            // Рентген (X включён): кожа полупрозрачная и глубину не пишет -
            // скрытые кромки и артефакты отрисовки видны для поиска дефектов.
            Model {
                visible: root.xray
                geometry: path ? path.partGeometry : null
                materials: [
                    PrincipledMaterial {
                        baseColor: "#B9BEC4"
                        roughness: 0.9
                        metalness: 0.0
                        specularAmount: 0.2
                        cullMode: Material.NoCulling
                        alphaMode: PrincipledMaterial.Blend
                        opacity: 0.35
                    }
                ]
            }
            Model {
                visible: root.showBrepEdges
                geometry: path ? path.hlrEdges : null
                // Малый bias + cam-float (смещение к глазу в set_segments):
                // видимые рёбра чёткие, скрытые отсекаются кожей.
                // -50000 (передний слой) давал ложный «рентген» детали.
                depthBias: -1000
                // Constant-width screen-space ribbons (2 px, distance-field AA):
                // crisp at any zoom (three.js LineMaterial style: alpha falls off
                // across fwidth() borders, not by MSAA coverage), depth-tested.
                materials: CustomMaterial {
                    shadingMode: CustomMaterial.Unshaded
                    property real lineWidthPx: 2.0
                    property real polyOffset: 0.00003
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
                // чёткими на видимых стенках.
                depthBias: -1000
                // Constant-width screen-space ribbons (2 px, distance-field AA):
                // crisp at any zoom (three.js LineMaterial style: alpha falls off
                // across fwidth() borders, not by MSAA coverage), depth-tested.
                materials: CustomMaterial {
                    shadingMode: CustomMaterial.Unshaded
                    property real lineWidthPx: 2.0
                    property real polyOffset: 0.00003
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

            // Cutter shape built from UI_TOOL (flat / ball / bull / chamfer / drill...)
            Model {
                // The solid skin writes depth; keep the cutter stack
                // (tool, holder, rings, outlines) always-forward so it
                // stays visible inside bores, as in the translucent era.
                depthBias: -50000
                geometry: path ? path.toolGeometry : null
                materials: [
                    DefaultMaterial {
                        lighting: DefaultMaterial.NoLighting
                        diffuseColor: "#C7CDD4"
                        cullMode: Material.NoCulling
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
            // Cutter horizontal edge rings (thin line outline)
            Model {
                depthBias: -50000
                geometry: path ? path.toolRings : null
                materials: [
                    DefaultMaterial { lighting: DefaultMaterial.NoLighting; diffuseColor: "#141B24" }
                ]
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
            // Holder horizontal edge rings (thin line outline)
            Model {
                depthBias: -50000
                geometry: path ? path.holderRings : null
                position: Qt.vector3d(0, toolNode.holderBase, 0)
                materials: [
                    DefaultMaterial { lighting: DefaultMaterial.NoLighting; diffuseColor: "#141B24" }
                ]
            }
            Model {
                depthBias: -50000
                geometry: path ? path.holderGeometry : null
                position: Qt.vector3d(0, toolNode.holderBase, 0)
                materials: [
                    DefaultMaterial {
                        lighting: DefaultMaterial.NoLighting
                        diffuseColor: "#9AA2AC"
                        cullMode: Material.NoCulling
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
    property bool showStockVisible: true     // show the stock (billet) box
    property bool showPartVisible: true      // show the part model (STL)
    property bool showAllCode: true          // true = whole code at once, false = limit draw distance
    property bool xray: false             // сплошная кожа по умолчанию:
                                          // рентген-режим (X) - полноэкранный
                                          // Blend-overdraw и на слабом GPU
                                          // режет FPS вдвое; включается
                                          // клавишей X для поиска артефактов
                                          // (два материала кожи разведены
                                          // видимостью - см. Node детали)
    property bool showCollisions: true    // красные диски столкновений;
                                          // off в render_probe - не
                                          // заслоняют контур
    property bool showToolpath: true         // линии УП; off в render_probe, чтобы
                                             // не заслонять обводку на скриншотах

    function togglePlay() {
        if (root.moveLength > 0) {
            root.playing = !root.playing
            if (root.playing && root.pathDist >= root.moveLength)
                root.pathDist = 0             // replay from the start
        }
    }
    function resetPath() { root.playing = false; root.pathDist = 0 }

    // Keep the mesh silhouette current: recomputed when a part finishes
    // loading and once per rendered frame during orbiting (events are
    // batched); hidden parts are cut by the GPU depth test, not here.
    property bool tangentsPending: false
    function refreshTangents() {
        if (path && root.showPartVisible)
            path.refreshTangents(camera.position)
    }
    FrameAnimation {
        running: root.tangentsPending
        onTriggered: {
            if (!root.tangentsPending)
                return
            root.tangentsPending = false
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
    // ---- Hotkeys: S stock, M part model, L limit length, T stages,
    // ----          PgUp/PgDn switch stage, P play ----
    // Раскладка не должна гасить горячие клавиши: на русской раскладке Qt
    // сообщает для физической X код Key_Ch, а не Key_X, и буквенные ветки ниже
    // не срабатывали. Буквы сверяются ещё и по nativeVirtualKey (VK-код
    // физической клавиши), как в панели (apps/qt-interface/main.qml, keyIs).
    // Служебные клавиши (Space, Esc, стрелки, PgUp/PgDn) от раскладки не
    // зависят и сверяются по Qt.Key.
    function keyIs(ev, vk, qtkey) {
        return ev.nativeVirtualKey === vk || ev.key === qtkey
    }

    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Space || root.keyIs(event, 0x50, Qt.Key_P)) {
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
        } else if (root.keyIs(event, 0x53, Qt.Key_S)) {
            root.showStockVisible = !root.showStockVisible
            event.accepted = true
        } else if (root.keyIs(event, 0x4D, Qt.Key_M)) {
            root.showPartVisible = !root.showPartVisible
            event.accepted = true
        } else if (root.keyIs(event, 0x4C, Qt.Key_L)) {
            root.showAllCode = !root.showAllCode
            event.accepted = true
        } else if (root.keyIs(event, 0x54, Qt.Key_T)) {
            root.toggleStageMode()
            event.accepted = true
        } else if (event.key === Qt.Key_PageUp) {
            root.prevStage()
            event.accepted = true
        } else if (event.key === Qt.Key_PageDown) {
            root.nextStage()
            event.accepted = true
        } else if (root.keyIs(event, 0x45, Qt.Key_E)) {
            root.showBrepEdges = !root.showBrepEdges
            event.accepted = true
        } else if (root.keyIs(event, 0x44, Qt.Key_D)) {
            root.showTangentLines = !root.showTangentLines
            event.accepted = true
        } else if (root.keyIs(event, 0x58, Qt.Key_X)) {
            root.xray = !root.xray
            event.accepted = true
        } else if (root.keyIs(event, 0x46, Qt.Key_F)) {
            root.showFps = !root.showFps
            if (!root.showFps) root.fpsSmoothed = 0
            event.accepted = true
        }
    }

    // Счётчик кадров: правый верхний угол, ниже строки подсказок. Строка
    // подсказок длиннее окна (1024 px) и доходит до этого угла: на одной с ней
    // высоте цифра сливается с её глифами, поэтому счётчик сдвинут под неё
    // (topMargin 30 > отступ 10 плюс высота строки), а z держит его поверх.
    Text {
        visible: root.showFps
        z: 1
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.top: parent.top
        anchors.topMargin: 30
        text: Math.round(root.fpsSmoothed) + " FPS"
        color: "#535C66"
        font.pixelSize: 12
        font.bold: true
    }

    Text {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 10
        text: "v" + root.viewerVersion + "   ·   "
              + (root.playing ? "\u25B6 " : "\u23F8 ")
              + Math.round(root.moveLength > 0 ? 100 * root.pathDist / root.moveLength : 0) + "%"
              + "   \u00D7" + root.speed.toFixed(1)
              + "   ·   Пробел — пуск/пауза   ·   Esc — в начало   ·   \u2191/\u2193 — скорость"
              + "   ·   S заготовка · M деталь · L длина · T этапы · PgUp/PgDn кадры"
              + "   ·   E рёбра: " + (root.showBrepEdges ? "вкл" : "выкл")
              + " · D касательные: " + (root.showTangentLines ? "вкл" : "выкл")
              + " · X рентген: " + (root.xray ? "вкл" : "выкл")
              + " · F fps: " + (root.showFps ? "вкл" : "выкл")
        color: "#535C66"
        font.pixelSize: 12
        font.bold: true
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

            // "Модель детали" toggle
            ViewButton {
                checked: root.showPartVisible
                text: "Модель детали"
                onClicked: root.showPartVisible = !root.showPartVisible
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
