import QtQuick
import QtQuick.Layouts
import QtQuick.Window

// Side navigation keys, ported from the React HardwareNav reference: five screen keys in one row,
// then Enter/Up/Cancel/PgUp and Left/Down/Right/PgDn in a 4x2 block.
Window {
    id: nav
    width: 320
    height: 300
    minimumWidth: 200
    minimumHeight: 188
    visible: false
    objectName: "navWindow"
    title: "Lynx Touch — клавиши навигации"
    color: "#2B3036"

    readonly property real logicalW: 320
    readonly property real logicalH: 300
    property bool ratioGuard: false
    onWidthChanged: if (!ratioGuard) { ratioGuard = true; height = Math.round(width * logicalH / logicalW); ratioGuard = false }
    onHeightChanged: if (!ratioGuard) { ratioGuard = true; width = Math.round(height * logicalW / logicalH); ratioGuard = false }

    property Item host

    readonly property string plate: "#2B3036"
    readonly property string edge: "#151A1F"
    readonly property string cap: "#E7E2D8"
    readonly property string capAlt: "#C9D7E8"
    readonly property string green: "#2F9E55"
    readonly property string red: "#B23A3A"
    readonly property string blue: "#3E7CB1"
    readonly property string darkKey: "#6B7178"
    readonly property string ink: "#1A1F25"

    readonly property var tabs: [
        { n: "1", title: "Работа", icon: "\ue121", target: "work" },
        { n: "2", title: "Смещения", icon: "\ue0ac", target: "offset" },
        { n: "3", title: "Инструмент", icon: "\ue1b1", target: "tool" },
        { n: "4", title: "Сообщения", icon: "\ue3b3", target: "alarm" },
        { n: "5", title: "Сервис", icon: "\ue603", target: "service" }
    ]

    function closeWindow() { if (host) host.navOpen = false }
    onClosing: function (close) { if (host) host.navOpen = false; close.accepted = true }
    function isWork(t) { return t === "work" && (host.activeScreen === "pos" || host.activeScreen === "prog") }
    function goTab(t) {
        if (!host) return
        if (t === "work") host.activeScreen = host.workplace === "SETUP" ? "pos" : "prog";
        else host.activeScreen = t;
    }
    function enter() {
        if (!host) return
        if (host.activeScreen === "service") host.svcEnter();
        else if (host.mdiOpen()) host.runMdi();
        else host.machineCycleStart();
    }
    function cancel() {
        if (!host) return
        if (host.activeScreen === "service") host.svcCancel();
        else if (host.mdiOpen()) host.mdiBuffer = "";
    }

    Item {
        id: canvas
        width: nav.logicalW
        height: nav.logicalH
        scale: Math.min(nav.width / nav.logicalW, nav.height / nav.logicalH)
        transformOrigin: Item.TopLeft
        x: (nav.width - nav.logicalW * scale) / 2
        y: (nav.height - nav.logicalH * scale) / 2

        Rectangle {
            anchors.fill: parent
            color: nav.plate
            radius: 10

            // Screen keys
            Row {
                x: 18; y: 14
                spacing: 6
                Repeater {
                    model: nav.tabs
                    delegate: Rectangle {
                        width: 52; height: 52; radius: 8
                        color: nav.isWork(modelData.target) || host.activeScreen === modelData.target ? nav.capAlt : nav.cap
                        border.color: nav.edge; border.width: 3
                        Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 4; color: nav.edge; radius: 2 }
                        Rectangle {
                            x: 4; y: 4; width: 16; height: 16; radius: 3; color: nav.ink
                            Text { anchors.centerIn: parent; text: modelData.n; color: nav.cap; font.bold: true; font.pixelSize: 9; font.family: "monospace" }
                        }
                        Text { anchors.centerIn: parent; text: modelData.icon; color: nav.ink; font.family: "lucide"; font.pixelSize: 20 }
                        Text { anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; anchors.bottomMargin: 5; text: modelData.title; color: nav.ink; font.bold: true; font.pixelSize: 7 }
                        MouseArea { anchors.fill: parent; onClicked: nav.goTab(modelData.target) }
                    }
                }
            }

            // Navigation block: Enter, Up, Cancel, PgUp / Left, Down, Right, PgDn
            Grid {
                x: 25; y: 84
                columns: 4
                rows: 2
                rowSpacing: 8
                columnSpacing: 8
                Repeater {
                    model: [
                        { t: "Ввод", c: nav.green, k: "enter" },
                        { t: "▲", c: nav.cap, k: "up" },
                        { t: "Отмена", c: nav.red, k: "cancel" },
                        { t: "PgUp", c: nav.blue, k: "pgup" },
                        { t: "◀", c: nav.darkKey, k: "left" },
                        { t: "▼", c: nav.cap, k: "down" },
                        { t: "▶", c: nav.darkKey, k: "right" },
                        { t: "PgDn", c: nav.blue, k: "pgdn" }
                    ]
                    delegate: Rectangle {
                        width: 60; height: 60; radius: 8
                        color: modelData.c
                        border.color: nav.edge; border.width: 3
                        Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 4; color: nav.edge; radius: 2 }
                        Text {
                            anchors.centerIn: parent
                            text: modelData.t
                            color: index === 0 || index === 2 ? "#FFFFFF" : nav.ink
                            font.bold: true
                            font.pixelSize: modelData.t.length > 2 ? 12 : 22
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                if (modelData.k === "enter") nav.enter();
                                else if (modelData.k === "cancel") nav.cancel();
                                else if (modelData.k === "up") host.navUp();
                                else if (modelData.k === "down") host.navDown();
                                else if (modelData.k === "left") host.machineJogRequest(-1);
                                else if (modelData.k === "right") host.machineJogRequest(1);
                                else if (modelData.k === "pgup") host.pageCursor(-10);
                                else if (modelData.k === "pgdn") host.pageCursor(10);
                            }
                        }
                    }
                }
            }
        }
    }
}
