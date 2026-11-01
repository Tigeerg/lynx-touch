import QtQuick
import QtQuick3D

// The viewer pane of the Work screen: the same GcodeToolpath geometries the standalone viewer and
// the hidden 3D tab use. The host owns the controller; this file only draws its geometries.
Item {
    id: pane
    objectName: "viewerPane"
    property Item host
    property var path: host ? host.toolpathObject : null

    View3D {
        anchors.fill: parent
        environment: SceneEnvironment {
            clearColor: "#0E1319"
            backgroundMode: SceneEnvironment.Color
        }
        PerspectiveCamera {
            position: Qt.vector3d(190, 150, 210)
            eulerRotation: Qt.vector3d(-24, -40, 0)
            clipNear: 1
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
