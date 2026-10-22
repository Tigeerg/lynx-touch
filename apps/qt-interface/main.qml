import QtQuick
import QtQuick.Layouts
import QtQuick3D
import MockBackend 1.0

/**
 * CNC Operator Panel «VMC-850» — QtQuick UI 2.15 visual skeleton.
 * Stage 1: HeaderBar (top panel) fully ported from React/Tailwind prototype.
 * No logic — UI frame only, for use inside QtVCP / LinuxCNC.
 * Color tokens converted from globals.css (OKLCH -> HEX), light theme.
 *
 * The root element is `Item`, so Qt Quick registers the type `Main`
 * (from filename main.qml). To use it as a subcomponent: Main { ... }
 */

Item {
    id: root
    width: 1024
    height: 600

    // Active screen (bind to logic in QtVCP). Values: pos, prog, ...
    property string activeScreen: "pos"
    onActiveScreenChanged: { if (activeScreen === "service") svcFocus = "nav"; }
    property string machineMode: "AUTO"

    // --- 3D toolpath: QML plugin MockBackend (C++) + QtQuick3D ---
    readonly property string viewSample: "O1000 (3D DEMO)\nG21 G90 G54\nT1 M06\nS8000 M03\nG0 X0 Y0 Z5\nG1 Z-2 F200\nG1 X60 Y0 F600\nG1 X60 Y40\nG1 X0 Y40\nG1 X0 Y0\nG0 Z5\nG0 X10 Y10\nG1 Z-4 F150\nG1 X50 Y10\nG1 X50 Y30\nG1 X10 Y30\nG1 X10 Y10\nG0 Z5\nG0 X0 Y0\n"
    GcodeToolpath {
        id: toolpath
        Component.onCompleted: load_string(root.viewSample)
    }

    // --- Ladder (ClassicLadder) ---
    property var ladderRungs: [
        { n: "001", title: "Готовность станка", contacts: [ { a: "I0.0", l: "Цепь АВ.СТОП" }, { a: "I0.1", l: "Ограждение" }, { a: "I0.2", l: "Давл. воздуха" } ], coil: { a: "Q0.0", l: "Станок готов" }, on: true },
        { n: "002", title: "Контактор шпинделя", contacts: [ { a: "Q0.0", l: "Станок готов" }, { a: "M0.0", l: "Команда M3" }, { a: "I0.3", l: "Инстр. зажат" } ], coil: { a: "Q0.1", l: "Шпиндель" }, on: false },
        { n: "003", title: "Насос СОЖ", contacts: [ { a: "Q0.0", l: "Станок готов" }, { a: "M0.1", l: "Команда M8" }, { a: "I0.5", l: "Уровень СОЖ" } ], coil: { a: "Q0.2", l: "Насос СОЖ" }, on: false },
        { n: "004", title: "Таймер смазки", contacts: [ { a: "Q0.0", l: "Станок готов" }, { a: "M0.2", l: "Цикл активен" } ], coil: { a: "T0", l: "Таймер смазки", kind: "ton", preset: 3000 }, on: false },
        { n: "005", title: "Централизованная смазка", contacts: [ { a: "Q0.0", l: "Станок готов" }, { a: "T0", l: "Таймер смазки" }, { a: "I0.4", l: "Уровень масла" } ], coil: { a: "Q0.3", l: "Насос смазки" }, on: false },
        { n: "006", title: "Колонна: красный", contacts: [ { a: "M0.3", l: "Есть авария" } ], coil: { a: "Q0.4", l: "Лампа авария" }, on: false },
        { n: "007", title: "Колонна: зелёный", contacts: [ { a: "Q0.0", l: "Станок готов" }, { a: "M0.2", l: "Цикл активен" }, { a: "M0.3", l: "Есть авария", nc: true } ], coil: { a: "Q0.5", l: "Лампа работа" }, on: false },
        { n: "008", title: "Дверь: красный", contacts: [ { a: "I0.1", l: "Ограждение", nc: true } ], coil: { a: "Q0.4", l: "Лампа авария" }, on: false },
        { n: "009", title: "Дверь: зелёный", contacts: [ { a: "I0.0", l: "Цепь АВ.СТОП" }, { a: "I0.2", l: "Давл. воздуха" } ], coil: { a: "Q0.5", l: "Лампа работа" }, on: false }
    ]

    // --- Keyboard logic (ported from React, the visual part is in QML) ---
    signal machineJogRequest(int dir)
    property int cursorProg: 0
    property int cursorOffset: 0
    property int cursorTool: 0
    property int cursorAlarm: 0
    function listLen() {
        if (activeScreen === "prog") return progLines.length;
        if (activeScreen === "offset") return offsetsData.length;
        if (activeScreen === "tool") return toolsData.length;
        if (activeScreen === "alarm") return alarmsData.length;
        if (activeScreen === "service") return serviceIds.length;
        return 0;
    }
    function cursorIndex() {
        if (activeScreen === "prog") return cursorProg;
        if (activeScreen === "offset") return cursorOffset;
        if (activeScreen === "tool") return cursorTool;
        if (activeScreen === "alarm") return cursorAlarm;
        if (activeScreen === "service") return Math.max(0, serviceIds.indexOf(serviceSection));
        return 0;
    }
    function setCursorIndex(i) {
        var n = listLen(); if (n <= 0) return;
        i = Math.max(0, Math.min(n - 1, i));
        if (activeScreen === "prog") cursorProg = i;
        else if (activeScreen === "offset") cursorOffset = i;
        else if (activeScreen === "tool") cursorTool = i;
        else if (activeScreen === "alarm") cursorAlarm = i;
        else if (activeScreen === "service") serviceSection = serviceIds[i];
    }
    function modeLabel(m) {
        var l = { AUTO: "АВТО", MDI: "MDI", JOG: "JOG", MPG: "МАХОВИК" };
        return l[m] || "";
    }
    function keyIs(ev, vk, qtkey) { return ev.nativeVirtualKey === vk || ev.key === qtkey; }
    function modeStartScreen(m) { return m === "AUTO" ? "prog" : "pos"; }
    function setMode(m) {
        // React SET_MODE: mode start screen + step correction (MPG)
        machineMode = m;
        activeScreen = modeStartScreen(m);
        if (m === "MPG" && (jogStep < 0 || jogStep > 2)) jogStep = 2;
        if (m === "AUTO" && activeScreen === "prog") svcEditing = false;
    }
    function modeCycle(dir) {
        var modes = ["AUTO", "MDI", "JOG", "MPG"];
        var i = modes.indexOf(machineMode); if (i < 0) i = 0;
        i = (i + dir + modes.length) % modes.length;
        setMode(modes[i]);
    }
    function submitServiceAuth() {
        if (authInput.text === "1234") { serviceWrite = true; serviceAuthOpen = false; serviceAuthError = false; }
        else { serviceAuthError = true; }
    }
    function tabLocked(sc) {
        if (machineMode === "AUTO") return sc === "offset" || sc === "tool" || sc === "service";
        if (machineMode === "MDI") return sc === "prog" || sc === "service";
        return false;
    }
    function serviceSectionStep(d) {
        var i = serviceIds.indexOf(serviceSection); if (i < 0) i = 0;
        i = Math.max(0, Math.min(serviceIds.length - 1, i + d));
        serviceSection = serviceIds[i];
    }
    function stepAxis(d) { var ax = ["X", "Y", "Z", "A"]; var i = ax.indexOf(jogAxis); if (i < 0) i = 0; i = (i + d + ax.length) % ax.length; jogAxis = ax[i]; }
    function navUp() {
        if (activeScreen === "service") {
            if (serviceAuthOpen) return;
            if (svcEditing) { adjustParam(1, true); return; }
            if (svcFocus === "nav") { serviceSectionStep(-1); return; }
            if (svcCursor > 0) svcCursor--; return;
        }
        cursorUp();
    }
    function navDown() {
        if (activeScreen === "service") {
            if (serviceAuthOpen) return;
            if (svcEditing) { adjustParam(-1, true); return; }
            if (svcFocus === "nav") { serviceSectionStep(1); return; }
            if (svcCursor < svcListLen() - 1) svcCursor++; return;
        }
        cursorDown();
    }
    function navLeft(c) {
        if (activeScreen === "service") {
            if (serviceAuthOpen) return;
            if (svcEditing) { adjustParam(-1, true); return; }
            if (svcFocus === "list" && (serviceSection === "joints" || serviceSection === "pid")) stepAxis(-1);
            return;
        }
        machineJogRequest(-1);
    }
    function navRight(c) {
        if (activeScreen === "service") {
            if (serviceAuthOpen) return;
            if (svcEditing) { adjustParam(1, true); return; }
            if (svcFocus === "list" && (serviceSection === "joints" || serviceSection === "pid")) stepAxis(1);
            return;
        }
        machineJogRequest(1);
    }
    function cursorUp()   { setCursorIndex(cursorIndex() - 1); }
    function cursorDown() { setCursorIndex(cursorIndex() + 1); }
    property var softkeyOn: ({})
    property int jogStep: 1
    property bool probeOpen: false
    property bool serviceWrite: false
    property bool serviceAuthOpen: false
    property bool serviceAuthError: false

    // --- Splash (v0.5.1 Lynx Touch) ---
    property bool helpOpen: false
    readonly property var helpGroups: [
        { title: "Управление циклом", items: [ { keys: ["Enter"], desc: "Пуск цикла / продолжить" }, { keys: ["Space"], desc: "Стоп подачи" }, { keys: ["Esc"], desc: "Сброс (RESET)" }, { keys: ["Shift", "Esc"], desc: "Аварийный стоп (вкл / отпустить)" } ] },
        { title: "Режимы и экраны", items: [ { keys: ["M"], desc: "Следующий режим (Shift+M — предыдущий)" }, { keys: ["1", "…", "6"], desc: "Экраны: позиция … сервис" }, { keys: ["V"], desc: "Вид траектории: ISO / XY / XZ" }, { keys: ["P"], desc: "Показать / скрыть траекторию" }, { keys: ["F1", "…", "F7"], desc: "Функциональные клавиши экрана" }, { keys: ["H"], desc: "Открыть / закрыть справку" } ] },
        { title: "Перемещение осей", items: [ { keys: ["X", "Y", "Z", "A"], desc: "Выбор оси" }, { keys: ["←", "→"], desc: "Шаг по оси −/+ (РУЧНОЙ, МАХОВИК)" }, { keys: ["↑", "↓"], desc: "Курсор в таблицах и программе" }, { keys: ["PgUp", "PgDn"], desc: "Курсор на 10 строк" } ] },
        { title: "Коррекции", items: [ { keys: ["−", "="], desc: "Подача F: −10% / +10%" }, { keys: ["[", "]"], desc: "Шпиндель S: −10% / +10%" }, { keys: ["9", "0"], desc: "Ускоренный ход: 5 / 25 / 50 / 100%" } ] }
    ]
    property string jogAxis: "X"
    property int feedOverride: 60
    property int spindleOverride: 90
    property int rapidOverride: 100
    readonly property var rapidSteps: [5, 25, 50, 100]
    function selectAxis(a) { jogAxis = a; }
    function axisSel(a) { return (machineMode === "JOG" || machineMode === "MPG") && jogAxis === a; }
    function adjustFeed(d) { feedOverride = Math.max(0, Math.min(150, feedOverride + d * 10)); }
    function adjustSpindle(d) { spindleOverride = Math.max(0, Math.min(150, spindleOverride + d * 10)); }
    function adjustRapid(d) { var i = rapidSteps.indexOf(rapidOverride); if (i < 0) i = rapidSteps.length - 1; i = Math.max(0, Math.min(rapidSteps.length - 1, i + d)); rapidOverride = rapidSteps[i]; }
    function pageCursor(d) { if (activeScreen === "service") { if (svcFocus === "list") svcCursor = Math.max(0, Math.min(svcListLen() - 1, svcCursor + d)); } else setCursorIndex(cursorIndex() + d); }
    property bool splashVisible: true
    property bool splashLeaving: false
    property bool splashGone: false
    property real splashProgress: 0
    function splashDismiss() { if (splashVisible && !splashLeaving) splashLeaving = true; }
    Timer { id: splashT; interval: 10000; running: root.splashVisible && !root.splashLeaving; onTriggered: root.splashLeaving = true }
    Timer { id: splashLeaveT; interval: 500; running: root.splashLeaving && !root.splashGone; onTriggered: root.splashGone = true }
    property int probeStep: 0
    property int probeCycle: 0
    property var probeSteps: ["Выбор цикла", "Подготовка щупа", "Подвод к заготовке", "Измерение", "Результат"]
    property var probeCycles: [
        { name: "Кромка X+", axes: "X", touches: 1 },
        { name: "Кромка X−", axes: "X", touches: 1 },
        { name: "Кромка Y+", axes: "Y", touches: 1 },
        { name: "Кромка Y−", axes: "Y", touches: 1 },
        { name: "Поверхность Z", axes: "Z", touches: 1 },
        { name: "Наружный угол", axes: "X·Y·Z", touches: 3 },
        { name: "Центр бобышки", axes: "X·Y", touches: 4 },
        { name: "Центр отверстия", axes: "X·Y", touches: 4 }
    ]
    property var probeList: [
        { h: "КРОМКИ" }, { i: 0 }, { i: 1 }, { i: 2 }, { i: 3 },
        { h: "ПЛОСКОСТЬ" }, { i: 4 },
        { h: "УГЛЫ И ЦЕНТРЫ" }, { i: 5 }, { i: 6 }, { i: 7 }
    ]
    property var probeViews: [
        { plane: "XY", ox: 165, oy: 150, k: 3, stock: [0, 65, -38, 38], feature: "edge-xp", start: { x: -10.384, y: 0, z: 0 },
          touches: [ { axis: "X", sign: 1, from: { x: -10.384, y: 0, z: 0 }, to: { x: -1, y: 0, z: 0 } } ] },
        { plane: "XY", ox: 235, oy: 150, k: 3, stock: [-65, 0, -38, 38], feature: "edge-xn", start: { x: 10.384, y: 0, z: 0 },
          touches: [ { axis: "X", sign: -1, from: { x: 10.384, y: 0, z: 0 }, to: { x: 1, y: 0, z: 0 } } ] },
        { plane: "XY", ox: 200, oy: 205, k: 3, stock: [-55, 55, 0, 45], feature: "edge-yp", start: { x: 0, y: -10.217, z: 0 },
          touches: [ { axis: "Y", sign: 1, from: { x: 0, y: -10.217, z: 0 }, to: { x: 0, y: -1, z: 0 } } ] },
        { plane: "XY", ox: 200, oy: 90, k: 3, stock: [-55, 55, -45, 0], feature: "edge-yn", start: { x: 0, y: 10.217, z: 0 },
          touches: [ { axis: "Y", sign: -1, from: { x: 0, y: 10.217, z: 0 }, to: { x: 0, y: 1, z: 0 } } ] },
        { plane: "XZ", ox: 200, oy: 175, k: 3, stock: [-50, 50, -30, 0], feature: "top", start: { x: 0, y: 0, z: 12.153 },
          touches: [ { axis: "Z", sign: -1, from: { x: 0, y: 0, z: 12.153 }, to: { x: 0, y: 0, z: 1 } } ] },
        { plane: "XY", ox: 130, oy: 215, k: 3, stock: [0, 65, 0, 48], feature: "corner", start: { x: -10.271, y: -10.442, z: 6.118 },
          touches: [ { axis: "Z", sign: -1, from: { x: 18, y: 16, z: 6.118 }, to: { x: 18, y: 16, z: 1 } },
                     { axis: "X", sign: 1, from: { x: -10.271, y: 32, z: -3 }, to: { x: -1, y: 32, z: -3 } },
                     { axis: "Y", sign: 1, from: { x: 36, y: -10.442, z: -3 }, to: { x: 36, y: -1, z: -3 } } ] },
        { plane: "XY", ox: 200, oy: 150, k: 2.6, stock: [-29.597, 30.421, -20.233, 19.759], feature: "center", start: { x: 0, y: 0, z: 6 },
          touches: [ { axis: "X", sign: 1, from: { x: -40.009, y: 0, z: -4 }, to: { x: -30.597, y: 0, z: -4 } },
                     { axis: "X", sign: -1, from: { x: 40.009, y: 0, z: -4 }, to: { x: 31.421, y: 0, z: -4 } },
                     { axis: "Y", sign: 1, from: { x: 0, y: -29.996, z: -4 }, to: { x: 0, y: -21.233, z: -4 } },
                     { axis: "Y", sign: -1, from: { x: 0, y: 29.996, z: -4 }, to: { x: 0, y: 20.759, z: -4 } } ] },
        { plane: "XY", ox: 200, oy: 150, k: 4, stock: [-44, 44, -32, 32], hole: { cx: -0.318, cy: 0.274, r: 15.006 }, feature: "center", start: { x: 0, y: 0, z: -5 },
          touches: [ { axis: "X", sign: 1, from: { x: 0, y: 0.274, z: -5 }, to: { x: 13.688, y: 0.274, z: -5 } },
                     { axis: "X", sign: -1, from: { x: 0, y: 0.274, z: -5 }, to: { x: -14.324, y: 0.274, z: -5 } },
                     { axis: "Y", sign: 1, from: { x: -0.318, y: 0, z: -5 }, to: { x: -0.318, y: 14.28, z: -5 } },
                     { axis: "Y", sign: -1, from: { x: -0.318, y: 0, z: -5 }, to: { x: -0.318, y: -13.732, z: -5 } } ] }
    ]
    property bool coolantOn: false
    property bool spindleOn: false
    property bool singleBlock: false
    property bool optStop: false
    property bool dryRun: false
    function skeyActive(id) { return softkeyOn[id] === true; }
    function setSkey(id, val) {
        var m = {}; for (var k in softkeyOn) m[k] = softkeyOn[k];
        m[id] = val; softkeyOn = m;
    }
    function pressSoftkey(k) {
        if (!k || k.disabled) return;
        if (k.machine === "coolant") { coolantOn = !coolantOn; machineToggleCoolant(); }
        else if (k.machine === "spindle") { spindleOn = !spindleOn; machineToggleSpindle(); }
        else if (k.id === "single") singleBlock = !singleBlock;
        else if (k.id === "optStop") optStop = !optStop;
        else if (k.id === "dryRun") dryRun = !dryRun;
        else if (k.toggle) setSkey(k.id, !skeyActive(k.id));
        else if (k.id === "act") activateOffset(cursorOffset);
        else if (k.id === "offClear") mutateOffsets(cursorOffset, { x: "+0.000", y: "+0.000", z: "+0.000", a: "+0.000" });
        else if (k.id === "teachX") mutateOffsets(cursorOffset, { x: "+0.000" });
        else if (k.id === "teachY") mutateOffsets(cursorOffset, { y: "+0.000" });
        else if (k.id === "teachZ") mutateOffsets(cursorOffset, { z: "+0.000" });
        else if (k.id === "call") setSpindleTool(cursorTool);
        else if (k.id === "measure") mutateTools(cursorTool, { l: "0.000" });
        else if (k.id === "resetLife") mutateTools(cursorTool, { life: 100 });
        else if (k.id === "ack") mutateAlarms(function (r, i) { if (i === cursorAlarm) r.active = false; return r; });
        else if (k.id === "alarmClearAll") clearAckedAlarms();
        else if (k.id === "probe") probeOpen = true;
        else if (k.id === "pClose") probeOpen = false;
        else if (k.id === "pBack") { if (probeStep > 0) probeStep--; }
        else if (k.id === "pNext") { if (probeStep < 4) probeStep++; }
        else if (k.id === "pRetry") probeStep = 3;
        else if (k.id === "pWrite") probeOpen = false;
        else if (k.id === "svcLock") { if (serviceWrite) { serviceWrite = false; } else { authInput.text = ""; serviceAuthError = false; serviceAuthOpen = true; } }
        else if (k.id === "svcSave") svcSave();
        else if (k.id === "svcDefaults") svcDefaults();
        else if (k.id === "svcBack") svcBack();
        if (k.step !== undefined) jogStep = k.step;
        if (k.nav) serviceSection = k.nav;
    }
    function activeOffsetCode() { for (var i = 0; i < offsetsData.length; i++) if (offsetsData[i].active) return offsetsData[i].code; return "G54"; }
    function activateOffset(i) { var d = offsetsData.slice(); for (var j = 0; j < d.length; j++) { var r = {}; for (var k in d[j]) r[k] = d[j][k]; r.active = (j === i); d[j] = r; } offsetsData = d; }
    function mutateOffsets(i, patch) { var d = offsetsData.slice(); var r = {}; for (var k in d[i]) r[k] = d[i][k]; for (var k2 in patch) r[k2] = patch[k2]; d[i] = r; offsetsData = d; }
    function mutateTools(i, patch) { var d = toolsData.slice(); var r = {}; for (var k in d[i]) r[k] = d[i][k]; for (var k2 in patch) r[k2] = patch[k2]; d[i] = r; toolsData = d; }
    function setSpindleTool(i) { var d = toolsData.slice(); for (var j = 0; j < d.length; j++) { var r = {}; for (var k in d[j]) r[k] = d[j][k]; r.inSpindle = (j === i); d[j] = r; } toolsData = d; }
    function mutateAlarms(fn) { var d = []; for (var i = 0; i < alarmsData.length; i++) { var r = {}; for (var k in alarmsData[i]) r[k] = alarmsData[i][k]; d.push(fn(r, i)); } alarmsData = d; }
    function activeAlarmCount() { var n = 0; for (var i = 0; i < alarmsData.length; i++) if (alarmsData[i].active) n++; return n; }
    function clearAckedAlarms() { var d = []; for (var i = 0; i < alarmsData.length; i++) if (alarmsData[i].active) d.push(alarmsData[i]); alarmsData = d; if (cursorAlarm >= alarmsData.length) cursorAlarm = Math.max(0, alarmsData.length - 1); }
    function padSoft(a) { var r = a.slice(); while (r.length < 7) r.push(null); return r.slice(0, 7); }
    function pressSoftkeyIndex(i) {
        var arr = softkeysFor(activeScreen);
        if (i >= 0 && i < arr.length) pressSoftkey(arr[i]);
    }
    function softkeysFor(screen) {
        var manual = machineMode === "JOG" || machineMode === "MPG";
        if (screen === "pos") return [
            { id: "zeroRel", label: "Обнулить ОТН", icon: "\ue0ac" },
            { id: "step0", label: "Шаг 0.001", icon: "\ue14b", disabled: !manual, step: 0 },
            { id: "step1", label: "Шаг 0.01", icon: "\ue14b", disabled: !manual, step: 1 },
            { id: "step2", label: "Шаг 0.1", icon: "\ue14b", disabled: !manual, step: 2 },
            { id: "coolant", label: "СОЖ", icon: "\ue0b5", machine: "coolant" },
            { id: "spindle", label: "Шпиндель", icon: "\ue379", machine: "spindle", disabled: machineMode === "AUTO" }
        ];
        if (screen === "prog") return [
            { id: "openFile", label: "Открыть файл", icon: "\ue247" },
            { id: "edit", label: "Редактировать", icon: "\ue1f9" },
            { id: "single", label: "Покадрово", icon: "\ue3ea", toggle: true },
            { id: "optStop", label: "Опц. стоп M01", icon: "\ue07f", toggle: true },
            { id: "dryRun", label: "Холостой ход", icon: "\ue0bd", toggle: true },
            { id: "rewind", label: "В начало", icon: "\ue15f" },
            { id: "toCursor", label: "Старт с кадра", icon: "\ue459" }
        ];
        if (screen === "offset") {
            if (probeOpen) return [
                { id: "pBack", label: "Назад", icon: "\ue06e", disabled: probeStep === 0 },
                { id: "pNext", label: "Далее", icon: "\ue06f", disabled: probeStep >= 4 },
                { id: "pTool", label: "Щуп T99", icon: "\ue1db" },
                { id: "pWrite", label: "Записать в " + activeOffsetCode(), icon: "\ue14d" },
                { id: "pRetry", label: "Повторить", icon: "\ue148" },
                null,
                { id: "pClose", label: "Закрыть щуп", icon: "\ue084" }
            ];
            return [
            { id: "act", label: "Активировать", icon: "\ue06c" },
            { id: "teachX", label: "Привязка X", icon: "\ue0ac" },
            { id: "teachY", label: "Привязка Y", icon: "\ue0ac" },
            { id: "teachZ", label: "Привязка Z", icon: "\ue0ac" },
            { id: "offClear", label: "Обнулить", icon: "\ue28f" },
            null,
            { id: "probe", label: "3D щуп", icon: "\ue1db" }
        ];
        }
        if (screen === "tool") return [
            { id: "call", label: "Вызвать в шпиндель", icon: "\ue1b1", disabled: machineMode === "AUTO" },
            { id: "measure", label: "Замер длины", icon: "\ue14b" },
            { id: "resetLife", label: "Сброс ресурса", icon: "\ue144" }
        ];
        if (screen === "alarm") return [
            { id: "ack", label: "Квитировать", icon: "\ue06c" },
            { id: "alarmClearAll", label: "Удалить квитир.", icon: "\ue18d" }
        ];
        if (screen === "service") {
            var keys2 = [
                { id: "svcBack", label: "Назад", icon: "\ue2a1" },
                { id: "svcLock", label: serviceWrite ? "Блок. записи" : "Разрешить запись", icon: serviceWrite ? "\ue10c" : "\ue10b" },
                { id: "svcSave", label: "Сохранить", icon: "\ue14d" },
                { id: "svcDefaults", label: "По умолчанию", icon: "\ue148" },
                serviceSection === "ladder" ? { id: "ladderEdit", label: "Правка схемы", icon: "\ue1f9" } : null
            ];
            return keys2;
        }
        return [];
    }
    readonly property var screenIds: ["pos", "prog", "offset", "tool", "alarm", "service", "view"]

    // --- Service: selected section (visual logic in QML) ---
    property string serviceSection: "system"
    onServiceSectionChanged: { svcCursor = 0; svcEditing = false; svcFocus = "nav"; }
    property var serviceNav: [
        { t: "head", text: "ЧПУ LINUXCNC" },
        { t: "item", id: "system",   page: 0, fk: "F1", label: "Система",    hint: "Интерфейс и [DISPLAY]",      icon: "\ue603" },
        { t: "item", id: "linuxcnc", page: 0, fk: "F2", label: "LinuxCNC",   hint: "[KINS] [RS274NGC] [HAL]",    icon: "\ue316" },
        { t: "item", id: "motion",   page: 0, fk: "F3", label: "Траектория", hint: "[TRAJ] [SPINDLE] [EMCMOT]",  icon: "\ue53e" },
        { t: "item", id: "joints",   page: 0, fk: "F4", label: "Оси",         hint: "[JOINT_n] пределы, поиск",   icon: "\ue2fe" },
        { t: "item", id: "pid",      page: 0, fk: "F5", label: "PID",         hint: "Контуры регулирования",      icon: "\ue29a" },
        { t: "head", text: "ОБОРУДОВАНИЕ" },
        { t: "item", id: "mesa",     page: 1, fk: "F1", label: "Mesa",        hint: "hostmot2 · плата, stepgen",  icon: "\ue403" },
        { t: "item", id: "hal",      page: 1, fk: "F2", label: "HAL",         hint: "Монитор пинов HAL",          icon: "\ue4e3" },
        { t: "item", id: "ladder",   page: 1, fk: "F3", label: "Ladder",      hint: "ClassicLadder, онлайн",      icon: "\ue425" }
    ]
    readonly property var serviceIds: ["system","linuxcnc","motion","joints","pid","mesa","hal","ladder"]
    function serviceLabelOf(id) {
        var m = { system:"Система", linuxcnc:"LinuxCNC", motion:"Траектория", joints:"Оси", pid:"PID", mesa:"Mesa", hal:"HAL", ladder:"Ladder" };
        return m[id] || "";
    }
    function serviceStep(d) {
        var i = serviceIds.indexOf(serviceSection);
        if (i < 0) i = 0;
        i = (i + d + serviceIds.length) % serviceIds.length;
        serviceSection = serviceIds[i];
    }

    // ---- Outgoing signals: machine control ONLY (sent to QtVCP) ----
    signal machineCycleStart()
    signal machineFeedHold()
    signal machineReset()
    signal machineEmergencyStop()
    signal machineToggleSpindle()
    signal machineToggleCoolant()

    // Program data (for the "Program" screen)


    // --- Service parameters (v0.5): per-section defs + value maps ---
    property int svcCursor: 0
    property bool svcEditing: false
    property string svcFocus: "nav"
    property string svcAxis: "X"
    property var svcValues: ({})
    property var svcSavedMap: ({})

    readonly property var systemDefs: [
        { label: "Тема интерфейса", key: "[DISPLAY] THEME", dflt: 0, options: ["Светлая", "Тёмная", "Графит", "Мята"], free: true },
        { label: "Предпросмотр траектории", key: "[DISPLAY] TOOLPATH_PREVIEW", dflt: 1, options: ["Скрыт", "Показан"], free: true },
        { label: "Язык интерфейса", key: "[DISPLAY] LANGUAGE", dflt: 0, options: ["Русский", "English"], free: true },
        { label: "Отображение позиции", key: "[DISPLAY] POSITION_FEEDBACK", dflt: 0, options: ["Фактическая", "Заданная"] },
        { label: "Период обновления экрана", key: "[DISPLAY] CYCLE_TIME", dflt: 100, unit: "мс", min: 50, max: 500, step: 50 },
        { label: "Макс. коррекция подачи", key: "[DISPLAY] MAX_FEED_OVERRIDE", dflt: 150, unit: "%", min: 100, max: 200, step: 10 },
        { label: "Макс. коррекция шпинделя", key: "[DISPLAY] MAX_SPINDLE_OVERRIDE", dflt: 120, unit: "%", min: 100, max: 150, step: 10 },
        { label: "Линейные единицы", key: "[TRAJ] LINEAR_UNITS", dflt: 0, options: ["мм", "дюйм"], restart: true },
        { label: "Уровень отладки", key: "[EMC] DEBUG", dflt: 0, min: 0, max: 7, step: 1 }
    ]
    readonly property var linuxcncDefs: [
        { label: "Модуль кинематики", key: "[KINS] KINEMATICS", dflt: 0, options: ["trivkins XYZA", "trivkins XYZ", "gantrykins"], restart: true },
        { label: "Число сочленений", key: "[KINS] JOINTS", dflt: 4, min: 3, max: 9, step: 1, restart: true },
        { label: "Стартовые G-коды", key: "[RS274NGC] RS274NGC_STARTUP_CODE", dflt: 0, options: ["G17 G21 G90 G54", "G17 G21 G90", "G18 G21 G90"] },
        { label: "Флаги интерпретатора", key: "[RS274NGC] FEATURES", dflt: 12, min: 0, max: 63, step: 1 },
        { label: "Подпрограмма при аборте", key: "[RS274NGC] ON_ABORT_COMMAND", dflt: 1, options: ["—", "o<on_abort>"] },
        { label: "Таблица инструмента", key: "[EMCIO] TOOL_TABLE", dflt: 0, options: ["tool.tbl", "tool_atc.tbl"] },
        { label: "Случайный магазин", key: "[EMCIO] RANDOM_TOOLCHANGER", dflt: 0, options: ["Нет", "Да"], restart: true },
        { label: "Основной HAL-файл", key: "[HAL] HALFILE", dflt: 0, options: ["vmc850.hal", "vmc850_sim.hal"], restart: true },
        { label: "Модуль halui", key: "[HAL] HALUI", dflt: 1, options: ["Выкл", "halui"], restart: true },
        { label: "Тайм-аут связи motion", key: "[EMCMOT] COMM_TIMEOUT", dflt: 1, unit: "с", min: 0.5, max: 5, step: 0.5, digits: 1 },
        { label: "Цифровые I/O motion", key: "[EMCMOT] NUM_DIO", dflt: 8, min: 0, max: 64, step: 1, restart: true },
        { label: "Работа без поиска нуля", key: "[TRAJ] NO_FORCE_HOMING", dflt: 0, options: ["Нет", "Да"] }
    ]
    readonly property var motionDefs: [
        { label: "Период servo-потока", key: "[EMCMOT] SERVO_PERIOD", dflt: 1000000, unit: "нс", min: 250000, max: 2000000, step: 250000, restart: true },
        { label: "Период task-цикла", key: "[TASK] CYCLE_TIME", dflt: 0.01, unit: "с", min: 0.001, max: 0.05, step: 0.001, digits: 3, restart: true },
        { label: "Макс. линейная скорость", key: "[TRAJ] MAX_LINEAR_VELOCITY", dflt: 250, unit: "мм/с", min: 10, max: 500, step: 5 },
        { label: "Скорость ручн. по умолч.", key: "[TRAJ] DEFAULT_LINEAR_VELOCITY", dflt: 50, unit: "мм/с", min: 5, max: 250, step: 5 },
        { label: "Макс. ускорение", key: "[TRAJ] MAX_LINEAR_ACCELERATION", dflt: 1500, unit: "мм/с²", min: 100, max: 5000, step: 50 },
        { label: "Сглаживание траектории", key: "[TRAJ] ARC_BLEND_ENABLE", dflt: 1, options: ["Выкл", "Вкл"] },
        { label: "Зазор сглаживания", key: "[TRAJ] ARC_BLEND_GAP_CYCLES", dflt: 4, unit: "цикл", min: 1, max: 10, step: 1 },
        { label: "Макс. обороты шпинделя", key: "[SPINDLE_0] MAX_FORWARD_VELOCITY", dflt: 12000, unit: "об/мин", min: 1000, max: 24000, step: 500 },
        { label: "Мин. обороты шпинделя", key: "[SPINDLE_0] MIN_FORWARD_VELOCITY", dflt: 300, unit: "об/мин", min: 0, max: 2000, step: 50 },
        { label: "Шаг оборотов (ручн.)", key: "[SPINDLE_0] INCREMENT", dflt: 100, unit: "об/мин", min: 50, max: 1000, step: 50 },
        { label: "Подъём Z при смене инстр.", key: "[EMCIO] TOOL_CHANGE_QUILL_UP", dflt: 1, options: ["Нет", "Да"] },
        { label: "Смена при вращении", key: "[EMCIO] TOOL_CHANGE_WITH_SPINDLE_ON", dflt: 0, options: ["Нет", "Да"] }
    ]
    readonly property var mesaDefs: [
        { label: "Плата Mesa", key: "[HOSTMOT2] BOARD", dflt: 0, options: ["7i96S", "7i76E", "7i92T + 7i76"], restart: true },
        { label: "IP-адрес платы", key: "[HOSTMOT2] IPADDR", dflt: 0, options: ["192.168.1.121", "10.10.10.10"], restart: true },
        { label: "Генераторы шагов", key: "[HOSTMOT2] NUM_STEPGENS", dflt: 4, min: 0, max: 5, step: 1, restart: true },
        { label: "Энкодеры", key: "[HOSTMOT2] NUM_ENCODERS", dflt: 1, min: 0, max: 2, step: 1, restart: true },
        { label: "ШИМ-генераторы", key: "[HOSTMOT2] NUM_PWMGENS", dflt: 1, min: 0, max: 1, step: 1, restart: true },
        { label: "Тайм-аут watchdog", key: "[HOSTMOT2] WATCHDOG_TIMEOUT", dflt: 5, unit: "мс", min: 1, max: 50, step: 1 },
        { label: "Опережение DPLL", key: "[HOSTMOT2] DPLL_TIMER", dflt: -100, unit: "мкс", min: -500, max: 0, step: 10 },
        { label: "Длительность импульса", key: "[HOSTMOT2] STEPLEN", dflt: 5000, unit: "нс", min: 1000, max: 20000, step: 500 },
        { label: "Пауза между импульсами", key: "[HOSTMOT2] STEPSPACE", dflt: 5000, unit: "нс", min: 1000, max: 20000, step: 500 },
        { label: "Установка направления", key: "[HOSTMOT2] DIRSETUP", dflt: 10000, unit: "нс", min: 1000, max: 50000, step: 1000 },
        { label: "Частота ШИМ шпинделя", key: "[HOSTMOT2] PWM_FREQUENCY", dflt: 20000, unit: "Гц", min: 1000, max: 50000, step: 1000 },
        { label: "Фильтр энкодера", key: "[HOSTMOT2] ENCODER_FILTER", dflt: 1, options: ["Выкл", "Вкл"] }
    ]
    readonly property var jointDefs: [
        { label: "Макс. скорость", key: "[JOINT_0] MAX_VELOCITY", dflt: 250, unit: "мм/с", min: 5, max: 500, step: 5, digits: 1 },
        { label: "Макс. ускорение", key: "[JOINT_0] MAX_ACCELERATION", dflt: 1500, unit: "мм/с²", min: 50, max: 5000, step: 50 },
        { label: "Мин. предел", key: "[JOINT_0] MIN_LIMIT", dflt: -850, unit: "мм", min: -2000, max: 0, step: 1, digits: 3 },
        { label: "Макс. предел", key: "[JOINT_0] MAX_LIMIT", dflt: 0.5, unit: "мм", min: 0, max: 2000, step: 1, digits: 3 },
        { label: "Ошибка рассогл.", key: "[JOINT_0] FERROR", dflt: 0.5, unit: "мм", min: 0.01, max: 5, step: 0.01, digits: 3 },
        { label: "Мин. рассогл.", key: "[JOINT_0] MIN_FERROR", dflt: 0.05, unit: "мм", min: 0.005, max: 1, step: 0.005, digits: 3 },
        { label: "Люфт", key: "[JOINT_0] BACKLASH", dflt: 0.004, unit: "мм", min: 0, max: 0.5, step: 0.001, digits: 3 },
        { label: "Масштаб", key: "[JOINT_0] SCALE", dflt: 1000, unit: "имп/мм", min: 10, max: 100000, step: 10 },
        { label: "Позиция нуля", key: "[JOINT_0] HOME", dflt: 0, unit: "мм", min: -100, max: 100, step: 0.1, digits: 3 },
        { label: "Поиск нуля", key: "[JOINT_0] HOME_SEARCH_VEL", dflt: 20, unit: "мм/с", min: -100, max: 100, step: 1, digits: 1 },
        { label: "Фиксация нуля", key: "[JOINT_0] HOME_LATCH_VEL", dflt: -1, unit: "мм/с", min: -20, max: 20, step: 0.5, digits: 1 },
        { label: "Порядок поиска", key: "[JOINT_0] HOME_SEQUENCE", dflt: 1, min: 0, max: 9, step: 1 }
    ]
    readonly property var pidDefs: [
        { label: "Пропорц.", key: "[JOINT_0] P", dflt: 1000, min: 0, max: 10000, step: 10 },
        { label: "Интеграл.", key: "[JOINT_0] I", dflt: 0, min: 0, max: 100, step: 0.1, digits: 1 },
        { label: "Дифференц.", key: "[JOINT_0] D", dflt: 0, min: 0, max: 100, step: 0.1, digits: 1 },
        { label: "Упреждение 0", key: "[JOINT_0] FF0", dflt: 0, min: 0, max: 10, step: 0.01, digits: 2 },
        { label: "Упреждение 1", key: "[JOINT_0] FF1", dflt: 1, min: 0, max: 10, step: 0.01, digits: 2 },
        { label: "Упреждение 2", key: "[JOINT_0] FF2", dflt: 0.0001, min: 0, max: 1, step: 0.0001, digits: 4 },
        { label: "Мёртвая зона", key: "[JOINT_0] DEADBAND", dflt: 0.0005, unit: "мм", min: 0, max: 0.1, step: 0.0001, digits: 4 },
        { label: "Смещение", key: "[JOINT_0] BIAS", dflt: 0, min: -10, max: 10, step: 0.01, digits: 2 },
        { label: "Огр. выхода", key: "[JOINT_0] MAX_OUTPUT", dflt: 300, unit: "мм/с", min: 0, max: 1000, step: 5 },
        { label: "Огр. ошибки", key: "[JOINT_0] MAX_ERRR", dflt: 0, unit: "мм", min: 0, max: 5, step: 0.01, digits: 3 }
    ]

    function sysParamsFor(section) {
        if (section === "system") return systemDefs;
        if (section === "linuxcnc") return linuxcncDefs;
        if (section === "motion") return motionDefs;
        if (section === "mesa") return mesaDefs;
        if (section === "joints") return jointDefs;
        if (section === "pid") return pidDefs;
        return [];
    }
    function hasSvcList() { var sc = serviceSection; return sc === "system" || sc === "linuxcnc" || sc === "motion" || sc === "joints" || sc === "pid" || sc === "mesa"; }
    readonly property var halDefs: [
        { title: "Питание и безопасность", pins: [ { name: "iocontrol.0.emc-enable-in", type: "bit", dir: "IN" }, { name: "halui.estop.is-activated", type: "bit", dir: "OUT" }, { name: "halui.machine.is-on", type: "bit", dir: "OUT" } ] },
        { title: "Режим и программа", pins: [ { name: "halui.mode.is-auto", type: "bit", dir: "OUT" }, { name: "halui.mode.is-mdi", type: "bit", dir: "OUT" }, { name: "halui.mode.is-manual", type: "bit", dir: "OUT" }, { name: "halui.program.is-running", type: "bit", dir: "OUT" }, { name: "halui.program.is-paused", type: "bit", dir: "OUT" }, { name: "halui.program.is-idle", type: "bit", dir: "OUT" }, { name: "motion.program-line", type: "s32", dir: "OUT" }, { name: "halui.feed-override.value", type: "float", dir: "OUT" } ] },
        { title: "Движение", pins: [ { name: "motion.in-position", type: "bit", dir: "OUT" }, { name: "motion.current-vel", type: "float", dir: "OUT" }, { name: "axis.z.pos-cmd (раб.)", type: "float", dir: "OUT" } ] },
        { title: "Оси: обратная связь по положению", pins: [ { name: "joint.0.pos-fb", type: "float", dir: "IN" }, { name: "joint.1.pos-fb", type: "float", dir: "IN" }, { name: "joint.2.pos-fb", type: "float", dir: "IN" }, { name: "joint.3.pos-fb", type: "float", dir: "IN" } ] },
        { title: "Оси: ошибка слежения", pins: [ { name: "joint.0.f-error", type: "float", dir: "OUT" }, { name: "joint.1.f-error", type: "float", dir: "OUT" }, { name: "joint.2.f-error", type: "float", dir: "OUT" }, { name: "joint.3.f-error", type: "float", dir: "OUT" } ] },
        { title: "Оси: базирование", pins: [ { name: "joint.0.homed", type: "bit", dir: "OUT" }, { name: "joint.1.homed", type: "bit", dir: "OUT" }, { name: "joint.2.homed", type: "bit", dir: "OUT" }, { name: "joint.3.homed", type: "bit", dir: "OUT" } ] },
        { title: "Шпиндель и СОЖ", pins: [ { name: "spindle.0.on", type: "bit", dir: "OUT" }, { name: "spindle.0.speed-out", type: "float", dir: "OUT" }, { name: "iocontrol.0.coolant-flood", type: "bit", dir: "OUT" } ] },
        { title: "Инструмент", pins: [ { name: "iocontrol.0.tool-number", type: "s32", dir: "OUT" } ] }
    ]
    function halRowCount() { var n = 0; for (var i = 0; i < halDefs.length; i++) n += halDefs[i].pins.length; return n; }
    function svcListLen() { if (serviceSection === "hal") return halRowCount(); return sysParamsFor(serviceSection).length; }
    function halValuesMap() {
        var v = {};
        v["iocontrol.0.emc-enable-in"] = true;
        v["halui.estop.is-activated"] = false;
        v["halui.machine.is-on"] = true;
        v["halui.mode.is-auto"] = (machineMode === "AUTO");
        v["halui.mode.is-mdi"] = (machineMode === "MDI");
        v["halui.mode.is-manual"] = (machineMode === "JOG" || machineMode === "MPG");
        v["halui.program.is-running"] = false;
        v["halui.program.is-paused"] = false;
        v["halui.program.is-idle"] = true;
        v["motion.program-line"] = 0;
        v["halui.feed-override.value"] = 1.0;
        v["motion.in-position"] = true;
        v["motion.current-vel"] = 0.0;
        v["axis.z.pos-cmd (раб.)"] = 210.4;
        v["spindle.0.on"] = root.spindleOn;
        v["spindle.0.speed-out"] = (root.spindleOn ? 3500.0 : 0.0);
        v["iocontrol.0.coolant-flood"] = root.coolantOn;
        v["iocontrol.0.tool-number"] = 1;
        for (var i = 0; i < 4; i++) { v["joint." + i + ".pos-fb"] = 0.0; v["joint." + i + ".f-error"] = 0.0001; v["joint." + i + ".homed"] = true; }
        return v;
    }
    function halRows() {
        var vals = halValuesMap();
        var rows = [];
        for (var g = 0; g < halDefs.length; g++) {
            var grp = halDefs[g];
            rows.push({ head: true, title: grp.title, count: grp.pins.length });
            for (var p = 0; p < grp.pins.length; p++) {
                var pin = grp.pins[p];
                var v = vals[pin.name] !== undefined ? vals[pin.name] : 0;
                if (pin.type === "bit") rows.push({ head: false, name: pin.name, type: pin.type, dir: pin.dir, bit: true, on: !!v });
                else if (pin.type === "s32") rows.push({ head: false, name: pin.name, type: pin.type, dir: pin.dir, bit: false, val: "" + Math.round(v) });
                else rows.push({ head: false, name: pin.name, type: pin.type, dir: pin.dir, bit: false, val: Number(v).toFixed(4) });
            }
        }
        return rows;
    }
    function svcKey(i) { return serviceSection + "." + i; }
    function svcVal(section, i, def) { var k = section + "." + i; return svcValues[k] !== undefined ? svcValues[k] : def.dflt; }
    function setSvcVal(i, v) { var m = {}; for (var k in svcValues) m[k] = svcValues[k]; m[svcKey(i)] = v; svcValues = m; }
    function fmtVal(def, v) { if (def.options) { var o = def.options[v]; return o !== undefined ? o : String(v); } var dg = def.digits !== undefined ? def.digits : 0; return v.toFixed(dg) + (def.unit ? " " + def.unit : ""); }
    function svcDefAt(i) { return sysParamsFor(serviceSection)[i]; }
    function svcValueAt(i) { var def = svcDefAt(i); return def ? svcVal(serviceSection, i, def) : 0; }
    function svcSavedValOf(i, def) { var k = svcKey(i); return svcSavedMap[k] !== undefined ? svcSavedMap[k] : def.dflt; }
    function svcUnsaved(i) { var def = svcDefAt(i); if (!def) return false; return svcValueAt(i) !== svcSavedValOf(i, def); }
    function svcUnsavedCount() { var arr = sysParamsFor(serviceSection); var n = 0; for (var i = 0; i < arr.length; i++) if (svcValueAt(i) !== svcSavedValOf(i, arr[i])) n++; return n; }
    function svcChanged(i) { var def = svcDefAt(i); return def ? svcValueAt(i) !== def.dflt : false; }
    function adjustParam(dir, coarse) {
        var i = svcCursor; var def = svcDefAt(i); if (!def) return;
        if (!def.free && !serviceWrite) return;
        var v = svcValueAt(i);
        if (def.options) v = Math.max(0, Math.min(def.options.length - 1, v + dir));
        else { var st = (def.step || 1) * (coarse ? 10 : 1); v = v + dir * st; v = Math.max(def.min !== undefined ? def.min : -1e12, Math.min(def.max !== undefined ? def.max : 1e12, v)); }
        setSvcVal(i, v);
    }
    function svcEnter() {
        if (serviceAuthOpen) { submitServiceAuth(); return; }
        if (svcEditing) { svcEditing = false; return; }
        if (svcFocus === "nav") { if (hasSvcList() || serviceSection === "hal") svcFocus = "list"; return; }
        if (serviceSection === "ladder") return;
        var def = svcDefAt(svcCursor); if (!def) return;
        if (def.free !== true && !serviceWrite) { authInput.text = ""; serviceAuthError = false; serviceAuthOpen = true; return; }
        svcEditing = true;
    }
    function svcCancel() {
        if (serviceAuthOpen) { serviceAuthOpen = false; return; }
        if (svcEditing) { svcEditing = false; return; }
        if (svcFocus === "list") { svcFocus = "nav"; return; }
    }
    function svcBack() {
        if (serviceAuthOpen || svcEditing || svcFocus === "list") { svcCancel(); return; }
        if (serviceSection !== serviceIds[0]) serviceSection = serviceIds[0];
        else activeScreen = "pos";
    }
    function svcSave() { var arr = sysParamsFor(serviceSection); var m = {}; for (var kk in svcSavedMap) m[kk] = svcSavedMap[kk]; for (var i = 0; i < arr.length; i++) m[svcKey(i)] = svcValueAt(i); svcSavedMap = m; svcEditing = false; }
    function svcDefaults() { var i = svcCursor; var def = svcDefAt(i); if (!def || !(def.free || serviceWrite)) return; setSvcVal(i, def.dflt); }

    // --- Screen data ---
    property var offsetsData: [
        { code: "G54", x: "−120.500", y: "+35.250", z: "−210.400", a: "+0.000", active: true },
        { code: "G55", x: "−320.000", y: "+35.250", z: "−210.400", a: "+90.000", active: false },
        { code: "G56", x: "+0.000", y: "+0.000", z: "+0.000", a: "+0.000", active: false },
        { code: "G57", x: "+0.000", y: "+0.000", z: "+0.000", a: "+0.000", active: false },
        { code: "G58", x: "+0.000", y: "+0.000", z: "+0.000", a: "+0.000", active: false },
        { code: "G59", x: "+0.000", y: "+0.000", z: "+0.000", a: "+0.000", active: false }
    ]
    property var toolsData: [
        { n: "T01", name: "Фреза концевая 4z", d: "12.0", l: "85.214", life: 72, inSpindle: true },
        { n: "T02", name: "Сверло спиральное", d: "6.8", l: "112.060", life: 54, inSpindle: false },
        { n: "T03", name: "Метчик M8", d: "8.0", l: "96.300", life: 88, inSpindle: false },
        { n: "T04", name: "Фреза торцевая", d: "50.0", l: "62.718", life: 8, inSpindle: false },
        { n: "T05", name: "Зенковка 90°", d: "16.0", l: "78.450", life: 65, inSpindle: false },
        { n: "T06", name: "Фреза сферическая", d: "6.0", l: "90.110", life: 41, inSpindle: false },
        { n: "T07", name: "Сверло центровочное", d: "10.0", l: "70.902", life: 93, inSpindle: false },
        { n: "T08", name: "Головка расточная", d: "32.0", l: "128.500", life: 77, inSpindle: false },
        { n: "T99", name: "3D щуп Renishaw OMP40-2", d: "2.0", l: "98.450", life: 100, inSpindle: false }
    ]
    property var alarmsData: [
        { code: "2051", text: "Ресурс инструмента T04 менее 10%", level: "warn", lvl: "Предупр.", time: "07:42:10", active: true },
        { code: "3012", text: "Плановое ТО шпинделя через 48 ч", level: "info", lvl: "Инфо", time: "07:30:02", active: true },
        { code: "1104", text: "Низкий уровень СОЖ в баке", level: "error", lvl: "Авария", time: "06:58:44", active: false }
    ]

    property var progLines: [
        { num: 1, cur: true, toks: [ { t: "O1024", c: "fg" }, { t: "(КОРПУС ФЛАНЦА)", c: "comment" } ] },
        { num: 2, cur: false, toks: [ { t: "N10", c: "muted" }, { t: "G21", c: "primary" }, { t: "G90", c: "primary" }, { t: "G17", c: "primary" }, { t: "G40", c: "primary" }, { t: "G49", c: "primary" }, { t: "G80", c: "primary" } ] },
        { num: 3, cur: false, toks: [ { t: "N20", c: "muted" }, { t: "T1", c: "success" }, { t: "M06", c: "destructive" }, { t: "(ФРЕЗА D12)", c: "comment" } ] },
        { num: 4, cur: false, toks: [ { t: "N30", c: "muted" }, { t: "G54", c: "primary" }, { t: "G00", c: "primary" }, { t: "X0", c: "fg" }, { t: "Y0", c: "fg" } ] },
        { num: 5, cur: false, toks: [ { t: "N40", c: "muted" }, { t: "S8000", c: "success" }, { t: "M03", c: "destructive" } ] },
        { num: 6, cur: false, toks: [ { t: "N50", c: "muted" }, { t: "G43", c: "primary" }, { t: "H01", c: "fg" }, { t: "Z50.0", c: "fg" }, { t: "M08", c: "destructive" } ] },
        { num: 7, cur: false, toks: [ { t: "N60", c: "muted" }, { t: "G00", c: "primary" }, { t: "X-40.0", c: "fg" }, { t: "Y-30.0", c: "fg" } ] },
        { num: 8, cur: false, toks: [ { t: "N70", c: "muted" }, { t: "Z5.0", c: "fg" } ] },
        { num: 9, cur: false, toks: [ { t: "N80", c: "muted" }, { t: "G01", c: "primary" }, { t: "Z-2.0", c: "fg" }, { t: "F400", c: "success" } ] },
        { num: 10, cur: false, toks: [ { t: "N90", c: "muted" }, { t: "G01", c: "primary" }, { t: "X40.0", c: "fg" }, { t: "F1200", c: "success" } ] },
        { num: 11, cur: false, toks: [ { t: "N100", c: "muted" }, { t: "Y30.0", c: "fg" } ] },
        { num: 12, cur: false, toks: [ { t: "N110", c: "muted" }, { t: "X-40.0", c: "fg" } ] },
        { num: 13, cur: false, toks: [ { t: "N120", c: "muted" }, { t: "Y-30.0", c: "fg" } ] },
        { num: 14, cur: false, toks: [ { t: "N130", c: "muted" }, { t: "G00", c: "primary" }, { t: "Z5.0", c: "fg" } ] },
        { num: 15, cur: false, toks: [ { t: "N135", c: "muted" }, { t: "M01", c: "destructive" }, { t: "(КОНТРОЛЬ РАЗМЕРА)", c: "comment" } ] },
        { num: 16, cur: false, toks: [ { t: "N140", c: "muted" }, { t: "X-30.0", c: "fg" }, { t: "Y-20.0", c: "fg" } ] }
    ]

    // ================= Themes (from globals.css v0.5.1) =====================
    // OKLCH -> sRGB HEX (Ottosson). Themes: light / dark / graphite / mint.
    readonly property var pal_light: ({ background: "#E3E7EA", foreground: "#141B24", card: "#FCFDFF", muted: "#EEF0F3", mutedForeground: "#535C66", accent: "#DCEDFF", accentForeground: "#003B7F", primary: "#0362BB", primaryFg: "#FCFCFC", destructive: "#D42125", success: "#1D9247", warning: "#F3A518", warningFg: "#4C2905", border: "#C9CED4", borderMuted: "#E2E6EA" })
    readonly property var pal_dark: ({ background: "#0F1318", foreground: "#E5E8EC", card: "#1A1F25", muted: "#23282F", mutedForeground: "#989FA8", accent: "#1E3149", accentForeground: "#B0D5FE", primary: "#4296EB", primaryFg: "#070E16", destructive: "#EA3B3F", success: "#3CB360", warning: "#F6AD39", warningFg: "#331700", border: "#383E46", borderMuted: "#292E36" })
    readonly property var pal_graphite: ({ background: "#332C28", foreground: "#F1EEEA", card: "#413A35", muted: "#3A342F", mutedForeground: "#B9B3AC", accent: "#624631", accentForeground: "#FFDAAF", primary: "#F3984E", primaryFg: "#211208", destructive: "#EA3B3F", success: "#3CB360", warning: "#F6AD39", warningFg: "#331700", border: "#5E5650", borderMuted: "#504842" })
    readonly property var pal_mint: ({ background: "#DEECE7", foreground: "#0B1E1E", card: "#FAFDFC", muted: "#EAF2F0", mutedForeground: "#485D5B", accent: "#CAF2E6", accentForeground: "#004740", primary: "#007565", primaryFg: "#FCFCFC", destructive: "#D42125", success: "#1D9247", warning: "#F3A518", warningFg: "#4C2905", border: "#BECFCA", borderMuted: "#DCE6E3" })
    // THEME param: 0 Light, 1 Dark, 2 Graphite, 3 Mint
    function pal() { var i = svcVal("system", 0, systemDefs[0]); return i === 1 ? pal_dark : i === 2 ? pal_graphite : i === 3 ? pal_mint : pal_light; }

    readonly property color cBackground:       root.pal().background
    readonly property color cForeground:       root.pal().foreground
    readonly property color cCard:             root.pal().card
    readonly property color cMuted:            root.pal().muted
    readonly property color cMutedForeground:  root.pal().mutedForeground
    readonly property color cAccent:           root.pal().accent
    readonly property color cPrimary:          root.pal().primary
    readonly property color cPrimaryFg:        root.pal().primaryFg
    readonly property color cDestructive:      root.pal().destructive
    readonly property color cSuccess:          root.pal().success
    readonly property color cWarning:          root.pal().warning
    readonly property color cWarningFg:        root.pal().warningFg
    readonly property color cBorder:           root.pal().border
    readonly property color cBorderMuted:      root.pal().borderMuted

    // ================= Typography tokens ====================================
    readonly property int  pxHeader:    16   // text-xs  (12px) — note: in QtQuick fontSize in px
    readonly property int  pxSmall:     13   // text-sm  (14px) small labels
    readonly property int  pxBody:      14   // text-sm  (14px)
    readonly property int  pxLarge:     18   // text-lg  (18px)
    readonly property int  pxXl:        20   // text-xl
    readonly property int  pxTitle:     22   // text-2xl
    readonly property int  pxHuge:      48   // text-5xl coords
    readonly property string fontSans:  "Inter"           // bundled TTF below
    readonly property string fontMono:  "JetBrains Mono"  // bundled TTF below

    // ================= Bundled fonts (OFL) ==================================
    // Inter (replaces --font-sans) and JetBrains Mono (--font-mono) must match
    // the React prototype. FontLoader registers them app-wide.
    FontLoader { id: interFont;   source: "fonts/Inter-Regular.ttf" }
    FontLoader { id: jbMonoFont;  source: "fonts/JetBrainsMono-Regular.ttf" }
    FontLoader { id: lucideFont;  source: "fonts/lucide.ttf" }

    // ================= Layout metrics =======================================
    readonly property int  gap:     6    // p-2 / gap-2 (reduced for compactness)
    readonly property int  pad:     8
    readonly property int  kh:      28   // keycap h-7
    readonly property int  bh:      44   // operator button h-11
    readonly property int  sh:      76   // softkey h-[76px]

    // ====== Stub: makeKeycap removed — in QtQuick 2.15 keycaps are static
    // ====== Rectangles inside layouts (see modeBlock 'M' cap below). ======

    Rectangle {   // ---------- page background ----------
        id: bg
        anchors.fill: parent
        color: cBackground
        radius: 8
    }

    ColumnLayout { // ==================== ROOT COLUMN ========================
        id: rootCol
        anchors.fill: parent
        anchors.margins: pad
        spacing: gap
        focus: true
        Component.onCompleted: rootCol.forceActiveFocus()
        Keys.onPressed: {
            if (!root.splashGone && !root.splashLeaving) root.splashDismiss();
            if (event.isAutoRepeat) return;
            if (event.modifiers & Qt.ControlModifier || event.modifiers & Qt.AltModifier) return;
            if (event.key === Qt.Key_Up)    { root.navUp(); event.accepted = true; return; }
            if (event.key === Qt.Key_Down)  { root.navDown(); event.accepted = true; return; }
            if (event.key === Qt.Key_Left)  { root.navLeft(event.modifiers & Qt.ShiftModifier); event.accepted = true; return; }
            if (event.key === Qt.Key_Right) { root.navRight(event.modifiers & Qt.ShiftModifier); event.accepted = true; return; }
            if (event.key >= Qt.Key_1 && event.key <= Qt.Key_6) {
                root.activeScreen = root.screenIds[event.key - Qt.Key_1]; event.accepted = true; return;
            }
            if (event.key >= Qt.Key_F1 && event.key <= Qt.Key_F7) {
                root.pressSoftkeyIndex(event.key - Qt.Key_F1); event.accepted = true; return;
            }
            if (event.key === Qt.Key_PageUp)   { root.pageCursor(-10); event.accepted = true; return; }
            if (event.key === Qt.Key_PageDown) { root.pageCursor(10); event.accepted = true; return; }
            if (root.keyIs(event, 0x58, Qt.Key_X)) { root.selectAxis("X"); event.accepted = true; return; }
            if (root.keyIs(event, 0x59, Qt.Key_Y)) { root.selectAxis("Y"); event.accepted = true; return; }
            if (root.keyIs(event, 0x5A, Qt.Key_Z)) { root.selectAxis("Z"); event.accepted = true; return; }
            if (root.keyIs(event, 0x41, Qt.Key_A)) { root.selectAxis("A"); event.accepted = true; return; }
            if (event.key === Qt.Key_Minus)        { root.adjustFeed(-1); event.accepted = true; return; }
            if (event.key === Qt.Key_Equal)        { root.adjustFeed(1); event.accepted = true; return; }
            if (event.key === Qt.Key_BracketLeft)  { root.adjustSpindle(-1); event.accepted = true; return; }
            if (event.key === Qt.Key_BracketRight) { root.adjustSpindle(1); event.accepted = true; return; }
            if (event.key === Qt.Key_9) { root.adjustRapid(-1); event.accepted = true; return; }
            if (event.key === Qt.Key_0) { root.adjustRapid(1); event.accepted = true; return; }
            if (root.keyIs(event, 0x48, Qt.Key_H)) { root.helpOpen = !root.helpOpen; event.accepted = true; return; }
            if (root.keyIs(event, 0x4D, Qt.Key_M)) { root.modeCycle(event.modifiers & Qt.ShiftModifier ? -1 : 1); event.accepted = true; return; }
            if (event.key === Qt.Key_Tab) { if (root.activeScreen === "service") { root.svcFocus = root.svcFocus === "list" ? "nav" : "list"; event.accepted = true; return; } }
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                if (root.activeScreen === "service") root.svcEnter();
                else root.machineCycleStart();
                event.accepted = true; return;
            }
            if (event.key === Qt.Key_Space)   { root.machineFeedHold();   event.accepted = true; return; }
            if (event.key === Qt.Key_Escape)  {
                if (root.helpOpen) { root.helpOpen = false; event.accepted = true; return; }
                if (root.activeScreen === "service") { root.svcCancel(); }
                else if (event.modifiers & Qt.ShiftModifier) root.machineEmergencyStop();
                else root.machineReset();
                event.accepted = true; return;
            }
            if (event.key === Qt.Key_Pause)   { root.machineEmergencyStop(); event.accepted = true; return; }
        }

        // ---------------------------------------------------------------- //
        // STAGE 1 — HEADER BAR (HeaderBar)                                  //
        // React: <header class="flex shrink-0 flex-wrap items-stretch gap-2"> //
        // ---------------------------------------------------------------- //
        RowLayout {
            id: headerRow
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            Layout.minimumHeight: 44
            Layout.maximumHeight: 44
            Layout.alignment: Qt.AlignVCenter          // items-stretch
            spacing: 6                                  // gap-2 (reduced)

            // ----- 1. Machine block (CNC) --------------------------------
            Rectangle {
                id: machBlock
                Layout.preferredWidth: 100
                Layout.minimumWidth: 100
                Layout.maximumWidth: 100
                Layout.fillHeight: true                 // items-stretch
                clip: true
                color: cCard
                border.color: cBorder
                radius: 6                               // rounded-md
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 8                  // more compact
                    spacing: 8
                    Rectangle {                          // logo 32x32
                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 28
                        Layout.minimumWidth: 28
                        Layout.maximumWidth: 28
                        Layout.minimumHeight: 28
                        Layout.maximumHeight: 28
                        color: cPrimary
                        radius: 4                        // rounded-sm
                        Text {
                            anchors.centerIn: parent
                            text: "ЧПУ"
                            color: cPrimaryFg
                            font.bold: true
                            font.pixelSize: 11
                        }
                    }
                    Text {                               // name only (the caption has been removed)
                        text: "VMC-850"
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        color: cForeground
                        font.bold: true
                        font.pixelSize: pxBody
                        elide: Text.ElideRight
                    }
                }
            }

            // ----- 2. Mode block (Mode / AUTO + 5 icons + M) ------------
            Rectangle {
                id: modeBlock
                Layout.preferredWidth: 188
                Layout.minimumWidth: 188
                Layout.maximumWidth: 188
                Layout.fillHeight: true
                clip: true
                color: cCard
                border.color: cBorder
                radius: 6
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 3
                    ColumnLayout {                       // "MODE" / "AUTO"
                        Layout.preferredWidth: 44
                        Layout.minimumWidth: 44
                        Layout.maximumWidth: 44
                        Layout.fillHeight: true
                        spacing: 2
                        Text {
                            text: "РЕЖИМ"
                            Layout.maximumWidth: 44
                            color: cMutedForeground
                            font.pixelSize: 9
                            font.letterSpacing: 1
                            font.bold: true
                            elide: Text.ElideRight
                        }
                        Text {
                            text: root.modeLabel(root.machineMode)
                            Layout.maximumWidth: 44
                            color: cPrimary
                            font.bold: true
                            font.pixelSize: 16
                            elide: Text.ElideRight
                        }
                    }
                                        // 4 mode icons (v0.4: AUTO/MDI/JOG/MPG), the active one follows machineMode
                    Rectangle { Layout.preferredWidth: 28; Layout.preferredHeight: 28; Layout.minimumWidth: 28; Layout.maximumWidth: 28; Layout.minimumHeight: 28; Layout.maximumHeight: 28; color: root.machineMode === "AUTO" ? cPrimary : cMuted; radius: 4; clip: true; Text { anchors.centerIn: parent; text: "\ue146"; color: root.machineMode === "AUTO" ? cPrimaryFg : cMutedForeground; font.family: "lucide"; font.pixelSize: 15 } }
                    Rectangle { Layout.preferredWidth: 28; Layout.preferredHeight: 28; Layout.minimumWidth: 28; Layout.maximumWidth: 28; Layout.minimumHeight: 28; Layout.maximumHeight: 28; color: root.machineMode === "MDI" ? cPrimary : cMuted; radius: 4; clip: true; Text { anchors.centerIn: parent; text: "\ue20a"; color: root.machineMode === "MDI" ? cPrimaryFg : cMutedForeground; font.family: "lucide"; font.pixelSize: 15 } }
                    Rectangle { Layout.preferredWidth: 28; Layout.preferredHeight: 28; Layout.minimumWidth: 28; Layout.maximumWidth: 28; Layout.minimumHeight: 28; Layout.maximumHeight: 28; color: root.machineMode === "JOG" ? cPrimary : cMuted; radius: 4; clip: true; Text { anchors.centerIn: parent; text: "\ue1d7"; color: root.machineMode === "JOG" ? cPrimaryFg : cMutedForeground; font.family: "lucide"; font.pixelSize: 15 } }
                    Rectangle { Layout.preferredWidth: 28; Layout.preferredHeight: 28; Layout.minimumWidth: 28; Layout.maximumWidth: 28; Layout.minimumHeight: 28; Layout.maximumHeight: 28; color: root.machineMode === "MPG" ? cPrimary : cMuted; radius: 4; clip: true; Text { anchors.centerIn: parent; text: "\ue494"; color: root.machineMode === "MPG" ? cPrimaryFg : cMutedForeground; font.family: "lucide"; font.pixelSize: 15 } }
                }
            }

            // ----- 3. Run state badge (READY) -----------------------------
            Rectangle {
                id: runBadge
                Layout.preferredWidth: 112
                Layout.minimumWidth: 112
                Layout.maximumWidth: 112
                Layout.fillHeight: true
                clip: true
                color: cCard
                border.color: cBorder
                border.width: 2
                radius: 6
                Text {
                    anchors.centerIn: parent
                    text: "ГОТОВ"
                    Layout.maximumWidth: 100
                    color: cForeground
                    font.bold: true
                    font.pixelSize: 16
                    elide: Text.ElideRight
                }
            }

            // ----- 4. Program block (Program / O1024 · FLANGE HOUSING) ---
            Rectangle {
                id: progBlock
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                color: cCard
                border.color: cBorder
                radius: 6
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 2
                    Text {
                        text: "ПРОГРАММА"
                        Layout.maximumWidth: 100000
                        color: cMutedForeground
                        font.pixelSize: 10
                        font.letterSpacing: 1
                        font.bold: true
                        elide: Text.ElideRight
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 4
                        Text {
                            text: "O1024"
                            Layout.maximumWidth: 60
                            color: cForeground
                            font.bold: true
                            font.pixelSize: pxBody
                            font.family: fontMono
                            elide: Text.ElideRight
                        }
                        Text {
                            text: "КОРПУС ФЛАНЦА"
                            Layout.maximumWidth: 100000
                            color: cMutedForeground
                            font.pixelSize: pxSmall
                            elide: Text.ElideRight
                        }
                    }
                }
            }

            // ----- 5. Alarms counter (2) --------------------------------
            Rectangle {
                id: alarmBadge
                Layout.preferredWidth: 64
                Layout.minimumWidth: 64
                Layout.maximumWidth: 64
                Layout.fillHeight: true
                clip: true
                color: root.activeAlarmCount() > 0 ? "#E5DDCA" : cCard
                border.color: root.activeAlarmCount() > 0 ? cWarning : cBorder
                radius: 6
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6
                    Text { text: "\ue193"; Layout.maximumWidth: 18; color: cWarningFg; font.family: "lucide"; font.pixelSize: 16 }   // triangle-alert
                    Text { text: String(root.activeAlarmCount()); Layout.maximumWidth: 22; color: cWarningFg; font.bold: true; font.pixelSize: pxLarge; font.family: fontMono }
                }
            }

            // ----- 6. Focus + clock (two rows) -----
            Rectangle {
                id: kbdBlock
                Layout.preferredWidth: 108
                Layout.minimumWidth: 108
                Layout.maximumWidth: 108
                Layout.fillHeight: true
                clip: true
                color: cCard
                border.color: cBorder
                radius: 6
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 2
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 4
                        Rectangle { Layout.preferredWidth: 8; Layout.preferredHeight: 8; Layout.minimumWidth: 8; Layout.maximumWidth: 8; radius: 4; color: cSuccess }
                        Text { text: "Активен"; color: cSuccess; font.pixelSize: 11; font.weight: Font.Medium; Layout.alignment: Qt.AlignVCenter }
                    }
                    Text {
                        text: "--:--:--"
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        color: cForeground
                        font.bold: true
                        font.pixelSize: pxLarge
                        font.family: fontMono
                        elide: Text.ElideRight
                    }
                }
            }
        }   // headerRow

        // ---------------------------------------------------------------- //
        // STAGE 2 — SCREEN TABS (ScreenTabs)                                //
        // React: <nav class="grid grid-cols-6 gap-2"> h-11 rounded-md        //
        // ---------------------------------------------------------------- //
        RowLayout {
            id: tabsRow
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            Layout.minimumHeight: 36
            Layout.maximumHeight: 36
            spacing: 6

            // ----- Tab 1: Position -----
            Rectangle {
                id: tab0
                property bool on: root.activeScreen === "pos"
                property bool locked: root.tabLocked("pos")
                opacity: locked ? 0.4 : 1.0
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 150
                clip: true
                color: cCard
                border.color: cBorder
                radius: 6
                state: on ? "active" : "idle"
                states: [
                    State { name: "idle" },
                    State {
                        name: "active"
                        PropertyChanges { target: tab0; color: cPrimary; border.color: cPrimary }
                    }
                ]
                transitions: Transition { ColorAnimation { properties: "color,border.color"; duration: 150 } }
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 4
                    Rectangle {
                        Layout.preferredWidth: 20; Layout.preferredHeight: 20
                        Layout.minimumWidth: 20; Layout.maximumWidth: 20
                        Layout.minimumHeight: 20; Layout.maximumHeight: 20
                        color: tab0.on ? cPrimaryFg : cCard
                        border.color: tab0.on ? cPrimaryFg : cBorderMuted
                        radius: 4
                        Text { anchors.centerIn: parent; text: "1"; color: tab0.on ? cPrimary : cForeground; font.bold: true; font.pixelSize: 10 }
                    }
                    Text { text: "\ue121"; color: tab0.on ? cPrimaryFg : cForeground; font.family: "lucide"; font.pixelSize: 16 }
                    Text { text: "Позиция"; Layout.fillWidth: true; color: tab0.on ? cPrimaryFg : cForeground; font.bold: true; font.pixelSize: 12; font.strikeout: tab0.locked; elide: Text.ElideRight }
                }
                MouseArea { anchors.fill: parent; enabled: !tab0.locked; onClicked: root.activeScreen = "pos" }
            }

            // ----- Tab 2: Program -----
            Rectangle {
                id: tab1
                property bool on: root.activeScreen === "prog"
                property bool locked: root.tabLocked("prog")
                opacity: locked ? 0.4 : 1.0
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 150
                clip: true
                color: cCard
                border.color: cBorder
                radius: 6
                state: on ? "active" : "idle"
                states: [
                    State { name: "idle" },
                    State {
                        name: "active"
                        PropertyChanges { target: tab1; color: cPrimary; border.color: cPrimary }
                    }
                ]
                transitions: Transition { ColorAnimation { properties: "color,border.color"; duration: 150 } }
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 4
                    Rectangle {
                        Layout.preferredWidth: 20; Layout.preferredHeight: 20
                        Layout.minimumWidth: 20; Layout.maximumWidth: 20
                        Layout.minimumHeight: 20; Layout.maximumHeight: 20
                        color: tab1.on ? cPrimaryFg : cCard
                        border.color: tab1.on ? cPrimaryFg : cBorderMuted
                        radius: 4
                        Text { anchors.centerIn: parent; text: "2"; color: tab1.on ? cPrimary : cForeground; font.bold: true; font.pixelSize: 10 }
                    }
                    Text { text: "\ue0c3"; color: tab1.on ? cPrimaryFg : cForeground; font.family: "lucide"; font.pixelSize: 16 }
                    Text { text: "Программа"; Layout.fillWidth: true; color: tab1.on ? cPrimaryFg : cForeground; font.bold: true; font.pixelSize: 12; font.strikeout: tab1.locked; elide: Text.ElideRight }
                }
                MouseArea { anchors.fill: parent; enabled: !tab1.locked; onClicked: root.activeScreen = "prog" }
            }

            // ----- Tab 3: Offsets -----
            Rectangle {
                id: tab2
                property bool on: root.activeScreen === "offset"
                property bool locked: root.tabLocked("offset")
                opacity: locked ? 0.4 : 1.0
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 150
                clip: true
                color: cCard
                border.color: cBorder
                radius: 6
                state: on ? "active" : "idle"
                states: [
                    State { name: "idle" },
                    State {
                        name: "active"
                        PropertyChanges { target: tab2; color: cPrimary; border.color: cPrimary }
                    }
                ]
                transitions: Transition { ColorAnimation { properties: "color,border.color"; duration: 150 } }
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 4
                    Rectangle {
                        Layout.preferredWidth: 20; Layout.preferredHeight: 20
                        Layout.minimumWidth: 20; Layout.maximumWidth: 20
                        Layout.minimumHeight: 20; Layout.maximumHeight: 20
                        color: tab2.on ? cPrimaryFg : cCard
                        border.color: tab2.on ? cPrimaryFg : cBorderMuted
                        radius: 4
                        Text { anchors.centerIn: parent; text: "3"; color: tab2.on ? cPrimary : cForeground; font.bold: true; font.pixelSize: 10 }
                    }
                    Text { text: "\ue0ac"; color: tab2.on ? cPrimaryFg : cForeground; font.family: "lucide"; font.pixelSize: 16 }
                    Text { text: "Смещения"; Layout.fillWidth: true; color: tab2.on ? cPrimaryFg : cForeground; font.bold: true; font.pixelSize: 12; font.strikeout: tab2.locked; elide: Text.ElideRight }
                }
                MouseArea { anchors.fill: parent; enabled: !tab2.locked; onClicked: root.activeScreen = "offset" }
            }

            // ----- Tab 4: Tool -----
            Rectangle {
                id: tab3
                property bool on: root.activeScreen === "tool"
                property bool locked: root.tabLocked("tool")
                opacity: locked ? 0.4 : 1.0
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 150
                clip: true
                color: cCard
                border.color: cBorder
                radius: 6
                state: on ? "active" : "idle"
                states: [
                    State { name: "idle" },
                    State {
                        name: "active"
                        PropertyChanges { target: tab3; color: cPrimary; border.color: cPrimary }
                    }
                ]
                transitions: Transition { ColorAnimation { properties: "color,border.color"; duration: 150 } }
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 4
                    Rectangle {
                        Layout.preferredWidth: 20; Layout.preferredHeight: 20
                        Layout.minimumWidth: 20; Layout.maximumWidth: 20
                        Layout.minimumHeight: 20; Layout.maximumHeight: 20
                        color: tab3.on ? cPrimaryFg : cCard
                        border.color: tab3.on ? cPrimaryFg : cBorderMuted
                        radius: 4
                        Text { anchors.centerIn: parent; text: "4"; color: tab3.on ? cPrimary : cForeground; font.bold: true; font.pixelSize: 10 }
                    }
                    Text { text: "\ue1b1"; color: tab3.on ? cPrimaryFg : cForeground; font.family: "lucide"; font.pixelSize: 16 }
                    Text { text: "Инструмент"; Layout.fillWidth: true; color: tab3.on ? cPrimaryFg : cForeground; font.bold: true; font.pixelSize: 12; font.strikeout: tab3.locked; elide: Text.ElideRight }
                }
                MouseArea { anchors.fill: parent; enabled: !tab3.locked; onClicked: root.activeScreen = "tool" }
            }

            // ----- Tab 5: Messages -----
            Rectangle {
                id: tab4
                property bool on: root.activeScreen === "alarm"
                property bool locked: root.tabLocked("alarm")
                opacity: locked ? 0.4 : 1.0
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 150
                clip: true
                color: cCard
                border.color: cBorder
                radius: 6
                state: on ? "active" : "idle"
                states: [
                    State { name: "idle" },
                    State {
                        name: "active"
                        PropertyChanges { target: tab4; color: cPrimary; border.color: cPrimary }
                    }
                ]
                transitions: Transition { ColorAnimation { properties: "color,border.color"; duration: 150 } }
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 4
                    Rectangle {
                        Layout.preferredWidth: 20; Layout.preferredHeight: 20
                        Layout.minimumWidth: 20; Layout.maximumWidth: 20
                        Layout.minimumHeight: 20; Layout.maximumHeight: 20
                        color: tab4.on ? cPrimaryFg : cCard
                        border.color: tab4.on ? cPrimaryFg : cBorderMuted
                        radius: 4
                        Text { anchors.centerIn: parent; text: "5"; color: tab4.on ? cPrimary : cForeground; font.bold: true; font.pixelSize: 10 }
                    }
                    Text { text: "\ue193"; color: tab4.on ? cPrimaryFg : cForeground; font.family: "lucide"; font.pixelSize: 16 }
                    Text { text: "Сообщения"; Layout.fillWidth: true; color: tab4.on ? cPrimaryFg : cForeground; font.bold: true; font.pixelSize: 12; font.strikeout: tab4.locked; elide: Text.ElideRight }
                    Rectangle {
                        visible: root.activeAlarmCount() > 0
                        Layout.preferredWidth: 20; Layout.preferredHeight: 18
                        Layout.minimumWidth: 20; Layout.maximumWidth: 20
                        Layout.minimumHeight: 18; Layout.maximumHeight: 18
                        color: tab4.on ? cPrimaryFg : cWarning; radius: 4
                        Text { anchors.centerIn: parent; text: String(root.activeAlarmCount()); color: tab4.on ? cPrimary : cWarningFg; font.bold: true; font.pixelSize: 11 }
                    }
                }
                MouseArea { anchors.fill: parent; enabled: !tab4.locked; onClicked: root.activeScreen = "alarm" }
            }

            // ----- Tab 6: Service -----
            Rectangle {
                id: tab5
                property bool on: root.activeScreen === "service"
                property bool locked: root.tabLocked("service")
                opacity: locked ? 0.4 : 1.0
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 150
                clip: true
                color: cCard
                border.color: cBorder
                radius: 6
                state: on ? "active" : "idle"
                states: [
                    State { name: "idle" },
                    State {
                        name: "active"
                        PropertyChanges { target: tab5; color: cPrimary; border.color: cPrimary }
                    }
                ]
                transitions: Transition { ColorAnimation { properties: "color,border.color"; duration: 150 } }
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 4
                    Rectangle {
                        Layout.preferredWidth: 20; Layout.preferredHeight: 20
                        Layout.minimumWidth: 20; Layout.maximumWidth: 20
                        Layout.minimumHeight: 20; Layout.maximumHeight: 20
                        color: tab5.on ? cPrimaryFg : cCard
                        border.color: tab5.on ? cPrimaryFg : cBorderMuted
                        radius: 4
                        Text { anchors.centerIn: parent; text: "6"; color: tab5.on ? cPrimary : cForeground; font.bold: true; font.pixelSize: 10 }
                    }
                    Text { text: "\ue154"; color: tab5.on ? cPrimaryFg : cForeground; font.family: "lucide"; font.pixelSize: 16 }
                    Text { text: "Сервис"; Layout.fillWidth: true; color: tab5.on ? cPrimaryFg : cForeground; font.bold: true; font.pixelSize: 12; font.strikeout: tab5.locked; elide: Text.ElideRight }
                }
                MouseArea { anchors.fill: parent; enabled: !tab5.locked; onClicked: root.activeScreen = "service" }
            }
            // ----- Tab 7: 3D -----
            Rectangle {
                id: tab6
                property bool on: root.activeScreen === "view"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 150
                clip: true
                color: cCard
                border.color: cBorder
                radius: 6
                state: on ? "active" : "idle"
                states: [
                    State { name: "idle" },
                    State {
                        name: "active"
                        PropertyChanges { target: tab6; color: cPrimary; border.color: cPrimary }
                    }
                ]
                transitions: Transition { ColorAnimation { properties: "color,border.color"; duration: 150 } }
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 4
                    Rectangle {
                        Layout.preferredWidth: 20; Layout.preferredHeight: 20
                        Layout.minimumWidth: 20; Layout.maximumWidth: 20
                        Layout.minimumHeight: 20; Layout.maximumHeight: 20
                        color: tab6.on ? cPrimaryFg : cCard
                        border.color: tab6.on ? cPrimaryFg : cBorderMuted
                        radius: 4
                        Text { anchors.centerIn: parent; text: "7"; color: tab6.on ? cPrimary : cForeground; font.bold: true; font.pixelSize: 10 }
                    }
                    Text { text: "\ue53e"; color: tab6.on ? cPrimaryFg : cForeground; font.family: "lucide"; font.pixelSize: 16 }
                    Text { text: "3D"; Layout.fillWidth: true; color: tab6.on ? cPrimaryFg : cForeground; font.bold: true; font.pixelSize: 12; elide: Text.ElideRight }
                }
                MouseArea { anchors.fill: parent; onClicked: root.activeScreen = "view" }
            }
        }   // tabsRow

        // ============ CONTENT ROW: POSITION SCREEN + SIDE STATUS ==========
        RowLayout {
            id: contentRow
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 6

            // ============ SCREENS STACK (screen switching in QML) ============
            StackLayout {
                id: screensStack
                Layout.fillWidth: true
                Layout.fillHeight: true
                // Indices: 0 = Position, 1 = Program
                currentIndex: root.activeScreen === "prog" ? 1
                            : root.activeScreen === "offset" ? 2
                            : root.activeScreen === "tool" ? 3
                            : root.activeScreen === "alarm" ? 4
                            : root.activeScreen === "service" ? 5
                            : root.activeScreen === "view" ? 6 : 0

            // ============ STAGE 3 — POSITION SCREEN (Axis position) ==========
            //      Single table: Axis | Work (G54) | Machine | Remaining | Rel.
            ColumnLayout {
                id: posWrap
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 6

            Rectangle {
                id: posScreen
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: cCard
                border.color: cBorder
            border.width: 1
            radius: 6
            clip: true

            ColumnLayout {
                anchors.fill: parent
                // Margin = frame thickness so the content does not overlap the border.
                anchors.topMargin: 1
                anchors.leftMargin: 1
                anchors.rightMargin: 1
                anchors.bottomMargin: 6
                anchors.margins: 0
                spacing: 0

                // ---------- Panel header (Axis position) ----------
// Direct child of the outer ColumnLayout, stretched to the full panel width.
// radius: 6 + clip: true — rounds the TOP corners of the header (the strip covers the bottom).
                Rectangle {
                    id: posHeader
                    Layout.fillWidth: true
                    Layout.preferredHeight: 28
                    Layout.minimumHeight: 28
                    Layout.maximumHeight: 28
                    color: cMuted
                    radius: 6
                    clip: true
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        anchors.topMargin: 0
                        anchors.bottomMargin: 0
                        Layout.alignment: Qt.AlignVCenter
                        spacing: 10

                        // icon: move \ue121
                        Text { text: "\ue121"; color: cMutedForeground; font.family: "lucide"; font.pixelSize: 16; Layout.alignment: Qt.AlignVCenter }
                        Text {
                            text: "ПОЛОЖЕНИЕ ОСЕЙ"
                            color: cMutedForeground
                            font.bold: true
                            font.pixelSize: 13
                            elide: Text.ElideRight
                        }
                        // spacer pushes right group to the edge
                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; Layout.minimumHeight: 1; Layout.maximumHeight: 1; color: "transparent" }
                        // right side: "Toolpath hidden [P]" + "G54 · mm"
                        Text {
                            visible: root.machineMode !== "MDI"
                            text: "Траектория скрыта"
                            color: cMutedForeground
                            font.pixelSize: 11
                            elide: Text.ElideRight
                            Layout.alignment: Qt.AlignVCenter
                        }
                        Rectangle {   // keycap P
                            Layout.preferredWidth: 22
                            Layout.preferredHeight: 22
                            Layout.minimumWidth: 22
                            Layout.maximumWidth: 22
                            Layout.minimumHeight: 22
                            Layout.maximumHeight: 22
                            color: cCard
                            border.color: cBorderMuted
                            radius: 4
                            Text { anchors.centerIn: parent; text: "P"; color: cMutedForeground; font.bold: true; font.pixelSize: 11 }
                        }
                        Text {
                            text: root.activeOffsetCode() + " · мм"
                            Layout.minimumWidth: 70
                            Layout.maximumWidth: 70
                            color: cForeground
                            font.bold: true
                            font.pixelSize: 12
                            font.family: fontMono
                            elide: Text.ElideRight
                        }
                    }
                }

                // ---------- Straight strip: hides bottom rounding of header ----------
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 4
                    Layout.minimumHeight: 4
                    Layout.maximumHeight: 4
                    color: cMuted
                }

                // ---------- Divider under panel title ----------
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    Layout.minimumHeight: 1
                    Layout.maximumHeight: 1
                    color: cBorder
                }

                // ---------- Column header row (5 cols) ----------
                // White card (cCard), like thead on bg-card in React.
                Rectangle {
                    id: axesHeader
                    Layout.fillWidth: true
                    Layout.preferredHeight: 30
                    Layout.minimumHeight: 30
                    Layout.maximumHeight: 30
                    color: cCard
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 4
                        // Columns: Axis | Work (G54) | Machine | Remaining | Rel.
                        Text {
                            text: "ОСЬ"
                            Layout.minimumWidth: 56; Layout.maximumWidth: 56; Layout.preferredWidth: 56
                            color: cMutedForeground; font.bold: true; font.pixelSize: 11; font.letterSpacing: 1
                            Layout.alignment: Qt.AlignVCenter
                        }
                        Text {
                            text: "РАБОЧИЕ (" + root.activeOffsetCode() + ")"
                            Layout.fillWidth: true
                            color: cMutedForeground; font.bold: true; font.pixelSize: 11; font.letterSpacing: 1
                            Layout.alignment: Qt.AlignVCenter
                        }
                        Text {
                            text: "МАШИННЫЕ"
                            Layout.minimumWidth: 110; Layout.maximumWidth: 110; Layout.preferredWidth: 110
                            color: cMutedForeground; font.bold: true; font.pixelSize: 11; font.letterSpacing: 1
                            horizontalAlignment: Text.AlignRight
                            Layout.alignment: Qt.AlignVCenter
                        }
                        Text {
                            text: "ОСТАТОК"
                            Layout.minimumWidth: 110; Layout.maximumWidth: 110; Layout.preferredWidth: 110
                            color: cMutedForeground; font.bold: true; font.pixelSize: 11; font.letterSpacing: 1
                            horizontalAlignment: Text.AlignRight
                            Layout.alignment: Qt.AlignVCenter
                        }
                        Text {
                            text: "ОТН."
                            Layout.minimumWidth: 90; Layout.maximumWidth: 90; Layout.preferredWidth: 90
                            color: cMutedForeground; font.bold: true; font.pixelSize: 11; font.letterSpacing: 1
                            horizontalAlignment: Text.AlignRight
                            Layout.alignment: Qt.AlignVCenter
                        }
                    }
                }

                // ---------- Divider ----------
                Rectangle { id: div1; Layout.fillWidth: true; Layout.preferredHeight: 1; Layout.minimumHeight: 1; Layout.maximumHeight: 1; color: cBorder }

                // ---------- Row X (active) ----------
                Rectangle {
                    id: rowX
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: root.axisSel("X") ? cAccent : cCard
                    border.color: "transparent"
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 4
                        // left accent bar + axis label
                        RowLayout {
                            Layout.minimumWidth: 78; Layout.maximumWidth: 78; Layout.preferredWidth: 78
                            Layout.fillHeight: true
                            spacing: 6
                            Rectangle { Layout.preferredWidth: 4; Layout.minimumWidth: 4; Layout.maximumWidth: 4; Layout.fillHeight: true; color: cPrimary; visible: root.axisSel("X") }
                            Text { text: "X"; color: root.axisSel("X") ? cPrimary : cForeground; font.bold: true; font.pixelSize: 36 }
                            Rectangle { Layout.preferredWidth: 20; Layout.preferredHeight: 18; Layout.minimumWidth: 20; Layout.maximumWidth: 20; Layout.minimumHeight: 18; Layout.maximumHeight: 18; color: cCard; border.color: cBorderMuted; radius: 3; opacity: (root.machineMode === "JOG" || root.machineMode === "MPG") ? 1.0 : 0.4
                                Text { anchors.centerIn: parent; text: "X"; color: cMutedForeground; font.bold: true; font.pixelSize: 10 } }
                        }
                        Text {
                            text: "−120.500"
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            color: cForeground; font.bold: true; font.pixelSize: root.machineMode === "MDI" ? 30 : 48; font.family: fontMono
                            horizontalAlignment: Text.AlignLeft
                        }
                        Text {
                            text: "+0.000"
                            Layout.minimumWidth: 110; Layout.maximumWidth: 110; Layout.preferredWidth: 110
                            Layout.alignment: Qt.AlignVCenter
                            color: cMutedForeground; font.bold: true; font.pixelSize: 20; font.family: fontMono
                            horizontalAlignment: Text.AlignRight
                        }
                        Text {
                            text: "+0.000"
                            Layout.minimumWidth: 110; Layout.maximumWidth: 110; Layout.preferredWidth: 110
                            Layout.alignment: Qt.AlignVCenter
                            color: cPrimary; font.bold: true; font.pixelSize: 20; font.family: fontMono
                            horizontalAlignment: Text.AlignRight
                        }
                        Text {
                            text: "+0.000"
                            Layout.minimumWidth: 90; Layout.maximumWidth: 90; Layout.preferredWidth: 90
                            Layout.alignment: Qt.AlignVCenter
                            color: cMutedForeground; font.bold: true; font.pixelSize: 20; font.family: fontMono
                            horizontalAlignment: Text.AlignRight
                        }
                    }
                }

                Rectangle { id: div2; Layout.fillWidth: true; Layout.preferredHeight: 1; Layout.minimumHeight: 1; Layout.maximumHeight: 1; color: cBorder }

                // ---------- Row Y ----------
                Rectangle {
                    id: rowY
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: root.axisSel("Y") ? cAccent : cCard
                    border.color: "transparent"
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 4
                        RowLayout {
                            Layout.minimumWidth: 78; Layout.maximumWidth: 78; Layout.preferredWidth: 78
                            Layout.fillHeight: true
                            spacing: 6
                            Rectangle { Layout.preferredWidth: 4; Layout.minimumWidth: 4; Layout.maximumWidth: 4; Layout.fillHeight: true; color: cPrimary; visible: root.axisSel("Y") }
                            Text { text: "Y"; color: root.axisSel("Y") ? cPrimary : cForeground; font.bold: true; font.pixelSize: 36 }
                            Rectangle { Layout.preferredWidth: 20; Layout.preferredHeight: 18; Layout.minimumWidth: 20; Layout.maximumWidth: 20; Layout.minimumHeight: 18; Layout.maximumHeight: 18; color: cCard; border.color: cBorderMuted; radius: 3; opacity: (root.machineMode === "JOG" || root.machineMode === "MPG") ? 1.0 : 0.4
                                Text { anchors.centerIn: parent; text: "Y"; color: cMutedForeground; font.bold: true; font.pixelSize: 10 } }
                        }
                        Text {
                            text: "+35.250"
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            color: cForeground; font.bold: true; font.pixelSize: root.machineMode === "MDI" ? 30 : 48; font.family: fontMono
                            horizontalAlignment: Text.AlignLeft
                        }
                        Text {
                            text: "+0.000"
                            Layout.minimumWidth: 110; Layout.maximumWidth: 110; Layout.preferredWidth: 110
                            Layout.alignment: Qt.AlignVCenter
                            color: cMutedForeground; font.bold: true; font.pixelSize: 20; font.family: fontMono
                            horizontalAlignment: Text.AlignRight
                        }
                        Text {
                            text: "+0.000"
                            Layout.minimumWidth: 110; Layout.maximumWidth: 110; Layout.preferredWidth: 110
                            Layout.alignment: Qt.AlignVCenter
                            color: cMutedForeground; font.bold: true; font.pixelSize: 20; font.family: fontMono
                            horizontalAlignment: Text.AlignRight
                        }
                        Text {
                            text: "+0.000"
                            Layout.minimumWidth: 90; Layout.maximumWidth: 90; Layout.preferredWidth: 90
                            Layout.alignment: Qt.AlignVCenter
                            color: cMutedForeground; font.bold: true; font.pixelSize: 20; font.family: fontMono
                            horizontalAlignment: Text.AlignRight
                        }
                    }
                }

                Rectangle { id: div3; Layout.fillWidth: true; Layout.preferredHeight: 1; Layout.minimumHeight: 1; Layout.maximumHeight: 1; color: cBorder }

                // ---------- Row Z ----------
                Rectangle {
                    id: rowZ
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: root.axisSel("Z") ? cAccent : cCard
                    border.color: "transparent"
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 4
                        RowLayout {
                            Layout.minimumWidth: 78; Layout.maximumWidth: 78; Layout.preferredWidth: 78
                            Layout.fillHeight: true
                            spacing: 6
                            Rectangle { Layout.preferredWidth: 4; Layout.minimumWidth: 4; Layout.maximumWidth: 4; Layout.fillHeight: true; color: cPrimary; visible: root.axisSel("Z") }
                            Text { text: "Z"; color: root.axisSel("Z") ? cPrimary : cForeground; font.bold: true; font.pixelSize: 36 }
                            Rectangle { Layout.preferredWidth: 20; Layout.preferredHeight: 18; Layout.minimumWidth: 20; Layout.maximumWidth: 20; Layout.minimumHeight: 18; Layout.maximumHeight: 18; color: cCard; border.color: cBorderMuted; radius: 3; opacity: (root.machineMode === "JOG" || root.machineMode === "MPG") ? 1.0 : 0.4
                                Text { anchors.centerIn: parent; text: "Z"; color: cMutedForeground; font.bold: true; font.pixelSize: 10 } }
                        }
                        Text {
                            text: "−210.400"
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            color: cForeground; font.bold: true; font.pixelSize: root.machineMode === "MDI" ? 30 : 48; font.family: fontMono
                            horizontalAlignment: Text.AlignLeft
                        }
                        Text {
                            text: "+0.000"
                            Layout.minimumWidth: 110; Layout.maximumWidth: 110; Layout.preferredWidth: 110
                            Layout.alignment: Qt.AlignVCenter
                            color: cMutedForeground; font.bold: true; font.pixelSize: 20; font.family: fontMono
                            horizontalAlignment: Text.AlignRight
                        }
                        Text {
                            text: "+0.000"
                            Layout.minimumWidth: 110; Layout.maximumWidth: 110; Layout.preferredWidth: 110
                            Layout.alignment: Qt.AlignVCenter
                            color: cMutedForeground; font.bold: true; font.pixelSize: 20; font.family: fontMono
                            horizontalAlignment: Text.AlignRight
                        }
                        Text {
                            text: "+0.000"
                            Layout.minimumWidth: 90; Layout.maximumWidth: 90; Layout.preferredWidth: 90
                            Layout.alignment: Qt.AlignVCenter
                            color: cMutedForeground; font.bold: true; font.pixelSize: 20; font.family: fontMono
                            horizontalAlignment: Text.AlignRight
                        }
                    }
                }

                Rectangle { id: div4; Layout.fillWidth: true; Layout.preferredHeight: 1; Layout.minimumHeight: 1; Layout.maximumHeight: 1; color: cBorder }

                // ---------- Row A ----------
                Rectangle {
                    id: rowA
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: root.axisSel("A") ? cAccent : cCard
                    border.color: "transparent"
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 4
                        RowLayout {
                            Layout.minimumWidth: 78; Layout.maximumWidth: 78; Layout.preferredWidth: 78
                            Layout.fillHeight: true
                            spacing: 6
                            Rectangle { Layout.preferredWidth: 4; Layout.minimumWidth: 4; Layout.maximumWidth: 4; Layout.fillHeight: true; color: cPrimary; visible: root.axisSel("A") }
                            Text { text: "A"; color: root.axisSel("A") ? cPrimary : cForeground; font.bold: true; font.pixelSize: 36 }
                            Rectangle { Layout.preferredWidth: 20; Layout.preferredHeight: 18; Layout.minimumWidth: 20; Layout.maximumWidth: 20; Layout.minimumHeight: 18; Layout.maximumHeight: 18; color: cCard; border.color: cBorderMuted; radius: 3; opacity: (root.machineMode === "JOG" || root.machineMode === "MPG") ? 1.0 : 0.4
                                Text { anchors.centerIn: parent; text: "A"; color: cMutedForeground; font.bold: true; font.pixelSize: 10 } }
                        }
                        Text {
                            text: "+0.000°"
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            color: cForeground; font.bold: true; font.pixelSize: root.machineMode === "MDI" ? 30 : 48; font.family: fontMono
                            horizontalAlignment: Text.AlignLeft
                        }
                        Text {
                            text: "+0.000"
                            Layout.minimumWidth: 110; Layout.maximumWidth: 110; Layout.preferredWidth: 110
                            Layout.alignment: Qt.AlignVCenter
                            color: cMutedForeground; font.bold: true; font.pixelSize: 20; font.family: fontMono
                            horizontalAlignment: Text.AlignRight
                        }
                        Text {
                            text: "+0.000"
                            Layout.minimumWidth: 110; Layout.maximumWidth: 110; Layout.preferredWidth: 110
                            Layout.alignment: Qt.AlignVCenter
                            color: cMutedForeground; font.bold: true; font.pixelSize: 20; font.family: fontMono
                            horizontalAlignment: Text.AlignRight
                        }
                        Text {
                            text: "+0.000"
                            Layout.minimumWidth: 90; Layout.maximumWidth: 90; Layout.preferredWidth: 90
                            Layout.alignment: Qt.AlignVCenter
                            color: cMutedForeground; font.bold: true; font.pixelSize: 20; font.family: fontMono
                            horizontalAlignment: Text.AlignRight
                        }
                    }
                }


            }
        }   // posScreen

                // ===== MDI input (MDI mode) =====
                Rectangle {
                    id: mdiPanel
                    visible: root.machineMode === "MDI"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 132; Layout.minimumHeight: 132; Layout.maximumHeight: 132
                    color: cCard; border.color: cPrimary; border.width: 2; radius: 6; clip: true
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 10; spacing: 6
                        RowLayout {
                            Layout.fillWidth: true; spacing: 6
                            Text { text: "\ue284"; color: cMutedForeground; font.family: "lucide"; font.pixelSize: 13 }
                            Text { text: "ВВОД С КЛАВИАТУРЫ СТОЙКИ"; color: cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 0.5; Layout.fillWidth: true }
                            Text { text: "Ожидание кадра"; color: cMutedForeground; font.pixelSize: 10 }
                        }
                        Rectangle {
                            Layout.fillWidth: true; Layout.preferredHeight: 46; Layout.minimumHeight: 46; Layout.maximumHeight: 46
                            color: cMuted; radius: 4
                            RowLayout {
                                anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 8
                                Text { text: "MDI>"; color: cPrimary; font.bold: true; font.pixelSize: 18; font.family: fontMono }
                                Text { text: "G0 X0 Y0"; Layout.fillWidth: true; color: cForeground; font.bold: true; font.pixelSize: 22; font.family: fontMono; elide: Text.ElideRight; Layout.alignment: Qt.AlignVCenter }
                                Rectangle { Layout.preferredWidth: 3; Layout.preferredHeight: 22; color: cPrimary }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 4
                            Repeater {
                                model: ["G0 X0 Y0", "G0 Z50", "S1500 M3", "M5", "G1 X10 F300", "G28"]
                                delegate: Rectangle {
                                    Layout.preferredHeight: 22; Layout.minimumHeight: 22; Layout.maximumHeight: 22
                                    width: exT.implicitWidth + 14; color: cCard; border.color: cBorderMuted; radius: 3
                                    Text { id: exT; anchors.centerIn: parent; text: modelData; color: cMutedForeground; font.pixelSize: 10; font.family: fontMono }
                                }
                            }
                            Item { Layout.fillWidth: true }
                            Rectangle { Layout.preferredWidth: 90; Layout.preferredHeight: 26; Layout.minimumWidth: 90; Layout.maximumWidth: 90; Layout.minimumHeight: 26; Layout.maximumHeight: 26; color: cCard; border.color: cBorderMuted; radius: 4
                                Text { anchors.centerIn: parent; text: "Очистить"; color: cForeground; font.pixelSize: 11 } }
                            Rectangle { Layout.preferredWidth: 90; Layout.preferredHeight: 26; Layout.minimumWidth: 90; Layout.maximumWidth: 90; Layout.minimumHeight: 26; Layout.maximumHeight: 26; color: cPrimary; radius: 4
                                Text { anchors.centerIn: parent; text: "ПУСК"; color: cPrimaryFg; font.bold: true; font.pixelSize: 11 } }
                        }
                    }
                }
            }   // posWrap

            // ============ STAGE 6 — PROGRAM SCREEN ============
            ColumnLayout {
                id: progScreen
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 6

                // ----- Part run (AUTO, v0.5.1) — separate card -----
                Rectangle {
                    id: autoProgressBar
                    visible: root.machineMode === "AUTO"
                    Layout.fillWidth: true; Layout.preferredHeight: 40; Layout.minimumHeight: 40; Layout.maximumHeight: 40
                    color: cCard; border.color: cBorder; border.width: 1; radius: 6
                    RowLayout { anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 12
                        ColumnLayout { Layout.preferredWidth: 150; Layout.minimumWidth: 150; Layout.maximumWidth: 150; spacing: 0
                            Text { text: "ВЫПОЛНЕНИЕ ДЕТАЛИ"; color: cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 1 }
                            Text { text: "Ожидание пуска"; color: cForeground; font.bold: true; font.pixelSize: 13 } }
                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 18; Layout.minimumHeight: 18; Layout.maximumHeight: 18; color: cMuted; radius: 3
                            Rectangle { width: parent.width * (root.cursorProg / Math.max(1, root.progLines.length - 1)); height: parent.height; color: cPrimary; radius: 3 } }
                        Text { text: (root.progLines.length > 0 ? "1/" + root.progLines.length : "—"); color: cMutedForeground; font.pixelSize: 11; font.family: fontMono }
                        Text { text: Math.round(root.cursorProg / Math.max(1, root.progLines.length - 1) * 100) + "%"; Layout.minimumWidth: 66; Layout.maximumWidth: 66; color: cForeground; font.bold: true; font.pixelSize: 26; font.family: fontMono; horizontalAlignment: Text.AlignRight }
                    }
                }

                // ----- "Program" panel -----
                Rectangle {
                    id: progPanel
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: cCard
                    border.color: cBorder
                    border.width: 1
                    radius: 6
                    clip: true

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.topMargin: 1; anchors.leftMargin: 1; anchors.rightMargin: 1; anchors.bottomMargin: 6
                        spacing: 0

                        // ----- header -----
                        Rectangle {
                            id: progHeader
                            Layout.fillWidth: true
                            Layout.preferredHeight: 32
                            Layout.minimumHeight: 32
                            Layout.maximumHeight: 32
                            color: cMuted
                            radius: 6
                            clip: true
                            Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 6; color: cMuted }
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                spacing: 8
                                Text { text: "\ue0c3"; color: cMutedForeground; font.family: "lucide"; font.pixelSize: 16 }
                                Text { text: "ПРОГРАММА  O1024 · КОРПУС ФЛАНЦА"; Layout.fillWidth: true; color: cMutedForeground; font.bold: true; font.pixelSize: 11; font.letterSpacing: 0.5; elide: Text.ElideRight }
                                Rectangle { Layout.preferredWidth: 10; Layout.preferredHeight: 10; Layout.minimumWidth: 10; Layout.maximumWidth: 10; radius: 5; border.color: "#2634363a"; border.width: 1; color: root.singleBlock ? cWarning : cMuted }
                                Text { text: "Покадр."; color: root.singleBlock ? cForeground : cMutedForeground; font.pixelSize: 11 }
                                Rectangle { Layout.preferredWidth: 10; Layout.preferredHeight: 10; Layout.minimumWidth: 10; Layout.maximumWidth: 10; radius: 5; border.color: "#2634363a"; border.width: 1; color: root.optStop ? cPrimary : cMuted }
                                Text { text: "M01"; color: root.optStop ? cForeground : cMutedForeground; font.pixelSize: 11 }
                                Rectangle { Layout.preferredWidth: 10; Layout.preferredHeight: 10; Layout.minimumWidth: 10; Layout.maximumWidth: 10; radius: 5; border.color: "#2634363a"; border.width: 1; color: root.dryRun ? cWarning : cMuted }
                                Text { text: "Холост."; color: root.dryRun ? cForeground : cMutedForeground; font.pixelSize: 11 }
                            }
                        }

                        // ----- divider -----
                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; Layout.minimumHeight: 1; Layout.maximumHeight: 1; color: cBorder }

                        // ----- program lines -----
                        Column {
                            id: progList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.topMargin: 2
                            spacing: 0
                            clip: true

                            Repeater {
                                model: root.progLines
                                delegate: Rectangle {
                                    width: progList.width
                                    height: 20
                                    color: modelData.cur ? cAccent : cCard
                                    Rectangle { anchors.fill: parent; color: "transparent"; border.color: "#6634363a"; border.width: 2; visible: index === root.cursorProg }
                                    Rectangle { x: 0; y: 0; width: 4; height: parent.height; color: cPrimary; visible: modelData.cur }
                                    Row {
                                        anchors.left: parent.left
                                        anchors.leftMargin: 8
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: 8
                                        Text { width: 32; text: modelData.num; color: cMutedForeground; font.pixelSize: 12; font.family: fontMono; horizontalAlignment: Text.AlignRight }
                                        Text { width: 16; text: modelData.cur ? "\u25B6" : ""; color: cPrimary; font.pixelSize: 11 }
                                        Repeater {
                                            model: modelData.toks
                                            delegate: Text {
                                                text: modelData.t + " "
                                                color: modelData.c === "primary" ? cPrimary
                                                     : modelData.c === "destructive" ? cDestructive
                                                     : modelData.c === "success" ? cSuccess
                                                     : modelData.c === "muted" ? cMutedForeground
                                                     : modelData.c === "comment" ? cMutedForeground
                                                     : cForeground
                                                font.family: fontMono
                                                font.pixelSize: 14
                                                font.italic: modelData.c === "comment"
                                                font.bold: (modelData.c === "primary" || modelData.c === "destructive")
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

                    // ===== "Offsets" screen =====
                    Rectangle {
                        id: offsetScreen
                        Layout.fillWidth: true; Layout.fillHeight: true
                        color: cCard; border.color: cBorder; border.width: 1; radius: 6; clip: true
                        ColumnLayout {
                            visible: !root.probeOpen
                            anchors.fill: parent; anchors.topMargin: 1; anchors.leftMargin: 1; anchors.rightMargin: 1; anchors.bottomMargin: 6; spacing: 0
                            Rectangle {
                                id: offsetHeader
                                Layout.fillWidth: true; Layout.preferredHeight: 32; Layout.minimumHeight: 32; Layout.maximumHeight: 32
                                color: cMuted; radius: 6; clip: true
                                Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 6; color: cMuted }
                                RowLayout {
                                    anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 6
                                    Text { text: "\ue0ac"; color: cMutedForeground; font.family: "lucide"; font.pixelSize: 14 }
                                    Text { text: "РАБОЧИЕ СМЕЩЕНИЯ"; color: cMutedForeground; font.bold: true; font.pixelSize: 12; font.letterSpacing: 0.5; Layout.alignment: Qt.AlignVCenter }
                                    Text { text: "Привязка записывает машинную координату"; Layout.fillWidth: true; color: cMutedForeground; font.pixelSize: 11; horizontalAlignment: Text.AlignRight; elide: Text.ElideRight; Layout.alignment: Qt.AlignVCenter }
                                }
                            }
                            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; Layout.minimumHeight: 1; Layout.maximumHeight: 1; color: cBorder }
                            Rectangle {
                                Layout.fillWidth: true; Layout.preferredHeight: 26; Layout.minimumHeight: 26; Layout.maximumHeight: 26; color: cCard
                                RowLayout {
                                    anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 6
                                    Text { text: "КОД"; Layout.minimumWidth: 80; Layout.maximumWidth: 80; Layout.preferredWidth: 80; color: cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 0.5 }
                                    Text { text: "X"; Layout.minimumWidth: 195; Layout.maximumWidth: 195; Layout.preferredWidth: 195; color: cMutedForeground; font.bold: true; font.pixelSize: 10; horizontalAlignment: Text.AlignRight }
                                    Text { text: "Y"; Layout.minimumWidth: 195; Layout.maximumWidth: 195; Layout.preferredWidth: 195; color: cMutedForeground; font.bold: true; font.pixelSize: 10; horizontalAlignment: Text.AlignRight }
                                    Text { text: "Z"; Layout.minimumWidth: 195; Layout.maximumWidth: 195; Layout.preferredWidth: 195; color: cMutedForeground; font.bold: true; font.pixelSize: 10; horizontalAlignment: Text.AlignRight }
                                    Text { text: "A"; Layout.minimumWidth: 195; Layout.maximumWidth: 195; Layout.preferredWidth: 195; color: cMutedForeground; font.bold: true; font.pixelSize: 10; horizontalAlignment: Text.AlignRight }
                                    Text { text: "СТАТУС"; Layout.minimumWidth: 92; Layout.maximumWidth: 92; Layout.preferredWidth: 92; color: cMutedForeground; font.bold: true; font.pixelSize: 10; horizontalAlignment: Text.AlignRight }
                                }
                            }
                            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; Layout.minimumHeight: 1; Layout.maximumHeight: 1; color: cBorder }
                            Repeater {
                                model: root.offsetsData
                                delegate: Rectangle {
                                    property bool cursor: index === root.cursorOffset
                                    Layout.fillWidth: true; Layout.preferredHeight: 30; Layout.minimumHeight: 30; Layout.maximumHeight: 30
                                    color: cursor ? cAccent : cCard
                                    border.color: cursor ? cPrimary : "transparent"; border.width: cursor ? 2 : 0
                                    RowLayout {
                                        anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 6
                                        Text { text: modelData.code; Layout.minimumWidth: 80; Layout.maximumWidth: 80; Layout.preferredWidth: 80; color: cForeground; font.bold: true; font.pixelSize: 16; font.family: fontMono; Layout.alignment: Qt.AlignVCenter }
                                        Text { text: modelData.x; Layout.minimumWidth: 195; Layout.maximumWidth: 195; Layout.preferredWidth: 195; color: cForeground; font.pixelSize: 16; font.family: fontMono; horizontalAlignment: Text.AlignRight; Layout.alignment: Qt.AlignVCenter }
                                        Text { text: modelData.y; Layout.minimumWidth: 195; Layout.maximumWidth: 195; Layout.preferredWidth: 195; color: cForeground; font.pixelSize: 16; font.family: fontMono; horizontalAlignment: Text.AlignRight; Layout.alignment: Qt.AlignVCenter }
                                        Text { text: modelData.z; Layout.minimumWidth: 195; Layout.maximumWidth: 195; Layout.preferredWidth: 195; color: cForeground; font.pixelSize: 16; font.family: fontMono; horizontalAlignment: Text.AlignRight; Layout.alignment: Qt.AlignVCenter }
                                        Text { text: modelData.a; Layout.minimumWidth: 195; Layout.maximumWidth: 195; Layout.preferredWidth: 195; color: cForeground; font.pixelSize: 16; font.family: fontMono; horizontalAlignment: Text.AlignRight; Layout.alignment: Qt.AlignVCenter }
                                        Item {
                                            Layout.minimumWidth: 92; Layout.maximumWidth: 92; Layout.preferredWidth: 92; Layout.fillHeight: true
                                            Rectangle {
                                                anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                                                visible: modelData.active
                                                width: 80; height: 20; radius: 4; color: cPrimary
                                                Text { anchors.centerIn: parent; text: "АКТИВНО"; color: cPrimaryFg; font.bold: true; font.pixelSize: 10 }
                                            }
                                        }
                                    }
                                    Rectangle { anchors.left: parent.left; anchors.bottom: parent.bottom; width: parent.width; height: 1; color: cBorder }
                                }
                            }
                            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; Layout.minimumHeight: 1; Layout.maximumHeight: 1; color: cBorder }
                            Rectangle {
                                Layout.fillWidth: true; Layout.preferredHeight: 30; Layout.minimumHeight: 30; Layout.maximumHeight: 30; color: cMuted
                                RowLayout {
                                    anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 6
                                    Text { text: "МАШИНА"; Layout.minimumWidth: 80; Layout.maximumWidth: 80; Layout.preferredWidth: 80; color: cMutedForeground; font.bold: true; font.pixelSize: 10 }
                                    Text { text: "+0.000"; Layout.minimumWidth: 195; Layout.maximumWidth: 195; Layout.preferredWidth: 195; color: cMutedForeground; font.pixelSize: 13; font.family: fontMono; horizontalAlignment: Text.AlignRight }
                                    Text { text: "+0.000"; Layout.minimumWidth: 195; Layout.maximumWidth: 195; Layout.preferredWidth: 195; color: cMutedForeground; font.pixelSize: 13; font.family: fontMono; horizontalAlignment: Text.AlignRight }
                                    Text { text: "+0.000"; Layout.minimumWidth: 195; Layout.maximumWidth: 195; Layout.preferredWidth: 195; color: cMutedForeground; font.pixelSize: 13; font.family: fontMono; horizontalAlignment: Text.AlignRight }
                                    Text { text: "+0.000"; Layout.minimumWidth: 195; Layout.maximumWidth: 195; Layout.preferredWidth: 195; color: cMutedForeground; font.pixelSize: 13; font.family: fontMono; horizontalAlignment: Text.AlignRight }
                                    Item { Layout.minimumWidth: 92; Layout.maximumWidth: 92; Layout.preferredWidth: 92 }
                                }
                            }
                            Item { Layout.fillWidth: true; Layout.fillHeight: true }
                        }

                        // ===== 3D probe — wizard =====
                        ColumnLayout {
                            id: probeWizard
                            visible: root.probeOpen
                            anchors.fill: parent
                            anchors.topMargin: 1; anchors.leftMargin: 1; anchors.rightMargin: 1; anchors.bottomMargin: 6
                            spacing: 0
                            Rectangle {
                                Layout.fillWidth: true; Layout.preferredHeight: 32; Layout.minimumHeight: 32; Layout.maximumHeight: 32
                                color: cMuted; radius: 6; clip: true
                                Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 6; color: cMuted }
                                RowLayout { anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 8
                                    Text { text: "\ue1db"; color: cMutedForeground; font.family: "lucide"; font.pixelSize: 14 }
                                    Text { text: "3D ЩУП — ЗАМЕР ЗАГОТОВКИ"; color: cMutedForeground; font.bold: true; font.pixelSize: 12; font.letterSpacing: 0.5; Layout.fillWidth: true }
                                    Text { text: "Запись в"; color: cMutedForeground; font.pixelSize: 11 }
                                    Rectangle { Layout.preferredWidth: 44; Layout.preferredHeight: 18; Layout.minimumWidth: 44; Layout.maximumWidth: 44; Layout.minimumHeight: 18; Layout.maximumHeight: 18; color: cPrimary; radius: 4; Text { anchors.centerIn: parent; text: root.activeOffsetCode(); color: cPrimaryFg; font.bold: true; font.pixelSize: 11; font.family: fontMono } }
                                    Rectangle { Layout.preferredWidth: 22; Layout.preferredHeight: 18; Layout.minimumWidth: 22; Layout.maximumWidth: 22; Layout.minimumHeight: 18; Layout.maximumHeight: 18; color: cCard; border.color: cBorderMuted; radius: 3; Text { anchors.centerIn: parent; text: "\u2191\u2193"; color: cMutedForeground; font.bold: true; font.pixelSize: 10 } }
                                }
                            }
                            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; Layout.minimumHeight: 1; Layout.maximumHeight: 1; color: cBorder }
                            // stepper
                            Rectangle {
                                Layout.fillWidth: true; Layout.preferredHeight: 40; Layout.minimumHeight: 40; Layout.maximumHeight: 40; color: cMuted
                                RowLayout { anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 6
                                    Repeater {
                                        model: root.probeSteps
                                        delegate: RowLayout {
                                            spacing: 6
                                            Rectangle { Layout.preferredWidth: 22; Layout.preferredHeight: 22; Layout.minimumWidth: 22; Layout.maximumWidth: 22; Layout.minimumHeight: 22; Layout.maximumHeight: 22; radius: 11
                                                color: index < root.probeStep ? cSuccess : (index === root.probeStep ? cPrimary : cCard)
                                                border.color: index < root.probeStep ? cSuccess : (index === root.probeStep ? cPrimary : cBorder); border.width: 2
                                                Text { anchors.centerIn: parent; text: index < root.probeStep ? "\u2713" : (index + 1); color: (index <= root.probeStep) ? cPrimaryFg : cMutedForeground; font.bold: true; font.pixelSize: 11 } }
                                            Text { text: modelData; color: index === root.probeStep ? cForeground : (index < root.probeStep ? cForeground : cMutedForeground); font.bold: true; font.pixelSize: 11 }
                                            Rectangle { visible: index < root.probeSteps.length - 1; Layout.fillWidth: true; Layout.preferredHeight: 2; Layout.minimumHeight: 2; Layout.maximumHeight: 2; color: index < root.probeStep ? cSuccess : cBorder }
                                        }
                                    }
                                }
                            }
                            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; Layout.minimumHeight: 1; Layout.maximumHeight: 1; color: cBorder }
                            RowLayout {
                                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
                                // left: cycles
                                Rectangle {
                                    Layout.preferredWidth: 250; Layout.minimumWidth: 250; Layout.maximumWidth: 250; Layout.fillHeight: true; color: cCard
                                    ColumnLayout { anchors.fill: parent; anchors.margins: 8; spacing: 6
                                        Text { text: "ЦИКЛЫ ИЗМЕРЕНИЯ"; color: cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 0.5 }
                                        Repeater {
                                            model: root.probeList
                                            delegate: Rectangle {
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: modelData.h !== undefined ? 18 : 34
                                                Layout.minimumHeight: modelData.h !== undefined ? 18 : 34
                                                Layout.maximumHeight: modelData.h !== undefined ? 18 : 34
                                                color: modelData.h !== undefined ? "transparent" : (modelData.i === root.probeCycle ? cAccent : cCard)
                                                border.width: (modelData.h === undefined && modelData.i === root.probeCycle) ? 2 : (modelData.h === undefined ? 1 : 0)
                                                border.color: (modelData.h === undefined && modelData.i === root.probeCycle) ? cPrimary : cBorderMuted
                                                radius: 4
                                                Text { visible: modelData.h !== undefined; anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; text: modelData.h || ""; color: cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 0.5 }
                                                RowLayout { visible: modelData.h === undefined; anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 8
                                                    Text { text: modelData.h === undefined ? root.probeCycles[modelData.i].name : ""; Layout.fillWidth: true; color: cForeground; font.bold: modelData.i === root.probeCycle; font.pixelSize: 12 }
                                                    Text { text: modelData.h === undefined ? (root.probeCycles[modelData.i].axes + " · " + root.probeCycles[modelData.i].touches + " кас.") : ""; color: cMutedForeground; font.pixelSize: 10; font.family: fontMono }
                                                }
                                                MouseArea { anchors.fill: parent; enabled: modelData.h === undefined; onClicked: root.probeCycle = modelData.i }
                                            }
                                        }
                                        Item { Layout.fillWidth: true; Layout.fillHeight: true }
                                    }
                                }
                                Rectangle { Layout.preferredWidth: 1; Layout.minimumWidth: 1; Layout.maximumWidth: 1; Layout.fillHeight: true; color: cBorder }
                                // center
                                ColumnLayout { Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
                                    Rectangle {
                                        Layout.fillWidth: true; Layout.fillHeight: true; color: cCard
                                        Rectangle {
                                            anchors.fill: parent; anchors.margins: 12; color: "#F3F6F9"; border.color: cBorder; radius: 4
                                            Canvas {
                                                id: probeCanvas
                                                anchors.fill: parent
                                                anchors.margins: 4
                                                onWidthChanged: requestPaint()
                                                onHeightChanged: requestPaint()
                                                onPaint: {
                                                    var ctx = getContext("2d");
                                                    var w = width, h = height;
                                                    ctx.clearRect(0, 0, w, h);
                                                    var sc = Math.min(w / 400, h / 300);
                                                    ctx.save();
                                                    ctx.translate((w - 400 * sc) / 2, (h - 300 * sc) / 2);
                                                    ctx.scale(sc, sc);
                                                    var V = root.probeViews[root.probeCycle];
                                                    var side = V.plane === "XZ";
                                                    function SX(q) { return V.ox + q.x * V.k; }
                                                    function SY(q) { return V.oy - (side ? q.z : q.y) * V.k; }
                                                    // grid
                                                    ctx.strokeStyle = "#E0E4E9"; ctx.lineWidth = 0.5;
                                                    for (var gx = 20; gx < 400; gx += 20) { ctx.beginPath(); ctx.moveTo(gx, 0); ctx.lineTo(gx, 300); ctx.stroke(); }
                                                    for (var gy = 20; gy < 300; gy += 20) { ctx.beginPath(); ctx.moveTo(0, gy); ctx.lineTo(400, gy); ctx.stroke(); }
                                                    // stock
                                                    var a0 = V.stock[0], a1 = V.stock[1], b0 = V.stock[2], b1 = V.stock[3];
                                                    var rx = V.ox + a0 * V.k, ry = V.oy - b1 * V.k, rw = (a1 - a0) * V.k, rh = (b1 - b0) * V.k;
                                                    ctx.fillStyle = "#EEF0F3"; ctx.strokeStyle = "#34363A"; ctx.lineWidth = 2;
                                                    ctx.beginPath(); ctx.rect(rx, ry, rw, rh); ctx.fill(); ctx.stroke();
                                                    ctx.save(); ctx.beginPath(); ctx.rect(rx, ry, rw, rh); ctx.clip();
                                                    ctx.strokeStyle = "rgba(52,54,58,0.15)"; ctx.lineWidth = 3;
                                                    for (var d = -400; d < 500; d += 10) { ctx.beginPath(); ctx.moveTo(rx + d, ry); ctx.lineTo(rx + d + rh, ry + rh); ctx.stroke(); }
                                                    ctx.restore();
                                                    // hole
                                                    if (V.hole) { ctx.fillStyle = "#FCFDFF"; ctx.strokeStyle = "#34363A"; ctx.lineWidth = 2; ctx.beginPath(); ctx.arc(V.ox + V.hole.cx * V.k, V.oy - V.hole.cy * V.k, V.hole.r * V.k, 0, 2 * Math.PI); ctx.fill(); ctx.stroke(); }
                                                    // feature
                                                    ctx.strokeStyle = "#0362BB"; ctx.lineWidth = 4; ctx.lineCap = "round";
                                                    var f = V.feature;
                                                    if (f === "edge-xp") { ctx.beginPath(); ctx.moveTo(rx, ry); ctx.lineTo(rx, ry + rh); ctx.stroke(); }
                                                    else if (f === "edge-xn") { ctx.beginPath(); ctx.moveTo(rx + rw, ry); ctx.lineTo(rx + rw, ry + rh); ctx.stroke(); }
                                                    else if (f === "edge-yp") { ctx.beginPath(); ctx.moveTo(rx, ry + rh); ctx.lineTo(rx + rw, ry + rh); ctx.stroke(); }
                                                    else if (f === "edge-yn" || f === "top") { ctx.beginPath(); ctx.moveTo(rx, ry); ctx.lineTo(rx + rw, ry); ctx.stroke(); }
                                                    else if (f === "corner") { ctx.beginPath(); ctx.moveTo(rx, ry + rh - 60); ctx.lineTo(rx, ry + rh); ctx.lineTo(rx + 60, ry + rh); ctx.stroke(); }
                                                    else if (f === "center") { var ccx = V.hole ? V.ox + V.hole.cx * V.k : rx + rw / 2; var ccy = V.hole ? V.oy - V.hole.cy * V.k : ry + rh / 2; ctx.lineWidth = 2.5; ctx.beginPath(); ctx.moveTo(ccx - 16, ccy); ctx.lineTo(ccx + 16, ccy); ctx.moveTo(ccx, ccy - 16); ctx.lineTo(ccx, ccy + 16); ctx.stroke(); }
                                                    // touches
                                                    ctx.font = "bold 11px monospace";
                                                    for (var i = 0; i < V.touches.length; i++) {
                                                        var t = V.touches[i];
                                                        var fx = SX(t.from), fy = SY(t.from), tx = SX(t.to), ty = SY(t.to);
                                                        var zTouch = (t.axis === "Z" && !side);
                                                        ctx.strokeStyle = "#535C66"; ctx.lineWidth = 2.5;
                                                        ctx.setLineDash([6, 4]);
                                                        if (zTouch) { ctx.beginPath(); ctx.arc(fx, fy, 11, 0, 2 * Math.PI); ctx.stroke(); }
                                                        else {
                                                            ctx.beginPath(); ctx.moveTo(fx, fy); ctx.lineTo(tx, ty); ctx.stroke();
                                                            ctx.setLineDash([]);
                                                            var ang = Math.atan2(ty - fy, tx - fx);
                                                            ctx.beginPath(); ctx.moveTo(tx, ty);
                                                            ctx.lineTo(tx - 8 * Math.cos(ang - 0.4), ty - 8 * Math.sin(ang - 0.4));
                                                            ctx.lineTo(tx - 8 * Math.cos(ang + 0.4), ty - 8 * Math.sin(ang + 0.4));
                                                            ctx.closePath(); ctx.fillStyle = "#535C66"; ctx.fill();
                                                        }
                                                        ctx.setLineDash([]);
                                                        var bx = fx - 14 - (zTouch ? 18 : 0), by = fy - 30;
                                                        ctx.fillStyle = "#FCFDFF"; ctx.strokeStyle = "#535C66"; ctx.lineWidth = 1.5;
                                                        ctx.beginPath(); ctx.rect(bx, by, 44, 20); ctx.fill(); ctx.stroke();
                                                        ctx.fillStyle = "#141B24"; ctx.textAlign = "center";
                                                        ctx.fillText((i + 1) + "·" + t.axis + (t.sign > 0 ? "+" : "\u2212"), bx + 22, by + 14);
                                                    }
                                                    // start
                                                    var stx = SX(V.start), sty = SY(V.start);
                                                    ctx.strokeStyle = "rgba(52,54,58,0.5)"; ctx.lineWidth = 1.5; ctx.setLineDash([2, 2]);
                                                    ctx.beginPath(); ctx.arc(stx, sty, 5, 0, 2 * Math.PI); ctx.stroke(); ctx.setLineDash([]);
                                                    ctx.fillStyle = "#535C66"; ctx.font = "11px monospace"; ctx.textAlign = "left";
                                                    ctx.fillText("\u0441\u0442\u0430\u0440\u0442", stx + 10, sty + 4);
                                                    // probe
                                                    var r = 7;
                                                    if (side) {
                                                        ctx.fillStyle = "rgba(52,54,58,0.7)"; ctx.beginPath(); ctx.rect(stx - 3, sty - 70, 6, 70 - r); ctx.fill();
                                                        ctx.fillStyle = "rgba(52,54,58,0.8)"; ctx.beginPath(); ctx.rect(stx - 22, sty - 110, 44, 42); ctx.fill();
                                                    } else {
                                                        ctx.fillStyle = "rgba(52,54,58,0.1)"; ctx.strokeStyle = "rgba(52,54,58,0.6)"; ctx.lineWidth = 1.5;
                                                        ctx.beginPath(); ctx.arc(stx, sty, r + 9, 0, 2 * Math.PI); ctx.fill(); ctx.stroke();
                                                    }
                                                    ctx.fillStyle = "#D42125"; ctx.strokeStyle = "#34363A"; ctx.lineWidth = 1.5;
                                                    ctx.beginPath(); ctx.arc(stx, sty, r, 0, 2 * Math.PI); ctx.fill(); ctx.stroke();
                                                    // gizmo
                                                    var gx2 = 26, gy2 = 278;
                                                    ctx.strokeStyle = "rgba(52,54,58,0.7)"; ctx.fillStyle = "rgba(52,54,58,0.7)"; ctx.lineWidth = 2;
                                                    ctx.beginPath(); ctx.moveTo(gx2, gy2); ctx.lineTo(gx2 + 34, gy2); ctx.stroke();
                                                    ctx.beginPath(); ctx.moveTo(gx2, gy2); ctx.lineTo(gx2, gy2 - 34); ctx.stroke();
                                                    ctx.font = "bold 12px monospace"; ctx.textAlign = "left";
                                                    ctx.fillText("X", gx2 + 40, gy2 + 4); ctx.fillText(side ? "Z" : "Y", gx2 - 4, gy2 - 40);
                                                    // label
                                                    ctx.fillStyle = "#535C66"; ctx.font = "600 12px sans-serif";
                                                    ctx.fillText(side ? "\u0412\u0418\u0414 \u0421\u0411\u041e\u041a\u0423 \u00b7 XZ" : "\u0412\u0418\u0414 \u0421\u0412\u0415\u0420\u0425\u0423 \u00b7 XY", 14, 22);
                                                    ctx.restore();
                                                }
                                            }
                                            Connections { target: root; function onProbeCycleChanged() { probeCanvas.requestPaint() } }
                                            Text { anchors.centerIn: parent; anchors.verticalCenterOffset: 40; text: root.probeCycles[root.probeCycle].name; color: cMutedForeground; font.pixelSize: 11 }
                                            Rectangle { anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 8; height: 22; width: readyTxt.implicitWidth + 34; color: cCard; border.color: cBorder; radius: 4
                                                RowLayout { anchors.fill: parent; anchors.margins: 4; spacing: 4
                                                    Rectangle { Layout.preferredWidth: 8; Layout.preferredHeight: 8; Layout.minimumWidth: 8; Layout.maximumWidth: 8; radius: 4; color: cSuccess }
                                                    Text { id: readyTxt; text: "Щуп T99 · готов"; color: cForeground; font.bold: true; font.pixelSize: 10 } } }
                                        }
                                    }
                                    Rectangle {
                                        Layout.fillWidth: true; Layout.preferredHeight: 36; Layout.minimumHeight: 36; Layout.maximumHeight: 36; color: cCard
                                        RowLayout { anchors.fill: parent; spacing: 0
                                            Repeater { model: ["X", "Y", "Z"]; delegate: RowLayout { Layout.fillWidth: true; spacing: 8; anchors.margins: 0
                                                Rectangle { Layout.preferredWidth: 22; Layout.preferredHeight: 22; Layout.minimumWidth: 22; Layout.maximumWidth: 22; Layout.minimumHeight: 22; Layout.maximumHeight: 22; color: cForeground; radius: 3; Text { anchors.centerIn: parent; text: modelData; color: cBackground; font.bold: true; font.pixelSize: 11; font.family: fontMono } }
                                                Text { text: "+0.000"; Layout.fillWidth: true; color: cForeground; font.bold: true; font.pixelSize: 18; font.family: fontMono; horizontalAlignment: Text.AlignRight }
                                            } }
                                        }
                                    }
                                    Rectangle {
                                        Layout.fillWidth: true; Layout.preferredHeight: 30; Layout.minimumHeight: 30; Layout.maximumHeight: 30; color: cMuted
                                        RowLayout { anchors.fill: parent; anchors.margins: 6; spacing: 6
                                            Repeater { model: root.probeCycles[root.probeCycle].touches; delegate: Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 18; Layout.minimumHeight: 18; Layout.maximumHeight: 18; color: cCard; border.color: cBorder; radius: 3
                                                Text { anchors.centerIn: parent; text: (index + 1) + "·" + root.probeCycles[root.probeCycle].axes; color: cMutedForeground; font.pixelSize: 10; font.family: fontMono } } }
                                        }
                                    }
                                }
                                Rectangle { Layout.preferredWidth: 1; Layout.minimumWidth: 1; Layout.maximumWidth: 1; Layout.fillHeight: true; color: cBorder }
                                // right: step card
                                ColumnLayout { Layout.preferredWidth: 300; Layout.minimumWidth: 300; Layout.maximumWidth: 300; Layout.fillHeight: true; spacing: 0
                                    Rectangle {
                                        Layout.fillWidth: true; Layout.preferredHeight: 48; Layout.minimumHeight: 48; Layout.maximumHeight: 48; color: cCard
                                        ColumnLayout { anchors.fill: parent; anchors.margins: 8; spacing: 0
                                            Text { text: "ШАГ " + (root.probeStep + 1) + " ИЗ 5"; color: cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 0.5 }
                                            Text { text: root.probeSteps[root.probeStep]; color: cForeground; font.bold: true; font.pixelSize: 16 } }
                                    }
                                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; Layout.minimumHeight: 1; Layout.maximumHeight: 1; color: cBorder }
                                    Rectangle {
                                        Layout.fillWidth: true; Layout.fillHeight: true; color: cCard
                                        ColumnLayout { anchors.fill: parent; anchors.margins: 10; spacing: 6
                                            Text { text: root.probeCycles[root.probeCycle].name; color: cForeground; font.bold: true; font.pixelSize: 14 }
                                            Text { text: "Установите щуп T99 в шпиндель, подведите к заготовке и запустите цикл измерения. Результат записывается в " + root.activeOffsetCode() + "."; Layout.fillWidth: true; color: cMutedForeground; font.pixelSize: 12; wrapMode: Text.WordWrap }
                                            Item { Layout.fillWidth: true; Layout.fillHeight: true }
                                        }
                                    }
                                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; Layout.minimumHeight: 1; Layout.maximumHeight: 1; color: cBorder }
                                    Rectangle {
                                        Layout.fillWidth: true; Layout.preferredHeight: 30; Layout.minimumHeight: 30; Layout.maximumHeight: 30; color: cMuted
                                        Text { anchors.left: parent.left; anchors.leftMargin: 10; anchors.verticalCenter: parent.verticalCenter; text: "↑↓ цикл · F1/F2 шаг · F6 закрыть"; color: cMutedForeground; font.pixelSize: 10 }
                                    }
                                }
                            }
                        }
                    }

                    // ===== "Tool" screen =====
                    Rectangle {
                        id: toolScreen
                        Layout.fillWidth: true; Layout.fillHeight: true
                        color: cCard; border.color: cBorder; border.width: 1; radius: 6; clip: true
                        ColumnLayout {
                            anchors.fill: parent; anchors.topMargin: 1; anchors.leftMargin: 1; anchors.rightMargin: 1; anchors.bottomMargin: 6; spacing: 0
                            Rectangle {
                                id: toolHeader
                                Layout.fillWidth: true; Layout.preferredHeight: 32; Layout.minimumHeight: 32; Layout.maximumHeight: 32
                                color: cMuted; radius: 6; clip: true
                                Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 6; color: cMuted }
                                RowLayout {
                                    anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 6
                                    Text { text: "\ue1b1"; color: cMutedForeground; font.family: "lucide"; font.pixelSize: 14 }
                                    Text { text: "ТАБЛИЦА ИНСТРУМЕНТА"; color: cMutedForeground; font.bold: true; font.pixelSize: 12; font.letterSpacing: 0.5; Layout.fillWidth: true; Layout.alignment: Qt.AlignVCenter }
                                }
                            }
                            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; Layout.minimumHeight: 1; Layout.maximumHeight: 1; color: cBorder }
                            Rectangle {
                                Layout.fillWidth: true; Layout.preferredHeight: 26; Layout.minimumHeight: 26; Layout.maximumHeight: 26; color: cCard
                                RowLayout {
                                    anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 6
                                    Text { text: "T"; Layout.minimumWidth: 70; Layout.maximumWidth: 70; Layout.preferredWidth: 70; color: cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 0.5 }
                                    Text { text: "НАИМЕНОВАНИЕ"; Layout.minimumWidth: 468; Layout.maximumWidth: 468; Layout.preferredWidth: 468; color: cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 0.5 }
                                    Text { text: "D, ММ"; Layout.minimumWidth: 90; Layout.maximumWidth: 90; Layout.preferredWidth: 90; color: cMutedForeground; font.bold: true; font.pixelSize: 10; horizontalAlignment: Text.AlignRight }
                                    Text { text: "L, ММ"; Layout.minimumWidth: 110; Layout.maximumWidth: 110; Layout.preferredWidth: 110; color: cMutedForeground; font.bold: true; font.pixelSize: 10; horizontalAlignment: Text.AlignRight }
                                    Text { text: "РЕСУРС"; Layout.minimumWidth: 220; Layout.maximumWidth: 220; Layout.preferredWidth: 220; color: cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 0.5 }
                                }
                            }
                            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; Layout.minimumHeight: 1; Layout.maximumHeight: 1; color: cBorder }
                            Repeater {
                                model: root.toolsData
                                delegate: Rectangle {
                                    property bool cursor: index === root.cursorTool
                                    Layout.fillWidth: true; Layout.preferredHeight: 30; Layout.minimumHeight: 30; Layout.maximumHeight: 30
                                    color: cursor ? cAccent : cCard
                                    border.color: cursor ? cPrimary : "transparent"; border.width: cursor ? 2 : 0
                                    RowLayout {
                                        anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 6
                                        Item {
                                            Layout.minimumWidth: 70; Layout.maximumWidth: 70; Layout.preferredWidth: 70; Layout.fillHeight: true
                                            Rectangle {
                                                anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                                                width: 44; height: 24; radius: 4
                                                color: modelData.inSpindle ? cPrimary : cMuted
                                                Text { anchors.centerIn: parent; text: modelData.n; color: modelData.inSpindle ? cPrimaryFg : cForeground; font.bold: true; font.pixelSize: 14; font.family: fontMono }
                                            }
                                        }
                                        RowLayout {
                                            Layout.minimumWidth: 468; Layout.maximumWidth: 468; Layout.preferredWidth: 468; spacing: 6
                                            Text { text: modelData.name; color: cForeground; font.pixelSize: 14; elide: Text.ElideRight }
                                            Text { visible: modelData.inSpindle; text: "в шпинделе"; color: cPrimary; font.bold: true; font.pixelSize: 12 }
                                        }
                                        Text { text: modelData.d; Layout.minimumWidth: 90; Layout.maximumWidth: 90; Layout.preferredWidth: 90; color: cForeground; font.pixelSize: 14; font.family: fontMono; horizontalAlignment: Text.AlignRight; Layout.alignment: Qt.AlignVCenter }
                                        Text { text: modelData.l; Layout.minimumWidth: 110; Layout.maximumWidth: 110; Layout.preferredWidth: 110; color: cForeground; font.pixelSize: 14; font.family: fontMono; horizontalAlignment: Text.AlignRight; Layout.alignment: Qt.AlignVCenter }
                                        RowLayout {
                                            Layout.minimumWidth: 220; Layout.maximumWidth: 220; Layout.preferredWidth: 220; spacing: 8
                                            Rectangle {
                                                Layout.fillWidth: true; Layout.preferredHeight: 10; Layout.minimumHeight: 10; Layout.maximumHeight: 10
                                                color: cMuted; border.color: cBorder; radius: 3
                                                Rectangle {
                                                    width: parent.width * (modelData.life / 100); height: parent.height - 2
                                                    anchors.verticalCenter: parent.verticalCenter; anchors.left: parent.left; anchors.leftMargin: 1; radius: 2
                                                    color: modelData.life < 15 ? cDestructive : modelData.life < 40 ? cWarning : cSuccess
                                                }
                                            }
                                            Text { text: modelData.life + "%"; Layout.minimumWidth: 40; Layout.maximumWidth: 40; Layout.preferredWidth: 40; color: cForeground; font.bold: true; font.pixelSize: 14; font.family: fontMono; horizontalAlignment: Text.AlignRight; Layout.alignment: Qt.AlignVCenter }
                                        }
                                    }
                                    Rectangle { anchors.left: parent.left; anchors.bottom: parent.bottom; width: parent.width; height: 1; color: cBorder }
                                }
                            }
                            Item { Layout.fillWidth: true; Layout.fillHeight: true }
                        }
                    }

                    // ===== "Messages" screen =====
                    Rectangle {
                        id: alarmScreen
                        Layout.fillWidth: true; Layout.fillHeight: true
                        color: cCard; border.color: cBorder; border.width: 1; radius: 6; clip: true
                        ColumnLayout {
                            anchors.fill: parent; anchors.topMargin: 1; anchors.leftMargin: 1; anchors.rightMargin: 1; anchors.bottomMargin: 6; spacing: 0
                    Rectangle {
                        id: alarmHeader
                        Layout.fillWidth: true
                        Layout.preferredHeight: 32; Layout.minimumHeight: 32; Layout.maximumHeight: 32
                        color: cMuted; radius: 6; clip: true
                        Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 6; color: cMuted }
                        RowLayout {
                            anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 8
                            Text { text: "\ue193"; color: cMutedForeground; font.family: "lucide"; font.pixelSize: 14 }
                            Text { text: "АВАРИИ И СООБЩЕНИЯ"; color: cMutedForeground; font.bold: true; font.pixelSize: 12; font.letterSpacing: 0.5; Layout.alignment: Qt.AlignVCenter }
                            Text { text: ""; Layout.fillWidth: true; color: cMutedForeground; font.pixelSize: 11; horizontalAlignment: Text.AlignRight; elide: Text.ElideRight; Layout.alignment: Qt.AlignVCenter }
                        }
                    }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; Layout.minimumHeight: 1; Layout.maximumHeight: 1; color: cBorder }
                            Repeater {
                                model: root.alarmsData
                                delegate: Rectangle {
                                    property bool cursor: index === root.cursorAlarm
                                    Layout.fillWidth: true; Layout.preferredHeight: 40; Layout.minimumHeight: 40; Layout.maximumHeight: 40
                                    opacity: modelData.active ? 1.0 : 0.55
                                    color: cursor ? cAccent : cCard
                                    border.color: cursor ? cPrimary : "transparent"; border.width: cursor ? 2 : 0
                                    RowLayout { anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 10
                                        Text {
                                            text: modelData.level === "error" ? "\ue0a0" : modelData.level === "warn" ? "\ue193" : "\ue0f9"
                                            color: modelData.level === "error" ? cDestructive : modelData.level === "warn" ? cWarningFg : cPrimary
                                            font.family: "lucide"; font.pixelSize: 24; Layout.alignment: Qt.AlignVCenter
                                        }
                                        Text { text: modelData.code; Layout.minimumWidth: 56; Layout.maximumWidth: 56; color: cForeground; font.bold: true; font.pixelSize: 16; font.family: fontMono; Layout.alignment: Qt.AlignVCenter }
                                        ColumnLayout { Layout.fillWidth: true; spacing: 0
                                            Text { text: modelData.text || ""; Layout.fillWidth: true; color: cForeground; font.pixelSize: 14; elide: Text.ElideRight }
                                            Text { text: modelData.lvl; color: cMutedForeground; font.pixelSize: 12 }
                                        }
                                        Text { text: modelData.time; Layout.minimumWidth: 76; Layout.maximumWidth: 76; color: cMutedForeground; font.pixelSize: 14; font.family: fontMono; horizontalAlignment: Text.AlignRight; Layout.alignment: Qt.AlignVCenter }
                                        Rectangle {
                                            Layout.preferredWidth: 96; Layout.preferredHeight: 20; Layout.minimumWidth: 96; Layout.maximumWidth: 96; Layout.minimumHeight: 20; Layout.maximumHeight: 20
                                            color: modelData.active ? cForeground : "transparent"; border.color: cMutedForeground; radius: 4
                                            Text { anchors.centerIn: parent; text: modelData.active ? "АКТИВНО" : "КВИТИРОВАНО"; color: modelData.active ? cBackground : cMutedForeground; font.bold: true; font.pixelSize: 12 }
                                        }
                                    }
                                    Rectangle { anchors.left: parent.left; anchors.bottom: parent.bottom; width: parent.width; height: 1; color: cBorder }
                                }
                            }
                            Item { Layout.fillWidth: true; Layout.fillHeight: true }
                        }
                    }

                    // ===== "Service" screen =====
                    Rectangle {
                        id: serviceScreen
                        Layout.fillWidth: true; Layout.fillHeight: true
                        color: cCard; border.color: cBorder; border.width: 1; radius: 6; clip: true
                        ColumnLayout {
                            anchors.fill: parent; anchors.topMargin: 1; anchors.leftMargin: 1; anchors.rightMargin: 1; anchors.bottomMargin: 6; spacing: 0
                            Rectangle {
                                id: serviceHeader
                                Layout.fillWidth: true; Layout.preferredHeight: 32; Layout.minimumHeight: 32; Layout.maximumHeight: 32; color: cMuted; radius: 6; clip: true
                                Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 6; color: cMuted }
                                RowLayout { anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 8
                                    Text { text: "\ue154"; color: cMutedForeground; font.family: "lucide"; font.pixelSize: 14 }
                                    Text { text: "СЕРВИС · " + root.serviceLabelOf(root.serviceSection); color: cMutedForeground; font.bold: true; font.pixelSize: 12; font.letterSpacing: 0.5; Layout.fillWidth: true; Layout.alignment: Qt.AlignVCenter }
                                    Rectangle { visible: root.svcUnsavedCount() > 0; Layout.preferredHeight: 18; Layout.minimumHeight: 18; Layout.maximumHeight: 18; width: unsTxt.implicitWidth + 14; color: "#1a0362BB"; border.color: "#800362BB"; radius: 3
                                        Text { id: unsTxt; anchors.centerIn: parent; text: "Не сохранено: " + root.svcUnsavedCount(); color: cPrimary; font.bold: true; font.pixelSize: 11 } }
                                    Text { text: "vmc850.ini"; color: cMutedForeground; font.pixelSize: 11; font.family: fontMono }
                                    Rectangle { Layout.preferredWidth: 72; Layout.preferredHeight: 18; Layout.minimumWidth: 72; Layout.maximumWidth: 72; Layout.minimumHeight: 18; Layout.maximumHeight: 18; color: "transparent"; border.color: cMutedForeground; radius: 3
                                        Text { anchors.centerIn: parent; text: "ЧТЕНИЕ"; color: cMutedForeground; font.bold: true; font.pixelSize: 10 } }
                                }
                            }
                            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; Layout.minimumHeight: 1; Layout.maximumHeight: 1; color: cBorder }
                            RowLayout { Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
                                // left nav (click + up/down keys)
                                Rectangle {
                                    Layout.preferredWidth: 240; Layout.minimumWidth: 240; Layout.maximumWidth: 240; Layout.fillHeight: true; color: cMuted
                                    ColumnLayout { anchors.fill: parent; anchors.margins: 6; spacing: 2
                                        Repeater {
                                            model: root.serviceNav
                                            delegate: Rectangle {
                                                id: navRow
                                                property bool isItem: modelData.t === "item"
                                                property bool on: isItem && modelData.id === root.serviceSection
                                                property int activePage: (root.serviceSection === "mesa" || root.serviceSection === "hal" || root.serviceSection === "ladder") ? 1 : 0
                                                property bool dim: isItem && modelData.page !== activePage
                                                Layout.fillWidth: true
                                                Layout.preferredHeight: isItem ? 34 : 22
                                                Layout.minimumHeight: isItem ? 34 : 22
                                                Layout.maximumHeight: isItem ? 34 : 22
                                                color: on ? cPrimary : "transparent"
                                                radius: 4
                                                Text {
                                                    visible: !isItem
                                                    anchors.left: parent.left; anchors.leftMargin: 2; anchors.verticalCenter: parent.verticalCenter
                                                    text: modelData.text || ""; color: cForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 1
                                                }
                                                RowLayout {
                                                    visible: parent.isItem
                                                    anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 10
                                                    opacity: parent.dim ? 0.5 : 1.0
                                                    Text { text: modelData.icon || ""; color: parent.parent.on ? cPrimaryFg : cForeground; font.family: "lucide"; font.pixelSize: 20 }
                                                    ColumnLayout { Layout.fillWidth: true; spacing: 0
                                                        Text { text: modelData.label || ""; Layout.fillWidth: true; color: parent.parent.on ? cPrimaryFg : cForeground; font.bold: true; font.pixelSize: 12; elide: Text.ElideRight }
                                                        Text { text: modelData.hint || ""; Layout.fillWidth: true; color: parent.parent.on ? cPrimaryFg : cMutedForeground; font.pixelSize: 10; elide: Text.ElideRight }
                                                    }
                                                }
                                                MouseArea { anchors.fill: parent; enabled: navRow.isItem; onClicked: if (navRow.isItem) { root.serviceSection = modelData.id; root.svcFocus = "nav"; } }
                                            }
                                        }
                                        Item { Layout.fillWidth: true; Layout.fillHeight: true }
                                    }
                                }
                                Rectangle { Layout.preferredWidth: 1; Layout.minimumWidth: 1; Layout.maximumWidth: 1; Layout.fillHeight: true; color: cBorder }
                                // right content
                                ColumnLayout { Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
                                    Rectangle {
                                        visible: root.hasSvcList(); Layout.fillWidth: true; Layout.preferredHeight: 40; Layout.minimumHeight: 40; Layout.maximumHeight: 40; color: cCard
                                        RowLayout { anchors.fill: parent; spacing: 1
                                        Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; color: cCard
                                            ColumnLayout { anchors.fill: parent; anchors.margins: 6; spacing: 2
                                                Text { text: "LinuxCNC"; color: cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 0.5; elide: Text.ElideRight }
                                                Text { text: "2.9.4"; color: cForeground; font.bold: true; font.pixelSize: 14; font.family: fontMono; elide: Text.ElideRight }
                                            }
                                        }
                                        Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; color: cCard
                                            ColumnLayout { anchors.fill: parent; anchors.margins: 6; spacing: 2
                                                Text { text: "Ядро RT"; color: cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 0.5; elide: Text.ElideRight }
                                                Text { text: "6.1.0-rt PREEMPT"; color: cForeground; font.bold: true; font.pixelSize: 14; font.family: fontMono; elide: Text.ElideRight }
                                            }
                                        }
                                        Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; color: cCard
                                            ColumnLayout { anchors.fill: parent; anchors.margins: 6; spacing: 2
                                                Text { text: "Servo-поток"; color: cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 0.5; elide: Text.ElideRight }
                                                Text { text: "1.000 мс"; color: cForeground; font.bold: true; font.pixelSize: 14; font.family: fontMono; elide: Text.ElideRight }
                                            }
                                        }
                                        Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; color: cCard
                                            ColumnLayout { anchors.fill: parent; anchors.margins: 6; spacing: 2
                                                Text { text: "Джиттер"; color: cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 0.5; elide: Text.ElideRight }
                                                Text { text: "11.8 мкс"; color: cForeground; font.bold: true; font.pixelSize: 14; font.family: fontMono; elide: Text.ElideRight }
                                            }
                                        }
                                        Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; color: cCard
                                            ColumnLayout { anchors.fill: parent; anchors.margins: 6; spacing: 2
                                                Text { text: "Интерфейс"; color: cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 0.5; elide: Text.ElideRight }
                                                Text { text: "Mesa 7i96S"; color: cForeground; font.bold: true; font.pixelSize: 14; font.family: fontMono; elide: Text.ElideRight }
                                            }
                                        }
                                        Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; color: cCard
                                            ColumnLayout { anchors.fill: parent; anchors.margins: 6; spacing: 2
                                                Text { text: "IP-адрес"; color: cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 0.5; elide: Text.ElideRight }
                                                Text { text: "192.168.1.121"; color: cForeground; font.bold: true; font.pixelSize: 14; font.family: fontMono; elide: Text.ElideRight }
                                            }
                                        }
                                        }
                                    }
                                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; Layout.minimumHeight: 1; Layout.maximumHeight: 1; color: cBorder }
                                    // ===== Axis selector (Joints/PID, v0.5.1) =====
                                    Rectangle {
                                        id: axisSelector
                                        visible: root.serviceSection === "joints" || root.serviceSection === "pid"
                                        Layout.fillWidth: true; Layout.preferredHeight: 40; Layout.minimumHeight: 40; Layout.maximumHeight: 40; color: cCard
                                        RowLayout { anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 8
                                            Text { text: "ОСЬ"; color: cMutedForeground; font.bold: true; font.pixelSize: 11; font.letterSpacing: 1 }
                                            Repeater { model: ["X", "Y", "Z", "A"]; delegate: Rectangle {
                                                Layout.preferredHeight: 28; Layout.minimumHeight: 28; Layout.maximumHeight: 28
                                                width: axLab.implicitWidth + 22; color: modelData === root.jogAxis ? cPrimary : cMuted; border.color: modelData === root.jogAxis ? cPrimary : cBorder; radius: 3
                                                RowLayout { anchors.centerIn: parent; spacing: 6
                                                    Text { id: axLab; text: modelData; color: modelData === root.jogAxis ? cPrimaryFg : cForeground; font.bold: true; font.pixelSize: 14; font.family: fontMono }
                                                    Text { text: "JOINT_" + index; color: modelData === root.jogAxis ? cPrimaryFg : cMutedForeground; font.pixelSize: 11 } } } }
                                            Item { Layout.fillWidth: true }
                                            Text { text: "← →  выбор оси"; color: cMutedForeground; font.pixelSize: 11 }
                                        }
                                        Rectangle { anchors.left: parent.left; anchors.bottom: parent.bottom; width: parent.width; height: 1; color: cBorder }
                                    }
                                    Rectangle {
                                        visible: root.hasSvcList(); Layout.fillWidth: true; Layout.preferredHeight: 26; Layout.minimumHeight: 26; Layout.maximumHeight: 26; color: cCard
                                        RowLayout { anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 6
                                            Text { text: "ПАРАМЕТР"; Layout.minimumWidth: 300; Layout.maximumWidth: 300; Layout.preferredWidth: 300; color: cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 0.5 }
                                            Text { text: "КЛЮЧ INI"; Layout.minimumWidth: 210; Layout.maximumWidth: 210; Layout.preferredWidth: 210; color: cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 0.5 }
                                            Text { text: "ЗНАЧЕНИЕ"; Layout.fillWidth: true; color: cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 0.5 }
                                            Text { text: "ПО УМОЛЧ."; Layout.minimumWidth: 80; Layout.maximumWidth: 80; Layout.preferredWidth: 80; color: cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 0.5; horizontalAlignment: Text.AlignRight }
                                        }
                                    }
                                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; Layout.minimumHeight: 1; Layout.maximumHeight: 1; color: cBorder }
                                    Repeater {
                                        model: root.sysParamsFor(root.serviceSection)
                                        delegate: Rectangle {
                                            property bool sel: index === root.svcCursor && root.svcFocus === "list"
                                            property bool editing: sel && root.svcEditing
                                            visible: root.hasSvcList()
                                            Layout.fillWidth: true; Layout.preferredHeight: 32; Layout.minimumHeight: 32; Layout.maximumHeight: 32
                                            color: editing ? "#1aF3A518" : (sel ? cAccent : cCard)
                                            RowLayout { anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 6
                                                RowLayout { Layout.minimumWidth: 300; Layout.maximumWidth: 300; Layout.preferredWidth: 300; spacing: 6
                                                    Text { text: modelData.label; Layout.fillWidth: true; color: cForeground; font.bold: true; font.pixelSize: 14; elide: Text.ElideRight; Layout.alignment: Qt.AlignVCenter }
                                                    Text { visible: modelData.restart === true; text: "\ue148"; color: cMutedForeground; font.family: "lucide"; font.pixelSize: 13; Layout.alignment: Qt.AlignVCenter }
                                                    Text { visible: modelData.free === true; text: "\ue10c"; color: cSuccess; font.family: "lucide"; font.pixelSize: 13; Layout.alignment: Qt.AlignVCenter }
                                                    Text { visible: modelData.free !== true; text: "\ue10b"; color: cMutedForeground; font.family: "lucide"; font.pixelSize: 13; Layout.alignment: Qt.AlignVCenter }
                                                    Rectangle { visible: root.svcUnsaved(index); Layout.preferredWidth: 8; Layout.preferredHeight: 8; Layout.minimumWidth: 8; Layout.maximumWidth: 8; Layout.minimumHeight: 8; Layout.maximumHeight: 8; Layout.alignment: Qt.AlignVCenter; radius: 4; color: cWarning }
                                                }
                                                Text { text: modelData.key; Layout.minimumWidth: 210; Layout.maximumWidth: 210; Layout.preferredWidth: 210; color: cMutedForeground; font.pixelSize: 12; font.family: fontMono; elide: Text.ElideRight }
                                                Text { visible: sel; text: "\ue06e"; color: editing ? cWarning : cPrimary; font.family: "lucide"; font.pixelSize: 14 }
                                                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 22; Layout.minimumHeight: 22; Layout.maximumHeight: 22
                                                    color: editing ? cWarning : (root.svcChanged(index) ? "#1a0362BB" : cMuted)
                                                    border.color: editing ? cWarning : (root.svcChanged(index) ? cPrimary : cBorder); radius: 4
                                                    Text { anchors.centerIn: parent; text: root.fmtVal(modelData, root.svcVal(root.serviceSection, index, modelData)); color: editing ? cWarningFg : cForeground; font.bold: true; font.pixelSize: 16; font.family: fontMono } }
                                                Text { visible: sel; text: "\ue06f"; color: editing ? cWarning : cPrimary; font.family: "lucide"; font.pixelSize: 14 }
                                                Text { text: root.fmtVal(modelData, modelData.dflt); Layout.minimumWidth: 80; Layout.maximumWidth: 80; Layout.preferredWidth: 80; color: cMutedForeground; font.pixelSize: 14; font.family: fontMono; horizontalAlignment: Text.AlignRight }
                                            }
                                            Rectangle { anchors.left: parent.left; anchors.bottom: parent.bottom; width: parent.width; height: 1; color: cBorder }
                                            MouseArea { anchors.fill: parent; onClicked: { root.svcCursor = index; root.svcFocus = "list"; } }
                                        }
                                    }
                                    // ===== HAL — pin monitor (v0.5.1) =====
                                    Flickable {
                                        id: halFlick
                                        objectName: "halGroup"
                                        visible: root.serviceSection === "hal"
                                        Layout.fillWidth: true; Layout.fillHeight: true
                                        contentWidth: halFlick.width; contentHeight: halCol.height
                                        clip: true; boundsBehavior: Flickable.StopAtBounds
                                        Column {
                                            id: halCol
                                            width: halFlick.width
                                            Rectangle {
                                                width: halCol.width; height: 24; color: cMuted
                                                RowLayout { anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 6
                                                    Text { text: "ПИН HAL"; Layout.fillWidth: true; color: cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 0.5 }
                                                    Text { text: "ТИП"; Layout.minimumWidth: 56; Layout.maximumWidth: 56; color: cMutedForeground; font.pixelSize: 10; horizontalAlignment: Text.AlignRight }
                                                    Text { text: "НАПР."; Layout.minimumWidth: 56; Layout.maximumWidth: 56; color: cMutedForeground; font.pixelSize: 10; horizontalAlignment: Text.AlignRight }
                                                    Text { text: "ЗНАЧЕНИЕ"; Layout.minimumWidth: 96; Layout.maximumWidth: 96; color: cMutedForeground; font.pixelSize: 10; horizontalAlignment: Text.AlignRight }
                                                }
                                            }
                                            Repeater {
                                                model: root.halRows()
                                                delegate: Column {
                                                    width: halCol.width
                                                    Rectangle {
                                                        visible: modelData.head; width: parent.width; height: 22; color: cMuted
                                                        RowLayout { anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 6
                                                            Text { text: modelData.head ? modelData.title : ""; Layout.fillWidth: true; color: cForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 0.5; elide: Text.ElideRight }
                                                            Text { text: modelData.head ? ("" + modelData.count) : ""; color: cMutedForeground; font.pixelSize: 10; font.family: fontMono }
                                                        }
                                                    }
                                                    Rectangle {
                                                        visible: !modelData.head; width: parent.width; height: 20; color: cCard
                                                        RowLayout { anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 6
                                                            Text { text: modelData.head ? "" : modelData.name; Layout.fillWidth: true; color: cForeground; font.pixelSize: 10; font.family: fontMono; elide: Text.ElideRight; Layout.alignment: Qt.AlignVCenter }
                                                            Text { text: modelData.head ? "" : modelData.type; Layout.minimumWidth: 56; Layout.maximumWidth: 56; color: cMutedForeground; font.pixelSize: 10; font.family: fontMono; horizontalAlignment: Text.AlignRight; Layout.alignment: Qt.AlignVCenter }
                                                            Text { text: modelData.head ? "" : modelData.dir; Layout.minimumWidth: 56; Layout.maximumWidth: 56; color: cMutedForeground; font.pixelSize: 10; font.family: fontMono; horizontalAlignment: Text.AlignRight; Layout.alignment: Qt.AlignVCenter }
                                                            Text {
                                                                text: modelData.head ? "" : (modelData.bit ? (modelData.on ? "TRUE" : "FALSE") : (modelData.val || ""))
                                                                Layout.minimumWidth: 96; Layout.maximumWidth: 96
                                                                color: (modelData.bit && modelData.on) ? cSuccess : cForeground
                                                                font.bold: true; font.pixelSize: 10; font.family: fontMono; horizontalAlignment: Text.AlignRight; Layout.alignment: Qt.AlignVCenter
                                                            }
                                                        }
                                                        Rectangle { anchors.left: parent.left; anchors.bottom: parent.bottom; width: parent.width; height: 1; color: cBorder }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                    // ===== Ladder (ClassicLadder) =====
                                    ColumnLayout {
                                        id: ladderGroup
                                        visible: root.serviceSection === "ladder"
                                        Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
                                        Repeater {
                                            model: root.ladderRungs
                                            delegate: Rectangle {
                                                Layout.fillWidth: true; Layout.preferredHeight: 40; Layout.minimumHeight: 40; Layout.maximumHeight: 40; color: cCard
                                                RowLayout {
                                                    anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 0
                                                    Text { text: modelData.n + "  " + modelData.title; Layout.minimumWidth: 200; Layout.maximumWidth: 200; Layout.preferredWidth: 200; color: cForeground; font.bold: true; font.pixelSize: 11; elide: Text.ElideRight; Layout.alignment: Qt.AlignVCenter }
                                                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 2; Layout.minimumHeight: 2; Layout.maximumHeight: 2; color: modelData.on ? cSuccess : cBorder }
                                                    Repeater {
                                                        model: modelData.contacts
                                                        delegate: RowLayout {
                                                            spacing: 0
                                                            Rectangle {
                                                                Layout.preferredWidth: 86; Layout.preferredHeight: 30; Layout.minimumWidth: 86; Layout.maximumWidth: 86; Layout.minimumHeight: 30; Layout.maximumHeight: 30
                                                                color: cCard; border.color: modelData.nc ? cDestructive : cBorder; border.width: 2; radius: 3
                                                                ColumnLayout { anchors.centerIn: parent; spacing: 0
                                                                    Text { text: modelData.a; color: cForeground; font.bold: true; font.pixelSize: 10; font.family: fontMono; horizontalAlignment: Text.AlignHCenter; Layout.alignment: Qt.AlignHCenter }
                                                                    Text { text: modelData.l; color: cMutedForeground; font.pixelSize: 8; elide: Text.ElideRight; Layout.maximumWidth: 82 }
                                                                }
                                                            }
                                                            Rectangle { Layout.preferredWidth: 12; Layout.preferredHeight: 2; Layout.minimumWidth: 12; Layout.maximumWidth: 12; Layout.minimumHeight: 2; Layout.maximumHeight: 2; color: cBorder }
                                                        }
                                                    }
                                                    Rectangle { Layout.preferredWidth: 56; Layout.minimumWidth: 56; Layout.maximumWidth: 56; Layout.fillHeight: true; color: cCard
                                                        // timer coil (ton/tof)
                                                        Rectangle {
                                                            visible: (modelData.coil.kind === "ton" || modelData.coil.kind === "tof")
                                                            anchors.centerIn: parent
                                                            width: 52; height: 24; color: "transparent"
                                                            border.color: modelData.on ? cSuccess : "#8a8f96"; border.width: 2
                                                            Text { anchors.centerIn: parent; text: modelData.coil.kind === "ton" ? "TON" : "TOF"; color: modelData.on ? cSuccess : cMutedForeground; font.bold: true; font.pixelSize: 11; font.family: fontMono }
                                                        }
                                                        // circle coil (coil/set/reset)
                                                        Rectangle {
                                                            visible: !(modelData.coil.kind === "ton" || modelData.coil.kind === "tof")
                                                            anchors.centerIn: parent
                                                            width: 24; height: 24; radius: 12
                                                            color: modelData.on ? cSuccess : "transparent"
                                                            border.color: modelData.on ? cSuccess : "#8a8f96"; border.width: 3
                                                            Text { anchors.centerIn: parent; text: modelData.coil.kind === "set" ? "S" : modelData.coil.kind === "reset" ? "R" : ""; color: modelData.on ? cPrimaryFg : cMutedForeground; font.bold: true; font.pixelSize: 11; font.family: fontMono }
                                                        } }
                                                    ColumnLayout { Layout.preferredWidth: 128; Layout.minimumWidth: 128; Layout.maximumWidth: 128; spacing: 0
                                                        Text { text: modelData.coil.a; color: cForeground; font.bold: true; font.pixelSize: 10; font.family: fontMono }
                                                        Text { text: (modelData.coil.kind === "ton" || modelData.coil.kind === "tof") ? "0,0 / " + (modelData.coil.preset / 1000).toFixed(1).replace(".", ",") + " с" : modelData.coil.l; color: cMutedForeground; font.pixelSize: 9; elide: Text.ElideRight } }
                                                }
                                                Rectangle { anchors.left: parent.left; anchors.bottom: parent.bottom; width: parent.width; height: 1; color: cBorder }
                                            }
                                        }
                                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; Layout.minimumHeight: 1; Layout.maximumHeight: 1; color: cBorder }
                                        RowLayout { Layout.fillWidth: true; Layout.preferredHeight: 24; Layout.minimumHeight: 24; Layout.maximumHeight: 24; spacing: 12
                                            Text { text: "ClassicLadder · онлайн-мониторинг"; color: cForeground; font.bold: true; font.pixelSize: 11 }
                                            Text { text: "── есть ток"; color: cSuccess; font.pixelSize: 10 }
                                            Text { text: "── нет тока"; color: cMutedForeground; font.pixelSize: 10 }
                                            Item { Layout.fillWidth: true }
                                            Text { text: "Сканирование 10 мс"; color: cMutedForeground; font.pixelSize: 10 } }
                                        Item { Layout.fillWidth: true; Layout.fillHeight: true }
                                    }
                                    Item { Layout.fillWidth: true; Layout.fillHeight: true }
                                }
                            }
                        }
                    
                        // ===== Write access dialog (v0.5 PasswordDialog) =====
                        Rectangle {
                            id: svcAuth
                            visible: root.serviceAuthOpen
                            anchors.fill: parent
                            z: 50
                            color: "#ccE3E7EA"
                            Rectangle {
                                anchors.centerIn: parent
                                width: 320; height: 196
                                color: cCard; border.color: cPrimary; border.width: 2; radius: 6
                                ColumnLayout {
                                    anchors.fill: parent; anchors.margins: 16; spacing: 10
                                    RowLayout { Layout.fillWidth: true; spacing: 8
                                        Text { text: "\ue4a3"; color: cPrimary; font.family: "lucide"; font.pixelSize: 20 }
                                        Text { text: "ДОСТУП К ЗАПИСИ"; color: cForeground; font.bold: true; font.pixelSize: 16 }
                                    }
                                    Text { text: "ПАРОЛЬ"; color: cMutedForeground; font.bold: true; font.pixelSize: 11; font.letterSpacing: 1 }
                                    Rectangle {
                                        Layout.fillWidth: true; Layout.preferredHeight: 40; Layout.minimumHeight: 40; Layout.maximumHeight: 40
                                        color: cBackground; border.color: root.serviceAuthError ? cDestructive : cBorder; border.width: 2; radius: 4
                                        TextInput {
                                            id: authInput
                                            anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10
                                            verticalAlignment: TextInput.AlignVCenter
                                            color: cForeground; font.family: fontMono; font.pixelSize: 18; font.letterSpacing: 4
                                            echoMode: TextInput.Password
                                            focus: root.serviceAuthOpen
                                            onAccepted: root.submitServiceAuth()
                                            Keys.onEscapePressed: root.serviceAuthOpen = false
                                        }
                                    }
                                    Text { text: root.serviceAuthError ? "Неверный пароль" : "Под-пароль: 1234"; color: root.serviceAuthError ? cDestructive : cMutedForeground; font.bold: root.serviceAuthError; font.pixelSize: 11; Layout.fillWidth: true }
                                    RowLayout { Layout.fillWidth: true; spacing: 8
                                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 34; Layout.minimumHeight: 34; Layout.maximumHeight: 34; color: cPrimary; radius: 4
                                            Text { anchors.centerIn: parent; text: "Ввод"; color: cPrimaryFg; font.bold: true; font.pixelSize: 13 }
                                            MouseArea { anchors.fill: parent; onClicked: root.submitServiceAuth() } }
                                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 34; Layout.minimumHeight: 34; Layout.maximumHeight: 34; color: cMuted; border.color: cBorder; radius: 4
                                            Text { anchors.centerIn: parent; text: "Отмена"; color: cForeground; font.bold: true; font.pixelSize: 13 }
                                            MouseArea { anchors.fill: parent; onClicked: root.serviceAuthOpen = false } }
                                    }
                                }
                            }
                        }
}

            // ============ STAGE 8 — 3D TOOLPATH (QtQuick3D) ============
            Rectangle {
                id: viewScreen
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#0E1319"
                border.color: cBorder
                border.width: 1
                radius: 6
                clip: true

                View3D {
                    anchors.fill: parent
                    anchors.margins: 1

                    environment: SceneEnvironment {
                        clearColor: "#0E1319"
                        backgroundMode: SceneEnvironment.Color
                    }
                    PerspectiveCamera {
                        position: Qt.vector3d(190, 150, 210)
                        eulerRotation: Qt.vector3d(-24, -40, 0)
                        clipNear: 1
                        clipFar: 4000
                    }
                    DirectionalLight {
                        eulerRotation: Qt.vector3d(-45, -35, 0)
                        brightness: 1.0
                    }
                    Model {
                        geometry: toolpath.rapidGeometry
                        materials: [ PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#F23333" } ]
                    }
                    Model {
                        geometry: toolpath.feedGeometry
                        materials: [ PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#338CFF" } ]
                    }
                }

                RowLayout {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.margins: 10
                    spacing: 8
                    Text { text: "\ue53e"; color: cMutedForeground; font.family: "lucide"; font.pixelSize: 14 }
                    Text { text: "3D ТРАЕКТОРИЯ · G0 — красный, G1 — синий"; color: cMutedForeground; font.bold: true; font.pixelSize: 12 }
                }
            }

            }   // screensStack

            // ============ PROGRAM SIDE (Machining, v0.4) ============
            Rectangle {
                id: programSide
                visible: root.activeScreen === "prog"
                Layout.preferredWidth: 300
                Layout.minimumWidth: 300
                Layout.maximumWidth: 300
                Layout.fillHeight: true
                color: cCard; border.color: cBorder; border.width: 1; radius: 6; clip: true
                ColumnLayout {
                    anchors.fill: parent
                    anchors.topMargin: 1; anchors.leftMargin: 1; anchors.rightMargin: 1; anchors.bottomMargin: 1
                    spacing: 0
                    // title
                    Rectangle {
                        Layout.fillWidth: true; Layout.preferredHeight: 32; Layout.minimumHeight: 32; Layout.maximumHeight: 32
                        color: cMuted; radius: 6; clip: true
                        Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 6; color: cMuted }
                        RowLayout { anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 6
                            Text { text: "\ue36a"; color: cMutedForeground; font.family: "lucide"; font.pixelSize: 14 }
                            Text { text: "ОБРАБОТКА"; color: cMutedForeground; font.bold: true; font.pixelSize: 12; font.letterSpacing: 0.5; Layout.alignment: Qt.AlignVCenter }
                        }
                    }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; Layout.minimumHeight: 1; Layout.maximumHeight: 1; color: cBorder }

                    // Section: Run
                    Rectangle {
                        Layout.fillWidth: true; Layout.preferredHeight: 108; Layout.minimumHeight: 108; Layout.maximumHeight: 108; color: cCard
                        ColumnLayout { anchors.fill: parent; anchors.margins: 8; spacing: 4
                            RowLayout { Layout.fillWidth: true; spacing: 6
                                Text { text: "\ue0c3"; color: cMutedForeground; font.family: "lucide"; font.pixelSize: 13 }
                                Text { text: "ВЫПОЛНЕНИЕ"; color: cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 0.5; Layout.fillWidth: true }
                                Text { text: "1/25"; color: cForeground; font.bold: true; font.pixelSize: 11; font.family: fontMono }
                            }
                            RowLayout { Layout.fillWidth: true; spacing: 8
                                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 8; Layout.minimumHeight: 8; Layout.maximumHeight: 8; color: cMuted; border.color: cBorder; radius: 3
                                    Rectangle { width: parent.width * 0.04; height: parent.height - 2; anchors.verticalCenter: parent.verticalCenter; anchors.left: parent.left; anchors.leftMargin: 1; color: cPrimary; radius: 2 } }
                                Text { text: "4%"; Layout.minimumWidth: 30; Layout.maximumWidth: 30; color: cForeground; font.bold: true; font.pixelSize: 11; font.family: fontMono; horizontalAlignment: Text.AlignRight }
                            }
                            RowLayout { Layout.fillWidth: true; spacing: 6
                                Text { text: "ДАЛЕЕ"; color: cMutedForeground; font.bold: true; font.pixelSize: 9; font.letterSpacing: 0.5 }
                                Text { text: "N10 G21 G90 G17 G40 G49 G80"; Layout.fillWidth: true; color: cForeground; font.bold: true; font.pixelSize: 10; font.family: fontMono; elide: Text.ElideRight }
                            }
                            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; Layout.minimumHeight: 1; Layout.maximumHeight: 1; color: cBorder }
                            RowLayout { Layout.fillWidth: true; spacing: 10
                                ColumnLayout { Layout.fillWidth: true; spacing: 1
                                    RowLayout { spacing: 4
                                        Text { text: "\ue1e0"; color: cMutedForeground; font.family: "lucide"; font.pixelSize: 12 }
                                        Text { text: "ЦИКЛ"; color: cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 0.5 } }
                                    Text { text: "00:00:42"; color: cForeground; font.bold: true; font.pixelSize: 12; font.family: fontMono } }
                                ColumnLayout { Layout.fillWidth: true; spacing: 1
                                    RowLayout { spacing: 4
                                        Text { text: "\ue1f5"; color: cMutedForeground; font.family: "lucide"; font.pixelSize: 12 }
                                        Text { text: "ПРОШЛЫЙ"; color: cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 0.5 } }
                                    Text { text: "00:03:18"; color: cForeground; font.bold: true; font.pixelSize: 12; font.family: fontMono } }
                                ColumnLayout { Layout.fillWidth: true; spacing: 1
                                    RowLayout { spacing: 4
                                        Text { text: "\ue129"; color: cMutedForeground; font.family: "lucide"; font.pixelSize: 12 }
                                        Text { text: "ДЕТАЛЕЙ"; color: cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 0.5 } }
                                    Text { text: "7"; color: cForeground; font.bold: true; font.pixelSize: 12; font.family: fontMono } }
                            }
                        }
                    }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; Layout.minimumHeight: 1; Layout.maximumHeight: 1; color: cBorder }

                    // Section: Program tool
                    Rectangle {
                        Layout.fillWidth: true; Layout.preferredHeight: 100; Layout.minimumHeight: 100; Layout.maximumHeight: 100; color: cCard
                        ColumnLayout { anchors.fill: parent; anchors.margins: 8; spacing: 3
                            RowLayout { Layout.fillWidth: true; spacing: 6
                                Text { text: "\ue1b1"; color: cMutedForeground; font.family: "lucide"; font.pixelSize: 13 }
                                Text { text: "ИНСТРУМЕНТ ПРОГРАММЫ"; color: cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 0.5; Layout.fillWidth: true }
                            }
                                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 24; Layout.minimumHeight: 24; Layout.maximumHeight: 24; color: cAccent; radius: 3
                                    RowLayout { anchors.fill: parent; anchors.leftMargin: 4; anchors.rightMargin: 4; spacing: 6
                                        Rectangle { Layout.preferredWidth: 40; Layout.preferredHeight: 18; Layout.minimumWidth: 40; Layout.maximumWidth: 40; Layout.minimumHeight: 18; Layout.maximumHeight: 18; color: cPrimary; radius: 3
                                            Text { anchors.centerIn: parent; text: "T01"; color: cPrimaryFg; font.bold: true; font.pixelSize: 11; font.family: fontMono } }
                                        Text { text: "Фреза концевая 4z"; Layout.fillWidth: true; color: cForeground; font.pixelSize: 11; elide: Text.ElideRight }
                                        Text { text: "72%"; color: cMutedForeground; font.bold: true; font.pixelSize: 11; font.family: fontMono }
                                    }
                                }
                                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 24; Layout.minimumHeight: 24; Layout.maximumHeight: 24; color: cCard; radius: 3
                                    RowLayout { anchors.fill: parent; anchors.leftMargin: 4; anchors.rightMargin: 4; spacing: 6
                                        Rectangle { Layout.preferredWidth: 40; Layout.preferredHeight: 18; Layout.minimumWidth: 40; Layout.maximumWidth: 40; Layout.minimumHeight: 18; Layout.maximumHeight: 18; color: cMuted; radius: 3
                                            Text { anchors.centerIn: parent; text: "T03"; color: cMutedForeground; font.bold: true; font.pixelSize: 11; font.family: fontMono } }
                                        Text { text: "Метчик M8"; Layout.fillWidth: true; color: cForeground; font.pixelSize: 11; elide: Text.ElideRight }
                                        Text { text: "88%"; color: cMutedForeground; font.bold: true; font.pixelSize: 11; font.family: fontMono }
                                    }
                                }
                                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 24; Layout.minimumHeight: 24; Layout.maximumHeight: 24; color: cCard; radius: 3
                                    RowLayout { anchors.fill: parent; anchors.leftMargin: 4; anchors.rightMargin: 4; spacing: 6
                                        Rectangle { Layout.preferredWidth: 40; Layout.preferredHeight: 18; Layout.minimumWidth: 40; Layout.maximumWidth: 40; Layout.minimumHeight: 18; Layout.maximumHeight: 18; color: cMuted; radius: 3
                                            Text { anchors.centerIn: parent; text: "T99"; color: cMutedForeground; font.bold: true; font.pixelSize: 11; font.family: fontMono } }
                                        Text { text: "3D щуп Renishaw"; Layout.fillWidth: true; color: cForeground; font.pixelSize: 11; elide: Text.ElideRight }
                                        Text { text: "100%"; color: cMutedForeground; font.bold: true; font.pixelSize: 11; font.family: fontMono }
                                    }
                                }
                        }
                    }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; Layout.minimumHeight: 1; Layout.maximumHeight: 1; color: cBorder }

                    // Section: Overrides
                    Rectangle {
                        Layout.fillWidth: true; Layout.preferredHeight: 92; Layout.minimumHeight: 92; Layout.maximumHeight: 92; color: cCard
                        ColumnLayout { anchors.fill: parent; anchors.margins: 8; spacing: 4
                            RowLayout { Layout.fillWidth: true; spacing: 6
                                Text { text: "\ue29a"; color: cMutedForeground; font.family: "lucide"; font.pixelSize: 13 }
                                Text { text: "КОРРЕКТОРЫ"; color: cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 0.5; Layout.fillWidth: true }
                            }
                            RowLayout { Layout.fillWidth: true; spacing: 6
                                Text { text: "S%"; Layout.minimumWidth: 22; Layout.maximumWidth: 22; color: cMutedForeground; font.bold: true; font.pixelSize: 10 }
                                Rectangle { Layout.preferredWidth: 22; Layout.preferredHeight: 18; Layout.minimumWidth: 22; Layout.maximumWidth: 22; Layout.minimumHeight: 18; Layout.maximumHeight: 18; color: cCard; border.color: cBorderMuted; radius: 3; Text { anchors.centerIn: parent; text: "["; color: cMutedForeground; font.bold: true; font.pixelSize: 10 } }
                                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 8; Layout.minimumHeight: 8; Layout.maximumHeight: 8; color: cMuted; border.color: cBorder; radius: 3
                                    Rectangle { width: parent.width * Math.min(1, root.spindleOverride / 150); height: parent.height - 2; anchors.verticalCenter: parent.verticalCenter; anchors.left: parent.left; anchors.leftMargin: 1; color: cPrimary; radius: 2 } }
                                Rectangle { Layout.preferredWidth: 22; Layout.preferredHeight: 18; Layout.minimumWidth: 22; Layout.maximumWidth: 22; Layout.minimumHeight: 18; Layout.maximumHeight: 18; color: cCard; border.color: cBorderMuted; radius: 3; Text { anchors.centerIn: parent; text: "]"; color: cMutedForeground; font.bold: true; font.pixelSize: 10 } }
                                Text { text: "90%"; Layout.minimumWidth: 34; Layout.maximumWidth: 34; color: cForeground; font.bold: true; font.pixelSize: 11; font.family: fontMono; horizontalAlignment: Text.AlignRight }
                            }
                            RowLayout { Layout.fillWidth: true; spacing: 6
                                Text { text: "F%"; Layout.minimumWidth: 22; Layout.maximumWidth: 22; color: cMutedForeground; font.bold: true; font.pixelSize: 10 }
                                Rectangle { Layout.preferredWidth: 22; Layout.preferredHeight: 18; Layout.minimumWidth: 22; Layout.maximumWidth: 22; Layout.minimumHeight: 18; Layout.maximumHeight: 18; color: cCard; border.color: cBorderMuted; radius: 3; Text { anchors.centerIn: parent; text: "−"; color: cMutedForeground; font.bold: true; font.pixelSize: 10 } }
                                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 8; Layout.minimumHeight: 8; Layout.maximumHeight: 8; color: cMuted; border.color: cBorder; radius: 3
                                    Rectangle { width: parent.width * Math.min(1, root.feedOverride / 150); height: parent.height - 2; anchors.verticalCenter: parent.verticalCenter; anchors.left: parent.left; anchors.leftMargin: 1; color: cPrimary; radius: 2 } }
                                Rectangle { Layout.preferredWidth: 22; Layout.preferredHeight: 18; Layout.minimumWidth: 22; Layout.maximumWidth: 22; Layout.minimumHeight: 18; Layout.maximumHeight: 18; color: cCard; border.color: cBorderMuted; radius: 3; Text { anchors.centerIn: parent; text: "="; color: cMutedForeground; font.bold: true; font.pixelSize: 10 } }
                                Text { text: "60%"; Layout.minimumWidth: 34; Layout.maximumWidth: 34; color: cForeground; font.bold: true; font.pixelSize: 11; font.family: fontMono; horizontalAlignment: Text.AlignRight }
                            }
                            RowLayout { Layout.fillWidth: true; spacing: 4
                                Text { text: "G0"; Layout.minimumWidth: 22; Layout.maximumWidth: 22; color: cMutedForeground; font.bold: true; font.pixelSize: 10 }
                                RowLayout { Layout.fillWidth: true; Layout.preferredHeight: 18; Layout.minimumHeight: 18; Layout.maximumHeight: 18; spacing: 2
                                    Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; color: cPrimary; radius: 3; Text { anchors.centerIn: parent; text: "100"; color: cPrimaryFg; font.bold: true; font.pixelSize: 9; font.family: fontMono } }
                                    Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; color: cMuted; radius: 3; Text { anchors.centerIn: parent; text: "75"; color: cMutedForeground; font.bold: true; font.pixelSize: 9; font.family: fontMono } }
                                    Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; color: cMuted; radius: 3; Text { anchors.centerIn: parent; text: "50"; color: cMutedForeground; font.bold: true; font.pixelSize: 9; font.family: fontMono } }
                                    Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; color: cMuted; radius: 3; Text { anchors.centerIn: parent; text: "25"; color: cMutedForeground; font.bold: true; font.pixelSize: 9; font.family: fontMono } }
                                    Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; color: cMuted; radius: 3; Text { anchors.centerIn: parent; text: "1"; color: cMutedForeground; font.bold: true; font.pixelSize: 9; font.family: fontMono } }
                                }
                            }
                        }
                    }
                    Item { Layout.fillWidth: true; Layout.fillHeight: true }
                }
            }

            // ============ STAGE 4 — SIDE STATUS (Machine state) ==========
            Rectangle {
                id: sideStatus
                                visible: root.activeScreen === "pos"
                Layout.preferredWidth: 300
                Layout.minimumWidth: 300
                Layout.maximumWidth: 300
                Layout.fillHeight: true
                color: cCard
                border.color: cBorder
                border.width: 1
                radius: 6
                clip: true

                // ----- Panel title: Modes — PINNED by anchors, outside the layout -----
                // Cannot drift: anchored to the top/left/right of the panel.
                Rectangle {
                    id: sideTitle
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    // Margin = frame thickness so the title does not overlap the border.
                    anchors.topMargin: 1
                    anchors.leftMargin: 1
                    anchors.rightMargin: 1
                    height: 32
                    color: cMuted
                    radius: 6
                    clip: true
                    // Straight strip at the bottom — removes the bottom rounding (the top stays rounded).
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 6
                        color: cMuted
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        anchors.topMargin: 0
                        anchors.bottomMargin: 0
                        Layout.alignment: Qt.AlignVCenter
                        spacing: 6
                        Text { text: "\ue1bf"; color: cMutedForeground; font.family: "lucide"; font.pixelSize: 14 }  // gauge
                        Text { text: "РЕЖИМЫ"; color: cMutedForeground; font.bold: true; font.pixelSize: 12; font.letterSpacing: 1; Layout.alignment: Qt.AlignVCenter }
                        Text { text: "АВТО"; Layout.fillWidth: true; color: cForeground; font.bold: true; font.pixelSize: 12; font.family: fontMono; horizontalAlignment: Text.AlignRight; Layout.alignment: Qt.AlignVCenter }
                    }
                }

                // ---------- Divider under the title (border-b as in React) ----------
                Rectangle {
                    id: sideTitleDivider
                    anchors.top: sideTitle.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 1
                    anchors.rightMargin: 1
                    height: 1
                    color: cBorder
                }

                // ---------- Sections: separate ColumnLayout, anchored under the divider ----------
                ColumnLayout {
                    id: sideBody
                                        anchors.top: sideTitleDivider.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    // Margin = panel frame thickness so the dividers do not overlap the border.
                    anchors.leftMargin: 1
                    anchors.rightMargin: 1
                    // Height by content: the sections do not stretch and do not overlap.
                    height: implicitHeight
                    spacing: 0

                    // ===== Section 1: Frame =====
                    Rectangle {
                        id: secFrame
                        objectName: "secFrameObj"
                            Layout.fillWidth: true
                            Layout.preferredHeight: frameCol.implicitHeight + 2
                        color: cCard
                        ColumnLayout {
                            id: frameCol
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.topMargin: 1
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 3

                            // header: Frame + N0001
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Text { text: "\ue0c3"; color: cMutedForeground; font.family: "lucide"; font.pixelSize: 14 }  // file-code
                                Text { text: "КАДР"; color: cMutedForeground; font.bold: true; font.pixelSize: 11; font.letterSpacing: 1 }
                                Text { text: "N0001"; Layout.fillWidth: true; color: cForeground; font.bold: true; font.pixelSize: 12; font.family: fontMono; horizontalAlignment: Text.AlignRight }
                            }
                            // current line — highlighted row as in React:
                            // bg-primary rounded-sm px-2 py-0.5 font-mono text-sm bold
                            Rectangle {
                                id: curLine
                                Layout.fillWidth: true
                                Layout.preferredHeight: 24
                                Layout.minimumHeight: 24
                                Layout.maximumHeight: 24
                                color: cPrimary
                                radius: 4
                                Text {
                                    anchors.left: parent.left
                                    anchors.leftMargin: 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "G0 G90 G54 X0 Y0"
                                    color: cPrimaryFg
                                    font.bold: true
                                    font.pixelSize: 13
                                    font.family: fontMono
                                    elide: Text.ElideRight
                                }
                            }
                            // progress bar
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                Rectangle {
                                    id: progressBg
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 10
                                    Layout.minimumHeight: 10
                                    Layout.maximumHeight: 10
                                    color: cMuted
                                    border.color: cBorder
                                    radius: 3
                                    Rectangle {
                                        id: progressFill
                                        width: parent.width * 0.12
                                        height: parent.height - 2
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.left: parent.left
                                        anchors.leftMargin: 1
                                        color: cPrimary
                                        radius: 2
                                    }
                                }
                                Text {
                                    text: "12%"
                                    Layout.minimumWidth: 34
                                    Layout.maximumWidth: 34
                                    color: cForeground
                                    font.bold: true
                                    font.pixelSize: 12
                                    font.family: fontMono
                                    horizontalAlignment: Text.AlignRight
                                }
                            }
                            // divider above stats (like border-t on <dl>)
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 1
                                Layout.minimumHeight: 1
                                Layout.maximumHeight: 1
                                color: cBorder
                            }
                            // stats row
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 12
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text { text: "\ue1e0 ЦИКЛ"; color: cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 0.5 }  // timer
                                    Text { text: "00:00:42"; color: cForeground; font.bold: true; font.pixelSize: 14; font.family: fontMono }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text { text: "\ue1f5 ПРОШЛЫЙ"; color: cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 0.5 }  // history
                                    Text { text: "00:03:18"; color: cForeground; font.bold: true; font.pixelSize: 14; font.family: fontMono }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text { text: "\ue129 ДЕТАЛЕЙ"; color: cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 0.5 }  // package
                                    Text { text: "7"; color: cForeground; font.bold: true; font.pixelSize: 12; font.family: fontMono }
                                }
                            }
                        }
                    }

                    // divider
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; Layout.minimumHeight: 1; Layout.maximumHeight: 1; color: cBorder }

                    // ===== Section 2: Spindle =====
                    Rectangle {
                        id: secSpindle
                        objectName: "secSpindleObj"
                            Layout.fillWidth: true
                            Layout.preferredHeight: spindleCol.implicitHeight + 2
                        color: cCard
                        ColumnLayout {
                            id: spindleCol
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.topMargin: 1
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 4
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Text { text: "\ue379"; color: cMutedForeground; font.family: "lucide"; font.pixelSize: 14 }  // fan
                                Text { text: "ШПИНДЕЛЬ"; color: cMutedForeground; font.bold: true; font.pixelSize: 11; font.letterSpacing: 1 }
                                // lamp + M03
                                Rectangle {
                                    Layout.preferredWidth: 8
                                    Layout.preferredHeight: 8
                                    Layout.minimumWidth: 8
                                    Layout.maximumHeight: 8
                                    radius: 4
                                    color: root.spindleOn ? cSuccess : cMuted
                                }
                                Text { text: root.spindleOn ? "M03 · CW" : "M05"; Layout.fillWidth: true; color: root.spindleOn ? cSuccess : cMutedForeground; font.bold: true; font.pixelSize: 11; horizontalAlignment: Text.AlignRight }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                Text {
                                    text: root.spindleOn ? "3500" : "0"
                                    color: cForeground
                                    font.bold: true
                                    font.pixelSize: 24
                                    font.family: fontMono
                                }
                                Text {
                                    text: "об/мин"
                                    color: cMutedForeground
                                    font.pixelSize: 11
                                }
                                Item { Layout.fillWidth: true }
                                Text {
                                    text: "S 4000"
                                    Layout.minimumWidth: 60
                                    Layout.maximumWidth: 60
                                    color: cMutedForeground
                                    font.pixelSize: 12
                                    font.family: fontMono
                                    horizontalAlignment: Text.AlignRight
                                }
                            }
                            // load bar (v0.5 LoadRow)
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                Text { text: "Нгр"; Layout.minimumWidth: 26; Layout.maximumWidth: 26; color: cMutedForeground; font.bold: true; font.pixelSize: 10 }
                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 12
                                    Layout.minimumHeight: 12
                                    Layout.maximumHeight: 12
                                    color: cMuted
                                    border.color: cBorder
                                    radius: 3
                                    Rectangle {
                                        width: parent.width * 0.38
                                        height: parent.height - 2
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.left: parent.left
                                        anchors.leftMargin: 1
                                        color: cPrimary
                                        radius: 2
                                    }
                                    Rectangle { x: parent.width * 0.60; y: 0; width: 1; height: parent.height; color: "#4d34363a" }
                                    Rectangle { x: parent.width * 0.80; y: 0; width: 1; height: parent.height; color: "#4d34363a" }
                                }
                                Text { text: "38%"; Layout.minimumWidth: 34; Layout.maximumWidth: 34; color: cForeground; font.bold: true; font.pixelSize: 14; font.family: fontMono; horizontalAlignment: Text.AlignRight }
                            }
                            // override row S%
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Text { text: "S%"; Layout.minimumWidth: 22; Layout.maximumWidth: 22; color: cMutedForeground; font.bold: true; font.pixelSize: 10 }
                                Rectangle {  // keycap [
                                    Layout.preferredWidth: 22; Layout.preferredHeight: 20
                                    Layout.minimumWidth: 22; Layout.maximumWidth: 22
                                    Layout.minimumHeight: 20; Layout.maximumHeight: 20
                                    color: cCard; border.color: cBorderMuted; radius: 4
                                    Text { anchors.centerIn: parent; text: "["; color: cMutedForeground; font.bold: true; font.pixelSize: 10 }
                                }
                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 10
                                    Layout.minimumHeight: 10
                                    Layout.maximumHeight: 10
                                    color: cMuted
                                    border.color: cBorder
                                    radius: 3
                                    Rectangle {
                                        width: parent.width * Math.min(1, root.spindleOverride / 150)
                                        height: parent.height - 2
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.left: parent.left
                                        anchors.leftMargin: 1
                                        color: cPrimary
                                        radius: 2
                                    }
                                }
                                Rectangle {  // keycap ]
                                    Layout.preferredWidth: 22; Layout.preferredHeight: 20
                                    Layout.minimumWidth: 22; Layout.maximumWidth: 22
                                    Layout.minimumHeight: 20; Layout.maximumHeight: 20
                                    color: cCard; border.color: cBorderMuted; radius: 4
                                    Text { anchors.centerIn: parent; text: "]"; color: cMutedForeground; font.bold: true; font.pixelSize: 10 }
                                }
                                Text { text: root.spindleOverride + "%"; Layout.minimumWidth: 34; Layout.maximumWidth: 34; color: cForeground; font.bold: true; font.pixelSize: 14; font.family: fontMono; horizontalAlignment: Text.AlignRight }
                            }
                        }
                    }

                    // divider
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; Layout.minimumHeight: 1; Layout.maximumHeight: 1; color: cBorder }

                    // ===== Section 3: Feed =====
                    Rectangle {
                        id: secFeed
                        objectName: "secFeedObj"
                            Layout.fillWidth: true
                            Layout.preferredHeight: feedCol.implicitHeight + 2
                        color: cCard
                        ColumnLayout {
                            id: feedCol
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.topMargin: 1
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 4
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Text { text: "\ue1bf"; color: cMutedForeground; font.family: "lucide"; font.pixelSize: 14 }  // gauge
                                Text { text: "ПОДАЧА"; color: cMutedForeground; font.bold: true; font.pixelSize: 11; font.letterSpacing: 1 }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                Text { text: "450"; color: cForeground; font.bold: true; font.pixelSize: 24; font.family: fontMono }
                                Text { text: "мм/мин"; color: cMutedForeground; font.pixelSize: 11 }
                                Item { Layout.fillWidth: true }
                                Text { text: "F 1000"; Layout.minimumWidth: 60; Layout.maximumWidth: 60; color: cMutedForeground; font.pixelSize: 12; font.family: fontMono; horizontalAlignment: Text.AlignRight }
                            }
                            // override row F%
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Text { text: "F%"; Layout.minimumWidth: 22; Layout.maximumWidth: 22; color: cMutedForeground; font.bold: true; font.pixelSize: 10 }
                                Rectangle {
                                    Layout.preferredWidth: 22; Layout.preferredHeight: 20
                                    Layout.minimumWidth: 22; Layout.maximumWidth: 22
                                    Layout.minimumHeight: 20; Layout.maximumHeight: 20
                                    color: cCard; border.color: cBorderMuted; radius: 4
                                    Text { anchors.centerIn: parent; text: "−"; color: cMutedForeground; font.bold: true; font.pixelSize: 10 }
                                }
                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 10
                                    Layout.minimumHeight: 10
                                    Layout.maximumHeight: 10
                                    color: cMuted
                                    border.color: cBorder
                                    radius: 3
                                    Rectangle {
                                        width: parent.width * Math.min(1, root.feedOverride / 150)
                                        height: parent.height - 2
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.left: parent.left
                                        anchors.leftMargin: 1
                                        color: cPrimary
                                        radius: 2
                                    }
                                }
                                Rectangle {
                                    Layout.preferredWidth: 22; Layout.preferredHeight: 20
                                    Layout.minimumWidth: 22; Layout.maximumWidth: 22
                                    Layout.minimumHeight: 20; Layout.maximumHeight: 20
                                    color: cCard; border.color: cBorderMuted; radius: 4
                                    Text { anchors.centerIn: parent; text: "="; color: cMutedForeground; font.bold: true; font.pixelSize: 10 }
                                }
                                Text { text: root.feedOverride + "%"; Layout.minimumWidth: 34; Layout.maximumWidth: 34; color: cForeground; font.bold: true; font.pixelSize: 14; font.family: fontMono; horizontalAlignment: Text.AlignRight }
                            }
                            // G0 rapid row
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Text { text: "G0"; Layout.minimumWidth: 22; Layout.maximumWidth: 22; color: cMutedForeground; font.bold: true; font.pixelSize: 10 }
                                Rectangle {
                                    Layout.preferredWidth: 22; Layout.preferredHeight: 20
                                    Layout.minimumWidth: 22; Layout.maximumWidth: 22
                                    Layout.minimumHeight: 20; Layout.maximumHeight: 20
                                    color: cCard; border.color: cBorderMuted; radius: 4
                                    Text { anchors.centerIn: parent; text: "9"; color: cMutedForeground; font.bold: true; font.pixelSize: 10 }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 20
                                    Layout.minimumHeight: 20
                                    Layout.maximumHeight: 20
                                    spacing: 2
                                    Repeater { model: root.rapidSteps; delegate: Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; color: modelData === root.rapidOverride ? cPrimary : cMuted; radius: 3
                                        Text { anchors.centerIn: parent; text: "" + modelData; color: modelData === root.rapidOverride ? cPrimaryFg : cMutedForeground; font.bold: true; font.pixelSize: 10; font.family: fontMono } } }
                                }
                                Rectangle {
                                    Layout.preferredWidth: 22; Layout.preferredHeight: 20
                                    Layout.minimumWidth: 22; Layout.maximumWidth: 22
                                    Layout.minimumHeight: 20; Layout.maximumHeight: 20
                                    color: cCard; border.color: cBorderMuted; radius: 4
                                    Text { anchors.centerIn: parent; text: "0"; color: cMutedForeground; font.bold: true; font.pixelSize: 10 }
                                }
                            }
                        }
                    }

                    // divider
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; Layout.minimumHeight: 1; Layout.maximumHeight: 1; color: cBorder }

                    // ===== Section 4: Tool =====
                    Rectangle {
                        id: secTool
                        Layout.fillWidth: true
                        Layout.preferredHeight: toolCol.implicitHeight
                        color: cCard
                        ColumnLayout {
                            id: toolCol
                            anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
                            anchors.topMargin: 1; anchors.leftMargin: 12; anchors.rightMargin: 12
                            spacing: 4
                            RowLayout { Layout.fillWidth: true; spacing: 6
                                Text { text: "\ue1b1"; color: cMutedForeground; font.family: "lucide"; font.pixelSize: 13 }
                                Text { text: "ИНСТРУМЕНТ"; color: cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 1 }
                                Text { text: "\ue0b5"; Layout.fillWidth: true; color: cPrimary; font.family: "lucide"; font.pixelSize: 13; horizontalAlignment: Text.AlignRight }
                                Text { text: root.coolantOn ? "СОЖ вкл" : "СОЖ выкл"; color: root.coolantOn ? cPrimary : cMutedForeground; font.bold: true; font.pixelSize: 10 }
                            }
                            RowLayout { Layout.fillWidth: true; spacing: 8
                                Rectangle {
                                    Layout.preferredWidth: 44; Layout.preferredHeight: 30; Layout.minimumWidth: 44; Layout.maximumWidth: 44; Layout.minimumHeight: 30; Layout.maximumHeight: 30
                                    color: cPrimary; radius: 4
                                    Text { anchors.centerIn: parent; text: "T01"; color: cPrimaryFg; font.bold: true; font.pixelSize: 14; font.family: fontMono }
                                }
                                ColumnLayout { Layout.fillWidth: true; spacing: 0
                                    Text { text: "Фреза концевая ø10"; color: cForeground; font.bold: true; font.pixelSize: 12; elide: Text.ElideRight }
                                    Text { text: "D10 · L 75.000 · H01"; color: cMutedForeground; font.pixelSize: 10; font.family: fontMono; elide: Text.ElideRight }
                                }
                            }
                        }
                    }
                    Rectangle { visible: root.machineMode === "JOG"; Layout.fillWidth: true; Layout.preferredHeight: 1; Layout.minimumHeight: 1; Layout.maximumHeight: 1; color: cBorder }
                    // ===== Manual jog (JOG) =====
                    Rectangle {
                        id: secJog
                        visible: root.machineMode === "JOG"
                        Layout.fillWidth: true
                        Layout.preferredHeight: jogCol.implicitHeight
                        color: cCard
                        ColumnLayout {
                            id: jogCol
                            anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
                            anchors.topMargin: 1; anchors.leftMargin: 12; anchors.rightMargin: 12
                            spacing: 4
                            RowLayout { Layout.fillWidth: true; spacing: 6
                                Text { text: "\ue1d7"; color: cMutedForeground; font.family: "lucide"; font.pixelSize: 13 }
                                Text { text: "РУЧНАЯ ПОДАЧА"; color: cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 1 }
                                Text { text: "Ось " + root.jogAxis + " · шаг " + (root.jogStep === 0 ? "0.001" : root.jogStep === 1 ? "0.01" : root.jogStep === 2 ? "0.1" : "1"); Layout.fillWidth: true; color: cForeground; font.bold: true; font.pixelSize: 10; font.family: fontMono; horizontalAlignment: Text.AlignRight }
                            }
                            RowLayout { Layout.fillWidth: true; spacing: 4
                                Repeater { model: [100, 500, 1000, 3000]; delegate: Rectangle {
                                    Layout.fillWidth: true; Layout.preferredHeight: 24; Layout.minimumHeight: 24; Layout.maximumHeight: 24
                                    color: index === 2 ? cPrimary : cBackground; border.color: cBorder; radius: 4
                                    Text { anchors.centerIn: parent; text: modelData; color: index === 2 ? cPrimaryFg : cForeground; font.bold: true; font.pixelSize: 11; font.family: fontMono }
                                } }
                            }
                        }
                    }
                    Rectangle { visible: root.machineMode === "MPG"; Layout.fillWidth: true; Layout.preferredHeight: 1; Layout.minimumHeight: 1; Layout.maximumHeight: 1; color: cBorder }
                    // ===== Handwheel (MPG) =====
                    Rectangle {
                        id: secMpg
                        visible: root.machineMode === "MPG"
                        Layout.fillWidth: true
                        Layout.preferredHeight: mpgCol.implicitHeight
                        color: cCard
                        ColumnLayout {
                            id: mpgCol
                            anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
                            anchors.topMargin: 1; anchors.leftMargin: 12; anchors.rightMargin: 12
                            spacing: 4
                            RowLayout { Layout.fillWidth: true; spacing: 6
                                Text { text: "\ue494"; color: cMutedForeground; font.family: "lucide"; font.pixelSize: 13 }
                                Text { text: "МАХОВИК"; color: cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 1 }
                                Text { text: "Ось " + root.jogAxis; Layout.fillWidth: true; color: cForeground; font.bold: true; font.pixelSize: 10; font.family: fontMono; horizontalAlignment: Text.AlignRight }
                            }
                            RowLayout { Layout.fillWidth: true; spacing: 4
                                Repeater { model: [ { m: 1, s: "0.001 мм" }, { m: 10, s: "0.01 мм" }, { m: 100, s: "0.1 мм" } ]; delegate: Rectangle {
                                    Layout.fillWidth: true; Layout.preferredHeight: 30; Layout.minimumHeight: 30; Layout.maximumHeight: 30
                                    color: index === 1 ? cPrimary : cBackground; border.color: index === 1 ? cPrimary : cBorder; radius: 4
                                    ColumnLayout { anchors.centerIn: parent; spacing: 0
                                        Text { text: "×" + modelData.m; Layout.alignment: Qt.AlignHCenter; color: index === 1 ? cPrimaryFg : cForeground; font.bold: true; font.pixelSize: 14; font.family: fontMono }
                                        Text { text: modelData.s; Layout.alignment: Qt.AlignHCenter; color: index === 1 ? cPrimaryFg : cMutedForeground; font.pixelSize: 9; font.family: fontMono }
                                    }
                                } }
                            }
                        }
                    }
                }
}   // sideStatus
        }   // contentRow

        // ============ STAGE 5 — SOFTKEY BAR (F1–F8) ============
        // React: <SoftkeyBar> grid-cols-8 gap-2, each key h-[76px]
        RowLayout {
            id: softkeyRow
            Layout.fillWidth: true
            Layout.preferredHeight: 52; Layout.minimumHeight: 52; Layout.maximumHeight: 52
            spacing: 6

            Repeater {
                model: root.padSoft(root.softkeysFor(root.activeScreen))
                delegate: Rectangle {
                    id: skey
                    property bool ph: modelData === null || modelData === undefined
                    property bool on: !ph && (modelData.step !== undefined ? root.jogStep === modelData.step
                        : modelData.id === "coolant" ? root.coolantOn
                        : modelData.id === "spindle" ? root.spindleOn
                        : modelData.id === "single" ? root.singleBlock
                        : modelData.id === "optStop" ? root.optStop
                        : modelData.id === "svcLock" ? root.serviceWrite
                        : modelData.id === "dryRun" ? root.dryRun
                        : root.skeyActive(modelData.id))
                    Layout.fillWidth: true; Layout.fillHeight: true; Layout.preferredWidth: 120
                    opacity: (!ph && modelData.disabled) ? 0.4 : 1.0
                    color: ph ? cMuted : ((on || skeyMA.containsMouse) ? cAccent : cCard)
                    border.color: ph ? cBorderMuted : ((on || skeyMA.containsMouse) ? cPrimary : cBorder)
                    radius: 6; clip: true
                    Behavior on color { ColorAnimation { duration: 150 } }
                    Behavior on border.color { ColorAnimation { duration: 150 } }
                    Rectangle { anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right; height: 2; color: cSuccess; visible: on && !ph }
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 6; spacing: 0
                        RowLayout {
                            Layout.fillWidth: true; spacing: 4
                            Rectangle {
                                Layout.preferredWidth: 22; Layout.preferredHeight: 20
                                Layout.minimumWidth: 22; Layout.maximumWidth: 22
                                Layout.minimumHeight: 20; Layout.maximumHeight: 20
                                color: cCard; border.color: cBorderMuted; radius: 4
                                opacity: skey.ph ? 0.4 : 1.0
                                Text { anchors.centerIn: parent; text: "F" + (index + 1); color: cMutedForeground; font.bold: true; font.pixelSize: 10 }
                            }
                            Text { visible: !skey.ph; text: skey.ph ? "" : modelData.icon; Layout.fillWidth: true; color: on ? cPrimary : cMutedForeground; font.family: "lucide"; font.pixelSize: 16; horizontalAlignment: Text.AlignRight }
                        }
                        Text { visible: !skey.ph; text: skey.ph ? "" : modelData.label; Layout.fillWidth: true; color: cForeground; font.bold: true; font.pixelSize: 12; elide: Text.ElideRight }
                    }
                    MouseArea { id: skeyMA; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: if (!skey.ph) root.pressSoftkey(modelData) }
                }
            }
        }   // softkeyRow
    }

    // ===== Help overlay (H, v0.5.1) =====
    Rectangle {
        id: helpOverlay
        visible: root.helpOpen
        anchors.fill: parent
        z: 60
        color: "#4d141B24"
        MouseArea { anchors.fill: parent; onClicked: root.helpOpen = false }
        Rectangle {
            anchors.centerIn: parent
            width: Math.min(parent.width - 24, 900)
            height: Math.min(parent.height - 24, 470)
            color: cCard; border.color: cBorder; radius: 6
            MouseArea { anchors.fill: parent }
            ColumnLayout {
                anchors.fill: parent; spacing: 0
                Rectangle {
                    Layout.fillWidth: true; Layout.preferredHeight: 40; Layout.minimumHeight: 40; Layout.maximumHeight: 40; color: cMuted; radius: 6; clip: true
                    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 6; color: cMuted }
                    RowLayout { anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16; spacing: 8
                        Text { text: "\ue082"; color: cPrimary; font.family: "lucide"; font.pixelSize: 18 }
                        Text { text: "РАСКЛАДКА ПУЛЬТА"; color: cForeground; font.bold: true; font.pixelSize: 16; Layout.fillWidth: true }
                        Text { text: "Закрыть"; color: cMutedForeground; font.pixelSize: 11 }
                        Rectangle { Layout.preferredWidth: 24; Layout.preferredHeight: 18; Layout.minimumWidth: 24; Layout.maximumWidth: 24; Layout.minimumHeight: 18; Layout.maximumHeight: 18; color: cCard; border.color: cBorderMuted; radius: 3; Text { anchors.centerIn: parent; text: "H"; color: cMutedForeground; font.bold: true; font.pixelSize: 10 } }
                        Rectangle { Layout.preferredWidth: 30; Layout.preferredHeight: 18; Layout.minimumWidth: 30; Layout.maximumWidth: 30; Layout.minimumHeight: 18; Layout.maximumHeight: 18; color: cCard; border.color: cBorderMuted; radius: 3; Text { anchors.centerIn: parent; text: "Esc"; color: cMutedForeground; font.bold: true; font.pixelSize: 10 } }
                    }
                }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; Layout.minimumHeight: 1; Layout.maximumHeight: 1; color: cBorder }
                RowLayout {
                    Layout.fillWidth: true; Layout.fillHeight: true; Layout.margins: 16; spacing: 24
                    ColumnLayout { Layout.fillWidth: true; Layout.alignment: Qt.AlignTop; spacing: 12
                        Repeater { model: [ root.helpGroups[0], root.helpGroups[1] ]; delegate: ColumnLayout {
                            Layout.fillWidth: true; spacing: 6
                            Text { text: modelData.title; color: cMutedForeground; font.bold: true; font.pixelSize: 11; font.letterSpacing: 1 }
                            Repeater { model: modelData.items; delegate: RowLayout {
                                Layout.fillWidth: true; spacing: 8
                                Row { Layout.minimumWidth: 118; Layout.maximumWidth: 118; spacing: 4
                                    Repeater { model: modelData.keys; delegate: Rectangle {
                                        height: 18; width: kcT.implicitWidth + 10; color: cCard; border.color: cBorderMuted; radius: 3
                                        Text { id: kcT; anchors.centerIn: parent; text: modelData; color: cMutedForeground; font.bold: true; font.pixelSize: 10 } } }
                                }
                                Text { text: modelData.desc; Layout.fillWidth: true; color: cForeground; font.pixelSize: 12; elide: Text.ElideRight; Layout.alignment: Qt.AlignVCenter }
                            } }
                        } }
                    }
                    ColumnLayout { Layout.fillWidth: true; Layout.alignment: Qt.AlignTop; spacing: 12
                        Repeater { model: [ root.helpGroups[2], root.helpGroups[3] ]; delegate: ColumnLayout {
                            Layout.fillWidth: true; spacing: 6
                            Text { text: modelData.title; color: cMutedForeground; font.bold: true; font.pixelSize: 11; font.letterSpacing: 1 }
                            Repeater { model: modelData.items; delegate: RowLayout {
                                Layout.fillWidth: true; spacing: 8
                                Row { Layout.minimumWidth: 118; Layout.maximumWidth: 118; spacing: 4
                                    Repeater { model: modelData.keys; delegate: Rectangle {
                                        height: 18; width: kcT2.implicitWidth + 10; color: cCard; border.color: cBorderMuted; radius: 3
                                        Text { id: kcT2; anchors.centerIn: parent; text: modelData; color: cMutedForeground; font.bold: true; font.pixelSize: 10 } } }
                                }
                                Text { text: modelData.desc; Layout.fillWidth: true; color: cForeground; font.pixelSize: 12; elide: Text.ElideRight; Layout.alignment: Qt.AlignVCenter }
                            } }
                        } }
                    }
                }
            }
        }
    }

    // ===== Splash (v0.5.1 Lynx Touch) =====
    Rectangle {
        id: splash
        visible: !root.splashGone
        anchors.fill: parent
        z: 100
        color: "#0F1620"
        opacity: root.splashLeaving ? 0 : 1
        Behavior on opacity { NumberAnimation { duration: 500; easing.type: Easing.OutCubic } }

        // grid
        Canvas {
            id: splashGrid
            anchors.fill: parent
            opacity: 0.07
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d"); var w = width, h = height;
                ctx.clearRect(0, 0, w, h);
                ctx.strokeStyle = "#C6CBD1"; ctx.lineWidth = 1;
                for (var x = 0; x < w; x += 32) { ctx.beginPath(); ctx.moveTo(x + 0.5, 0); ctx.lineTo(x + 0.5, h); ctx.stroke(); }
                for (var y = 0; y < h; y += 32) { ctx.beginPath(); ctx.moveTo(0, y + 0.5); ctx.lineTo(w, y + 0.5); ctx.stroke(); }
            }
        }
        // radial vignette (masks grid + logo edges)
        Canvas {
            id: splashVig
            anchors.fill: parent
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d"); var w = width, h = height;
                ctx.clearRect(0, 0, w, h);
                var g = ctx.createRadialGradient(w / 2, h * 0.45, 0, w / 2, h * 0.45, Math.max(w, h) * 0.62);
                g.addColorStop(0.0, "rgba(15,22,32,0)");
                g.addColorStop(0.52, "rgba(15,22,32,0)");
                g.addColorStop(0.80, "rgba(15,22,32,1)");
                g.addColorStop(1.0, "rgba(15,22,32,1)");
                ctx.fillStyle = g; ctx.fillRect(0, 0, w, h);
            }
        }

        Column {
            anchors.centerIn: parent
            spacing: 0
            Canvas {
                id: splashLogo
                anchors.horizontalCenter: parent.horizontalCenter
                width: 300; height: 300
                opacity: 0
                property real rise: 14
                property real sc: 0.97
                transform: [ Translate { y: splashLogo.rise }, Scale { origin.x: 150; origin.y: 150; xScale: splashLogo.sc; yScale: splashLogo.sc } ]
                onImageLoaded: requestPaint()
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onPaint: {
                    var ctx = getContext("2d");
                    var w = width, h = height;
                    ctx.clearRect(0, 0, w, h);
                    ctx.save();
                    ctx.drawImage("assets/lynx-logo.png", 0, 0, w, h);
                    // circular mask: opaque to 52% radius, fade to transparent at 71%
                    ctx.globalCompositeOperation = "destination-in";
                    var cx = w / 2, cy = h / 2, R = (Math.min(w, h) / 2) * 0.71;
                    var g = ctx.createRadialGradient(cx, cy, 0, cx, cy, R);
                    g.addColorStop(0.0, "rgba(0,0,0,1)");
                    g.addColorStop(0.73, "rgba(0,0,0,1)");
                    g.addColorStop(1.0, "rgba(0,0,0,0)");
                    ctx.fillStyle = g;
                    ctx.fillRect(0, 0, w, h);
                    ctx.restore();
                }
            }
            Row {
                id: splashTitle
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 12
                opacity: 0
                property real rise: 14
                transform: Translate { y: splashTitle.rise }
                Text { text: "Lynx"; color: "#F6F9FB"; font.pixelSize: 46; font.weight: Font.DemiBold; font.letterSpacing: -0.5 }
                Text { text: "Touch"; color: "#4296EB"; font.pixelSize: 46; font.weight: Font.Light; font.letterSpacing: -0.5 }
            }
            Text {
                id: splashSub
                anchors.horizontalCenter: parent.horizontalCenter
                text: "ЧПУ КОНТРОЛЛЕР НА БАЗЕ LINUXCNC"
                color: "#C6CBD1"; font.pixelSize: 13; font.letterSpacing: 2.6
                opacity: 0
                property real rise: 14
                transform: Translate { y: splashSub.rise }
            }
        }

        Column {
            id: splashBottom
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom; anchors.bottomMargin: 24
            spacing: 8
            opacity: 0
            Rectangle {
                width: 288; height: 2; color: "#33C6CBD1"
                Rectangle { width: parent.width * root.splashProgress; height: parent.height; color: "#4296EB" }
            }
            Text { id: splashHint; anchors.horizontalCenter: parent.horizontalCenter; text: "НАЖМИТЕ ЛЮБУЮ КЛАВИШУ"; color: "#B3C6CBD1"; font.family: fontMono; font.pixelSize: 11; font.letterSpacing: 3 }
        }

        MouseArea { anchors.fill: parent; onClicked: root.splashDismiss() }

        // progress 10s
        NumberAnimation { target: root; property: "splashProgress"; from: 0; to: 1; duration: 10000; running: !root.splashGone }

        // staggered rise (logo 0ms, title 350ms, subtitle 600ms, bottom 1200ms fade)
        SequentialAnimation {
            running: true
            NumberAnimation { target: splashLogo; property: "opacity"; from: 0; to: 1; duration: 1100; easing.type: Easing.OutCubic }
            NumberAnimation { target: splashLogo; property: "rise"; from: 14; to: 0; duration: 1100; easing.type: Easing.OutCubic }
            NumberAnimation { target: splashLogo; property: "sc"; from: 0.97; to: 1.0; duration: 1100; easing.type: Easing.OutCubic }
        }
        SequentialAnimation {
            running: true
            PauseAnimation { duration: 350 }
            NumberAnimation { target: splashTitle; property: "opacity"; from: 0; to: 1; duration: 900; easing.type: Easing.OutCubic }
            NumberAnimation { target: splashTitle; property: "rise"; from: 14; to: 0; duration: 900; easing.type: Easing.OutCubic }
        }
        SequentialAnimation {
            running: true
            PauseAnimation { duration: 600 }
            NumberAnimation { target: splashSub; property: "opacity"; from: 0; to: 1; duration: 900; easing.type: Easing.OutCubic }
            NumberAnimation { target: splashSub; property: "rise"; from: 14; to: 0; duration: 900; easing.type: Easing.OutCubic }
        }
        SequentialAnimation {
            running: true
            PauseAnimation { duration: 1200 }
            NumberAnimation { target: splashBottom; property: "opacity"; from: 0; to: 1; duration: 800; easing.type: Easing.OutCubic }
        }
        // blink hint
        SequentialAnimation {
            running: true
            loops: Animation.Infinite
            NumberAnimation { target: splashHint; property: "opacity"; from: 1.0; to: 0.35; duration: 1200; easing.type: Easing.InOutSine }
            NumberAnimation { target: splashHint; property: "opacity"; from: 0.35; to: 1.0; duration: 1200; easing.type: Easing.InOutSine }
        }
    }


}