import QtQuick
import QtQuick.Layouts

// Hybrid cycle bar: always visible above the softkeys, so Start/Stop/Reset/Emergency are present
// even when the right column shows the normal side panel rather than the touch deck.
Rectangle {
    id: bar
    property Item host
    height: 44
    color: host ? host.cCard : "#FCFDFF"
    border.color: host ? host.cBorder : "#C9CED4"
    border.width: 1
    radius: 5

    RowLayout {
        anchors.fill: parent
        anchors.margins: 4
        spacing: 6
        Repeater {
            model: [
                { t: "ПУСК", a: "start", c: host ? host.cSuccess : "#1D9247", icon: "▶" },
                { t: "СТОП", a: "hold", c: host ? host.cWarning : "#F3A518", icon: "II" },
                { t: "СБРОС", a: "reset", c: host ? host.cPrimary : "#0362BB", icon: "↺" },
                { t: "АВАРИЯ", a: "estop", c: host ? host.cDestructive : "#D42125", icon: "■" }
            ]
            delegate: Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 4
                color: modelData.c
                RowLayout {
                    anchors.centerIn: parent
                    spacing: 5
                    Text { text: modelData.icon; color: "#FFFFFF"; font.pixelSize: 13; font.bold: true }
                    Text { text: modelData.t; color: "#FFFFFF"; font.bold: true; font.pixelSize: 11 }
                }
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
}
