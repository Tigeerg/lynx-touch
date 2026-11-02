import QtQuick
import QtQuick.Layouts

// Portrait Work screen (touch only): live position, MDI when SETUP is active, and the real viewer.
// It does not reuse the 1024x600 WorkScreen; the rows and controls are sized for 864x1536.
Item {
    id: work
    objectName: "touchWork"
    property Item host

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        // Position
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 150
            Layout.minimumHeight: 150
            Layout.maximumHeight: 150
            color: host.cCard
            border.color: host.cBorder
            radius: 8
            clip: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 4
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "ПОЛОЖЕНИЕ · ММ"; color: host.cMutedForeground; font.bold: true; font.pixelSize: 11; font.letterSpacing: 1 }
                    Item { Layout.fillWidth: true }
                    Rectangle {
                        width: 52; height: 24; radius: 4; color: host.cPrimary
                        Text { anchors.centerIn: parent; text: host.activeOffsetCode(); color: host.cPrimaryFg; font.bold: true; font.pixelSize: 12; font.family: host.fontMono }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 0
                    Repeater {
                        model: ["X", "Y", "Z", "A"]
                        delegate: ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Text { text: modelData; color: host.cMutedForeground; font.bold: true; font.pixelSize: 12 }
                            Text {
                                Layout.fillWidth: true
                                text: host.machineCoordinate(modelData) + (modelData === "A" ? "°" : "")
                                color: host.cForeground
                                font.bold: true
                                font.pixelSize: modelData === "A" ? 30 : 34
                                font.family: host.fontMono
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }
        }

        // MDI (SETUP only), typed with the panel keyboard / TouchUI soft keyboard if present
        Rectangle {
            visible: host.mdiOpen()
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 88 : 0
            Layout.minimumHeight: visible ? 88 : 0
            Layout.maximumHeight: visible ? 88 : 0
            color: host.cCard
            border.color: host.cPrimary
            border.width: 2
            radius: 8

            RowLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 10
                Text { text: "MDI>"; color: host.cPrimary; font.bold: true; font.pixelSize: 24; font.family: host.fontMono }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        Layout.fillWidth: true
                        text: host.mdiBuffer !== "" ? host.mdiBuffer : "Введите команду: G0 X0 Y0"
                        color: host.mdiBuffer !== "" ? host.cForeground : host.cMutedForeground
                        font.pixelSize: 20
                        font.family: host.fontMono
                        elide: Text.ElideRight
                    }
                    RowLayout {
                        spacing: 6
                        Repeater {
                            model: host.mdiHistory
                            delegate: Rectangle {
                                width: mdiHist.implicitWidth + 14; height: 22; radius: 4; color: host.cMuted
                                Text { id: mdiHist; anchors.centerIn: parent; text: modelData; color: host.cMutedForeground; font.pixelSize: 12; font.family: host.fontMono }
                                MouseArea { anchors.fill: parent; onClicked: host.mdiRecall(index) }
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }
                }
                Rectangle {
                    width: 100; height: 44; radius: 6; color: host.cMuted; border.color: host.cBorder
                    Text { anchors.centerIn: parent; text: "Очистить"; color: host.cForeground; font.bold: true; font.pixelSize: 14 }
                    MouseArea { anchors.fill: parent; onClicked: host.mdiBuffer = "" }
                }
                Rectangle {
                    width: 86; height: 44; radius: 6; color: host.cPrimary
                    Text { anchors.centerIn: parent; text: "Пуск"; color: host.cPrimaryFg; font.bold: true; font.pixelSize: 15 }
                    MouseArea { anchors.fill: parent; onClicked: host.runMdi() }
                }
            }
        }

        // Viewer
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: host.cCard
            border.color: host.cBorder
            radius: 8
            clip: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 1
                spacing: 0
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 38
                    Layout.minimumHeight: 38
                    Layout.maximumHeight: 38
                    color: host.cMuted
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 8
                        Text { text: "ТРАЕКТОРИЯ · " + host.activeProgram().name; color: host.cMutedForeground; font.bold: true; font.pixelSize: 12; font.letterSpacing: 1 }
                        Item { Layout.fillWidth: true }
                        Text { text: "Покадр. · M01 · Холост."; color: host.cMutedForeground; font.pixelSize: 10 }
                    }
                }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: host.cBorder }
                ViewerPane {
                    id: workViewer
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    host: work.host
                    settings: work.host ? work.host.viewerSettings() : ({})
                }
            }
        }
    }
}
