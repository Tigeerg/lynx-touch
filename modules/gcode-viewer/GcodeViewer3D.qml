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
 * Key layout (handler - Keys.onPressed below, buttons - in the bottom panel):
 *   Space/P - play/pause, Esc - to start, Up/Down - speed;
 *   S - stock (bounding box), M - part view cycle (solid -> transparent ->
 *   hidden), K - part skin (body without edges), G - toolpath lines, B - the former
 *   box instead of the cut surface, V - stock translucent;
 *   C - material stock palette (presets), R - stock view cycle
 *   (exact -> smooth -> rounded);
 *   L - length, T - stages, PgUp/PgDn - stage frames;
 *   E - edges, D - tangents, Q - quality, F - fps.
 * Part view - one switch with three positions, and the transparent skin on it is
 * the former x-ray view: the viewer no longer has a separate X key
 * (analysis - at the partMode property).
 * The same clean-view toggles are available before start via environment variables
 * (cpp/viewer/main.cpp): CNC_STOCK_BOX, CNC_NO_TOOLPATH, CNC_NO_PART_SKIN,
 * CNC_STOCK_GHOST (stock translucent, default; =0 or false - solid),
 * CNC_PART_MODE (solid/transparent/hidden - the same single part-view switch),
 * stock view - CNC_STOCK_VIEW (exact/smooth/rounded, default smooth) and
 * the former shorthand CNC_STOCK_ROUND=1 for rounded, and the palette colors -
 * CNC_STOCK_RAW and CNC_STOCK_CUT (as "#RRGGBB").
 */
