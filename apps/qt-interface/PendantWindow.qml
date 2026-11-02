import QtQuick
import QtQuick.Layouts
import QtQuick.Window

// Full hardware console, ported from the React hardware panel reference. Logical canvas 1000x280:
// emergency stop, cycle + workplaces, mode/auxiliary keys, jog + steps, and three override knobs.
Window {
    id: panel
    width: 1200
    height: 300
    minimumWidth: 600
    minimumHeight: 150
    visible: false
    objectName: "pendantWindow"
    title: "Lynx Touch — аппаратная панель"
    color: "#23282E"

    readonly property real logicalW: 1200
    readonly property real logicalH: 300
    property bool ratioGuard: false
    onWidthChanged: if (!ratioGuard) { ratioGuard = true; height = Math.round(width * logicalH / logicalW); ratioGuard = false }
    onHeightChanged: if (!ratioGuard) { ratioGuard = true; width = Math.round(height * logicalW / logicalH); ratioGuard = false }

    property Item host

    readonly property string plate: "#2B3036"
    readonly property string edge: "#151A1F"
    readonly property string cap: "#E7E2D8"
    readonly property string capAlt: "#C9D7E8"
    readonly property string ink: "#1A1F25"
    readonly property string green: "#2F9E55"
    readonly property string amber: "#F3A518"
    readonly property string blue: "#3E7CB1"
    readonly property string red: "#B23A3A"

    function closeWindow() { if (host) host.pendantOpen = false }
    onClosing: function (close) { if (host) host.pendantOpen = false; close.accepted = true }
    function overrideValue(kind) {
        if (!host) return 100
        return kind === "feed" ? host.feedOverride : kind === "spindle" ? host.spindleOverride : host.rapidOverride
    }
    function overrideAngle(kind) { return (overrideValue(kind) - 100) * 1.2 }

    Item {
        id: canvas
        width: panel.logicalW
        height: panel.logicalH
        scale: Math.min(panel.width / panel.logicalW, panel.height / panel.logicalH)
        transformOrigin: Item.TopLeft
        x: (panel.width - panel.logicalW * scale) / 2
        y: (panel.height - panel.logicalH * scale) / 2

        Rectangle {
            anchors.fill: parent
            color: panel.plate
            radius: 12

            RowLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10

                // 1. Emergency stop
                Item {
                    Layout.preferredWidth: 128
                    Layout.fillHeight: true
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: 18
                        width: 112; height: 112; radius: 56
                        color: panel.amber
                        border.color: panel.edge; border.width: 4
                        Rectangle {
                            anchors.centerIn: parent
                            width: 88; height: 88; radius: 44
                            color: panel.red
                            border.color: "#8E1518"; border.width: 4
                            Rectangle {
                                anchors.left: parent.left; anchors.top: parent.top
                                anchors.leftMargin: 12; anchors.topMargin: 10
                                width: 26; height: 8; radius: 4
                                color: "#FFFFFF"; opacity: 0.25; rotation: -20
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: { host.machineEmergencyStop(); host.message = "Аварийный стоп"; }
                        }
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottom: parent.bottom; anchors.bottomMargin: 18
                        width: parent.width
                        text: host && host.activeAlarmCount() > 0 ? "Авария" : "Нажать для останова"
                        color: panel.cap; font.pixelSize: 10; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                    }
                }

                Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: panel.edge }

                // 2. Cycle + workplaces
                ColumnLayout {
                    Layout.preferredWidth: 252
                    Layout.fillHeight: true
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 110
                        Layout.minimumHeight: 110
                        Layout.maximumHeight: 110
                        spacing: 8
                        Repeater {
                            model: [
                                { t: "ПУСК", sub: "Enter", a: "start", c: panel.green },
                                { t: "СТОП", sub: "Space", a: "hold", c: panel.amber },
                                { t: "СБРОС", sub: "Esc", a: "reset", c: panel.blue }
                            ]
                            delegate: Rectangle {
                                Layout.fillWidth: true; Layout.fillHeight: true; radius: 8
                                color: modelData.c
                                border.color: panel.edge; border.width: 3
                                ColumnLayout {
                                    anchors.centerIn: parent
                                    spacing: 2
                                    Text { Layout.alignment: Qt.AlignHCenter; text: modelData.t; color: "#FFFFFF"; font.bold: true; font.pixelSize: 14 }
                                    Text { Layout.alignment: Qt.AlignHCenter; text: modelData.sub; color: "#FFFFFF"; opacity: 0.8; font.pixelSize: 9; font.family: "monospace" }
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: {
                                        if (modelData.a === "start") host.machineCycleStart();
                                        else if (modelData.a === "hold") host.machineFeedHold();
                                        else host.machineReset();
                                    }
                                }
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 62
                        Layout.minimumHeight: 62
                        Layout.maximumHeight: 62
                        spacing: 6
                        Repeater {
                            model: [ { id: "RUN", t: "АВТО" }, { id: "TEST", t: "ПРОГОН" }, { id: "SETUP", t: "НАЛАДКА" } ]
                            delegate: Rectangle {
                                Layout.fillWidth: true; Layout.fillHeight: true; radius: 6
                                color: host.workplace === modelData.id ? panel.capAlt : panel.cap
                                border.color: panel.edge; border.width: 3
                                Text { anchors.centerIn: parent; text: modelData.t; color: panel.ink; font.bold: true; font.pixelSize: 11 }
                                MouseArea { anchors.fill: parent; onClicked: host.setWorkplace(modelData.id) }
                            }
                        }
                    }
                }

                Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: panel.edge }

                // 3. Mode / auxiliary block (5 x 3)
                GridLayout {
                    Layout.preferredWidth: 340
                    Layout.fillHeight: true
                    columns: 5
                    rowSpacing: 6
                    columnSpacing: 6
                    Repeater {
                        model: [
                            { t: "Покадр.", k: "single", on: host.singleBlock },
                            { t: "M01", k: "opt", on: host.optStop },
                            { t: "Холост.", k: "dry", on: host.dryRun },
                            { t: "Прогон", k: "test", on: host.workplace === "TEST" },
                            { t: "Безоп.", k: "safe", on: false },
                            { t: "Зажим", k: "clamp", on: host.toolClamped },
                            { t: "Разжим", k: "unclamp", on: !host.toolClamped },
                            { t: "M3", k: "m3", on: host.spindleOn && host.spindleDirection === "M03" },
                            { t: "M4", k: "m4", on: host.spindleOn && host.spindleDirection === "M04" },
                            { t: "M5", k: "m5", on: !host.spindleOn },
                            { t: "СОЖ", k: "coolant", on: host.coolantOn },
                            { t: "Туман", k: "mist", on: host.mistOn },
                            { t: "Смазка", k: "lube", on: host.lubeOn },
                            { t: "Свет", k: "light", on: host.workLightOn },
                            { t: "Стружка", k: "chip", on: host.chipConveyorOn }
                        ]
                        delegate: Rectangle {
                            Layout.fillWidth: true; Layout.fillHeight: true; radius: 6
                            color: modelData.on ? panel.capAlt : panel.cap
                            border.color: panel.edge; border.width: 3
                            Text { anchors.centerIn: parent; text: modelData.t; color: panel.ink; font.bold: true; font.pixelSize: 11 }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    if (modelData.k === "single") host.singleBlock = !host.singleBlock;
                                    else if (modelData.k === "opt") host.optStop = !host.optStop;
                                    else if (modelData.k === "dry") host.dryRun = !host.dryRun;
                                    else if (modelData.k === "test") host.setWorkplace(host.workplace === "TEST" ? "RUN" : "TEST");
                                    else if (modelData.k === "safe") host.goSafePoint();
                                    else if (modelData.k === "clamp") { host.toolClamped = true; host.message = "Инструмент зажат"; }
                                    else if (modelData.k === "unclamp") { host.toolClamped = false; host.message = "Инструмент разжат"; }
                                    else if (modelData.k === "m3") host.spindleCommand("M03");
                                    else if (modelData.k === "m4") host.spindleCommand("M04");
                                    else if (modelData.k === "m5") host.spindleCommand("M05");
                                    else host.toggleAux(modelData.k);
                                }
                            }
                        }
                    }
                }

                Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: panel.edge }

                // 4. Jog 3x3 + steps
                RowLayout {
                    Layout.preferredWidth: 196
                    Layout.fillHeight: true
                    spacing: 6
                    Grid {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        columns: 3
                        rows: 3
                        rowSpacing: 6
                        columnSpacing: 6
                        Repeater {
                            model: [
                                { t: "Z+", ax: "Z", d: 1 }, { t: "Y+", ax: "Y", d: 1 }, { t: "A+", ax: "A", d: 1 },
                                { t: "X−", ax: "X", d: -1 }, { t: "Быстро", ax: "", d: 0 }, { t: "X+", ax: "X", d: 1 },
                                { t: "Z−", ax: "Z", d: -1 }, { t: "Y−", ax: "Y", d: -1 }, { t: "A−", ax: "A", d: -1 }
                            ]
                            delegate: Rectangle {
                                width: 56; height: 56; radius: 6
                                color: modelData.ax === "" ? panel.amber : panel.cap
                                border.color: panel.edge; border.width: 3
                                Text { anchors.centerIn: parent; text: modelData.t; color: panel.ink; font.bold: true; font.pixelSize: 12; font.family: "monospace" }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: {
                                        if (modelData.ax === "") host.rapidJog = !host.rapidJog;
                                        else { host.selectAxis(modelData.ax); host.machineJogRequest(modelData.d); }
                                    }
                                }
                            }
                        }
                    }
                    Column {
                        Layout.preferredWidth: 60
                        Layout.fillHeight: true
                        spacing: 6
                        Repeater {
                            model: [0, 1, 2]
                            delegate: Rectangle {
                                width: 60; height: 56; radius: 6
                                color: host.jogStep === index ? panel.capAlt : panel.cap
                                border.color: panel.edge; border.width: 3
                                Text { anchors.centerIn: parent; text: ["0.001", "0.01", "0.1"][index]; color: panel.ink; font.bold: true; font.pixelSize: 11; font.family: "monospace" }
                                MouseArea { anchors.fill: parent; onClicked: host.jogStep = index }
                            }
                        }
                    }
                }

                Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: panel.edge }

                // 5. Overrides: feed / spindle / rapid
                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 8
                    Repeater {
                        model: [
                            { label: "ПОДАЧА", kind: "feed" },
                            { label: "ШПИНДЕЛЬ", kind: "spindle" },
                            { label: "УСКОР. G0", kind: "rapid" }
                        ]
                        delegate: ColumnLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: 2
                            Item {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                Rectangle {
                                    anchors.centerIn: parent
                                    width: 78; height: 78; radius: 39
                                    color: panel.edge
                                    border.color: "#5A636C"; border.width: 2
                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: 62; height: 62; radius: 31
                                        color: panel.cap
                                        Rectangle {
                                            width: 3; height: 24; radius: 2
                                            color: panel.ink
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            y: 6
                                            rotation: panel.overrideAngle(modelData.kind)
                                            transformOrigin: Item.Bottom
                                        }
                                    }
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: function (mouse) {
                                        var d = mouse.x < width / 2 ? -1 : 1;
                                        if (modelData.kind === "feed") host.adjustFeed(d);
                                        else if (modelData.kind === "spindle") host.adjustSpindle(d);
                                        else host.adjustRapid(d);
                                    }
                                    onWheel: function (w) {
                                        var d = w.angleDelta.y > 0 ? 1 : -1;
                                        if (modelData.kind === "feed") host.adjustFeed(d);
                                        else if (modelData.kind === "spindle") host.adjustSpindle(d);
                                        else host.adjustRapid(d);
                                    }
                                }
                            }
                            Text { Layout.alignment: Qt.AlignHCenter; text: modelData.label; color: panel.cap; font.pixelSize: 9; font.letterSpacing: 0.5 }
                            Text { Layout.alignment: Qt.AlignHCenter; text: panel.overrideValue(modelData.kind) + "%"; color: panel.cap; font.bold: true; font.pixelSize: 14; font.family: "monospace" }
                        }
                    }
                }
            }
        }
    }
}

