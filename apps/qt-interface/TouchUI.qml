import QtQuick
import QtQuick.Layouts

// The touch variant of the panel. The 15.6" panel is rotated to portrait: the logical canvas is
// 864x1536 (React FhdStage), not the 1024x600 of the physical-key panel. It reuses the same host
// state and the same Work/AUTO screens; only the frame, tabs, softkeys and the machine deck are
// touch-sized.
Item {
    id: touch
    objectName: "touchPanel"

    property Item host

    readonly property var tabTitles: ["tab.work", "tab.offset", "tab.tool", "tab.alarm", "tab.service"]
    readonly property var tabIcons: ["\ue121", "\ue0ac", "\ue1b1", "\ue3b3", "\ue603"]
    readonly property int workTab: (host && (host.activeScreen === "pos" || host.activeScreen === "prog")) ? 0
                                      : host && host.activeScreen === "offset" ? 1
                                      : host && host.activeScreen === "tool" ? 2
                                      : host && host.activeScreen === "alarm" ? 3 : 4
    property string clockText: ""

    function tabTarget(i) {
        if (!host) return;
        if (i === 0) host.activeScreen = host.workplace === "SETUP" ? "pos" : "prog";
        else if (i === 1) host.activeScreen = "offset";
        else if (i === 2) host.activeScreen = "tool";
        else if (i === 3) host.activeScreen = "alarm";
        else host.activeScreen = "service";
    }
    function tableRows() {
        var out = [];
        if (!host) return out;
        if (host.activeScreen === "offset") {
            for (var i = 0; i < host.offsetsData.length; i++) {
                var r = host.offsetsData[i];
                out.push({ a: r.code, b: r.x + "  " + r.y + "  " + r.z + "  " + r.a, on: r.active, idx: i });
            }
        } else if (host.activeScreen === "tool") {
            for (var j = 0; j < host.toolsData.length; j++) {
                var t = host.toolsData[j];
                out.push({ a: "T" + t.number, b: t.name + "  ·  Ø" + t.d + "  ·  " + t.life + "%", on: t.inSpindle, idx: j });
            }
        } else if (host.activeScreen === "alarm") {
            for (var k = 0; k < host.alarmsData.length; k++) {
                var a = host.alarmsData[k];
                out.push({ a: a.code, b: a.text, on: a.active, idx: k });
            }
        }
        return out;
    }
    function pickRow(i) {
        if (!host) return;
        if (host.activeScreen === "offset") host.activateOffset(i);
        else if (host.activeScreen === "tool") host.cursorTool = i;
        else if (host.activeScreen === "alarm") { host.cursorAlarm = i; host.pressSoftkey({ id: "ack" }); }
    }

    Timer {
        interval: 1000; running: true; repeat: true
        onTriggered: touch.clockText = Qt.formatTime(new Date(), "HH:mm:ss")
    }
    Component.onCompleted: clockText = Qt.formatTime(new Date(), "HH:mm:ss")

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        // ---------- Header ----------
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            Layout.minimumHeight: 56
            Layout.maximumHeight: 56
            spacing: 8
            Rectangle {
                Layout.preferredWidth: 220; Layout.fillHeight: true
                color: host.cCard; border.color: host.cBorder; radius: 8
                RowLayout {
                    anchors.fill: parent; anchors.margins: 8; spacing: 8
                    Rectangle {
                        Layout.preferredWidth: 40; Layout.preferredHeight: 40
                        color: host.cPrimary; radius: 6
                        Text { anchors.centerIn: parent; text: "ЧПУ"; color: host.cPrimaryFg; font.bold: true; font.pixelSize: 11 }
                    }
                    ColumnLayout { Layout.fillWidth: true; spacing: 0
                        Text { text: "VMC-850"; color: host.cForeground; font.bold: true; font.pixelSize: 16 }
                        Text { text: host.workplace + " · " + host.modeLabel(host.machineMode); color: host.cPrimary; font.bold: true; font.pixelSize: 12; font.family: host.fontMono }
                    }
                }
            }
            Rectangle {
                Layout.preferredWidth: 92; Layout.fillHeight: true
                color: host.cMuted; border.color: host.cBorder; radius: 8
                Text { anchors.centerIn: parent; text: "ГОТОВ"; color: host.cSuccess; font.bold: true; font.pixelSize: 14 }
            }
            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true
                color: host.cCard; border.color: host.cBorder; radius: 8
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 10; spacing: 0
                    Text { text: "ПРОГРАММА"; color: host.cMutedForeground; font.bold: true; font.pixelSize: 10; font.letterSpacing: 1 }
                    Text {
                        Layout.fillWidth: true
                        text: host.activeProgram().name + "   " + host.activeProgram().title
                        color: host.cForeground; font.bold: true; font.pixelSize: 16; font.family: host.fontMono
                        elide: Text.ElideRight
                    }
                }
            }
            Rectangle {
                Layout.preferredWidth: 88; Layout.fillHeight: true
                color: host.cCard; border.color: host.cWarning; border.width: 2; radius: 8
                RowLayout { anchors.centerIn: parent; spacing: 4
                    Text { text: "\ue3b3"; color: host.cWarning; font.family: "lucide"; font.pixelSize: 20 }
                    Text { text: host.activeAlarmCount(); color: host.cWarning; font.bold: true; font.pixelSize: 18; font.family: host.fontMono }
                }
                MouseArea { anchors.fill: parent; onClicked: host.activeScreen = "alarm" }
            }
            Rectangle {
                Layout.preferredWidth: 56; Layout.fillHeight: true
                color: host.cCard; border.color: host.cBorder; radius: 8
                Text { anchors.centerIn: parent; text: "?"; color: host.cForeground; font.bold: true; font.pixelSize: 22 }
                MouseArea { anchors.fill: parent; onClicked: host.helpOpen = !host.helpOpen }
            }
            Rectangle {
                Layout.preferredWidth: 120; Layout.fillHeight: true
                color: host.cCard; border.color: host.cBorder; radius: 8
                Text { anchors.centerIn: parent; text: touch.clockText; color: host.cForeground; font.bold: true; font.pixelSize: 18; font.family: host.fontMono }
            }
        }

        // ---------- Tabs ----------
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 42
            Layout.minimumHeight: 42
            Layout.maximumHeight: 42
            spacing: 6
            Repeater {
                model: touch.tabTitles
                delegate: Rectangle {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    radius: 8
                    color: touch.workTab === index ? host.cPrimary : host.cCard
                    border.color: touch.workTab === index ? host.cPrimary : host.cBorder
                    RowLayout { anchors.centerIn: parent; spacing: 6
                        Text { text: touch.tabIcons[index]; color: touch.workTab === index ? host.cPrimaryFg : host.cForeground; font.family: "lucide"; font.pixelSize: 16 }
                        Text { text: host.i18n.t(modelData); color: touch.workTab === index ? host.cPrimaryFg : host.cForeground; font.bold: true; font.pixelSize: 14 }
                    }
                    MouseArea { anchors.fill: parent; onClicked: touch.tabTarget(index) }
                }
            }
        }

        // ---------- Screen content ----------
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 300
            clip: true

            // Touch-specific screens: no reuse of the 1024x600 WorkScreen/AutoScreen.
            TouchWork {
                anchors.fill: parent
                visible: host.activeScreen === "pos"
                host: touch.host
            }

            TouchProgram {
                anchors.fill: parent
                visible: host.activeScreen === "prog"
                host: touch.host
            }

            // Offset / tool / alarm tables.
            Flickable {
                visible: host.activeScreen === "offset" || host.activeScreen === "tool" || host.activeScreen === "alarm"
                anchors.fill: parent
                contentWidth: width; contentHeight: touchTableCol.height
                clip: true; boundsBehavior: Flickable.StopAtBounds
                Column {
                    id: touchTableCol
                    width: parent.width
                    height: touch.tableRows().length * 58 + 8
                    Repeater {
                        model: touch.tableRows()
                        delegate: Rectangle {
                            width: touchTableCol.width; height: 58
                            color: modelData.on ? host.cAccent : host.cCard
                            border.color: host.cBorder
                            RowLayout {
                                anchors.fill: parent; anchors.margins: 8; spacing: 10
                                Text { text: modelData.a; Layout.preferredWidth: 120; color: host.cForeground; font.bold: true; font.pixelSize: 18; font.family: host.fontMono }
                                Text { text: modelData.b; Layout.fillWidth: true; color: host.cForeground; font.pixelSize: 14; elide: Text.ElideRight }
                                Text { text: modelData.on ? "АКТИВНО" : ""; color: host.cSuccess; font.bold: true; font.pixelSize: 11 }
                            }
                            MouseArea { anchors.fill: parent; onClicked: touch.pickRow(modelData.idx) }
                        }
                    }
                }
            }

            // Service: vertical icon navigation + the React-style parameter table.
            RowLayout {
                visible: host.activeScreen === "service"
                anchors.fill: parent
                spacing: 6

                Rectangle {
                    Layout.preferredWidth: 64
                    Layout.minimumWidth: 64
                    Layout.maximumWidth: 64
                    Layout.fillHeight: true
                    color: host.cMuted
                    radius: 6
                    Column {
                        anchors.top: parent.top
                        anchors.topMargin: 6
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: 6
                        Repeater {
                            model: host.serviceIds
                            delegate: Rectangle {
                                width: 52; height: 48; radius: 6
                                color: host.serviceSection === modelData ? host.cPrimary : "transparent"
                                border.color: host.serviceSection === modelData ? host.cPrimary : "transparent"
                                Text { anchors.centerIn: parent; text: host.serviceIconOf(modelData); color: host.serviceSection === modelData ? host.cPrimaryFg : host.cForeground; font.family: "lucide"; font.pixelSize: 22 }
                                MouseArea { anchors.fill: parent; onClicked: host.serviceSection = modelData }
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 0

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 30
                        Layout.minimumHeight: 30
                        Layout.maximumHeight: 30
                        color: host.cMuted
                        RowLayout {
                            anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 6
                            Text { text: host.i18n.t("svc.param"); Layout.minimumWidth: 220; Layout.fillWidth: true; color: host.cMutedForeground; font.bold: true; font.pixelSize: 10 }
                            Text { text: host.i18n.t("svc.key"); Layout.preferredWidth: 170; Layout.minimumWidth: 170; color: host.cMutedForeground; font.bold: true; font.pixelSize: 10 }
                            Text { text: host.i18n.t("svc.value"); Layout.preferredWidth: 140; Layout.minimumWidth: 140; color: host.cMutedForeground; font.bold: true; font.pixelSize: 10 }
                            Text { text: host.i18n.t("svc.default"); Layout.preferredWidth: 64; Layout.minimumWidth: 64; color: host.cMutedForeground; font.bold: true; font.pixelSize: 10 }
                            Item { Layout.preferredWidth: 86 }
                        }
                    }

                    Flickable {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        contentWidth: width
                        contentHeight: touchSvcCol.height
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        Column {
                            id: touchSvcCol
                            width: parent.width
                            height: host.svcListLen() * 54
                            Repeater {
                                model: host.svcListLen()
                                delegate: Rectangle {
                                    width: touchSvcCol.width; height: 54
                                    color: host.svcCursor === index ? host.cAccent : host.cCard
                                    border.color: host.cBorder
                                    RowLayout {
                                        anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 6
                                        Text { text: host.svcDefAt(index) ? host.svcDefAt(index).label : ""; Layout.minimumWidth: 220; Layout.fillWidth: true; color: host.cForeground; font.pixelSize: 13; elide: Text.ElideRight }
                                        Text { text: host.svcDefAt(index) ? host.svcDefAt(index).key : ""; Layout.preferredWidth: 170; Layout.minimumWidth: 170; color: host.cMutedForeground; font.pixelSize: 11; font.family: host.fontMono; elide: Text.ElideRight }
                                        Text {
                                            Layout.preferredWidth: 140; Layout.minimumWidth: 140
                                            text: host.svcDefAt(index) ? host.fmtVal(host.svcDefAt(index), host.svcValueAt(index)) : ""
                                            color: host.cForeground; font.bold: true; font.pixelSize: 13; font.family: host.fontMono
                                            horizontalAlignment: Text.AlignRight; elide: Text.ElideRight
                                        }
                                        Text {
                                            Layout.preferredWidth: 64; Layout.minimumWidth: 64
                                            text: host.svcDefAt(index) ? host.fmtVal(host.svcDefAt(index), host.svcDefAt(index).dflt) : ""
                                            color: host.cMutedForeground; font.pixelSize: 11; font.family: host.fontMono
                                            horizontalAlignment: Text.AlignRight; elide: Text.ElideRight
                                        }
                                        Rectangle {
                                            Layout.preferredWidth: 40; Layout.preferredHeight: 38; radius: 4; color: host.cMuted
                                            Text { anchors.centerIn: parent; text: "−"; color: host.cForeground; font.bold: true; font.pixelSize: 20 }
                                            MouseArea { anchors.fill: parent; onClicked: { host.svcCursor = index; host.adjustParam(-1, false) } }
                                        }
                                        Rectangle {
                                            Layout.preferredWidth: 40; Layout.preferredHeight: 38; radius: 4; color: host.cMuted
                                            Text { anchors.centerIn: parent; text: "+"; color: host.cForeground; font.bold: true; font.pixelSize: 20 }
                                            MouseArea { anchors.fill: parent; onClicked: { host.svcCursor = index; host.adjustParam(1, false) } }
                                        }
                                    }
                                    MouseArea { anchors.fill: parent; onClicked: host.svcCursor = index }
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 44
                        Layout.minimumHeight: 44
                        Layout.maximumHeight: 44
                        radius: 6; color: host.cPrimary
                        Text { anchors.centerIn: parent; text: host.i18n.t("svc.save"); color: host.cPrimaryFg; font.bold: true; font.pixelSize: 15 }
                        MouseArea { anchors.fill: parent; onClicked: host.svcSave() }
                    }
                }
            }
        }

        // ---------- Message line ----------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            Layout.minimumHeight: 28
            Layout.maximumHeight: 28
            color: host.message !== "" ? host.cAccent : "transparent"
            Text {
                anchors.left: parent.left; anchors.leftMargin: 8; anchors.verticalCenter: parent.verticalCenter
                text: host.message
                color: host.message !== "" ? host.cForeground : "transparent"
                font.pixelSize: 12
            }
        }

        // ---------- Softkeys ----------
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 70
            Layout.minimumHeight: 70
            Layout.maximumHeight: 70
            spacing: 6
            Repeater {
                model: host.softkeysFor(host.activeScreen)
                delegate: Rectangle {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    radius: 8
                    color: modelData && !modelData.disabled ? (modelData.active ? host.cPrimary : host.cCard) : host.cMuted
                    border.color: modelData && !modelData.disabled ? host.cBorder : host.cBorderMuted
                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 2
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: modelData ? (modelData.icon || "") : ""
                            color: modelData && !modelData.disabled ? (modelData.active ? host.cPrimaryFg : host.cForeground) : host.cMutedForeground
                            font.family: "lucide"; font.pixelSize: 20
                        }
                        Text {
                            Layout.fillWidth: true
                            Layout.maximumWidth: 116
                            text: modelData ? modelData.label : ""
                            color: modelData && !modelData.disabled ? (modelData.active ? host.cPrimaryFg : host.cForeground) : host.cMutedForeground
                            font.bold: true; font.pixelSize: 11; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight
                        }
                    }
                    MouseArea { anchors.fill: parent; onClicked: host.pressSoftkey(modelData) }
                }
            }
        }

        // ---------- Machine deck ----------
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 280
            Layout.minimumHeight: 280
            Layout.maximumHeight: 280
            spacing: 8

            // Workplace / mode column
            ColumnLayout {
                Layout.preferredWidth: 150
                Layout.minimumWidth: 150
                Layout.maximumWidth: 150
                Layout.fillHeight: true
                spacing: 8
                Repeater {
                    model: [ { id: "RUN", label: "АВТО" }, { id: "TEST", label: "ПРОГОН" }, { id: "SETUP", label: "НАЛАДКА" } ]
                    delegate: HwKey {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        host: touch.host
                        label: modelData.label
                        active: host.workplace === modelData.id
                        led: false
                        onClicked: host.setWorkplace(modelData.id)
                    }
                }
            }

            // Jog pad
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 6
                GridLayout {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    columns: 3; rowSpacing: 6; columnSpacing: 6
                    Repeater {
                        model: [
                            { label: "Z+", axis: "Z", dir: 1 },
                            { label: "Y+", axis: "Y", dir: 1 },
                            { label: "A+", axis: "A", dir: 1 },
                            { label: "X−", axis: "X", dir: -1 },
                            { label: "БЫСТРО", axis: "", dir: 0 },
                            { label: "X+", axis: "X", dir: 1 },
                            { label: "Z−", axis: "Z", dir: -1 },
                            { label: "Y−", axis: "Y", dir: -1 },
                            { label: "A−", axis: "A", dir: -1 }
                        ]
                        delegate: HwKey {
                            Layout.fillWidth: true; Layout.fillHeight: true
                            host: touch.host
                            label: modelData.label
                            active: modelData.axis !== "" && host.jogAxis === modelData.axis
                            led: false
                            onClicked: {
                                if (modelData.axis === "") return;
                                host.selectAxis(modelData.axis);
                                host.machineJogRequest(modelData.dir);
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true; Layout.preferredHeight: 44
                    Layout.minimumHeight: 44; Layout.maximumHeight: 44
                    spacing: 6
                    Repeater {
                        model: [0, 1, 2]
                        delegate: HwKey {
                            Layout.fillWidth: true; Layout.fillHeight: true
                            host: touch.host
                            label: ["0.001", "0.01", "0.1"][index]
                            active: host.jogStep === index
                            led: false
                            onClicked: host.jogStep = index
                        }
                    }
                }
            }

            // Functions: 4x3 block exactly like the React touch deck. The safe-point key is one
            // cell like the others (the React reference had a three-cell bug).
            GridLayout {
                Layout.preferredWidth: 300
                Layout.minimumWidth: 300
                Layout.maximumWidth: 300
                Layout.fillHeight: true
                columns: 4
                rowSpacing: 6
                columnSpacing: 6
                Repeater {
                    model: [
                        { t: "M3", i: "▶", k: "m3", on: host.spindleOn && host.spindleDirection === "M03" },
                        { t: "M4", i: "◀", k: "m4", on: host.spindleOn && host.spindleDirection === "M04" },
                        { t: "M5", i: "■", k: "m5", on: !host.spindleOn },
                        { t: "СОЖ", i: "●", k: "coolant", on: host.coolantOn },
                        { t: "Туман", i: "≈", k: "mist", on: host.mistOn },
                        { t: "Свет", i: "☀", k: "light", on: host.workLightOn },
                        { t: "Стружка", i: "▤", k: "chip", on: host.chipConveyorOn },
                        { t: "Обдув", i: "≋", k: "air", on: host.airBlastOn },
                        { t: "Промывка", i: "≈", k: "wash", on: host.washOn },
                        { t: "Смазка", i: "◌", k: "lube", on: host.lubeOn },
                        { t: "Отвод", i: "⇱", k: "safe", on: false },
                        { t: host.toolClamped ? "Разжать" : "Зажать", i: host.toolClamped ? "↥" : "↧", k: "clamp", on: host.toolClamped }
                    ]
                    delegate: HwKey {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        host: touch.host
                        icon: modelData.i || ""
                        label: modelData.t
                        active: modelData.on
                        led: true
                        onClicked: {
                            if (modelData.k === "m3") host.spindleCommand("M03");
                            else if (modelData.k === "m4") host.spindleCommand("M04");
                            else if (modelData.k === "m5") host.spindleCommand("M05");
                            else if (modelData.k === "safe") host.goSafePoint();
                            else if (modelData.k === "clamp") { host.toolClamped = !host.toolClamped; host.message = host.toolClamped ? "Инструмент зажат" : "Инструмент разжат"; }
                            else host.toggleAux(modelData.k);
                        }
                    }
                }
            }
        }

        // ---------- Cycle row ----------
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 72
            Layout.minimumHeight: 72
            Layout.maximumHeight: 72
            spacing: 8
            Repeater {
                model: [
                    { label: host.i18n.t("cycle.start"), action: "start", color: host.cSuccess, icon: "▶" },
                    { label: host.i18n.t("cycle.hold"), action: "hold", color: host.cWarning, icon: "II" },
                    { label: host.i18n.t("cycle.reset"), action: "reset", color: host.cPrimary, icon: "↺" },
                    { label: host.i18n.t("cycle.estop"), action: "estop", color: host.cDestructive, icon: "■" }
                ]
                delegate: Rectangle {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    radius: 8; color: modelData.color
                    RowLayout {
                        anchors.centerIn: parent
                        spacing: 6
                        Text { text: modelData.icon; color: host.cPrimaryFg; font.pixelSize: 18; font.bold: true }
                        Text { text: modelData.label; color: host.cPrimaryFg; font.bold: true; font.pixelSize: 16; font.letterSpacing: 1 }
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            if (modelData.action === "start") host.machineCycleStart();
                            else if (modelData.action === "hold") host.machineFeedHold();
                            else if (modelData.action === "reset") host.machineReset();
                            else host.machineEmergencyStop();
                        }
                    }
                }
            }
        }
    }

    function lineText(modelData) {
        var s = "";
        for (var i = 0; i < modelData.toks.length; i++)
            s += modelData.toks[i].t + (i + 1 < modelData.toks.length ? " " : "");
        return s;
    }
}