Item {
    id: root
    focus: true

    property color backgroundColor: "#FCFDFF"
    property string gcode: ""
    property bool loading: false     // show the "loading" overlay while a program loads
    readonly property string viewerVersion: "0.58.3"   // keep in sync with modules/gcode-viewer/VERSION

    // ---- orbit / pan camera state ----
    property real yaw: 35          // degrees (eulerRotation.y)
    property real pitch: -32       // degrees (eulerRotation.x)
    property real dist: 240        // distance from the pivot
    property real panX: 0          // pan offset along camera-right
    property real panY: 0          // pan offset along camera-up
    property bool invertX: false   // invert horizontal orbit (false = natural)

    // ---- render quality: level low/med/high/safe (DESIGN.md, sec. 13) ----
    // The level is set by the entry point (CNC_QUALITY) or the Q key. The med
    // level numbers repeat the values in effect before the presets, so without
    // CNC_QUALITY the picture is the same; the level affects the render, the
    // geometry is parsed by the core.
    property string quality: "med"
    readonly property bool qualityReduced: root.quality === "low" || root.quality === "safe"
    readonly property bool qualitySafe: root.quality === "safe"
    // MSAA View3D: med - 4x (today), high - 8x, low/safe - off (the main
    // frame-rate bottleneck, see the comment at environment).
    readonly property int msaaMode: root.qualityReduced ? SceneEnvironment.NoAA
                                                        : SceneEnvironment.MSAA
    readonly property int msaaQuality: root.quality === "high" ? SceneEnvironment.High
                                        : SceneEnvironment.Medium
    // Outline: safe is thinner (0.8 px vs 1.5) and the offset is deeper - cheaper
    // on the software rasterizer; med/high/low keep today's numbers.
    // Width reduced from 2.0/1.0 (measurement: part edges, tangents and
    // tool-with-holder edges read as 1.5 px, and on safe 0.8 px - the ribbon with
    // distance-field AA stays crisp). On safe the picture moreover reads thicker:
    // there the scene is drawn in logical pixels (DPR 1, renderTextureWidth below)
    // and stretched to the screen, so the same width lands on more physical pixels
    // and looks coarser.
    readonly property real edgeLineWidthPx: root.qualitySafe ? 0.8 : 1.5
    // Outline offset toward the eye in clip z (stepledges.vert, polyOffset): it
    // holds the ribbon in front of the skin, but now under the outline there is
    // also the material stock shifted toward the eye (stockDepthOffset, 3e-5 NDC).
    // While the outline offset equalled the stock offset (3e-5), the shifted
    // surface ate half the outline: measurement on a 1024x600 frame (Test_model,
    // billet area 66126 px) - ribbon black pixels 1347 vs 2866 without the stock
    // offset. 6e-5 is twice the stock offset, and therefore the same at all levels:
    // the former 6e-5 was on safe, now it is on all. The outline stays in place on
    // screen (offset in depth only) and reads as the same 2900 pixels as before the
    // stock offset.
    readonly property real edgePolyOffset: 0.00006
    readonly property real edgeDepthBias: root.qualitySafe ? -1500 : -1000
    // Silhouette recompute step: low/safe - every 4th frame, med/high - every.
    readonly property int silhouetteStep: root.qualityReduced ? 4 : 1
    // Floor grid: low/safe coarser - fewer lines and wider feathering.
    readonly property int gridStep: root.qualityReduced ? 40 : 20
    readonly property int gridFeather: root.qualityReduced ? 48 : 24
    // DPR limiting: low/safe render the scene in logical pixels (DPR 1) instead of
    // logical size * DPR; 0 - the size follows the element (today).
    readonly property int renderTextureWidth: root.qualityReduced ? Math.round(view3d.width) : 0
    readonly property int renderTextureHeight: root.qualityReduced ? Math.round(view3d.height) : 0

    // ---- edge layer toggles (diagnostics: E / D) ----
    property bool showBrepEdges: true      // static STEP B-rep outline
    property bool showTangentLines: true   // view-dependent tangent contour

    // ---- FPS counter (F) ----
    // Exponential average of the frame rate: the instantaneous 1/frameTime sits
    // still on vsync monitors and there is nothing to read, while a single dropped
    // frame must not throw the number. Factor 0.9 -> a jump is visible over
    // ~10 frames.
    property bool showFps: false
    property real fpsSmoothed: 0            // average, frames/s
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
    // Width reduced from 1.0: the part, cutter and holder outline reads as 0.8 px.
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
    // The fade band belongs to the length-limiting mode: when the whole code is
    // shown there is nothing to fade, and the path tail was drawn as translucent
    // lines. On the billet this read as light streaks over the pocket: measurement
    // on a 1024x600 frame (Test_model, solid skin, glass off, camera yaw 35
    // pitch -32 dist 341) - points 484,290 and 476,294 in the pocket gave #1576CF
    // and #3192EB (blue #0162BB diluted by the background) with a band of 0.1 and
    // pure #0162BB with a band of 0.0001, that is, the pale bands were the path
    // tail, not a fight between surfaces. They were visible at the depth of the
    // pocket, where the program tail (drilling) lands in dense frames. The condition
    // is the same as at pathEndT: the visible window is less than whole - the band
    // exists, the window is whole - it does not.
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

    // Live field publication: while the program plays, the field catches up with
    // pathDist once per 100 ms - the cadence from DESIGN.md (more often is not
    // allowed: updating the texture is a whole buffer; on the Test_model stock
    // 150x100 mm this is 242 KiB per tick at a 0.5 mm node and 1.44 MiB at 0.2 mm).
    // Outside playback there is no timer: stop and seek are caught up by a single
    // call on a pathDist/playing change, and the first pass is computed without them
    // anyway.
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
        // area at the edges). low/safe - coarser (quality).
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

    // ---- Material stock: the top field mesh and the box shell ----
    // Both are built in MACHINE millimeters, from the same numbers the controller
    // publishes about the field (stockBounds/stockCellMm/stockCountX/Y): the top
    // mesh - one vertex per field node, so there is no second sampling level, and
    // the vertex shader stocksurface.vert subtracts the vertex height from the field
    // texture.
    // The shell is the same box without the top face (the field draws the top), with
    // the former micro-offset against z-fighting: it moves only the walls, the
    // physical stock bounding box does not change.
    // The wall bounds (bounding box plus gaps) live here, not at the material: the
    // shell geometry is built from them, and from them the wall vertex shader
    // (stockshell.vert) picks which face a vertex stands on - one number, one source.
    // The side wall gap is ZERO: the wall stands on the field bounding box and is not
    // shifted outward. The former 1.5 pixels were a box offset against z-fighting
    // when the box carried the top too; with the field the top is drawn by the mesh,
    // and a wall shifted outward left a horizontal shelf 1.5 px wide along the whole
    // top edge (measurement: 0.64 mm at a distance of 220 mm at 0.42 mm/px, 0.87 mm
    // at 300 mm) - through it the background and the billet interior read at the
    // edge. The mesh top reaches exactly the bounding box (stockSurfaceMesh clamps
    // the outermost node), so with a zero gap the top and the wall meet in the same
    // edge the core comment promised (core/stock_sim, stockShellVertices).
    readonly property real shellGapSideMm: 0
    readonly property real shellGapTopMm: 1.0 * root.outlineMmPerPx
    readonly property real shellGapBottomMm: 1.0 * root.outlineMmPerPx
    // Field bounding box in machine mm [xmin, xmax, ymin, ymax, zmin, zmax] for the
    // stock materials (fieldMin/fieldTopMm/fieldCount at the walls). An empty list
    // (no field yet) gives zeros, not undefined: on undefined QML answers with a
    // warning, and the run must be free of "[QML warning]". One source for both the
    // field top and the walls.
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
        // Subdivision of the field cell in the drawn mesh (smooth view). The field, its
        // texture and the checks are computed by nodes: subdivision lives only here.
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
        // DPR limiting (quality): 0 - the texture size follows the element (logical
        // size * DPR, today's behavior), low/safe - logical pixels (DPR 1). The sizes
        // change only at these levels.
        explicitTextureWidth: root.renderTextureWidth
        explicitTextureHeight: root.renderTextureHeight

        environment: SceneEnvironment {
            clearColor: root.backgroundColor
            backgroundMode: SceneEnvironment.Color
            // MSAA keeps thin edges and toolpath lines solid (otherwise dotted).
            // High (8x) costs almost twice per frame; on a weak GPU this is the main
            // bottleneck (see DESIGN.md, sec. 12). The ribbon lines do not need it -
            // they have their own distance-field AA in stepledges.frag, Medium (4x)
            // holds them the same (render_probe measurement: dark outline 10062 px at
            // Medium vs 11324 px at High - one order of magnitude, the difference is
            // within the skin-mode semantics, and there are no gaps).
            // On a fast card the frame cost is invisible: QSG_RENDER_TIMING gives
            // render=0 ms at swap=16 ms - limited by vsync, not the rasterizer.
            antialiasingMode: root.msaaMode
            antialiasingQuality: root.msaaQuality
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

        // ---- Material stock: height field (top) plus walls and bottom ----
        // Frame. The field mesh and texture are in MACHINE millimeters, while the
        // scene is (X, Z, -Y): rotating the node by -90 about X turns machine
        // (x, y, z) into (x, z, -y) - the same substitution the backend does when
        // walking the path (core/stock_sim.cpp, simulateStock). The extrusion axis
        // is machine Z, in the frame it goes up; a field node (x, y) with depth d
        // stands at (x, zMax - d, -y), and it is this same number that the
        // stock_sim_probe case stocksurface checks.
        // The "always forward" rule (depthBias -50000) is not applied to the surface:
        // this bias belongs only to the cutter, which stands INSIDE the material;
        // the cut surface is the material itself, and with it the surface would stop
        // hiding the path under itself, and a cut wall would lie about the geometry.
        // Who wins on a coincidence: in a finished program the cut surface is the
        // part skin, and where they touch they fight over one pixel.
        // The right is given to the CUT SURFACE: during simulation the subject is the
        // cut metal, and the part skin is the reference (including solid, partMode 0),
        // and on a coinciding pixel it yields. The fight is settled by an offset of
        // DEPTH ONLY in the vertex shader (stocksurface.vert and stockshell.vert, the
        // stockDepthOffset property): vertex positions do not move, so the bounding
        // box and the cut depth do not lie, and the cut metal wins the coinciding
        // surfaces.
        // An offset backward (as it was before the change) gave the opposite: on a
        // coinciding top the skin was visible, not the surface.
        // Solid skin writes depth, so the right on a coinciding pixel is decided not
        // by it, but by the field with the sign of its offset (measurements - at
        // partMode and at stockDepthOffset: with the opposite sign the cut goes under
        // the skin).
        // Where the stock is STILL HIGHER than the part, there is no fight at all: the
        // cut metal covers the skin with itself, and this is the correct look - the
        // material above the part is not cut.
        Node {
            visible: root.showStockVisible && root.stockFieldDrawn
            eulerRotation: Qt.vector3d(-90, 0, 0)

            // The field top: opaque and writes depth - it is the material.
            // The tone is its own, from the normal (stockTone/stockLamp, analysis -
            // stocksurface.vert): the scene light gave the pocket bottom a lower tone
            // than the wall, and the cut metal read as a cone, while the walls read as
            // almost one color.
            // The color is the preset palette (key C, CNC_STOCK_RAW/CNC_STOCK_CUT):
            // the untouched top (depth 0) in one color, the cut metal in another.
            // The 0.05 mm threshold cuts off burrs at tenths of the 0.5 mm node.
            // The field texture is nearest: a grid node stands at the center of its
            // texel, linear R32F sampling on the OpenGL 3.3 branch is not guaranteed
            // and would blur the cut wall by half a cell. The smooth view interpolates
            // the field itself, with four nearest samples (stocksurface.vert): the
            // driver filter does not touch its own interpolation.
            // The translucent view (key V, CNC_STOCK_GHOST) - the same material with
            // the stockAlpha density and with blending: the cut metal reads through
            // the wall, and the wall itself does not cast a shadow on what is behind
            // it. It writes depth even when blending (AlwaysDepthDraw): without the
            // write, the far faces and the reference skin would go over the billet,
            // and instead of a translucent stock there would be a mess of hidden
            // faces - the very complaint that the reversed cull side of the solid
            // shell cured.
            // Blending is rasterizer state, not shader code, so the view is switched
            // by material properties (with the skin alphaMode this does not work: it
            // is baked into the shader, and there the modes are split into two models).
            Model {
                geometry: stockSurface
                materials: CustomMaterial {
                    shadingMode: CustomMaterial.Unshaded
                    property real cellMm: path ? path.stockCellMm : 0.5
                    // Depth offset toward the eye relative to the part skin: the
                    // magnitude, sign and measurements live at the node property
                    // (stockDepthOffset, and there too - why the offset is the same
                    // for the top and the walls). Only clip z is shifted, vertex
                    // positions do not move, so the stock bounding box and the cut
                    // depth do not lie (cf. the stocksurface case of the probe:
                    // vertex height - top minus field).
                    property real depthOffset: root.stockDepthOffset
                    property color stockRawColor: root.stockRawColor
                    property color stockCutColor: root.stockCutColor
                    property real cutMm: 0.05
                    property real stockAlpha: root.stockGhost ? root.stockGhostAlpha : 1.0
                    // Stock view: the node property (key R, CNC_STOCK_VIEW,
                    // CNC_STOCK_ROUND). The breakdown of the views and their cost -
                    // in stocksurface.vert.
                    property int stockView: root.stockView
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

            // Walls and bottom: a SOLID body - opaque and writing depth, so through them
            // neither the far wall, nor the bottom, nor the part inside is visible:
            // the stock reads as a billet, as in a machining simulator. They have no
            // top face - the field draws it.
            //
            // The cull side here is the OPPOSITE of the usual. The shell is built by
            // the core (core/stock_sim, stockShellVertices), and the winding of its
            // pentagons is such that the right normal points INTO the box; this buffer
            // is checked by the stockshell case of the stock_sim_probe probe and must
            // not be changed. With BackFaceCulling it was not the far walls that were
            // culled but the NEAR ones: the three faces looking at the camera
            // disappeared, and through them the floor, the reference part skin and the
            // toolpath lines read - the very complaint about the see-through side face
            // of the billet. Measurement on 1024x600 frames (Test_model, view with the
            // cut field, neither toolpath lines nor skin): BackFaceCulling vs
            // FrontFaceCulling - 13990 pixels on the billet wall, that is, the near
            // wall was removed entirely, and in its place the floor and the reference
            // part skin read. FrontFaceCulling removes the far faces (the depth buffer
            // hides them anyway) and keeps the near ones. If the winding in the core is
            // ever fixed, this line must go back to BackFaceCulling.
            //
            // Color and tone are the same as at the field top: the same raw metal
            // (stockRawColor) and the same billet lamp, but the face normal is computed
            // here, in the vertex shader, from the vertex position (stockshell.vert):
            // the shell buffer is positions only, it is checked by the stockshell case
            // of the probe (30 vertices of three numbers each), and must not be
            // changed. Formerly the shell went with no light at all (the material had
            // no normal) and all walls were flooded with one #B79A55 - hence the
            // complaint about the strange color of the side faces.
            // The wall top is clipped by the height field (stockshell.frag): the wall
            // stands on the bounding box and would not know that the tool removed metal
            // over the edge. The field and its bounding box arrive here with the same
            // numbers as at the top: the bounding box (fieldMin - the corner bottom,
            // fieldTopMm - the stock top), the step (fieldCellMm), the nodes
            // (fieldCount) and the texture itself (stockField).
            // The translucent view (V, CNC_STOCK_GHOST) - as at the field top (the
            // comment there): the same density and the same depth write.
            // The tool inside is visible anyway: the cutter, the holder and the
            // toolpath lines go "always forward" (depthBias -50000) and are drawn over
            // the billet.
            Model {
                geometry: stockShell
                materials: CustomMaterial {
                    shadingMode: CustomMaterial.Unshaded
                    property color stockRawColor: root.stockRawColor
                    property vector3d stockLamp: root.stockLamp
                    property real stockTone: root.stockTone
                    property real stockAlpha: root.stockGhost ? root.stockGhostAlpha : 1.0
                    // The same depth offset toward the eye as at the field top: the outer
                    // walls of the billet coincide with the outer faces of the part, and
                    // without the offset the coinciding pixel was decided by rasterizer
                    // noise - the wall went into the part skin, and along its top a band
                    // ran (analysis and measurements - at the stockDepthOffset property).
                    property real depthOffset: root.stockDepthOffset
                    property vector3d fieldMin: Qt.vector3d(root.stockFieldBounds[0],
                                                            root.stockFieldBounds[2],
                                                            root.stockFieldBounds[4])
                    property real fieldTopMm: root.stockFieldBounds[5]
                    property real fieldCellMm: path ? path.stockCellMm : 0.5
                    property vector2d fieldCount: Qt.vector2d(path ? path.stockCountX : 0,
                                                             path ? path.stockCountY : 0)
                    // Stock view: the wall-top clipping takes the depth with the same
                    // sample as the drawn top (stockshell.frag).
                    property int stockView: root.stockView
                    property TextureInput stockField: TextureInput {
                        texture: Texture {
                            textureData: path ? path.stockField : null
                            minFilter: Texture.Nearest
                            magFilter: Texture.Nearest
                            mipFilter: Texture.None
                        }
                    }
                    // Wall bounds in machine mm (bounding box plus gaps): the same
                    // numbers the shell geometry is built from stand next to it - at the
                    // material stock node.
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
        }

        // ---- The former box: comparison with the field and the view while there
        // is no field ----
        // Visible when there is no field (state not ready or safe level) or it was
        // turned on deliberately (key B, CNC_STOCK_BOX). The former look: raised
        // against the part by fractions of a pixel (otherwise coinciding faces
        // flicker while orbiting), translucent (opacity 0.2) and not writing depth,
        // so the trajectory inside is visible through it.
        Model {
            visible: root.showStockVisible && (path !== null) && path.hasStock
                     && !root.stockFieldDrawn
            source: "#Cube"
        // Micro-offset against z-fighting (depth order only, about 1.5 px): the
        // stock faces coincide with the raw faces of the part, and the top and
        // bottom lie in its planes - coinciding surfaces flicker while orbiting.
        // The stock bounding box (collisions, gaps) does not change from this, it is
        // exact.
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
                // A bounding shell, not geometry: the box encloses the part, so with
                // an opaque skin its far faces lost the depth test and the outline
                // dimmed. Like the toolpath lines, it is always composited forward
                // (does not write depth).
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
            // Solid skin - the default part view (partMode 0). The material is
            // strictly Opaque and writes depth: only then does the rasterizer hide the
            // HIDDEN edges and back faces. The former Blend with opacity 1.0 went to
            // the transparent queue and did not write depth - back faces and hidden
            // edges showed through as gray. alphaMode is not recompiled at runtime, so
            // the modes are split into two models (visible), not by changing a property
            // on one material.
            // Key K removes the skin entirely (the body), leaving the edges and
            // tangents: that way on the cut metal only the reference part wireframe
            // remains.
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
            // Transparent part (partMode 1): the same skin translucent, it does not
            // write depth - through it the cut metal, hidden edges and what is behind
            // the part are visible. Density - the partSkinOpacity property. The former
            // x-ray (key X) and the simulation's reference skin merged here: the same
            // view, one key, one density (analysis - at partMode). The cost is named
            // directly: blending happens out of depth order and costs a frame, so this
            // is a "look inside" view, not a working one.
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
                // Small bias + cam-float (offset toward the eye in set_segments):
                // visible edges are crisp, hidden ones are culled by the skin.
                // -50000 (front layer) gave a false "x-ray" of the part.
                // safe - deeper (quality): coarse precision of the software rasterizer.
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
                    property real camFloatMm: 0.0   // offset toward the eye in stepledges.vert
                    cullMode: Material.NoCulling
                    vertexShader: "stepledges.vert"
                    fragmentShader: "stepledges.frag"
                }
            }
            Model {
                visible: root.showTangentLines
                geometry: path ? path.hlrTangents : null
                // Silhouette from the mesh (an edge at the surface horizon): a depth
                // test is needed, otherwise the far parts of the horizon are drawn
                // through the body and double with the seams. A small bias + cam-float
                // keep them crisp on visible walls; safe - deeper (quality).
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
                    property real camFloatMm: 0.0   // offset toward the eye in stepledges.vert
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
        // The toolpath lines have no width: the geometry is hardware GL_LINES
        // (cpp/qt/gv_geometry, PrimitiveType::Lines), that is exactly 1 px, and a
        // lineWidthPx-like knob cannot be added to them without turning the
        // trajectory into ribbons. They read thicker on low/safe: there the scene is
        // drawn in logical pixels (renderTextureWidth) and stretched to the screen, so
        // one pixel lands on more physical ones.
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

            // The cutter is assembled from UI_TOOL (flat / ball / bull-nose / chamfer / drill...)
            Model {
                // depthBias -50000 (always forward) is kept deliberately: the cutter
                // stands INSIDE the stock and the part, and the depth test would hide it
                // behind their skin. The material level (PrincipledMaterial) is the same
                // as at the part - the cutter reads as its metal under the same light.
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
            // Cutter edges: profile rings and longitudinal generatrices - the same
            // stepledges ribbons as at the part (constant width in px, AA, depth test).
            // depthBias -50000: the cutter is inside the stock and the part, the depth
            // test would hide it (see the comment at the body).
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
            // Holder edges: profile rings and longitudinal generatrices - with
            // stepledges ribbons, as at the part and the cutter (see the comment at
            // toolProfileEdges).
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
                // depthBias -50000 (always forward): the holder stands inside the
                // stock; the depth test would hide it behind the skin. The material is as
                // at the part, the same scene light (PrincipledMaterial picks up the
                // scene DirectionalLights itself).
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
    property bool showStockVisible: true     // show the stock
    // What the stock is drawn with. The field - when the controller has published a
    // ready field (stockHasField) and the level is not safe: on safe the field is
    // drawn with the coarsest node and costs the most per frame (DESIGN.md, sec. 13).
    // The field's own flag, not the stockSimState state: while a new field is
    // computed (tick, seek), the former whole stays on screen, and the drawing must
    // not disappear for that time.
    // The box - while there is no field (first pass), after a refusal (the tool
    // cannot be expressed by the map) and via key B / CNC_STOCK_BOX=1 for comparison.
    property bool stockPlainBox: false       // box instead of the field (B, CNC_STOCK_BOX)
    // Stock translucent: key V, CNC_STOCK_GHOST before start (the same two knobs as
    // the other views). ON by default - the operator asked for it (twice): the glass
    // billet reads as material, through it the pocket, the part and the tool are
    // visible. The price: by the billet color, cut and untouched metal are
    // distinguished worse than with solid (blending with the background), so the
    // density is raised from 0.35 to 0.90 - this is the share of the material color
    // in a pixel; measurement on a 1024x600 frame (Test_model, skin and lines off):
    // copper cut #BD5215 under glass reads as #DA7A44, untouched ochre #BEA05C as
    // #D3B87C at 0.90, while at 0.62 the same points gave #FFCFAA and #F2DFB5 -
    // almost the background. Through 0.90 the interior and the tool are visible, that
    // is, the view stayed glass, not solid. The solid view - on the same key V and via
    // CNC_STOCK_GHOST=0 before start.
    // One density for the field top and the walls: the billet reads as one body.
    // Do not confuse with the box comparison (B, CNC_STOCK_BOX): there the material
    // went into the box without a field, here it is the same cut metal, but it is
    // visible through it.
    property bool stockGhost: true
    readonly property real stockGhostAlpha: 0.90
    // Stock view: 0 - exact by the field nodes, 1 - smooth (mesh subdivided, height
    // bilinear), 2 - rounded (the node depth is the average over its neighborhood).
    // One switch for all three: key R cycles, CNC_STOCK_VIEW sets the view before
    // start, and CNC_STOCK_ROUND=1 remains a shorthand for the third (the former
    // rounding knob).
    // Smooth by default: it removed both complaints about raggedness - the band along
    // the step edge and the mosaic of small facets on the wall. Costs: with the exact
    // view, a checkered band at the top near the edge and mesh facets; with the
    // rounded one, the cut edge goes deep up to a node (0.5 mm) and a small window
    // loses up to half its depth; with the smooth one, the drawn surface diverges from
    // the field by up to a quarter of the cell twist, the cut edge rounds by half a
    // cell, and the mesh is larger by subdiv^2 times. The breakdown - in
    // stocksurface.vert.
    property int stockView: 1
    // Subdivision of the field cell in the drawn mesh: only for the smooth view. 2 -
    // because the bands and the mosaic live at the cell scale, and a twice finer cell
    // is enough: a vertex also lands in the middle of the cell, where the fight over
    // the side of the step is decided (stocksurface.vert). The cost is four times more
    // vertices and triangles (measurement: 60501 field nodes and 120000 triangles vs
    // 241001 vertices and 480000 triangles; the field, its texture and the checks do
    // not change).
    readonly property int stockSubdiv: root.stockView === 1 ? 2 : 1
    readonly property var stockViewNames: ["точный", "гладкий", "скруглённый"]
    readonly property string stockViewName: root.stockViewNames[root.stockView]
    function cycleStockView() { root.stockView = (root.stockView + 1) % root.stockViewNames.length }
    readonly property bool stockFieldDrawn: !root.stockPlainBox && !root.qualitySafe
        && path !== null && path.hasStock && path.stockHasField

    // Depth offset of the stock toward the eye: both the field top
    // (stocksurface.vert) and the walls with the bottom (stockshell.vert) are moved
    // toward the camera by this fraction of NDC (clip.z += offset * clip.w). The minus
    // sign - the cut metal wins the coinciding pixel from the part skin: in a finished
    // program the cut surface is the skin, and the skin is the reference by which the
    // cut is read.
    // One knob for the top and the walls, because the stock bounding box equals the
    // part bounding box: not only the pocket bottom coincides with the skin, but also
    // the outer walls of the billet with the outer faces of the part. The offset was
    // only at the top, and the walls were decided by rasterizer noise - the outer wall
    // went entirely into the part skin, and the band along its top ran in stripes.
    // The magnitude is 3e-5 NDC. Measurement on a 1024x600 frame (Test_model, solid
    // skin, toolpath lines off, camera yaw 35 pitch -32), billet area 66126 px,
    // contested pixels (the skin is visible where it would not be without the skin):
    // 1e-6 - 803, 1e-5 - 365, 3e-5 - 7, 1e-4 - 7. That is, the fight ends between 1e-5
    // and 3e-5: that is its span. The span is not in float32 noise but in the geometry:
    // the cut wall in the field is inclined by half a cell (0.5 mm at a 0.5 mm node),
    // while the part wall is vertical, and they intersect. 3e-5 at an orbit distance of
    // 341 mm is about 1.0 mm (341^2 / 3.41 * 3e-5), that is, 1 % of the 100x160x30 mm
    // billet bounding box. No more is taken, because the offset here is constant in
    // NDC, and hence in millimeters grows with distance: 3e-5 - 1.0 mm at 341 and
    // 2.7 mm at 900, while 1e-4 - 3.4 mm at 341 with no gain in the fight (the same
    // 7 px). The offset does not change the screen position (clip z only), so it is
    // visible only where it settles the fight - the part outline does not go under the
    // shifted stock (its ribbons are offset more, edgePolyOffset 6e-5).
    readonly property real stockDepthOffset: -3e-5

    // ---- Material stock palette: two colors and presets for tuning contrast ----
    // The field answers one question - "where it is already cut": the untouched top
    // in one color, the cut metal in another. The presets are tuned to their on-screen
    // neighbors - the part skin #B9BEC4 (brightened by light to #E5E6E5), the green
    // rapid #1D9247, the blue feed #0362BB, the black edges, the gray-blue floor grid
    // #C9CED4 and the white background #FCFDFF - and to each other: in "light-dark" the
    // steps are separated by tone (the top is almost white, the cut metal almost
    // black), in "yellow-violet" and "turquoise-cinnabar" by the complementary hue
    // (the top and the cut metal on opposite sides of the wheel).
    // Key C cycles the presets live; CNC_STOCK_RAW and CNC_STOCK_CUT set their own
    // colors before start (cpp/viewer/main.cpp): the colors then stand as given, and
    // the preset stays what it was.
    property int stockPreset: 0
    readonly property var stockPresets: [
        { name: "охра-медь",          raw: "#C3A45E", cut: "#C9571A" },
        { name: "светлое-тёмное",     raw: "#F7ECC0", cut: "#3A332A" },
        { name: "жёлтый-фиолетовый",  raw: "#F5D31F", cut: "#6B34C8" },
        { name: "бирюза-киноварь",    raw: "#35C4C0", cut: "#D6391C" }
    ]
    property color stockRawColor: stockPresets[stockPreset].raw
    property color stockCutColor: stockPresets[stockPreset].cut

    // ---- Billet lamp: its own tone instead of the scene light ----
    // The scene light is tuned for the part skin (faces almost vertical), and on the
    // billet the pocket bottom came out darker than the wall (measurement in
    // stocksurface.vert: #B34B10 vs #C75414, 0.88) - the pocket read as a cone.
    // Therefore the billet tone (field top and walls - stocksurface.vert and
    // stockshell.vert) is taken from the normal: a tone step plus one direction. The
    // lamp is in machine axes (X, Y, Z): from above and from the camera side (machine
    // +X, -Y), so visible walls are lighter than invisible ones; tone 0.35 - the step
    // of the invisible wall, the top gets 0.95. Measurement on a 1024x600 frame
    // (Test_model, toolpath lines and skin off, default preset): untouched top
    // #BEA05C, pocket bottom #C45516, near pocket wall #7D330A, billet walls #907944
    // and #967E47 - the bottom is 1.57 times lighter than the near wall. A flat top
    // and a flat pocket bottom get exactly one tone: the palette color distinguishes
    // them.
    property vector3d stockLamp: Qt.vector3d(0.9, -0.55, 2.4)
    property real stockTone: 0.35

    // The preset sets both colors at once.
    function applyStockPreset(index) {
        var count = root.stockPresets.length
        root.stockPreset = ((index % count) + count) % count
        root.stockRawColor = root.stockPresets[root.stockPreset].raw
        root.stockCutColor = root.stockPresets[root.stockPreset].cut
    }
    function cycleStockPreset() { root.applyStockPreset(root.stockPreset + 1) }

    // ---- Part: solid -> transparent -> hidden (key M) ----
    // Part view: 0 - solid skin, 1 - transparent skin, 2 - the part is hidden
    // entirely (both the skin and the edges and the tangents). One switch with three
    // positions: key M cycles, CNC_PART_MODE sets the view before start
    // (cpp/viewer/main.cpp). This is the ONLY part-view switch: the former x-ray
    // (key X, the xray property) and the simulation's reference skin (partSkinGhost)
    // are merged here - from them it was impossible to tell which view is current and
    // how it differs from the neighboring one, while the transparent skin does what
    // the x-ray did.
    // Solid by default - and this is not taste: the part is the reference by which the
    // cut metal is read, and the transparent skin blends its color with its own, and
    // the cut depth can no longer be measured from the picture. The part view depends
    // neither on the stock nor on the cut field: the rule "the skin goes to ghost while
    // the field is on screen" is revoked, because it made the part translucent without
    // asking, while the cut metal reads even without it - the cut surface goes toward
    // the eye by 3e-5 NDC (stocksurface.vert, the stockDepthOffset property): the
    // coinciding pixel is won by the cut metal, not by the skin. Measurement at the
    // former offset magnitude 1e-6 (frame 1024x600, Test_model, glass stock, toolpath
    // lines off, camera yaw 35 pitch -32 dist 341): copper cut 11967 pixels with the
    // solid skin vs 12323 with no skin at all, untouched top 9924 vs 10678 - the ghost
    // is not needed for the cut to be readable. The sign of the offset with the solid
    // skin decides everything: with the opposite sign the skin wins the coinciding
    // pixel, the top goes into it, and the copper cut is left with 4578 pixels -
    // therefore the offset stays negative (the numbers and their breakdown - at
    // stockDepthOffset).
    // The former x-ray ban at the low/safe levels is lifted together with it: the
    // transparent part is set by the operator, and a silent keypress would read as
    // breakage. The cost is named: the translucent skin is Blend-overdraw, on a weak
    // GPU the frame is more expensive than solid, so this is a "look inside" view, not
    // a working one.
    property int partMode: 0
    readonly property var partModeNames: ["сплошная", "прозрачная", "скрыта"]
    readonly property string partModeName: root.partModeNames[root.partMode]
    function cyclePartMode() { root.partMode = (root.partMode + 1) % root.partModeNames.length }
    // The former part visibility toggle: derived from the view, not a separate
    // property - otherwise two sources of one state could diverge.
    readonly property bool showPartVisible: root.partMode < 2
    // The part skin separately from the edges: key K (CNC_NO_PART_SKIN) removes the
    // part body, leaving the reference wireframe - on the cut metal only it is
    // visible.
    property bool showPartSkin: true
    // Which of the two skin models the part node draws. One rule - the part view
    // (partMode) and key K: the stock, the cut field and the quality level do not
    // affect it. The properties are exposed because it is them that the view is
    // checked by (keyprobe): the scene and the check look at one thing, not at two
    // similar conditions.
    readonly property bool partSkinSolid: root.showPartSkin && root.partMode === 0
    readonly property bool partSkinClear: root.showPartSkin && root.partMode === 1
    // Skin density: solid is opaque (1.0), transparent - 0.35. Measurement on a
    // 1024x600 frame (Test_model, copper cut without skin #CA5614): at 0.22 it reads as
    // #C37E53 - a gray film ate the color, at 0.35 as #C57241; the former ghost density
    // 0.15 would have gone unnoticed by a keypress, and the view would have looked
    // silent.
    readonly property real partSkinOpacity: root.partMode === 1 ? 0.35 : 1.0
    property bool showAllCode: true          // true = whole code at once, false = limit draw distance
    property bool showCollisions: true    // red collision disks;
                                          // off in render_probe - they do
                                          // not obscure the outline
    // Toolpath lines separately from the material: key G (CNC_NO_TOOLPATH) and the
    // "Toolpath lines" button - a clean view of the billet, without the trajectory over
    // it. The lines go "always forward" (depthBias -50000), so on a solid stock they
    // are visible through it too; removing the lines is the answer to that.
    property bool showToolpath: true

    function togglePlay() {
        if (root.moveLength > 0) {
            root.playing = !root.playing
            if (root.playing && root.pathDist >= root.moveLength)
                root.pathDist = 0             // replay from the start
        }
    }
    function resetPath() { root.playing = false; root.pathDist = 0 }

    // The silhouette of the part and the solids of revolution (tool, holder) is
    // recomputed on part readiness and once per rendered frame while the camera moves
    // (events accumulate); the hidden part is culled by the GPU depth test, not by this
    // layer. low/safe recompute the silhouette every silhouetteStep-th frame (quality).
    property bool tangentsPending: false
    property int silhouetteFrame: 0
    function refreshTangents() {
        if (!path)
            return
        // The tool tip in the scene frame: the silhouette eye of the solid of
        // revolution is measured from it, so it goes together with the camera eye.
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
    // ---- Hotkeys: S stock, M part view - one switch with three positions
    // ----          (solid -> transparent -> hidden; it is also the former x-ray),
    // ----          K part skin (part body without edges),
    // ----          G toolpath lines, L limit length, T stages,
    // ----          PgUp/PgDn switch stage, P play, E edges, D tangents,
    // ----          F fps, Q - quality cycle (med -> low -> high -> safe),
    // ----          B - the former stock box instead of the cut surface,
    // ----          C - billet palette, R - stock view (exact/smooth/rounded),
    // ----          V - stock translucent (CNC_STOCK_GHOST).
    // The layout must not kill the hotkeys: on the Russian layout Qt reports the code
    // Key_Ch for the physical X, not Key_X, and the letter branches below do not work.
    // The letters are matched by nativeScanCode - the scan code of the physical key, it
    // does not depend on the layout. nativeVirtualKey is not suitable for this: the QML
    // type KeyEvent (QtQuick, qquickevents_p_p.h, class QQuickKeyEvent) does not provide
    // it at all, in the Keys handler it is always undefined.
    // Measured with CNC_KEY_DEBUG=1 (the live viewer and the build/viewer/keyprobe.exe
    // probe), keys P S M L T E D F (the viewer no longer handles X - the part view is
    // one):
    //   Russian layout: sc 25 31 50 38 20 18 32 33, key 1047 1067 1068 1044
    //                   1045 1059 1042 1040 (symbols z y ' d e u v a);
    //   Latin:          the same sc, key 80 83 77 76 84 69 68 70.
    // The G and K keys are verified with the same probe (it prints sc/key/cp/vk of each)
    // and are taken by physical scan code, like the other letters: 0x22 and 0x25.
    // The second term is Qt.Key: a key without a scan code and a layout where the letter
    // is in another place. Combinations with Ctrl/Alt/Meta are skipped only for letters:
    // for them these are application commands and the layout switcher, not a viewer
    // hotkey; service keys (Space, Esc, arrows, PgUp/PgDn) stay as they were - they do
    // not depend on the layout and are matched by Qt.Key.
    function keyIs(ev, scan, qtkey) {
        if (ev.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))
            return false
        return ev.nativeScanCode === scan || ev.key === qtkey
    }

    // Key Q: cycle of the levels med -> low -> high -> safe -> med. The mechanism is
    // the same as for the other letters: the physical scan code 0x10 or Qt.Key_Q on a
    // layout where Q is in another place.
    function cycleQuality() {
        var order = ["med", "low", "high", "safe"]
        var i = order.indexOf(root.quality)
        root.quality = order[(i + 1) % order.length]
    }

    // Keyboard diagnostics: prints the event attributes and the toggle states after
    // handling. Silent by default (a line per keypress is noise), enabled by the entry
    // point via the CNC_KEY_DEBUG environment variable (cpp/viewer/main.cpp).
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
            // Part view cycle: solid -> transparent -> hidden. The same switch as
            // CNC_PART_MODE at the entry (viewer/main.cpp), and the only one: the
            // transparent skin is the former x-ray (the viewer has no X key any more).
            root.cyclePartMode()
            event.accepted = true
        } else if (root.keyIs(event, 0x2F, Qt.Key_V)) {
            // Stock translucent: the same switch as CNC_STOCK_GHOST at the entry
            // (viewer/main.cpp).
            root.stockGhost = !root.stockGhost
            event.accepted = true
        } else if (root.keyIs(event, 0x25, Qt.Key_K)) {
            // The part skin separately from the edges: removing the body leaves on
            // the cut metal just the reference wireframe. The same switch as
            // CNC_NO_PART_SKIN at the entry (viewer/main.cpp).
            root.showPartSkin = !root.showPartSkin
            event.accepted = true
        } else if (root.keyIs(event, 0x22, Qt.Key_G)) {
            // Toolpath lines over the billet: remove them - and only the cut metal
            // remains. The same switch as CNC_NO_TOOLPATH at the entry
            // (viewer/main.cpp).
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
            // Comparison: the former box instead of the cut surface. The same switch
            // as CNC_STOCK_BOX at the entry (viewer/main.cpp).
            root.stockPlainBox = !root.stockPlainBox
            event.accepted = true
        } else if (root.keyIs(event, 0x2E, Qt.Key_C)) {
            // Material stock palette: presets one after another (tuning contrast on
            // a live frame). The colors before start are set by
            // CNC_STOCK_RAW/CNC_STOCK_CUT (cpp/viewer/main.cpp).
            root.cycleStockPreset()
            event.accepted = true
        } else if (root.keyIs(event, 0x13, Qt.Key_R)) {
            // Stock view: exact -> smooth -> rounded (the cost of each - in the
            // property comment). The same switch as CNC_STOCK_VIEW and CNC_STOCK_ROUND
            // at the entry (cpp/viewer/main.cpp).
            root.cycleStockView()
            event.accepted = true
        }
        if (root.keyDebug)
            root.keyLogState()
    }

    // Frame counter: top right corner. The full-window-width hint line is gone (the
    // hint is a column in the left corner), so the counter is back at the top margin,
    // and z keeps it above the scene.
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

    // ---- Hotkey hint: a compact column in the top left corner ----
    // Formerly this was one line across the whole window width: it is longer than
    // 1024 px, ran across the whole top of the scene and got in the way of looking at
    // the billet. The column takes ~210x150 px in the corner where the scene is empty,
    // and touches neither the stock nor the frame counter.
    // The keys are named in Latin, as on the keyboard: the Keys handler matches letters
    // by physical scan code, so they work on the Russian layout too (keyprobe runs
    // both). The states (part, palette, speed, percent and toggles) stand there too, in
    // the same words as at the bottom panel buttons.
    Rectangle {
        id: hintPanel
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 8
        z: 1
        width: hintCol.implicitWidth + 16
        height: hintCol.implicitHeight + 12
        radius: 6
        // The background is translucent while the text is not: opacity on the
        // rectangle would dim the glyphs too, and on a white background the hint would
        // go pale.
        color: Qt.rgba(0.96, 0.97, 0.98, 0.90)
        border.color: "#D7DBE0"
        border.width: 1

        Column {
            id: hintCol
            x: 8
            y: 6
            spacing: 2
            // The lines are grouped: on top the run state and status, below the keys
            // one by one. The font does not draw the pause glyph (\u23F8) - in a frame it
            // is an empty square, so the run is named with a word, as at the bottom panel
            // button.
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
                text: "K skin: " + (root.showPartSkin ? "вкл" : "выкл")
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

            // "Play / Pause" - playback is always manual now (no auto-play)
            ViewButton {
                checked: root.playing
                text: root.playing ? "\u23F8 Пауза" : "\u25B6 Играть"
                onClicked: root.togglePlay()
            }

            // "Stock bounding box" toggle
            ViewButton {
                checked: root.showStockVisible
                text: "Габарит заготовки"
                onClicked: root.showStockVisible = !root.showStockVisible
            }

            // Part view (key M): cycle - solid, transparent, hidden.
            // The button names the CURRENT view in words, not the action: there are three
            // states, and from the "Part model" label it is impossible to tell which one
            // is next, while the word "transparent" next to the former x-ray did not say
            // how these two views differ. The position name is shared with the on-screen
            // hint (partModeNames), so the labels do not diverge.
            ViewButton {
                checked: root.partMode !== 2
                text: "Деталь: " + root.partModeName
                onClicked: root.cyclePartMode()
            }

            // "Part skin" toggle (key K): the part body without edges - a clean view
            // of the cut metal, with only the reference wireframe over it.
            ViewButton {
                checked: root.showPartSkin
                text: "Кожа детали"
                onClicked: root.showPartSkin = !root.showPartSkin
            }

            // "Toolpath lines" toggle (key G): the lines always go forward and are
            // visible over a solid stock; removing the lines leaves a bare billet.
            ViewButton {
                checked: root.showToolpath
                text: "Линии УП"
                onClicked: root.showToolpath = !root.showToolpath
            }

            // "Whole code" vs "Limit length": limit mode draws only the part
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

        // Row 2: stages ("by stages") + frame (stage) switching
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

            // Current stage label (e.g. "T1 cutter D4   (2/4)")
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
