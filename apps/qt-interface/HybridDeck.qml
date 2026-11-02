import QtQuick
import QtQuick.Layouts

// Hybrid right column: it replaces the side status column on Work/Program instead of floating over
// it. The physical keyboard still works; these are the finger-sized machine controls.
Rectangle {
    id: deck
    objectName: "hybridDeck"
    property Item host
    color: host ? host.cCard : "#FCFDFF"
    border.color: host ? host.cBorder : "#C9CED4"
    border.width: 1
    radius: 6
    clip: true

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        Text { text: "СЕНСОРНЫЙ БЛОК · ГИБРИД"; color: host ? host.cMutedForeground : "#535C66"; font.bold: true; font.pixelSize: 10; font.letterSpacing: 1 }

        GridLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 96
            Layout.minimumHeight: 96
            Layout.maximumHeight: 96
            columns: 2
            rowSpacing: 5; columnSpacing: 5
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
                    Text { anchors.centerIn: parent; text: modelData.t; color: "#FFFFFF"; font.bold: true; font.pixelSize: 12 }
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
            Layout.preferredHeight: 150
            Layout.minimumHeight: 150
            Layout.maximumHeight: 150
            columns: 3
            rowSpacing: 5; columnSpacing: 5
            Repeater {
                model: [
                    { t: "Z+", ax: "Z", d: 1 }, { t: "Y+", ax: "Y", d: 1 }, { t: "A+", ax: "A", d: 1 },
                    { t: "X−", ax: "X", d: -1 }, { t: "БЫСТРО", ax: "", d: 0 }, { t: "X+", ax: "X", d: 1 },
                    { t: "Z−", ax: "Z", d: -1 }, { t: "Y−", ax: "Y", d: -1 }, { t: "A−", ax: "A", d: -1 }
                ]
                delegate: Rectangle {
                    Layout.fillWidth: true; Layout.fillHeight: true; radius: 4
                    color: modelData.ax === "" ? (host ? host.cWarning : "#F3A518") : (host ? host.cMuted : "#EEF0F3")
                    border.color: host ? host.cBorder : "#C9CED4"
                    Text { anchors.centerIn: parent; text: modelData.t; color: host ? host.cForeground : "#141B24"; font.bold: true; font.pixelSize: modelData.ax === "" ? 9 : 12; font.family: host ? host.fontMono : "monospace" }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            if (modelData.ax === "") return;
                            host.selectAxis(modelData.ax); host.machineJogRequest(modelData.d);
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            Layout.minimumHeight: 32
            Layout.maximumHeight: 32
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

        GridLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: 2
            rowSpacing: 5; columnSpacing: 5
            Repeater {
                model: [
                    { t: "СОЖ", k: "coolant", on: host ? host.coolantOn : false },
                    { t: "ШПИНДЕЛЬ", k: "spindle", on: host ? host.spindleOn : false },
                    { t: "MDI", k: "mdi", on: false },
                    { t: "ОТВОД", k: "safe", on: false },
                    { t: host && host.toolClamped ? "РАЗЖАТЬ" : "ЗАЖАТЬ", k: "clamp", on: host ? host.toolClamped : false },
                    { t: "СВЕТ", k: "light", on: host ? host.workLightOn : false }
                ]
                delegate: HwKey {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    host: deck.host
                    label: modelData.t
                    active: modelData.on
                    led: true
                    onClicked: {
                        if (modelData.k === "coolant") host.toggleAux("coolant");
                        else if (modelData.k === "spindle") host.spindleCommand(host.spindleOn ? "M05" : "M03");
                        else if (modelData.k === "mdi") { host.setWorkplace("SETUP"); host.activeScreen = "pos"; }
                        else if (modelData.k === "safe") host.goSafePoint();
                        else if (modelData.k === "clamp") { host.toolClamped = !host.toolClamped; host.message = host.toolClamped ? "Инструмент зажат" : "Инструмент разжат"; }
                        else if (modelData.k === "light") host.toggleAux("light");
                    }
                }
            }
        }
    }
}
