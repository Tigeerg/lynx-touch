import QtQuick

// The debug window host: the panel keeps its logical canvas (1024x600 or 864x1536), while the
// window may be scaled down to fit the developer screen. The aspect ratio is locked in C++
// (PanelView); here the logical item only gets a uniform scale and stays centred.
Item {
    id: host

    readonly property real logicalW: panel.item && panel.item.touchMode ? 864 : 1024
    readonly property real logicalH: panel.item && panel.item.touchMode ? 1536 : 600
    readonly property real scaleFactor: Math.min(width / logicalW, height / logicalH)

    Loader {
        id: panel
        width: host.logicalW
        height: host.logicalH
        scale: host.scaleFactor
        transformOrigin: Item.TopLeft
        x: (host.width - host.logicalW * host.scaleFactor) / 2
        y: (host.height - host.logicalH * host.scaleFactor) / 2
        source: "main.qml"
    }
}
