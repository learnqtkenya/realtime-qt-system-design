pragma ComponentBehavior: Bound

import QtQuick
import QtQMLPro.Common
import Viewer

Item {
    id: viewport

    required property FrameMonitor monitor
    readonly property bool topDown: Lab.viewMode === "top"

    objectName: "viewport"
    clip: true

    Loader {
        anchors.fill: parent
        sourceComponent: {
            if (!viewport.topDown)
                return null;
            switch (Lab.renderer) {
            case "canvas":
                return canvasView;
            case "painted":
                return paintedView;
            case "worker":
                return workerView;
            case "sg":
                return sgView;
            case "rhi":
                return rhiView;
            default:
                return null;
            }
        }
    }

    // Loaded by URL on first use, so the other renderers never import Quick 3D. Kept afterwards:
    // destroying a View3D leaves GPU resources queued until exit.
    Loader {
        id: quick3d

        anchors.fill: parent
        active: false
        source: "Quick3DPoints.qml"

        Component.onCompleted: active = Lab.renderer === "quick3d"

        Connections {
            function onRendererChanged(): void {
                if (Lab.renderer === "quick3d")
                    quick3d.active = true;
            }

            target: Lab
        }
    }

    // The orbit view: created on first use, then kept (see the Quick 3D loader above).
    Loader {
        id: orbit

        anchors.fill: parent
        active: false
        source: "Orbit3DView.qml"

        Component.onCompleted: active = Lab.viewMode === "orbit"

        Connections {
            function onViewModeChanged(): void {
                if (Lab.viewMode === "orbit")
                    orbit.active = true;
            }

            target: Lab
        }
    }

    TapHandler {
        enabled: viewport.topDown
        onTapped: point => Selection.pickTopDown((point.position.x - viewport.width / 2) / ViewSettings.pxPerMetre, (viewport.height / 2 - point.position.y) / ViewSettings.pxPerMetre)
    }

    Component {
        id: canvasView

        Canvas {
            id: canvas

            property var clouds: [[], [], [], []]
            property real newestCaptureNs: 0

            antialiasing: false // the other renderers draw hard-edged squares

            onPaint: {
                const t0 = Stages.nowNs();
                const ctx = getContext("2d");
                ctx.clearRect(0, 0, width, height);
                const cx = width / 2;
                const cy = height / 2;
                const s = ViewSettings.pxPerMetre;
                for (let k = 0; k < clouds.length; ++k) {
                    const pts = clouds[k];
                    ctx.fillStyle = ViewSettings.sensorColors[k];
                    for (let i = 0; i < pts.length; ++i) {
                        const p = pts[i];
                        ctx.fillRect(cx + p.x * s, cy - p.y * s, 1.5, 1.5);
                    }
                }
                Stages.recordSince("canvas_paint_ms", t0);
            }
            onPainted: viewport.monitor.reportDisplayed(newestCaptureNs)

            Connections {
                function onLidarUpdated(sensorId: int, points: var, captureNs: real): void {
                    canvas.clouds[sensorId] = points;
                    canvas.newestCaptureNs = Math.max(canvas.newestCaptureNs, captureNs);
                    canvas.requestPaint();
                }

                target: Scene.canvasFeed
            }
        }
    }

    Component {
        id: paintedView

        PointCloudView {
            store: Scene.store
            pxPerMetre: ViewSettings.pxPerMetre
        }
    }

    Component {
        id: workerView

        PointLayersView {
            pipeline: Scene.pipeline
            pxPerMetre: ViewSettings.pxPerMetre
        }
    }

    Component {
        id: sgView

        SgPointCloudView {
            mailbox: Scene.mailbox
            pxPerMetre: ViewSettings.pxPerMetre
            pointSize: ViewSettings.pointSize
        }
    }

    Component {
        id: rhiView

        RhiPointCloudView {
            mailbox: Scene.mailbox
            pxPerMetre: ViewSettings.pxPerMetre
            pointSize: ViewSettings.pointSize
        }
    }

    Repeater {
        model: viewport.topDown ? Scene.tracks : null

        delegate: Item {
            id: track

            required property int trackId
            required property real posX
            required property real posY
            required property real heading
            required property real speed
            required property real boxLength
            required property real boxWidth
            readonly property bool selected: trackId === Selection.trackId

            x: viewport.width / 2 + posX * ViewSettings.pxPerMetre
            y: viewport.height / 2 - posY * ViewSettings.pxPerMetre

            Rectangle {
                x: -width / 2
                y: -height / 2
                width: track.boxLength * ViewSettings.pxPerMetre
                height: track.boxWidth * ViewSettings.pxPerMetre
                rotation: -track.heading * 180 / Math.PI
                color: track.selected ? "#40FBBF24" : "transparent"
                border.color: track.selected ? "#FBBF24" : "#F8FAFC"
                border.width: track.selected ? 2 : 1
            }

            Text {
                x: 8
                y: -20
                color: "#CBD5E1"
                font.pixelSize: 10
                text: "#" + track.trackId + "  " + track.speed.toFixed(1) + " m/s"
            }
        }
    }

    CameraStrip {
        anchors.left: parent.left
        anchors.bottom: timelineBar.top
        anchors.margins: 8
    }

    TimelineBar {
        id: timelineBar

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
    }

    Hud {
        x: 12
        y: 12
        monitor: viewport.monitor
        heading: Lab.sensors + " × " + Lab.pointsPerSensor.toLocaleString(Qt.locale("en_US"), "f", 0) + " pts @ " + Lab.rateHz + " Hz · " + Lab.objects + " objects · " + Lab.eventsPerSecond + " ev/s"
    }
}
