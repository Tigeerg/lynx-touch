import QtQuick
import QtQuick.Layouts

// Wave B Work screen: the work DRO in one large line, the toolpath preview under it and the
// autonomous status column on the right. Every reading comes from the host panel:
// offsetsData/activeOffsetCode, machine flags and the loaded viewSample path.
Item {
    id: workScreen
    objectName: "workScreen"

    property Item host

    readonly property var axes: ["X", "Y", "Z", "A"]

    // The active work offset row is the one and only source of the DRO numbers: the same row
    // the Offsets screen edits.
    function activeRow() {
        if (!host || !host.offsetsData) return null;
        for (var i = 0; i < host.offsetsData.length; i++)
            if (host.offsetsData[i].active) return host.offsetsData[i];
        return null;
    }
    function dro(axis) {
        var row = activeRow();
        if (!row) return "—";
        var value = row[axis.toLowerCase()];
        return value === undefined ? "—" : value;
    }
    function offsetCode() { return host ? host.activeOffsetCode() : "—"; }

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        // ---------- Coordinate strip: G54 / X Y Z A in one row ----------
        Rectangle {
            id: workCoordStrip
            objectName: "workCoordStrip"
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
                anchors.margins: 10
                spacing: 2

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Text { text: "\ue121"; color: host.cMutedForeground; font.family: "lucide"; font.pixelSize: 13 }
                    Text {
                        text: "СМЕЩЕНИЕ G54 · ММ"
                        color: host.cMutedForeground
                        font.bold: true
                        font.pixelSize: 10
                        font.letterSpacing: 1
                    }
                    Item { Layout.fillWidth: true }
                    Rectangle {
                        Layout.preferredWidth: 42
                        Layout.preferredHeight: 18
                        Layout.minimumWidth: 42
                        Layout.maximumWidth: 42
                        Layout.minimumHeight: 18
                        Layout.maximumHeight: 18
                        color: host.cPrimary
                        radius: 3
                        Text {
                            anchors.centerIn: parent
                            text: workScreen.offsetCode()
                            color: host.cPrimaryFg
                            font.bold: true
                            font.pixelSize: 10
                            font.family: host.fontMono
                        }
                    }
                    Text { text: "· мм"; color: host.cMutedForeground; font.pixelSize: 10 }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 0

                    Repeater {
                        model: workScreen.axes
                        delegate: RowLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: 6

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                spacing: 0
                                Text {
                                    text: modelData
                                    color: host.cMutedForeground
                                    font.bold: true
                                    font.pixelSize: 10
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: workScreen.dro(modelData) + (modelData === "A" ? "°" : "")
                                    color: host.cForeground
                                    font.bold: true
                                    font.pixelSize: modelData === "A" ? 26 : 30
                                    font.family: host.fontMono
                                    elide: Text.ElideRight
                                }
                            }

                            Rectangle {
                                visible: index < 3
                                Layout.preferredWidth: 1
                                Layout.minimumWidth: 1
                                Layout.maximumWidth: 1
                                Layout.fillHeight: true
                                color: host.cBorder
                            }
                        }
                    }
                }
            }
        }

        // ---------- Toolpath preview: the loaded viewSample path, G0 red, G1 blue ----------
        Rectangle {
            id: workPreview
            objectName: "workPreview"
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
                    Layout.preferredHeight: 28
                    Layout.minimumHeight: 28
                    Layout.maximumHeight: 28
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
                        Text { text: "\ue53e"; color: host.cMutedForeground; font.family: "lucide"; font.pixelSize: 13 }
                        Text {
                            text: "ТРАЕКТОРИЯ · O1024"
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
                                    Layout.preferredWidth: 7
                                    Layout.preferredHeight: 7
                                    Layout.minimumWidth: 7
                                    Layout.maximumWidth: 7
                                    Layout.minimumHeight: 7
                                    Layout.maximumHeight: 7
                                    radius: 4
                                    color: modelData.on ? host.cWarning : host.cMuted
                                }
                                Text { text: modelData.label; color: host.cMutedForeground; font.pixelSize: 10 }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    Layout.minimumHeight: 1
                    Layout.maximumHeight: 1
                    color: host.cBorder
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: "#0E1319"

                    Canvas {
                        id: pathCanvas
                        anchors.fill: parent
                        onWidthChanged: requestPaint()
                        onHeightChanged: requestPaint()
                        onPaint: {
                            var ctx = getContext("2d");
                            ctx.clearRect(0, 0, width, height);
                            ctx.fillStyle = "#0E1319";
                            ctx.fillRect(0, 0, width, height);
                            var text = workScreen.host ? workScreen.host.viewSample : "";
                            var lines = text.split("\n");
                            var segs = [];
                            var cx = 0, cy = 0;
                            var mode = 0;
                            var minX = 0, maxX = 0, minY = 0, maxY = 0;
                            var first = true;
                            for (var i = 0; i < lines.length; i++) {
                                var clean = lines[i].replace(/\(.*?\)/g, " ");
                                var toks = clean.trim().split(/\s+/);
                                var nx = cx, ny = cy;
                                for (var j = 0; j < toks.length; j++) {
                                    var tk = toks[j];
                                    if (tk === "G0" || tk === "G00") mode = 0;
                                    else if (tk === "G1" || tk === "G01") mode = 1;
                                    else if (tk.charAt(0) === "X") nx = parseFloat(tk.substring(1));
                                    else if (tk.charAt(0) === "Y") ny = parseFloat(tk.substring(1));
                                }
                                if (nx !== cx || ny !== cy) {
                                    if (first) {
                                        minX = Math.min(cx, nx); maxX = Math.max(cx, nx);
                                        minY = Math.min(cy, ny); maxY = Math.max(cy, ny);
                                        first = false;
                                    } else {
                                        minX = Math.min(minX, cx, nx); maxX = Math.max(maxX, cx, nx);
                                        minY = Math.min(minY, cy, ny); maxY = Math.max(maxY, cy, ny);
                                    }
                                    segs.push({ x1: cx, y1: cy, x2: nx, y2: ny, feed: mode === 1 });
                                    cx = nx; cy = ny;
                                }
                            }
                            if (segs.length === 0 || width < 20 || height < 20) return;
                            var pad = 24;
                            var spanX = Math.max(1, maxX - minX);
                            var spanY = Math.max(1, maxY - minY);
                            var scale = Math.min((width - pad * 2) / spanX, (height - pad * 2) / spanY);
                            var ox = (width - spanX * scale) / 2;
                            var oy = (height - spanY * scale) / 2;
                            function px(v) { return ox + (v - minX) * scale; }
                            function py(v) { return height - (oy + (v - minY) * scale); }
                            ctx.strokeStyle = "#1B2530";
                            ctx.lineWidth = 1;
                            for (var gx = 0; gx <= width; gx += 50) {
                                ctx.beginPath(); ctx.moveTo(gx, 0); ctx.lineTo(gx, height); ctx.stroke();
                            }
                            for (var gy = 0; gy <= height; gy += 50) {
                                ctx.beginPath(); ctx.moveTo(0, gy); ctx.lineTo(width, gy); ctx.stroke();
                            }
                            for (var k = 0; k < segs.length; k++) {
                                var s = segs[k];
                                ctx.strokeStyle = s.feed ? "#338CFF" : "#F23333";
                                ctx.lineWidth = s.feed ? 2 : 1.2;
                                ctx.beginPath();
                                ctx.moveTo(px(s.x1), py(s.y1));
                                ctx.lineTo(px(s.x2), py(s.y2));
                                ctx.stroke();
                            }
                        }
                    }
                    RowLayout {
                        anchors.left: parent.left
                        anchors.bottom: parent.bottom
                        anchors.margins: 8
                        spacing: 8
                        Text { text: "G0 — красный, G1 — синий"; color: host.cMutedForeground; font.bold: true; font.pixelSize: 10 }
                        Text { text: "· " + workScreen.offsetCode() + " · мм"; color: host.cMutedForeground; font.pixelSize: 10; font.family: host.fontMono }
                    }
                }
            }
        }
    }
}
