import QtQuick
import QtQuick.Layouts

// The AUTO workplace screen. The operator sees the live position first, then the distance to the
// current block and the remaining path, then one panel whose F8 view switches between the
// trajectory, the program, both side by side and the program as a translucent window over the
// trajectory. The real viewer is the same GcodeViewer3D module as on the manual Work screen.
Item {
    id: autoScreen
    objectName: "autoScreen"

    property Item host

    readonly property var viewTitles: ({
        trajectory: "ТРАЕКТОРИЯ",
        gcode: "УП",
        both: "СОВМЕСТНО",
        overlay: "УП ПОВЕРХ"
    })
    readonly property string view: host ? host.autoView : "trajectory"
    readonly property var program: host ? host.activeProgram() : null
    readonly property var stats: pathStats()

    function cycleStart() { autoViewer.cycleStart(); }
    function feedHold() { autoViewer.feedHold(); }
    function reset() { autoViewer.reset(); }
    function seek(fraction) {
        if (autoViewer.active) autoViewer.seek(fraction)
        else if (overlayViewer.active) overlayViewer.seek(fraction)
        else if (bothViewer.active) bothViewer.seek(fraction)
    }

    // Distance from the live position to the next programmed point and the remaining XY path
    // from the cursor to the end of the active program. Both are estimates for the operator.
    function pathStats() {
        var out = { toFrame: 0, rest: 0 };
        if (!host || !host.activeProgram) return out;
        var lines = host.activeProgram().lines;
        var from = Math.max(0, host.cursorProg || 0);
        var x = host.machinePos ? host.machinePos.x : 0;
        var y = host.machinePos ? host.machinePos.y : 0;
        var z = host.machinePos ? host.machinePos.z : 0;
        var first = true;
        for (var i = from; i < lines.length; i++) {
            var clean = lines[i].replace(/\(.*?\)/g, " ");
            var toks = clean.trim().split(/\s+/);
            var nx = x, ny = y, nz = z;
            for (var j = 0; j < toks.length; j++) {
                var t = toks[j];
                if (t.charAt(0) === "X") nx = parseFloat(t.substring(1));
                else if (t.charAt(0) === "Y") ny = parseFloat(t.substring(1));
                else if (t.charAt(0) === "Z") nz = parseFloat(t.substring(1));
            }
            if (nx !== x || ny !== y || nz !== z) {
                var d = Math.sqrt((nx - x) * (nx - x) + (ny - y) * (ny - y) + (nz - z) * (nz - z));
                if (first) { out.toFrame = d; first = false; }
                else out.rest += Math.sqrt((nx - x) * (nx - x) + (ny - y) * (ny - y));
                x = nx; y = ny; z = nz;
            }
        }
        return out;
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        // ---------- Live position and quiet estimates ----------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 94
            Layout.minimumHeight: 94
            Layout.maximumHeight: 94
            color: host.cCard
            border.color: host.cBorder
            border.width: 1
            radius: 6
            clip: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 0
                    Repeater {
                        model: ["X", "Y", "Z", "A"]
                        delegate: ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Text { text: modelData; color: host.cMutedForeground; font.bold: true; font.pixelSize: 10 }
                            Text {
                                Layout.fillWidth: true
                                text: host.machineCoordinate(modelData) + (modelData === "A" ? "°" : "")
                                color: host.cForeground
                                font.bold: true
                                font.pixelSize: modelData === "A" ? 24 : 28
                                font.family: host.fontMono
                            }
                        }
                    }
                }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: host.cBorder }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 18
                    spacing: 6
                    Text {
                        text: autoScreen.stats.toFrame.toFixed(3)
                        color: host.cForeground
                        font.family: host.fontMono
                        font.bold: true
                        font.pixelSize: 12
                    }
                    Text { text: "мм до кадра"; color: host.cMutedForeground; font.pixelSize: 10 }
                    Item { Layout.fillWidth: true }
                    Text { text: "ОСТАТОК ПУТИ"; color: host.cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 0.5 }
                    Text {
                        text: "~ " + autoScreen.stats.rest.toFixed(1) + " мм"
                        color: host.cForeground
                        font.family: host.fontMono
                        font.bold: true
                        font.pixelSize: 12
                    }
                }
            }
        }

        // ---------- One panel, F8 switches the view ----------
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: host.cCard
            border.color: host.cBorder
            border.width: 1
            radius: 6
            clip: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 1
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 30
                    Layout.minimumHeight: 30
                    Layout.maximumHeight: 30
                    color: host.cMuted
                    radius: 6
                    clip: true
                    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 6; color: host.cMuted }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 6
                        Text { text: "\ue53e"; color: host.cMutedForeground; font.family: "lucide"; font.pixelSize: 13 }
                        Text {
                            text: autoScreen.viewTitles[autoScreen.view] + " · " + (autoScreen.program ? autoScreen.program.name : "")
                            color: host.cMutedForeground
                            font.bold: true
                            font.pixelSize: 11
                            font.letterSpacing: 0.5
                        }
                        Item { Layout.fillWidth: true }
                        Repeater {
                            model: [
                                { label: "Покадр.", on: host.singleBlock },
                                { label: "M01", on: host.optStop },
                                { label: "Холост.", on: host.dryRun }
                            ]
                            delegate: RowLayout {
                                spacing: 3
                                Rectangle {
                                    Layout.preferredWidth: 7; Layout.preferredHeight: 7
                                    Layout.minimumWidth: 7; Layout.maximumWidth: 7
                                    Layout.minimumHeight: 7; Layout.maximumHeight: 7
                                    radius: 4
                                    color: modelData.on ? host.cWarning : host.cMuted
                                }
                                Text { text: modelData.label; color: host.cMutedForeground; font.pixelSize: 10 }
                            }
                        }
                    }
                }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: host.cBorder }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    // Trajectory only
                    ViewerPane {
                        id: autoViewer
                        anchors.fill: parent
                        host: autoScreen.host
                        visible: autoScreen.view === "trajectory"
                        active: visible
                    }

                    // Program only
                    Flickable {
                        visible: autoScreen.view === "gcode"
                        anchors.fill: parent
                        contentWidth: width
                        contentHeight: gcodeOnlyCol.height
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        Column {
                            id: gcodeOnlyCol
                            width: parent.width
                            height: autoScreen.rootLines().length * 20
                            Repeater {
                                model: rootLines()
                                delegate: autoLine
                            }
                        }
                    }

                    // Both: program left, trajectory right
                    RowLayout {
                        visible: autoScreen.view === "both"
                        anchors.fill: parent
                        spacing: 1
                        Flickable {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.preferredWidth: 400
                            Layout.maximumWidth: 430
                            contentWidth: width
                            contentHeight: gcodeBothCol.height
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            Column {
                                id: gcodeBothCol
                                width: parent.width
                                height: autoScreen.rootLines().length * 20
                                Repeater { model: rootLines(); delegate: autoLine }
                            }
                        }
                        Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: host.cBorder }
                        ViewerPane {
                            id: bothViewer
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumWidth: 180
                            Layout.preferredWidth: 260
                            host: autoScreen.host
                            active: autoScreen.view === "both"
                        }
                    }

                    // Overlay: trajectory with a translucent program window on the left half
                    ViewerPane {
                        id: overlayViewer
                        anchors.fill: parent
                        host: autoScreen.host
                        visible: autoScreen.view === "overlay"
                        active: visible
                        Flickable {
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            width: parent.width / 2
                            contentWidth: width
                            contentHeight: gcodeOverlayCol.height
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            Rectangle { anchors.fill: parent; color: Qt.rgba(1, 1, 1, 0.86) }
                            Column {
                                id: gcodeOverlayCol
                                width: parent.width
                                height: autoScreen.rootLines().length * 20
                                Repeater { model: rootLines(); delegate: autoLine }
                            }
                        }
                    }
                }
            }
        }
    }

    function rootLines() {
        return host ? host.progLines : [];
    }

    Component {
        id: autoLine
        Rectangle {
            width: parent ? parent.width : 0
            height: 20
            color: index === autoScreen.host.cursorProg ? autoScreen.host.cAccent : "transparent"
            Text {
                anchors.left: parent.left
                anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 16
                text: (modelData.num < 10 ? " " : "") + modelData.num + "  " + lineText(modelData)
                color: index === autoScreen.host.cursorProg ? autoScreen.host.cForeground : autoScreen.host.cMutedForeground
                font.family: autoScreen.host.fontMono
                font.pixelSize: 12
                elide: Text.ElideRight
            }
            MouseArea { anchors.fill: parent; onClicked: autoScreen.host.cursorProg = index }
        }
    }

    function lineText(modelData) {
        var s = "";
        for (var i = 0; i < modelData.toks.length; i++)
            s += modelData.toks[i].t + (i + 1 < modelData.toks.length ? " " : "");
        return s;
    }
}
