import QtQuick
import QtQuick.Layouts

// The Work screen: the live DRO in one large line, the active NC toolpath under it and the
// autonomous status column on the right. The DRO is the live machine position in the active
// offset; the offsets screen edits the offset table, not these numbers.
Item {
    id: workScreen
    objectName: "workScreen"

    property Item host

    readonly property var axes: ["X", "Y", "Z", "A"]

    function dro(axis) {
        return host ? host.machineCoordinate(axis) : "—";
    }
    function offsetCode() { return host ? host.activeOffsetCode() : "—"; }

    // Panel actuators feed the active viewer: manual on TEST/SETUP, AUTO on RUN.
    function viewerCycleStart() {
        if (host && host.workplace === "RUN") autoScreen.cycleStart(); else viewerPane.cycleStart();
    }
    function viewerFeedHold() {
        if (host && host.workplace === "RUN") autoScreen.feedHold(); else viewerPane.feedHold();
    }
    function viewerReset() {
        if (host && host.workplace === "RUN") autoScreen.reset(); else viewerPane.reset();
    }
    function viewerSeek(fraction) {
        if (host && host.workplace === "RUN") autoScreen.seek(fraction); else viewerPane.seek(fraction);
    }

    ColumnLayout {
        anchors.fill: parent
        visible: !host || host.workplace !== "RUN"
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
                        text: "ПОЛОЖЕНИЕ · ММ"
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

        // ---------- MDI line (SETUP): typed with the panel keyboard, Enter runs ----------
        Rectangle {
            id: mdiPanel
            objectName: "mdiPanel"
            visible: host.workplace === "SETUP"
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 62 : 0
            Layout.minimumHeight: visible ? 62 : 0
            Layout.maximumHeight: visible ? 62 : 0
            color: host.cCard
            border.color: host.cPrimary
            border.width: 2
            radius: 6
            clip: true

            RowLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 10

                Text {
                    text: "MDI>"
                    color: host.cPrimary
                    font.bold: true
                    font.pixelSize: 18
                    font.family: host.fontMono
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 2
                    Text {
                        Layout.fillWidth: true
                        text: host.mdiBuffer !== "" ? host.mdiBuffer : "Введите команду: G0 X0 Y0"
                        color: host.mdiBuffer !== "" ? host.cForeground : host.cMutedForeground
                        font.pixelSize: 17
                        font.family: host.fontMono
                        elide: Text.ElideRight
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Repeater {
                            model: host.mdiHistory
                            delegate: Rectangle {
                                Layout.preferredHeight: 16
                                width: histText.implicitWidth + 10
                                radius: 3
                                color: host.cMuted
                                Text {
                                    id: histText
                                    anchors.centerIn: parent
                                    text: modelData
                                    color: host.cMutedForeground
                                    font.pixelSize: 9
                                    font.family: host.fontMono
                                }
                                MouseArea { anchors.fill: parent; onClicked: host.mdiRecall(index) }
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }
                }
                Rectangle {
                    Layout.preferredWidth: 84
                    Layout.preferredHeight: 30
                    radius: 4
                    color: host.cMuted
                    border.color: host.cBorder
                    Text { anchors.centerIn: parent; text: "Очистить"; color: host.cForeground; font.bold: true; font.pixelSize: 11 }
                    MouseArea { anchors.fill: parent; onClicked: host.mdiBuffer = "" }
                }
                Rectangle {
                    Layout.preferredWidth: 70
                    Layout.preferredHeight: 30
                    radius: 4
                    color: host.cPrimary
                    Text { anchors.centerIn: parent; text: "Пуск"; color: host.cPrimaryFg; font.bold: true; font.pixelSize: 12 }
                    MouseArea { anchors.fill: parent; onClicked: host.runMdi() }
                }
            }
        }

        // ---------- Toolpath preview: the active NC path, G0 red, G1 blue ----------
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

                    ViewerPane {
                        id: viewerPane
                        anchors.fill: parent
                        host: workScreen.host
                        active: !host || host.workplace !== "RUN"
                        settings: workScreen.host ? workScreen.host.viewerSettings() : ({})
                    }
                    RowLayout {
                        anchors.left: parent.left
                        anchors.bottom: parent.bottom
                        anchors.margins: 8
                        spacing: 8
                        Text { text: "G0 — красный, G1 — синий"; color: host.cMutedForeground; font.bold: true; font.pixelSize: 10 }
                        Text { text: "· положение в " + workScreen.offsetCode() + " · мм"; color: host.cMutedForeground; font.pixelSize: 10; font.family: host.fontMono }
                    }
                }
            }
        }
    }

    // The AUTO workplace shares the real viewer and the active NC file, but has its own layout
    // (position, distance to the block, remaining path, F8 views).
    AutoScreen {
        id: autoScreen
        anchors.fill: parent
        visible: host && host.workplace === "RUN"
        host: workScreen.host
    }
}
