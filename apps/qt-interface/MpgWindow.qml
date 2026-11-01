import QtQuick
import QtQuick.Layouts
import QtQuick.Window

// MPG handwheel emulator for debugging: axis and multiplier choose how a pulse reaches the host;
// the host keeps the machine state, this window only sends the pulse.
Window {
    id: mpg
    width: 360
    height: 360
    visible: false
    objectName: "mpgWindow"
    title: "MPG-пульт · отладка"
    color: host ? host.cBackground : "#e3e7ea"

    property Item host
    property string axis: "X"
    property int step: 10
    property int pulses: 0

    function closeWindow() { if (host) host.mpgOpen = false }
    onClosing: function(close) { if (host) host.mpgOpen = false; close.accepted = true }

    function pulse(sign) {
        if (!host) return
        host.selectAxis(mpg.axis)
        host.jogStep = mpg.step
        host.machineJogRequest(sign)
        mpg.pulses += sign
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            Text { text: "MPG-ПУЛЬТ"; color: host.cForeground; font.bold: true; font.pixelSize: 15; Layout.fillWidth: true }
            Rectangle {
                Layout.preferredWidth: 26; Layout.preferredHeight: 26
                color: host.cMuted; radius: 4
                Text { anchors.centerIn: parent; text: "×"; color: host.cForeground; font.bold: true; font.pixelSize: 18 }
                MouseArea { anchors.fill: parent; onClicked: mpg.closeWindow() }
            }
        }

        Text {
            text: "Ось " + mpg.axis + " · шаг " + (mpg.step / 1000).toFixed(3) + " мм · импульсов " + mpg.pulses
            color: host.cMutedForeground; font.bold: true; font.pixelSize: 11; font.family: "JetBrains Mono"
        }

        RowLayout {
            Layout.fillWidth: true; spacing: 4
            Repeater {
                model: ["X", "Y", "Z", "A"]
                delegate: Rectangle {
                    Layout.fillWidth: true; Layout.preferredHeight: 38
                    color: mpg.axis === modelData ? host.cPrimary : host.cCard
                    border.color: mpg.axis === modelData ? host.cPrimary : host.cBorder
                    radius: 4
                    Text { anchors.centerIn: parent; text: modelData; color: mpg.axis === modelData ? host.cPrimaryFg : host.cForeground; font.bold: true; font.pixelSize: 16; font.family: "JetBrains Mono" }
                    MouseArea { anchors.fill: parent; onClicked: { mpg.axis = modelData; host.selectAxis(modelData) } }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true; spacing: 4
            Repeater {
                model: [ { s: 1, t: "×1" }, { s: 10, t: "×10" }, { s: 100, t: "×100" } ]
                delegate: Rectangle {
                    Layout.fillWidth: true; Layout.preferredHeight: 32
                    color: mpg.step === modelData.s ? host.cAccent : host.cCard
                    border.color: mpg.step === modelData.s ? host.cPrimary : host.cBorder
                    radius: 4
                    Text { anchors.centerIn: parent; text: modelData.t; color: host.cForeground; font.bold: true; font.pixelSize: 12; font.family: "JetBrains Mono" }
                    MouseArea { anchors.fill: parent; onClicked: { mpg.step = modelData.s; host.jogStep = modelData.s } }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12
            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true
                color: host.cCard; border.color: host.cBorder; radius: 8
                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 4
                    Text { text: "−"; Layout.alignment: Qt.AlignHCenter; color: host.cForeground; font.bold: true; font.pixelSize: 40 }
                    Text { text: "импульс"; Layout.alignment: Qt.AlignHCenter; color: host.cMutedForeground; font.pixelSize: 10 }
                }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: mpg.pulse(-1) }
            }
            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true
                color: host.cCard; border.color: host.cBorder; radius: 8
                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 4
                    Text { text: "+"; Layout.alignment: Qt.AlignHCenter; color: host.cForeground; font.bold: true; font.pixelSize: 40 }
                    Text { text: "импульс"; Layout.alignment: Qt.AlignHCenter; color: host.cMutedForeground; font.pixelSize: 10 }
                }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: mpg.pulse(1) }
            }
        }
    }
}
