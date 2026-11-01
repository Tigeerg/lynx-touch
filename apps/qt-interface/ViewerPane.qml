import QtQuick
import QtQuick3D

// The viewer pane of the Work screen: the same GcodeToolpath geometries the standalone viewer and
// the hidden 3D tab use. The camera is the viewer's look-at construction: pivot is the path centre,
// position = pivot - distance * forward.
Item {
    id: pane
    objectName: "viewerPane"
    property Item host
    property var path: host ? host.toolpathObject : null

    readonly property real yaw: 35
    readonly property real pitch: -32
    readonly property real dist: Math.max(60, (path ? path.radius : 40) * 1.8 + 30)
    readonly property vector3d center: path ? path.center : Qt.vector3d(0, 0, 0)
    readonly property vector3d fwd: Qt.vector3d(
        -Math.sin(yaw * Math.PI / 180) * Math.cos(pitch * Math.PI / 180),
        Math.sin(pitch * Math.PI / 180),
        -Math.cos(yaw * Math.PI / 180) * Math.cos(pitch * Math.PI / 180))

    View3D {
        anchors.fill: parent
        environment: SceneEnvironment {
            clearColor: "#0E1319"
            backgroundMode: SceneEnvironment.Color
        }
        PerspectiveCamera {
            position: Qt.vector3d(pane.center.x - pane.dist * pane.fwd.x,
                                  pane.center.y - pane.dist * pane.fwd.y,
                                  pane.center.z - pane.dist * pane.fwd.z)
            eulerRotation: Qt.vector3d(pane.pitch, pane.yaw, 0)
            clipNear: Math.max(0.05, pane.dist / 1000)
            clipFar: 4000
        }
        DirectionalLight {
            eulerRotation: Qt.vector3d(-45, -35, 0)
            brightness: 1.0
        }
        Model {
            geometry: pane.path ? pane.path.rapidGeometry : null
            materials: [ PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#F23333" } ]
        }
        Model {
            geometry: pane.path ? pane.path.feedGeometry : null
            materials: [ PrincipledMaterial { lighting: PrincipledMaterial.NoLighting; baseColor: "#338CFF" } ]
        }
    }
}
