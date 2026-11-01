import QtQuick
import QtQuick.Layouts
import QtQuick.Window

// Bottom G-code keyboard of the physical console (React HardwareKeyboard): the same 72 px caps,
// digits and the two signs in the main grid, the address letters on the pale keys, Shift with the
// alternate symbols. It types into the MDI line; with no input field it explains why nothing
// happened instead of staying silent.
Window {
    id: kb
    width: 700
    height: 360
    visible: false
    objectName: "keyboardWindow"
    title: "Lynx Touch — клавиатура оператора"
    color: "#2B3036"

    property Item host
    property string shiftMode: "off"
    property string hint: ""

    readonly property string plate: "#2B3036"
    readonly property string edge: "#151A1F"
    readonly property string cap: "#E7E2D8"
    readonly property string capAlt: "#C9D7E8"
    readonly property string ink: "#1A1F25"
    readonly property string amber: "#F3A518"

    readonly property var keys: [
        { m: "1" }, { m: "2" }, { m: "3" }, { m: "4" }, { m: "5" }, { m: "6" },
        { m: "7" }, { m: "8" }, { m: "9" }, { m: "0" }, { m: "-" }, { m: "." },
        { m: "G", alt: "#", addr: true }, { m: "M", alt: "%", addr: true }, { m: "X", alt: "(", addr: true },
        { m: "Y", alt: ")", addr: true }, { m: "Z", alt: "[", addr: true }, { m: "S", alt: "]", addr: true },
        { m: "F", alt: "=", addr: true }, { m: "T", alt: "*", addr: true }, { m: "N", alt: "+", addr: true },
        { m: "O", alt: "/", addr: true }, { m: "A", alt: ",", addr: true }, { m: "B", alt: ";", addr: true },
        { m: "Shift", kind: "shift" }, { m: "C", alt: ":", addr: true }, { m: "D", alt: "_", addr: true },
        { m: "Пробел", kind: "space" }, { m: "Стереть", kind: "clear" }, { m: "⌫", kind: "back" }
    ]

    function closeWindow() { if (host) host.keyboardOpen = false }
    onClosing: function (close) { if (host) host.keyboardOpen = false; close.accepted = true }

    function flash(text) {
        hint = text
        hintTimer.restart()
    }
    function cycleShift() {
        shiftMode = shiftMode === "off" ? "once" : shiftMode === "once" ? "lock" : "off"
    }
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
        var ch = (shiftMode !== "off" && key.alt) ? key.alt : key.m
        typeText(ch)
        if (shiftMode === "once") shiftMode = "off"
    }

    Timer { id: hintTimer; interval: 2500; onTriggered: kb.hint = "" }

    Rectangle {
        anchors.fill: parent
        color: kb.plate
        radius: 10

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 10

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 28
                color: kb.edge
                radius: 4
                Text {
                    anchors.centerIn: parent
                    text: kb.hint !== "" ? kb.hint
                                          : (host && host.mdiOpen() ? "MDI> " + host.mdiBuffer : "Клавиатура: выберите поле ввода (MDI в НАЛАДКЕ)")
                    color: kb.amber
                    font.bold: true
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }

            GridLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                columns: 6
                rowSpacing: 8
                columnSpacing: 8
                Repeater {
                    model: kb.keys
                    delegate: Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 6
                        color: modelData.kind === "shift" ? (kb.shiftMode !== "off" ? kb.amber : kb.capAlt)
                             : modelData.addr ? kb.capAlt
                             : kb.cap
                        border.color: kb.edge
                        border.width: 3
                        ColumnLayout {
                            anchors.centerIn: parent
                            spacing: 1
                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                text: modelData.kind === "shift"
                                        ? (kb.shiftMode === "lock" ? "Shift 🔒" : "Shift")
                                        : (kb.shiftMode !== "off" && modelData.alt ? modelData.alt : modelData.m)
                                color: kb.ink
                                font.bold: true
                                font.pixelSize: modelData.kind === "space" ? 12 : 18
                                font.family: host ? host.fontMono : "monospace"
                            }
                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                visible: modelData.kind === "shift"
                                text: kb.shiftMode === "off" ? "выкл." : kb.shiftMode === "once" ? "1 буква" : "зажат"
                                color: kb.ink
                                font.pixelSize: 9
                            }
                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                visible: modelData.alt !== undefined && modelData.kind !== "shift"
                                text: kb.shiftMode !== "off" ? modelData.m : modelData.alt
                                color: kb.ink
                                opacity: 0.7
                                font.pixelSize: 10
                                font.family: host ? host.fontMono : "monospace"
                            }
                        }
                        MouseArea { anchors.fill: parent; onClicked: kb.press(modelData) }
                    }
                }
            }
        }
    }
}
