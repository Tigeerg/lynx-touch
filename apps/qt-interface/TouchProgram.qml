import QtQuick
import QtQuick.Layouts

// Portrait Program screen (touch only). RUN shows the live position, distance to the block and
// the four F8 views with finger buttons; SETUP/TEST show the block list. It does not reuse the
// panel's Program screen or AutoScreen.
Item {
    id: prog
    objectName: "touchProgram"
    property Item host
    readonly property var viewList: [
        { id: "trajectory", label: "Траектория", icon: "\ue53e" },
        { id: "gcode", label: "УП", icon: "\ue0c3" },
        { id: "both", label: "Совместно", icon: "\ue121" },
        { id: "overlay", label: "УП поверх", icon: "\ue0ba" }
    ]
    readonly property var stats: pathStats()

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
        spacing: 8

        // Position strip (RUN only)
        Rectangle {
            visible: host.workplace === "RUN"
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 132 : 0
            Layout.minimumHeight: visible ? 132 : 0
            Layout.maximumHeight: visible ? 132 : 0
            color: host.cCard
            border.color: host.cBorder
            radius: 8

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 2
                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 0
                    Repeater {
                        model: ["X", "Y", "Z", "A"]
                        delegate: ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Text { text: modelData; color: host.cMutedForeground; font.bold: true; font.pixelSize: 11 }
                            Text {
                                Layout.fillWidth: true
                                text: host.machineCoordinate(modelData) + (modelData === "A" ? "°" : "")
                                color: host.cForeground; font.bold: true
                                font.pixelSize: modelData === "A" ? 28 : 32; font.family: host.fontMono
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 24
                    Text { text: prog.stats.toFrame.toFixed(3); color: host.cForeground; font.bold: true; font.pixelSize: 14; font.family: host.fontMono }
                    Text { text: "мм до кадра"; color: host.cMutedForeground; font.pixelSize: 11 }
                    Item { Layout.fillWidth: true }
                    Text { text: "ОСТАТОК ПУТИ"; color: host.cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 1 }
                    Text { text: "~ " + prog.stats.rest.toFixed(1) + " мм"; color: host.cForeground; font.bold: true; font.pixelSize: 13; font.family: host.fontMono }
                }
            }
        }

        // Content: RUN views or the block list
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            TouchCodeList {
                id: fullCode
                anchors.fill: parent
                visible: host.workplace !== "RUN" || host.autoView === "gcode"
                host: prog.host
            }

            ViewerPane {
                id: trajectoryViewer
                anchors.fill: parent
                visible: host.workplace === "RUN" && host.autoView === "trajectory"
                active: prog.host.touchMode && prog.host.activeScreen === "prog" && visible
                host: prog.host
                settings: prog.host ? prog.host.viewerSettings() : ({})
            }

            RowLayout {
                visible: host.workplace === "RUN" && host.autoView === "both"
                anchors.fill: parent
                spacing: 1
                TouchCodeList {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 380
                    Layout.maximumWidth: 420
                    host: prog.host
                }
                Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: host.cBorder }
                ViewerPane {
                    id: bothViewer
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumWidth: 300
                    active: prog.host.touchMode && prog.host.activeScreen === "prog" && prog.host.autoView === "both"
                    host: prog.host
                    settings: prog.host ? prog.host.viewerSettings() : ({})
                }
            }

            ViewerPane {
                id: overlayViewer
                anchors.fill: parent
                visible: host.workplace === "RUN" && host.autoView === "overlay"
                active: prog.host.touchMode && prog.host.activeScreen === "prog" && visible
                host: prog.host
                settings: prog.host ? prog.host.viewerSettings() : ({})

                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: parent.width / 2
                    color: Qt.rgba(1, 1, 1, 0.86)
                    TouchCodeList {
                        anchors.fill: parent
                        host: prog.host
                        translucent: true
                    }
                }
            }
        }

        // AUTO status zone under the viewer, as in the React touch panel.
        TouchStatusZone {
            visible: host.workplace === "RUN" && host.autoView === "trajectory"
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 310 : 0
            Layout.minimumHeight: visible ? 310 : 0
            Layout.maximumHeight: visible ? 310 : 0
            host: prog.host
        }
    }
}
