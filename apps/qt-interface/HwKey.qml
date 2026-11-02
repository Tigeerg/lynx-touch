import QtQuick

// The single key style of the product. Mirrors the React hardware key: 8 px radius, a thick dark
// edge with a heavier bottom face, an optional LED, icon and a two-line caption. Layout size is
// supplied by the caller through Layout attached properties.
Rectangle {
    id: key
    property Item host
    property string label: ""
    property string sub: ""
    property string icon: ""
    property bool active: false
    property bool led: false
    property color capColor: host ? host.cCard : "#FCFDFF"
    property color activeColor: host ? host.cPrimary : "#0362BB"
    property color ink: host ? host.cForeground : "#141B24"
    property color activeInk: host ? host.cPrimaryFg : "#FCFCFC"
    property color edge: "#7C858E"
    signal clicked()

    radius: 8
    color: active ? activeColor : capColor
    border.color: active ? (host ? host.cPrimary : "#0362BB") : edge
    border.width: 2
    clip: true

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 5
        color: key.active ? Qt.darker(key.activeColor, 1.35) : "#3A4148"
        radius: 2
    }

    Rectangle {
        visible: key.led
        width: 18; height: 5; radius: 2
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: -3
        color: key.active ? (host ? host.cSuccess : "#1D9247") : "#9AA3AD"
    }

    Column {
        anchors.centerIn: parent
        spacing: 1
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: key.icon !== ""
            text: key.icon
            color: key.active ? key.activeInk : key.ink
            font.family: "lucide"; font.pixelSize: 18
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: key.label
            color: key.active ? key.activeInk : key.ink
            font.bold: true; font.pixelSize: 11
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: key.sub !== ""
            text: key.sub
            color: key.active ? key.activeInk : key.ink
            opacity: 0.75; font.pixelSize: 8; font.family: host ? host.fontMono : "monospace"
        }
    }

    MouseArea { anchors.fill: parent; onClicked: key.clicked() }
}
