import QtQuick
import QtQuick.Layouts
import QtQuick.Window

// Virtual pendant for debugging on real hardware before the physical console exists.
// The window sends the same host signals the panel keys do: cycle start/hold/reset/estop,
// coolant, spindle and jog pulses. It keeps no machine state of its own.
Window {
    id: pendant
    width: 520
    height: 560
    visible: false
    objectName: "pendantWindow"
    title: "Виртуальный пульт · отладка"
    color: host ? host.cBackground : "#e3e7ea"

    property Item host
    property string axis: "X"
    property int step: 1

    function closeWindow() { if (host) host.pendantOpen = false }

    onClosing: function(close) { if (host) host.pendantOpen = false; close.accepted = true }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Text { text: "ВИРТУАЛЬНЫЙ ПУЛЬТ"; color: host.cForeground; font.bold: true; font.pixelSize: 15; Layout.fillWidth: true }
            Rectangle {
                Layout.preferredWidth: 26; Layout.preferredHeight: 26
                color: host.cMuted; radius: 4
                Text { anchors.centerIn: parent; text: "×"; color: host.cForeground; font.bold: true; font.pixelSize: 18 }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: pendant.closeWindow() }
            }
        }

        Text {
            text: host ? (host.workplace + " · " + host.machineMode + " · " + host.jogAxis + " " + host.activeOffsetCode()) : ""
            color: host.cMutedForeground; font.bold: true; font.pixelSize: 11; font.family: "JetBrains Mono"
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: [
                    { t: "ПУСК", sig: "machineCycleStart", c: "cSuccess" },
                    { t: "СТОП", sig: "machineFeedHold", c: "cWarning" },
                    { t: "СБРОС", sig: "machineReset", c: "cPrimary" },
                    { t: "АВАРИЯ", sig: "machineEmergencyStop", c: "cDestructive" }
                ]
                delegate: Rectangle {
                    Layout.fillWidth: true; Layout.preferredHeight: 54
                    color: host[modelData.c]; radius: 6
                    Text { anchors.centerIn: parent; text: modelData.t; color: host.cPrimaryFg; font.bold: true; font.pixelSize: 13 }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: host[modelData.sig]() }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: host.cBorder }

        Text { text: "ОСЬ И ШАГ"; color: host.cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 1 }
        RowLayout {
            Layout.fillWidth: true; spacing: 4
            Repeater {
                model: ["X", "Y", "Z", "A"]
                delegate: Rectangle {
                    Layout.fillWidth: true; Layout.preferredHeight: 36
                    color: pendant.axis === modelData ? host.cPrimary : host.cCard
                    border.color: pendant.axis === modelData ? host.cPrimary : host.cBorder
                    radius: 4
                    Text { anchors.centerIn: parent; text: modelData; color: pendant.axis === modelData ? host.cPrimaryFg : host.cForeground; font.bold: true; font.pixelSize: 16; font.family: "JetBrains Mono" }
                    MouseArea { anchors.fill: parent; onClicked: { pendant.axis = modelData; host.selectAxis(modelData) } }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 4
            Repeater {
                model: [ { s: 1, t: "×1" }, { s: 10, t: "×10" }, { s: 100, t: "×100" } ]
                delegate: Rectangle {
                    Layout.fillWidth: true; Layout.preferredHeight: 30
                    color: pendant.step === modelData.s ? host.cAccent : host.cCard
                    border.color: pendant.step === modelData.s ? host.cPrimary : host.cBorder
                    radius: 4
                    Text { anchors.centerIn: parent; text: modelData.t; color: host.cForeground; font.bold: true; font.pixelSize: 12; font.family: "JetBrains Mono" }
                    MouseArea { anchors.fill: parent; onClicked: { pendant.step = modelData.s; host.jogStep = modelData.s } }
                }
            }
        }

        Text { text: "РУЧНАЯ ПОДАЧА"; color: host.cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 1 }
        GridLayout {
            Layout.fillWidth: true
            columns: 3
            rowSpacing: 6; columnSpacing: 6
            Repeater {
                model: [
                    { t: "", act: null, row: 0, col: 0 },
                    { t: "Y+", sign: 1, axis: "Y" }, { t: "", act: null },
                    { t: "X−", sign: -1, axis: "X" }, { t: "Z+", sign: 1, axis: "Z" }, { t: "X+", sign: 1, axis: "X" },
                    { t: "", act: null }, { t: "Z−", sign: -1, axis: "Z" }, { t: "A+", sign: 1, axis: "A" },
                    { t: "Y−", sign: -1, axis: "Y" }, { t: "A−", sign: -1, axis: "A" }, { t: "", act: null }
                ]
                delegate: Rectangle {
                    Layout.fillWidth: true; Layout.preferredHeight: 42
                    visible: modelData.t !== ""
                    color: host.cMuted; border.color: host.cBorder; radius: 4
                    Text { anchors.centerIn: parent; text: modelData.t; color: host.cForeground; font.bold: true; font.pixelSize: 14 }
                    MouseArea {
                        anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                        onClicked: { host.selectAxis(modelData.axis); host.jogStep = pendant.step; host.machineJogRequest(modelData.sign) }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true; spacing: 6
            Rectangle {
                Layout.fillWidth: true; Layout.preferredHeight: 40
                color: host.coolantOn ? host.cPrimary : host.cCard
                border.color: host.cBorder; radius: 4
                Text { anchors.centerIn: parent; text: "СОЖ"; color: host.coolantOn ? host.cPrimaryFg : host.cForeground; font.bold: true }
                MouseArea { anchors.fill: parent; onClicked: host.machineToggleCoolant() }
            }
            Rectangle {
                Layout.fillWidth: true; Layout.preferredHeight: 40
                color: host.spindleOn ? host.cPrimary : host.cCard
                border.color: host.cBorder; radius: 4
                Text { anchors.centerIn: parent; text: "ШПИНДЕЛЬ"; color: host.spindleOn ? host.cPrimaryFg : host.cForeground; font.bold: true }
                MouseArea { anchors.fill: parent; onClicked: host.machineToggleSpindle() }
            }
        }
        Item { Layout.fillHeight: true }
    }
}
