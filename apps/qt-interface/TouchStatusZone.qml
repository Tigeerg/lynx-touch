import QtQuick
import QtQuick.Layouts

// The touch status zone under the viewer: execution, tool, load and codes/correctors, the same
// four blocks the React touch panel shows in AUTO.
Item {
    id: zone
    property Item host

    function codeLine(modelData) {
        if (!modelData) return ""
        var s = ""
        for (var i = 0; i < modelData.toks.length; i++)
            s += modelData.toks[i].t + (i + 1 < modelData.toks.length ? " " : "")
        return s
    }

    GridLayout {
        anchors.fill: parent
        columns: 2
        rowSpacing: 6
        columnSpacing: 6

        // Execution
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: host.cCard
            border.color: host.cBorder
            radius: 6
            clip: true
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 8; spacing: 4
                Text { text: "ВЫПОЛНЕНИЕ"; color: host.cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 1 }
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: (host.cursorProg + 1) + "/" + host.progLines.length; color: host.cForeground; font.bold: true; font.pixelSize: 14; font.family: host.fontMono }
                    Item { Layout.fillWidth: true }
                    Text { text: Math.round(host.cursorProg / Math.max(1, host.progLines.length - 1) * 100) + "%"; color: host.cForeground; font.bold: true; font.pixelSize: 20; font.family: host.fontMono }
                }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 10; color: host.cMuted; radius: 3
                    Rectangle { width: parent.width * (host.cursorProg / Math.max(1, host.progLines.length - 1)); height: parent.height; color: host.cPrimary; radius: 3 } }
                Text { text: "ДАЛЕЕ " + zone.codeLine(host.progLines[Math.min(host.cursorProg, host.progLines.length - 1)]); Layout.fillWidth: true; color: host.cMutedForeground; font.pixelSize: 10; font.family: host.fontMono; elide: Text.ElideRight }
                RowLayout { Layout.fillWidth: true
                    Text { text: "⏱ ЦИКЛ"; color: host.cMutedForeground; font.pixelSize: 9 }
                    Item { Layout.fillWidth: true }
                    Text { text: "ПРОШЛЫЙ"; color: host.cMutedForeground; font.pixelSize: 9 }
                    Item { Layout.fillWidth: true }
                    Text { text: "ДЕТАЛЕЙ " + host.partCount; color: host.cForeground; font.bold: true; font.pixelSize: 10 }
                }
                RowLayout { Layout.fillWidth: true
                    Text { text: "00:00:00"; color: host.cForeground; font.bold: true; font.pixelSize: 12; font.family: host.fontMono }
                    Item { Layout.fillWidth: true }
                    Text { text: "00:00:00"; color: host.cForeground; font.bold: true; font.pixelSize: 12; font.family: host.fontMono }
                }
            }
        }

        // Tool
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: host.cCard
            border.color: host.cBorder
            radius: 6
            clip: true
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 8; spacing: 4
                Text { text: "ИНСТРУМЕНТ"; color: host.cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 1 }
                RowLayout { Layout.fillWidth: true
                    Rectangle { width: 40; height: 26; radius: 4; color: host.cPrimary
                        Text { anchors.centerIn: parent; text: host.toolsData[host.cursorTool] ? host.toolsData[host.cursorTool].n : "T--"; color: host.cPrimaryFg; font.bold: true; font.pixelSize: 11; font.family: host.fontMono } }
                    Text { text: host.toolsData[host.cursorTool] ? host.toolsData[host.cursorTool].name : ""; Layout.fillWidth: true; color: host.cForeground; font.pixelSize: 12; elide: Text.ElideRight }
                }
                Text { text: "D" + (host.toolsData[host.cursorTool] ? host.toolsData[host.cursorTool].d : "—") + " · L " + (host.toolsData[host.cursorTool] ? host.toolsData[host.cursorTool].l : "—") + " · " + (host.toolsData[host.cursorTool] ? host.toolsData[host.cursorTool].life : 0) + "%"; color: host.cMutedForeground; font.pixelSize: 10; font.family: host.fontMono }
            }
        }

        // Load
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: host.cCard
            border.color: host.cBorder
            radius: 6
            clip: true
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 8; spacing: 3
                Text { text: "НАГРУЗКА (ИМИТАЦИЯ)"; color: host.cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 1 }
                Repeater {
                    model: [ { l: "Нр", v: 0.38 }, { l: "X", v: 0.02 }, { l: "Y", v: 0.02 }, { l: "Z", v: 0.03 } ]
                    delegate: RowLayout { Layout.fillWidth: true; spacing: 6
                        Text { text: modelData.l; width: 22; color: host.cMutedForeground; font.pixelSize: 9; font.family: host.fontMono }
                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 6; color: host.cMuted; radius: 3
                            Rectangle { width: parent.width * modelData.v; height: parent.height; color: host.cPrimary; radius: 3 } }
                        Text { text: Math.round(modelData.v * 100) + "%"; width: 34; color: host.cForeground; font.pixelSize: 9; font.family: host.fontMono; horizontalAlignment: Text.AlignRight }
                    }
                }
            }
        }

        // Codes and correctors
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: host.cCard
            border.color: host.cBorder
            radius: 6
            clip: true
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 8; spacing: 3
                Text { text: "АКТИВНЫЕ КОДЫ"; color: host.cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 1 }
                RowLayout { spacing: 4
                    Repeater { model: ["G0", "G17", "G21", "G90", host.activeOffsetCode()]
                        delegate: Rectangle { width: 38; height: 20; radius: 3; color: index === 4 ? host.cPrimary : host.cMuted
                            Text { anchors.centerIn: parent; text: modelData; color: index === 4 ? host.cPrimaryFg : host.cForeground; font.bold: true; font.pixelSize: 10; font.family: host.fontMono } } }
                }
                RowLayout { spacing: 4
                    Repeater { model: ["G40", "G49", "G80", "M05", "M09"]
                        delegate: Rectangle { width: 38; height: 20; radius: 3; color: host.cMuted
                            Text { anchors.centerIn: parent; text: modelData; color: host.cForeground; font.pixelSize: 10; font.family: host.fontMono } } }
                }
                Text { text: "КОРРЕКТОРЫ"; color: host.cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 1; Layout.topMargin: 2 }
                RowLayout { Layout.fillWidth: true
                    Text { text: "S%"; color: host.cMutedForeground; font.pixelSize: 9 }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 6; color: host.cMuted; radius: 3
                        Rectangle { width: parent.width * host.spindleOverride / 150; height: parent.height; color: host.cPrimary; radius: 3 } }
                    Text { text: host.spindleOverride + "%"; color: host.cForeground; font.pixelSize: 9; font.family: host.fontMono }
                }
                RowLayout { Layout.fillWidth: true
                    Text { text: "F%"; color: host.cMutedForeground; font.pixelSize: 9 }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 6; color: host.cMuted; radius: 3
                        Rectangle { width: parent.width * host.feedOverride / 150; height: parent.height; color: host.cPrimary; radius: 3 } }
                    Text { text: host.feedOverride + "%"; color: host.cForeground; font.pixelSize: 9; font.family: host.fontMono }
                }
            }
        }
    }
}
