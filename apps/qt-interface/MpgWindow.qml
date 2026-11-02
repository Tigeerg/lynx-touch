import QtQuick
import QtQuick.Layouts
import QtQuick.Window

// MPG pendant emulator: not a row of buttons but the real handwheel layout — a round encoder in
// the middle, a rotary axis switch and a rotary multiplier switch. One detent is 18 degrees and
// sends exactly one jog pulse to the host; the host still owns the machine state.
Window {
    id: mpg
    width: 520
    height: 800
    visible: false
    objectName: "mpgWindow"
    title: "MPG-пульт — маховик"
    color: "#23282E"
    minimumWidth: 260
    minimumHeight: 400

    readonly property real logicalW: 520
    readonly property real logicalH: 800
    property bool ratioGuard: false
    onWidthChanged: if (!ratioGuard) { ratioGuard = true; height = Math.round(width * logicalH / logicalW); ratioGuard = false }
    onHeightChanged: if (!ratioGuard) { ratioGuard = true; width = Math.round(height * logicalW / logicalH); ratioGuard = false }

    property Item host
    property string axis: "X"
    property int stepIndex: 1          // 0 = 0.001, 1 = 0.01, 2 = 0.1 mm
    property int pulses: 0
    property real encoderAngle: 0
    property real encoderAccum: 0
    property real lastPointerAngle: -9999

    readonly property var axisList: ["X", "Y", "Z", "A"]
    readonly property var stepList: [
        { m: "×1", v: "0.001 мм" },
        { m: "×10", v: "0.01 мм" },
        { m: "×100", v: "0.1 мм" }
    ]
    readonly property string plate: "#2B3036"
    readonly property string edge: "#14181D"
    readonly property string metal: "#3A4149"
    readonly property string cap: "#E7E2D8"
    readonly property string ink: "#1A1F25"
    readonly property string amber: "#F3A518"
    readonly property string red: "#D42125"

    function closeWindow() { if (host) host.mpgOpen = false }
    onClosing: function (close) { if (host) host.mpgOpen = false; close.accepted = true }

    function pulse(sign) {
        if (!host) return
        host.selectAxis(mpg.axis)
        host.jogStep = mpg.stepIndex
        host.machineJogRequest(sign)
        mpg.pulses += sign
    }
    function angleAt(mx, my) {
        var dx = mx - encoder.width / 2
        var dy = my - encoder.height / 2
        return Math.atan2(dy, dx) * 180 / Math.PI
    }
    function encoderStart(mx, my) { lastPointerAngle = angleAt(mx, my) }
    function encoderMove(mx, my) {
        var a = angleAt(mx, my)
        if (lastPointerAngle < -9000) { lastPointerAngle = a; return }
        var d = a - lastPointerAngle
        if (d > 180) d -= 360
        if (d < -180) d += 360
        if (Math.abs(d) > 60) { lastPointerAngle = a; return }   // crossing the seam
        lastPointerAngle = a
        encoderAngle += d
        encoderAccum += d
        while (encoderAccum >= 18) { pulse(1); encoderAccum -= 18 }
        while (encoderAccum <= -18) { pulse(-1); encoderAccum += 18 }
    }
    function wheel(delta) {
        encoderAngle += delta > 0 ? 18 : -18
        encoderAccum += delta > 0 ? 18 : -18
        while (encoderAccum >= 18) { pulse(1); encoderAccum -= 18 }
        while (encoderAccum <= -18) { pulse(-1); encoderAccum += 18 }
    }

    Item {
        id: canvas
        width: mpg.logicalW
        height: mpg.logicalH
        scale: Math.min(mpg.width / mpg.logicalW, mpg.height / mpg.logicalH)
        transformOrigin: Item.TopLeft
        x: (mpg.width - mpg.logicalW * scale) / 2
        y: (mpg.height - mpg.logicalH * scale) / 2

    Rectangle {
        anchors.fill: parent
        color: mpg.plate
        radius: 14

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 8

            // Header
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 44
                spacing: 8
                Rectangle {
                    Layout.preferredWidth: 42; Layout.preferredHeight: 42
                    color: mpg.edge; radius: 8
                    Text { anchors.centerIn: parent; text: "MPG"; color: mpg.amber; font.bold: true; font.pixelSize: 12; font.family: "monospace" }
                }
                ColumnLayout { Layout.fillWidth: true; spacing: 0
                    Text { text: "РУЧНОЙ ПУЛЬТ"; color: mpg.cap; font.bold: true; font.pixelSize: 15; font.letterSpacing: 1 }
                    Text { text: "маховик · ось · множитель"; color: "#9AA3AD"; font.pixelSize: 10 }
                }
                Rectangle {
                    Layout.preferredWidth: 34; Layout.preferredHeight: 34
                    color: mpg.edge; radius: 6
                    Text { anchors.centerIn: parent; text: "×"; color: mpg.cap; font.bold: true; font.pixelSize: 18 }
                    MouseArea { anchors.fill: parent; onClicked: mpg.closeWindow() }
                }
            }

            // Emergency stop
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 104; Layout.preferredHeight: 104
                radius: 52
                color: mpg.edge
                Rectangle {
                    anchors.centerIn: parent
                    width: 86; height: 86; radius: 43
                    color: mpg.red
                    border.color: "#8E1518"; border.width: 4
                    Text { anchors.centerIn: parent; text: "STOP"; color: "#FFFFFF"; font.bold: true; font.pixelSize: 15 }
                    MouseArea { anchors.fill: parent; onClicked: host.machineEmergencyStop() }
                }
            }

            // Round encoder
            Item {
                id: encoder
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 268; Layout.preferredHeight: 268
                Rectangle {
                    anchors.fill: parent
                    radius: width / 2
                    color: "#1B2025"
                    border.color: mpg.edge; border.width: 4
                }
                Repeater {
                    model: 40
                    delegate: Rectangle {
                        width: index % 5 === 0 ? 3 : 2
                        height: index % 5 === 0 ? 18 : 12
                        color: index % 5 === 0 ? mpg.amber : "#8B949E"
                        x: encoder.width / 2 - width / 2
                        y: 8
                        transformOrigin: Item.Bottom
                        rotation: index * 9
                        transform: Rotation {
                            origin.x: width / 2
                            origin.y: encoder.height / 2 - 8
                            angle: index * 9
                        }
                    }
                }
                Rectangle {
                    anchors.centerIn: parent
                    width: 196; height: 196; radius: 98
                    color: mpg.metal
                    border.color: "#5A636C"; border.width: 3
                    rotation: mpg.encoderAngle
                    Repeater {
                        model: 36
                        delegate: Rectangle {
                            width: 2; height: 14; color: "#2A3036"
                            x: 98 - width / 2; y: 6
                            transformOrigin: Item.Bottom
                            transform: Rotation {
                                origin.x: width / 2
                                origin.y: 98 - 6
                                angle: index * 10
                            }
                        }
                    }
                    Rectangle {
                        width: 10; height: 34; radius: 5
                        color: mpg.amber
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: 8
                    }
                    Rectangle {
                        anchors.centerIn: parent
                        width: 46; height: 46; radius: 23
                        color: mpg.edge
                        border.color: "#6B747D"; border.width: 2
                    }
                }
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    anchors.topMargin: -2
                    width: 14; height: 8; radius: 3
                    color: mpg.amber
                }
                MouseArea {
                    anchors.fill: parent
                    onPressed: function (mouse) { mpg.encoderStart(mouse.x, mouse.y) }
                    onPositionChanged: function (mouse) { mpg.encoderMove(mouse.x, mouse.y) }
                    onReleased: mpg.lastPointerAngle = -9999
                    onWheel: function (wheel) { mpg.wheel(wheel.angleDelta.y) }
                }
            }

            Text {
                Layout.alignment: Qt.AlignHCenter
                text: mpg.pulses === 0 ? "0 импульсов"
                                              : (mpg.pulses > 0 ? "+" : "") + mpg.pulses + " имп. · " +
                                                mpg.axis + " " + mpg.stepList[mpg.stepIndex].v
                color: mpg.amber
                font.bold: true
                font.pixelSize: 14
                font.family: "monospace"
            }

            // Rotary axis and multiplier switches
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 14

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredHeight: 190
                    Rectangle {
                        anchors.centerIn: parent
                        width: 164; height: 164; radius: 82
                        color: "#1B2025"
                        border.color: mpg.edge; border.width: 3
                        Rectangle {
                            anchors.centerIn: parent
                            width: 112; height: 112; radius: 56
                            color: mpg.metal
                            border.color: "#5A636C"; border.width: 2
                            Text {
                                anchors.centerIn: parent
                                text: mpg.axis
                                color: mpg.amber
                                font.bold: true
                                font.pixelSize: 38
                                font.family: "monospace"
                            }
                        }
                    }
                    Repeater {
                        model: mpg.axisList
                        delegate: Rectangle {
                            width: 34; height: 34; radius: 17
                            x: parent.width / 2 + 70 * Math.sin(index * Math.PI / 2) - width / 2
                            y: parent.height / 2 - 70 * Math.cos(index * Math.PI / 2) - height / 2
                            color: mpg.axis === modelData ? mpg.amber : mpg.cap
                            Text { anchors.centerIn: parent; text: modelData; color: mpg.ink; font.bold: true; font.pixelSize: 14; font.family: "monospace" }
                            MouseArea { anchors.fill: parent; onClicked: { mpg.axis = modelData; if (host) host.selectAxis(modelData); mpg.pulses = 0 } }
                        }
                    }
                    Text { anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; text: "ОСЬ"; color: "#9AA3AD"; font.bold: true; font.pixelSize: 10; font.letterSpacing: 1 }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredHeight: 190
                    Rectangle {
                        anchors.centerIn: parent
                        width: 164; height: 164; radius: 82
                        color: "#1B2025"
                        border.color: mpg.edge; border.width: 3
                        Rectangle {
                            anchors.centerIn: parent
                            width: 112; height: 112; radius: 56
                            color: mpg.metal
                            border.color: "#5A636C"; border.width: 2
                            ColumnLayout {
                                anchors.centerIn: parent
                                spacing: 0
                                Text { Layout.alignment: Qt.AlignHCenter; text: mpg.stepList[mpg.stepIndex].m; color: mpg.amber; font.bold: true; font.pixelSize: 24; font.family: "monospace" }
                                Text { Layout.alignment: Qt.AlignHCenter; text: mpg.stepList[mpg.stepIndex].v; color: "#C9D1D9"; font.pixelSize: 9 }
                            }
                        }
                    }
                    Repeater {
                        model: mpg.stepList
                        delegate: Rectangle {
                            width: 44; height: 34; radius: 17
                            x: parent.width / 2 + 70 * Math.sin((index - 1) * Math.PI / 3) - width / 2
                            y: parent.height / 2 - 70 * Math.cos((index - 1) * Math.PI / 3) - height / 2
                            color: mpg.stepIndex === index ? mpg.amber : mpg.cap
                            Text { anchors.centerIn: parent; text: modelData.m; color: mpg.ink; font.bold: true; font.pixelSize: 12; font.family: "monospace" }
                            MouseArea { anchors.fill: parent; onClicked: { mpg.stepIndex = index; if (host) host.jogStep = index; mpg.pulses = 0 } }
                        }
                    }
                    Text { anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; text: "МНОЖИТЕЛЬ"; color: "#9AA3AD"; font.bold: true; font.pixelSize: 10; font.letterSpacing: 1 }
                }
            }
        }
    }
    }
}

