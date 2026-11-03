pragma ComponentBehavior: Bound

import QtQuick
import QtQuick3D
import Viewer

View3D {
    id: view

    readonly property bool live: Lab.renderer === "quick3d"

    visible: live

    camera: topDown
    environment: SceneEnvironment {
        backgroundMode: SceneEnvironment.Transparent
        antialiasingMode: SceneEnvironment.NoAA
        tonemapMode: SceneEnvironment.TonemapModeLinear // includes the conversion back to sRGB
    }

    OrthographicCamera {
        id: topDown

        z: 100
        horizontalMagnification: ViewSettings.pxPerMetre
        verticalMagnification: ViewSettings.pxPerMetre
    }

    Repeater3D {
        model: 4

        delegate: Model {
            id: model

            required property int index

            geometry: LidarGeometry {
                mailbox: view.live ? Scene.mailbox : null
                sensor: model.index
            }
            materials: PrincipledMaterial {
                lighting: PrincipledMaterial.NoLighting
                baseColor: ViewSettings.sensorColors[model.index]
                pointSize: ViewSettings.pointSize
            }
        }
    }
}
