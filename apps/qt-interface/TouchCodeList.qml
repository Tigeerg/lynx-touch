import QtQuick

// Touch-sized NC block list. Used by the portrait Work/Program screens and by the translucent
// overlay; the rows are finger-height and never reuse the 1024-panel list.
Flickable {
    id: list
    property Item host
    property color background: "transparent"
    property bool translucent: false

    contentWidth: width
    contentHeight: col.height
    clip: true
    boundsBehavior: Flickable.StopAtBounds

    Rectangle {
        anchors.fill: parent
        color: list.background
    }

    Column {
        id: col
        width: list.width
        height: list.host ? list.host.progLines.length * 34 + 8 : 0

        Repeater {
            model: list.host ? list.host.progLines : []
            delegate: Rectangle {
                width: col.width
                height: 34
                color: index === list.host.cursorProg ? list.host.cAccent : (list.translucent ? "transparent" : list.host.cCard)
                Row {
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8
                    Text {
                        width: 28
                        text: modelData.num
                        color: list.host.cMutedForeground
                        font.family: list.host.fontMono
                        font.pixelSize: 13
                        horizontalAlignment: Text.AlignRight
                    }
                    Repeater {
                        model: modelData.toks
                        delegate: Text {
                            text: modelData.t + " "
                            color: modelData.c === "primary" ? list.host.cPrimary
                                 : modelData.c === "destructive" ? list.host.cDestructive
                                 : modelData.c === "success" ? list.host.cSuccess
                                 : modelData.c === "muted" ? list.host.cMutedForeground
                                 : modelData.c === "comment" ? list.host.cMutedForeground
                                 : list.host.cForeground
                            font.family: list.host.fontMono
                            font.pixelSize: 15
                            font.italic: modelData.c === "comment"
                            font.bold: (modelData.c === "primary" || modelData.c === "destructive")
                        }
                    }
                }
                MouseArea { anchors.fill: parent; onClicked: list.host.cursorProg = index }
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
