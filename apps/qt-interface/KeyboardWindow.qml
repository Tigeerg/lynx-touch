import QtQuick
import QtQuick.Layouts
import QtQuick.Window

// Bottom G-code keyboard, ported from the React HardwareKeyboard reference. Logical canvas
// 500x470: six 72 px columns, five rows, the same white/blue/amber caps and Shift/alt legends.
Window {
    id: kb
    width: 500
    height: 470
    minimumWidth: 250
    minimumHeight: 235
    visible: false
    objectName: "keyboardWindow"
    title: "Lynx Touch — клавиатура оператора"
    color: "#2B3036"

    readonly property real logicalW: 500
    readonly property real logicalH: 470
    property bool ratioGuard: false
    onWidthChanged: if (!ratioGuard) { ratioGuard = true; height = Math.round(width * logicalH / logicalW); ratioGuard = false }
    onHeightChanged: if (!ratioGuard) { ratioGuard = true; width = Math.round(height * logicalW / logicalH); ratioGuard = false }

    property Item host
    property string shiftMode: "off"
    property string hint: ""

    readonly property string plate: "#2B3036"
    readonly property string edge: "#151A1F"
    readonly property string cap: "#E7E2D8"
    readonly property string capAlt: "#C9D7E8"
    readonly property string ink: "#1A1F25"
    readonly property string amber: "#F3A518"
    readonly property string blue: "#3E7CB1"

    readonly property var keys: [
        { m: "1" }, { m: "2" }, { m: "3" }, { m: "4" }, { m: "5" }, { m: "6" },
        { m: "7" }, { m: "8" }, { m: "9" }, { m: "0" }, { m: "-" }, { m: "." },
        { m: "G", alt: "#", addr: true }, { m: "M", alt: "%", addr: true }, { m: "X", alt: "(", addr: true },
        { m: "Y", alt: ")", addr: true }, { m: "Z", alt: "[", addr: true }, { m: "S", alt: "]", addr: true },
        { m: "F", alt: "=", addr: true }, { m: "T", alt: "*", addr: true }, { m: "N", alt: "+", addr: true },
        { m: "O", alt: "/", addr: true }, { m: "A", alt: ",", addr: true }, { m: "B", alt: ";", addr: true },
        { m: "Shift", kind: "shift" }, { m: "C", alt: ":", addr: true }, { m: "D", alt: "_", addr: true },
        { m: "Пробел", kind: "space" }, { m: "Очист.", kind: "clear" }, { m: "⌫", kind: "back" }
    ]

    function closeWindow() { if (host) host.keyboardOpen = false }
    onClosing: function (close) { if (host) host.keyboardOpen = false; close.accepted = true }

    function flash(text) { hint = text; hintTimer.restart() }
    function cycleShift() { shiftMode = shiftMode === "off" ? "once" : shiftMode === "once" ? "lock" : "off" }
    function typeText(text) {
        if (host && host.mdiOpen()) { host.mdiType(text); hint = ""; }
        else flash("Нет поля ввода: откройте MDI в НАЛАДКЕ")
    }
    function press(key) {
        if (key.kind === "shift") { cycleShift(); return }
        if (key.kind === "space") { typeText(" "); return }
        if (key.kind === "back") {
            if (host && host.mdiOpen()) host.mdiBackspace(); else flash("Нет поля ввода: откройте MDI в НАЛАДКЕ");
            return
        }
        if (key.kind === "clear") {
            if (host && host.mdiOpen()) host.mdiBuffer = ""; else flash("Нет поля ввода: откройте MDI в НАЛАДКЕ");
            return
        }
        typeText((shiftMode !== "off" && key.alt) ? key.alt : key.m)
        if (shiftMode === "once") shiftMode = "off"
    }

    Timer { id: hintTimer; interval: 2500; onTriggered: kb.hint = "" }

    Item {
        id: canvas
        width: kb.logicalW
        height: kb.logicalH
        scale: Math.min(kb.width / kb.logicalW, kb.height / kb.logicalH)
        transformOrigin: Item.TopLeft
        x: (kb.width - kb.logicalW * scale) / 2
        y: (kb.height - kb.logicalH * scale) / 2

        Rectangle {
            anchors.fill: parent
            color: kb.plate
            radius: 10

            Rectangle {
                x: 14; y: 10
                width: parent.width - 28; height: 26
                color: kb.edge; radius: 4
                Text {
                    anchors.centerIn: parent
                    text: kb.hint !== "" ? kb.hint
                                          : (host && host.mdiOpen() ? "MDI> " + host.mdiBuffer : "Клавиатура: выберите поле ввода (MDI в НАЛАДКЕ)")
                    color: kb.amber; font.bold: true; font.pixelSize: 12; elide: Text.ElideRight
                }
            }

            GridLayout {
                x: 14; y: 46
                columns: 6
                rowSpacing: 8
                columnSpacing: 8
                Repeater {
                    model: kb.keys
                    delegate: Rectangle {
                        Layout.preferredWidth: 72
                        Layout.preferredHeight: 72
                        Layout.minimumWidth: 72
                        Layout.minimumHeight: 72
                        radius: 8
                        color: modelData.kind === "shift" ? (kb.shiftMode !== "off" ? kb.amber : kb.blue)
                             : modelData.kind === "back" ? kb.amber
                             : modelData.addr ? kb.capAlt
                             : kb.cap
                        border.color: kb.edge
                        border.width: 3
                        Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 5; color: kb.edge; radius: 2 }

                        Text {
                            anchors.centerIn: parent
                            text: modelData.kind === "shift"
                                    ? (kb.shiftMode === "lock" ? "Shift 🔒" : "Shift")
                                    : (kb.shiftMode !== "off" && modelData.alt ? modelData.alt : modelData.m)
                            color: kb.ink
                            font.bold: true
                            font.pixelSize: (modelData.kind === "space" || modelData.kind === "clear") ? 12 : 28
                            font.family: host ? host.fontMono : "monospace"
                        }
                        Text {
                            anchors.right: parent.right; anchors.rightMargin: 4; anchors.top: parent.top; anchors.topMargin: 3
                            visible: modelData.alt !== undefined && modelData.kind !== "shift"
                            text: (kb.shiftMode !== "off" ? modelData.m : modelData.alt) || ""
                            color: kb.ink; opacity: 0.75; font.pixelSize: 12; font.bold: true
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; anchors.bottomMargin: 8
                            visible: modelData.kind === "shift"
                            text: kb.shiftMode === "off" ? "выкл." : kb.shiftMode === "once" ? "1 буква" : "зажат"
                            color: kb.ink; font.pixelSize: 9
                        }
                        MouseArea { anchors.fill: parent; onClicked: kb.press(modelData) }
                    }
                }
            }
        }
    }
}
