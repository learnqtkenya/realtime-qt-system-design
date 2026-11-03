pragma ComponentBehavior: Bound

import QtQuick
import QtQuick3D
import QtQuick3D.Helpers
import QtQMLPro.Common
import Viewer

// The world is metres, z up. Quick 3D is y up, so one node rotates world into scene.
View3D {
    id: view

    readonly property bool live: Lab.viewMode === "orbit"
    // Found when the selection or the boxes change, not every frame; null once its box is destroyed.
    property Model selectedBox: null
    // Level of detail: double the stride each time the camera distance doubles past 60 m.
    readonly property int lodStride: Lab.lodEnabled ? Math.max(1, Math.min(8, Math.pow(2, Math.floor(Math.log2(Math.max(1, eye.z) / 60))))) : 1

    function worldFromScene(p: vector3d): vector3d {
        return Qt.vector3d(p.x, -p.z, p.y);
    }

    function pickAt(x: real, y: real): void {
        const t0 = Stages.nowNs();
        const hit = view.pick(x, y);
        const quick3dMs = (Stages.nowNs() - t0) / 1e6;
        const name = hit.objectHit ? hit.objectHit.objectName : "";
        const quick3dId = name.startsWith("track:") ? parseInt(name.substring(6)) : -1;

        const near = view.worldFromScene(view.mapTo3DScene(Qt.vector3d(x, y, 0)));
        const far = view.worldFromScene(view.mapTo3DScene(Qt.vector3d(x, y, 1000)));
        const ours = Selection.pickRay(near, far.minus(near));
        Selection.recordComparison(quick3dId, ours, quick3dMs, hit.objectHit ? hit.distance : -1, near, far.minus(near));
    }

    visible: live
    camera: eye
    environment: SceneEnvironment {
        backgroundMode: SceneEnvironment.Color
        clearColor: "#0B1020"
        antialiasingMode: SceneEnvironment.NoAA
        tonemapMode: SceneEnvironment.TonemapModeLinear
    }

    Node {
        id: pivot

        eulerRotation.x: -35

        PerspectiveCamera {
            id: eye

            z: Number(AppOptions.extra("zoom", 90))
            clipNear: 0.5
            clipFar: 2000
        }
    }

    Node {
        id: world

        eulerRotation.x: -90

        Repeater3D {
            model: 4

            delegate: Model {
                id: points

                required property int index

                geometry: LidarGeometry {
                    mailbox: view.live ? Scene.mailbox : null
                    sensor: points.index
                    pointStep: view.lodStride
                }
                materials: CustomMaterial {
                    property real pointSize: ViewSettings.pointSize

                    shadingMode: CustomMaterial.Unshaded
                    vertexShader: "heightcolor.vert"
                    fragmentShader: "heightcolor.frag"
                }
            }
        }

        Repeater3D {
            id: boxes

            model: view.live ? Scene.tracks : null

            onObjectAdded: (index, object) => {
                if (object.objectName === "track:" + Selection.trackId)
                    view.selectedBox = object as Model;
            }

            delegate: Model {
                id: box

                required property int trackId
                required property real posX
                required property real posY
                required property real heading
                required property real boxLength
                required property real boxWidth
                required property int trackClass
                readonly property real boxHeight: Selection.heightFor(trackClass)
                readonly property bool selected: trackId === Selection.trackId

                objectName: "track:" + trackId
                source: "#Cube"
                pickable: true
                position: Qt.vector3d(posX, posY, Selection.groundZ() + boxHeight / 2)
                scale: Qt.vector3d(boxLength / 100, boxWidth / 100, boxHeight / 100)
                eulerRotation.z: heading * 180 / Math.PI
                materials: PrincipledMaterial {
                    lighting: PrincipledMaterial.NoLighting
                    baseColor: box.selected ? "#FBBF24" : "#E2E8F0"
                    opacity: box.selected ? 0.85 : 0.25
                }
            }
        }
    }

    // Keeps the selection visible at any distance: a ring and label projected from 3D every frame.
    Item {
        id: marker

        property string label: ""

        visible: false
        width: 34
        height: 34

        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: "transparent"
            border.color: "#FBBF24"
            border.width: 2
        }

        Text {
            anchors.left: parent.right
            anchors.leftMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            text: marker.label
            color: "#FBBF24"
            font.pixelSize: 12
            font.bold: true
            style: Text.Outline
            styleColor: "#0B1020"
        }
    }

    FrameAnimation {
        running: view.live && Selection.trackId >= 0
        onRunningChanged: if (!running)
            marker.visible = false
        onTriggered: {
            const t0 = Stages.nowNs();
            view.placeMarker();
            Stages.recordSince("marker_ms", t0);
        }
    }

    function findSelectedBox(): Model {
        for (let i = 0; i < boxes.count; ++i) {
            const b = boxes.objectAt(i) as Model;
            if (b && b.objectName === "track:" + Selection.trackId)
                return b;
        }
        return null;
    }

    function placeMarker(): void {
        const b = view.selectedBox;
        if (!b) {
            marker.visible = false;
            return;
        }
        const p = view.mapFrom3DScene(b.scenePosition);
        marker.visible = p.z > 0;
        marker.x = p.x - marker.width / 2;
        marker.y = p.y - marker.height / 2;
        marker.label = "#" + Selection.trackId;
    }

    Connections {
        function onTrackIdChanged(): void {
            view.selectedBox = view.findSelectedBox();
        }

        target: Selection
    }

    OrbitCameraController {
        anchors.fill: parent
        origin: pivot
        camera: eye
    }

    TapHandler {
        onTapped: point => view.pickAt(point.position.x, point.position.y)
    }

    // --set robot=click3d: click a random object every 200 ms (picking comparison).
    Timer {
        interval: 200
        repeat: true
        running: view.live && AppOptions.extra("robot", "") === "click3d"
        onTriggered: {
            if (boxes.count === 0)
                return;
            const b = boxes.objectAt(Math.floor(Math.random() * boxes.count)) as Model;
            const p = view.mapFrom3DScene(b.scenePosition);
            if (p.z > 0 && p.x >= 0 && p.y >= 0 && p.x < view.width && p.y < view.height)
                view.pickAt(p.x, p.y);
        }
    }
}
