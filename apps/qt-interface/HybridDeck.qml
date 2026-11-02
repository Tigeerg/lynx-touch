import QtQuick
import QtQuick.Layouts

// Hybrid console layer: the 1024x600 key panel still owns the screens and F-keys, while this
// touch pad gives the operator the cycle row and jog without reaching for the physical keys.
Rectangle {
    id: deck
    objectName: "hybridDeck"
    property Item host
    width: 320
    height: 246
    radius: 10
    color: host ? Qt.rgba(0.96, 0.97, 0.98, 0.96) : "#F5F7F9"
    border.color: host ? host.cBorder : "#C9CED4"
    border.width: 2

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        Text { text: "СЕНСОРНЫЙ БЛОК · ГИБРИД"; color: host ? host.cMutedForeground : "#535C66"; font.bold: true; font.pixelSize: 10; font.letterSpacing: 1 }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 54
            Layout.minimumHeight: 54
            Layout.maximumHeight: 54
            spacing: 6
            Repeater {
                model: [
                    { t: "ПУСК", a: "start", c: host ? host.cSuccess : "#1D9247" },
                    { t: "СТОП", a: "hold", c: host ? host.cWarning : "#F3A518" },
                    { t: "СБРОС", a: "reset", c: host ? host.cPrimary : "#0362BB" },
                    { t: "АВАРИЯ", a: "estop", c: host ? host.cDestructive : "#D42125" }
                ]
                delegate: Rectangle {
                    Layout.fillWidth: true; Layout.fillHeight: true; radius: 5
                    color: modelData.c
                    Text { anchors.centerIn: parent; text: modelData.t; color: host ? host.cPrimaryFg : "#FCFCFC"; font.bold: true; font.pixelSize: 11 }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            if (modelData.a === "start") host.machineCycleStart();
                            else if (modelData.a === "hold") host.machineFeedHold();
                            else if (modelData.a === "reset") host.machineReset();
                            else host.machineEmergencyStop();
                        }
                    }
                }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: 3
            rowSpacing: 4; columnSpacing: 4
            Repeater {
                model: [
                    { t: "Z+", ax: "Z", d: 1 }, { t: "Y+", ax: "Y", d: 1 }, { t: "A+", ax: "A", d: 1 },
                    { t: "X−", ax: "X", d: -1 }, { t: "БЫСТРО", ax: "", d: 0 }, { t: "X+", ax: "X", d: 1 },
                    { t: "Z−", ax: "Z", d: -1 }, { t: "Y−", ax: "Y", d: -1 }, { t: "A−", ax: "A", d: -1 }
                ]
                delegate: Rectangle {
                    Layout.fillWidth: true; Layout.fillHeight: true; radius: 4
                    color: modelData.ax === "" ? (host ? host.cWarning : "#F3A518") : (host ? host.cCard : "#FCFDFF")
                    border.color: host ? host.cBorder : "#C9CED4"
                    Text { anchors.centerIn: parent; text: modelData.t; color: host ? host.cForeground : "#141B24"; font.bold: true; font.pixelSize: modelData.ax === "" ? 10 : 13; font.family: host ? host.fontMono : "monospace" }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            if (modelData.ax === "") return;
                            host.selectAxis(modelData.ax);
                            host.machineJogRequest(modelData.d);
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            Layout.minimumHeight: 28
            Layout.maximumHeight: 28
            spacing: 4
            Repeater {
                model: [0, 1, 2]
                delegate: Rectangle {
                    Layout.fillWidth: true; Layout.fillHeight: true; radius: 4
                    color: host && host.jogStep === index ? host.cPrimary : (host ? host.cMuted : "#EEF0F3")
                    Text { anchors.centerIn: parent; text: ["0.001", "0.01", "0.1"][index]; color: host && host.jogStep === index ? host.cPrimaryFg : (host ? host.cForeground : "#141B24"); font.bold: true; font.pixelSize: 10; font.family: host ? host.fontMono : "monospace" }
                    MouseArea { anchors.fill: parent; onClicked: host.jogStep = index }
                }
            }
        }
    }
}
