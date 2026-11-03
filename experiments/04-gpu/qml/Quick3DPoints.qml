pragma ComponentBehavior: Bound

import QtQuick
import QtQuick3D
import M04Gpu

View3D {
    id: view

    required property real pxPerMetre
    property real pointSize: 1.5
    readonly property list<color> colors: ["#38BDF8", "#34D399", "#FBBF24", "#F472B6"]

    camera: topDown
    environment: SceneEnvironment {
        backgroundMode: SceneEnvironment.Transparent
        antialiasingMode: SceneEnvironment.NoAA
        tonemapMode: SceneEnvironment.TonemapModeLinear // includes the conversion back to sRGB
    }

    OrthographicCamera {
        id: topDown

        z: 100
        horizontalMagnification: view.pxPerMetre
        verticalMagnification: view.pxPerMetre
    }

    Repeater3D {
        model: Scene.mailbox ? 4 : 0

        delegate: Model {
            id: model

            required property int index

            geometry: LidarGeometry {
                mailbox: Scene.mailbox
                sensor: model.index
            }
            materials: PrincipledMaterial {
                lighting: PrincipledMaterial.NoLighting
                baseColor: view.colors[model.index]
                pointSize: view.pointSize
            }
        }
    }
}
