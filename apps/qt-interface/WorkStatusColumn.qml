import QtQuick
import QtQuick.Layouts

// Wave B right column in the React reading order: Tool, Correctors, Active codes, Load,
// Execution. The numbers come from the host panel (toolsData, overrides, offsets, flags);
// the block only arranges them, so there is no second source of machine state.
Rectangle {
    id: workStatusColumn
    objectName: "workStatusColumn"

    property Item host

    // Mock load from the old side-status block: the Qt port has no load model yet.
    readonly property int spindleLoad: 38

    function currentTool() {
        if (!host || !host.toolsData) return null;
        for (var i = 0; i < host.toolsData.length; i++)
            if (host.toolsData[i].inSpindle) return host.toolsData[i];
        return null;
    }
    function toolNumber() {
        var tool = currentTool();
        return tool ? tool.n : "T—";
    }
    function toolName() {
        var tool = currentTool();
        return tool ? tool.name : "—";
    }
    function toolDetail() {
        var tool = currentTool();
        if (!tool) return "—";
        var h = tool.n.length > 1 ? tool.n.substring(1) : tool.n;
        return "D" + tool.d + " · L " + tool.l + " · H" + h;
    }
    function activeOffsetCode() { return host ? host.activeOffsetCode() : "G54"; }

    // The modal G/M codes visible on the current block: same values the React ActiveCodes pulls
    // from the program line, taken here from the loaded program flags and the active offset.
    function codes() {
        return [
            { label: "Движение", value: "G00" },
            { label: "Плоскость", value: "G17" },
            { label: "Единицы", value: "G21" },
            { label: "Координаты", value: "G90" },
            { label: "Система координат", value: activeOffsetCode() },
            { label: "Коррекция радиуса", value: "G40" },
            { label: "Коррекция длины", value: "G49" },
            { label: "Цикл", value: "G80" },
            { label: "Шпиндель", value: host && host.spindleOn ? "M03" : "M05" },
            { label: "СОЖ", value: host && host.coolantOn ? "M08" : "M09" }
        ];
    }
    function chipColor(value) {
        if (value === "G00") return host.cWarning;
        if (value === activeOffsetCode()) return host.cPrimary;
        return host.cMuted;
    }
    function chipTextColor(value) {
        if (value === "G00") return host.cWarningFg;
        if (value === activeOffsetCode()) return host.cPrimaryFg;
        return host.cMutedForeground;
    }

    color: host.cCard
    border.color: host.cBorder
    border.width: 1
    radius: 6
    clip: true

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 1
        spacing: 0

        // ---------- Title: the same "Режимы" frame as the old side column ----------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            Layout.minimumHeight: 32
            Layout.maximumHeight: 32
            color: host.cMuted
            radius: 6
            clip: true
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 6
                color: host.cMuted
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 6
                Text { text: "\ue1bf"; color: host.cMutedForeground; font.family: "lucide"; font.pixelSize: 13 }
                Text { text: "РЕЖИМЫ"; color: host.cMutedForeground; font.bold: true; font.pixelSize: 11; font.letterSpacing: 1 }
                Text {
                    Layout.fillWidth: true
                    text: host.modeLabel(host.machineMode)
                    color: host.cForeground
                    font.bold: true
                    font.pixelSize: 11
                    font.family: host.fontMono
                    horizontalAlignment: Text.AlignRight
                }
            }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: host.cBorder }

        // ---------- 1. Tool ----------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 54
            Layout.minimumHeight: 54
            Layout.maximumHeight: 54
            color: host.cCard
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 3
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    Text { text: "\ue1b1"; color: host.cMutedForeground; font.family: "lucide"; font.pixelSize: 13 }
                    Text { text: "ИНСТРУМЕНТ"; color: host.cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 1 }
                    Item { Layout.fillWidth: true }
                    Text { text: workStatusColumn.toolNumber(); color: host.cForeground; font.bold: true; font.pixelSize: 11; font.family: host.fontMono }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Rectangle {
                        Layout.preferredWidth: 40
                        Layout.preferredHeight: 24
                        Layout.minimumWidth: 40
                        Layout.maximumWidth: 40
                        Layout.minimumHeight: 24
                        Layout.maximumHeight: 24
                        color: host.cPrimary
                        radius: 4
                        Text {
                            anchors.centerIn: parent
                            text: workStatusColumn.toolNumber()
                            color: host.cPrimaryFg
                            font.bold: true
                            font.pixelSize: 12
                            font.family: host.fontMono
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            Layout.fillWidth: true
                            text: workStatusColumn.toolName()
                            color: host.cForeground
                            font.bold: true
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: workStatusColumn.toolDetail()
                            color: host.cMutedForeground
                            font.pixelSize: 9
                            font.family: host.fontMono
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: host.cBorder }

        // ---------- 2. Correctors ----------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 86
            Layout.minimumHeight: 86
            Layout.maximumHeight: 86
            color: host.cCard
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 3
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    Text { text: "\ue29a"; color: host.cMutedForeground; font.family: "lucide"; font.pixelSize: 12 }
                    Text { text: "КОРРЕКТОРЫ"; color: host.cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 1 }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Text { text: "S%"; Layout.minimumWidth: 20; Layout.maximumWidth: 20; color: host.cMutedForeground; font.bold: true; font.pixelSize: 10 }
                    Rectangle { Layout.preferredWidth: 18; Layout.preferredHeight: 18; Layout.minimumWidth: 18; Layout.maximumWidth: 18; Layout.minimumHeight: 18; Layout.maximumHeight: 18; color: host.cCard; border.color: host.cBorderMuted; radius: 3
                        Text { anchors.centerIn: parent; text: "["; color: host.cMutedForeground; font.bold: true; font.pixelSize: 9 } }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 8
                        Layout.minimumHeight: 8
                        Layout.maximumHeight: 8
                        color: host.cMuted
                        border.color: host.cBorder
                        radius: 3
                        Rectangle { width: parent.width * Math.min(1, host.spindleOverride / 150); height: parent.height - 2; anchors.verticalCenter: parent.verticalCenter; anchors.left: parent.left; anchors.leftMargin: 1; color: host.cPrimary; radius: 2 }
                    }
                    Rectangle { Layout.preferredWidth: 18; Layout.preferredHeight: 18; Layout.minimumWidth: 18; Layout.maximumWidth: 18; Layout.minimumHeight: 18; Layout.maximumHeight: 18; color: host.cCard; border.color: host.cBorderMuted; radius: 3
                        Text { anchors.centerIn: parent; text: "]"; color: host.cMutedForeground; font.bold: true; font.pixelSize: 9 } }
                    Text { text: host.spindleOverride + "%"; Layout.minimumWidth: 32; Layout.maximumWidth: 32; color: host.cForeground; font.bold: true; font.pixelSize: 11; font.family: host.fontMono; horizontalAlignment: Text.AlignRight }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Text { text: "F%"; Layout.minimumWidth: 20; Layout.maximumWidth: 20; color: host.cMutedForeground; font.bold: true; font.pixelSize: 10 }
                    Rectangle { Layout.preferredWidth: 18; Layout.preferredHeight: 18; Layout.minimumWidth: 18; Layout.maximumWidth: 18; Layout.minimumHeight: 18; Layout.maximumHeight: 18; color: host.cCard; border.color: host.cBorderMuted; radius: 3
                        Text { anchors.centerIn: parent; text: "−"; color: host.cMutedForeground; font.bold: true; font.pixelSize: 9 } }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 8
                        Layout.minimumHeight: 8
                        Layout.maximumHeight: 8
                        color: host.cMuted
                        border.color: host.cBorder
                        radius: 3
                        Rectangle { width: parent.width * Math.min(1, host.feedOverride / 150); height: parent.height - 2; anchors.verticalCenter: parent.verticalCenter; anchors.left: parent.left; anchors.leftMargin: 1; color: host.cPrimary; radius: 2 }
                    }
                    Rectangle { Layout.preferredWidth: 18; Layout.preferredHeight: 18; Layout.minimumWidth: 18; Layout.maximumWidth: 18; Layout.minimumHeight: 18; Layout.maximumHeight: 18; color: host.cCard; border.color: host.cBorderMuted; radius: 3
                        Text { anchors.centerIn: parent; text: "="; color: host.cMutedForeground; font.bold: true; font.pixelSize: 9 } }
                    Text { text: host.feedOverride + "%"; Layout.minimumWidth: 32; Layout.maximumWidth: 32; color: host.cForeground; font.bold: true; font.pixelSize: 11; font.family: host.fontMono; horizontalAlignment: Text.AlignRight }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Text { text: "G0"; Layout.minimumWidth: 20; Layout.maximumWidth: 20; color: host.cMutedForeground; font.bold: true; font.pixelSize: 10 }
                    Rectangle { Layout.preferredWidth: 18; Layout.preferredHeight: 18; Layout.minimumWidth: 18; Layout.maximumWidth: 18; Layout.minimumHeight: 18; Layout.maximumHeight: 18; color: host.cCard; border.color: host.cBorderMuted; radius: 3
                        Text { anchors.centerIn: parent; text: "9"; color: host.cMutedForeground; font.bold: true; font.pixelSize: 9 } }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 18
                        Layout.minimumHeight: 18
                        Layout.maximumHeight: 18
                        spacing: 2
                        Repeater {
                            model: host.rapidSteps
                            delegate: Rectangle {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                color: modelData === host.rapidOverride ? host.cPrimary : host.cMuted
                                radius: 2
                                Text { anchors.centerIn: parent; text: "" + modelData; color: modelData === host.rapidOverride ? host.cPrimaryFg : host.cMutedForeground; font.bold: true; font.pixelSize: 9; font.family: host.fontMono }
                            }
                        }
                    }
                    Rectangle { Layout.preferredWidth: 18; Layout.preferredHeight: 18; Layout.minimumWidth: 18; Layout.maximumWidth: 18; Layout.minimumHeight: 18; Layout.maximumHeight: 18; color: host.cCard; border.color: host.cBorderMuted; radius: 3
                        Text { anchors.centerIn: parent; text: "0"; color: host.cMutedForeground; font.bold: true; font.pixelSize: 9 } }
                }
            }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: host.cBorder }

        // ---------- 3. Active G/M codes ----------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 60
            Layout.minimumHeight: 60
            Layout.maximumHeight: 60
            color: host.cCard
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 3
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    Text { text: "\ue284"; color: host.cMutedForeground; font.family: "lucide"; font.pixelSize: 12 }
                    Text { text: "АКТИВНЫЕ КОДЫ"; color: host.cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 1 }
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 5
                    columnSpacing: 3
                    rowSpacing: 3
                    Repeater {
                        model: workStatusColumn.codes()
                        delegate: Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 18
                            Layout.minimumHeight: 18
                            Layout.maximumHeight: 18
                            radius: 2
                            color: workStatusColumn.chipColor(modelData.value)
                            Text {
                                anchors.centerIn: parent
                                text: modelData.value
                                color: workStatusColumn.chipTextColor(modelData.value)
                                font.bold: true
                                font.pixelSize: 10
                                font.family: host.fontMono
                            }
                        }
                    }
                }
            }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: host.cBorder }

        // ---------- 4. Load (simulation) ----------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            Layout.minimumHeight: 44
            Layout.maximumHeight: 44
            color: host.cCard
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 3
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    Text { text: "\ue379"; color: host.cMutedForeground; font.family: "lucide"; font.pixelSize: 12 }
                    Text { text: "НАГРУЗКА (ИМИТАЦИЯ)"; color: host.cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 0.5; Layout.fillWidth: true }
                    Text { text: workStatusColumn.spindleLoad + "%"; color: host.cForeground; font.bold: true; font.pixelSize: 11; font.family: host.fontMono }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Text { text: "Нгр"; Layout.minimumWidth: 24; Layout.maximumWidth: 24; color: host.cMutedForeground; font.bold: true; font.pixelSize: 9 }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 10
                        Layout.minimumHeight: 10
                        Layout.maximumHeight: 10
                        color: host.cMuted
                        border.color: host.cBorder
                        radius: 3
                        Rectangle { width: parent.width * 0.38; height: parent.height - 2; anchors.verticalCenter: parent.verticalCenter; anchors.left: parent.left; anchors.leftMargin: 1; color: host.cPrimary; radius: 2 }
                        Rectangle { x: parent.width * 0.60; y: 0; width: 1; height: parent.height; color: "#4d34363a" }
                        Rectangle { x: parent.width * 0.80; y: 0; width: 1; height: parent.height; color: "#4d34363a" }
                    }
                    Text { text: workStatusColumn.spindleLoad + "%"; Layout.minimumWidth: 30; Layout.maximumWidth: 30; color: host.cForeground; font.bold: true; font.pixelSize: 11; font.family: host.fontMono; horizontalAlignment: Text.AlignRight }
                }
            }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: host.cBorder }

        // ---------- 5. Execution ----------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 78
            Layout.minimumHeight: 78
            Layout.maximumHeight: 78
            color: host.cCard
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 3
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    Text { text: "\ue0c3"; color: host.cMutedForeground; font.family: "lucide"; font.pixelSize: 12 }
                    Text { text: "ВЫПОЛНЕНИЕ"; color: host.cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 1 }
                    Item { Layout.fillWidth: true }
                    Text { text: "1/16"; color: host.cForeground; font.bold: true; font.pixelSize: 10; font.family: host.fontMono }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 8
                        Layout.minimumHeight: 8
                        Layout.maximumHeight: 8
                        color: host.cMuted
                        border.color: host.cBorder
                        radius: 3
                        Rectangle { width: parent.width * 0.12; height: parent.height - 2; anchors.verticalCenter: parent.verticalCenter; anchors.left: parent.left; anchors.leftMargin: 1; color: host.cPrimary; radius: 2 }
                    }
                    Text { text: "12%"; Layout.minimumWidth: 30; Layout.maximumWidth: 30; color: host.cForeground; font.bold: true; font.pixelSize: 11; font.family: host.fontMono; horizontalAlignment: Text.AlignRight }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text { text: "ЦИКЛ"; color: host.cMutedForeground; font.bold: true; font.pixelSize: 9; font.letterSpacing: 0.5 }
                        Text { text: "00:00:42"; color: host.cForeground; font.bold: true; font.pixelSize: 11; font.family: host.fontMono }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text { text: "ПРОШЛЫЙ"; color: host.cMutedForeground; font.bold: true; font.pixelSize: 9; font.letterSpacing: 0.5 }
                        Text { text: "00:03:18"; color: host.cForeground; font.bold: true; font.pixelSize: 11; font.family: host.fontMono }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text { text: "ДЕТАЛЕЙ"; color: host.cMutedForeground; font.bold: true; font.pixelSize: 9; font.letterSpacing: 0.5 }
                        Text { text: "7"; color: host.cForeground; font.bold: true; font.pixelSize: 11; font.family: host.fontMono }
                    }
                }
            }
        }

        Item { Layout.fillWidth: true; Layout.fillHeight: true }
    }
}
