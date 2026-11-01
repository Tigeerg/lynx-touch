import QtQuick
import QtQuick.Layouts
import QtQuick.Window

// Side navigation keyboard of the physical console (React HardwareNav): the five screen keys,
// Enter/Cancel and the cursor/page block. It only sends host actions; the screen sets and guards
// stay in main.qml, exactly as for the F-keys.
Window {
    id: nav
    width: 310
    height: 640
    visible: false
    objectName: "navWindow"
    title: "Lynx Touch — клавиши навигации"
    color: "#2B3036"

    property Item host

    readonly property string plate: "#2B3036"
    readonly property string edge: "#151A1F"
    readonly property string cap: "#E7E2D8"
    readonly property string capAlt: "#C9D7E8"
    readonly property string green: "#2F9E55"
    readonly property string red: "#D42125"
    readonly property string blue: "#0362BB"
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

    function isWorkTab(t) { return t === "work" && (host.activeScreen === "pos" || host.activeScreen === "prog") }
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
    function leftRight(dir) {
        if (!host) return
        if (host.activeScreen === "service") host.adjustParam(dir, false);
        else host.machineJogRequest(dir);
    }

    Rectangle {
        anchors.fill: parent
        color: nav.plate
        radius: 10

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 10

            // Screen keys 1..5
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 250
                color: nav.edge
                radius: 8
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6
                    Repeater {
                        model: nav.tabs
                        delegate: Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: 6
                            color: nav.isWorkTab(modelData.target) || host.activeScreen === modelData.target ? nav.capAlt : nav.cap
                            border.color: nav.edge
                            border.width: 3
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                spacing: 8
                                Rectangle {
                                    Layout.preferredWidth: 30; Layout.preferredHeight: 22
                                    color: nav.ink; radius: 3
                                    Text { anchors.centerIn: parent; text: modelData.n; color: nav.cap; font.bold: true; font.pixelSize: 12; font.family: "monospace" }
                                }
                                Text { text: modelData.icon; color: nav.ink; font.family: "lucide"; font.pixelSize: 20 }
                                Text { text: modelData.title; Layout.fillWidth: true; color: nav.ink; font.bold: true; font.pixelSize: 13 }
                            }
                            MouseArea { anchors.fill: parent; onClicked: nav.goTab(modelData.target) }
                        }
                    }
                }
            }

            // Navigation block: Enter, Up, Cancel / Left, Down, Right / PgUp, PgDn
            GridLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                columns: 3
                rowSpacing: 8
                columnSpacing: 8
                Repeater {
                    model: [
                        { t: "Ввод", kind: "enter", color: nav.green },
                        { t: "▲", kind: "up" },
                        { t: "Отмена", kind: "cancel", color: nav.red },
                        { t: "◀", kind: "left" },
                        { t: "▼", kind: "down" },
                        { t: "▶", kind: "right" },
                        { t: "PgUp", kind: "pgup" },
                        { t: "", kind: "none" },
                        { t: "PgDn", kind: "pgdn" }
                    ]
                    delegate: Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 6
                        color: modelData.kind === "none" ? "transparent" : (modelData.color ? modelData.color : nav.cap)
                        border.color: nav.edge
                        border.width: modelData.kind === "none" ? 0 : 3
                        enabled: modelData.kind !== "none"
                        Text {
                            anchors.centerIn: parent
                            text: modelData.t
                            color: modelData.color ? "#FFFFFF" : nav.ink
                            font.bold: true
                            font.pixelSize: modelData.t.length > 2 ? 13 : 20
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                if (modelData.kind === "enter") nav.enter();
                                else if (modelData.kind === "cancel") nav.cancel();
                                else if (modelData.kind === "up") host.navUp();
                                else if (modelData.kind === "down") host.navDown();
                                else if (modelData.kind === "left") nav.leftRight(-1);
                                else if (modelData.kind === "right") nav.leftRight(1);
                                else if (modelData.kind === "pgup") host.pageCursor(-10);
                                else if (modelData.kind === "pgdn") host.pageCursor(10);
                            }
                        }
                    }
                }
            }
        }
    }
}
