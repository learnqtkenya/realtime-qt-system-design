pragma ComponentBehavior: Bound

import QtQuick
import QtQMLPro.Common
import M01First

Window {
    id: root

    readonly property real pxPerMetre: 7
    readonly property int logWidth: 360

    width: 1400
    height: 800
    visible: true
    title: "M01 first implementation"
    color: "#0B1020"

    FrameMonitor {
        id: frameMonitor

        window: root
    }

    Canvas {
        id: cloud

        property var clouds: [[], [], [], []]
        property real newestCaptureNs: 0
        property real paintStartNs: 0

        x: 0
        y: 0
        width: root.width - root.logWidth
        height: root.height

        onPaint: {
            const t0 = Stages.nowNs();
            paintStartNs = t0;
            const ctx = getContext("2d");
            ctx.clearRect(0, 0, width, height);
            const cx = width / 2;
            const cy = height / 2;
            const s = root.pxPerMetre;
            const colors = ["#38BDF8", "#34D399", "#FBBF24", "#F472B6"];
            for (let k = 0; k < clouds.length; ++k) {
                const pts = clouds[k];
                ctx.fillStyle = colors[k];
                for (let i = 0; i < pts.length; ++i) {
                    const p = pts[i];
                    ctx.fillRect(cx + p.x * s, cy - p.y * s, 1.5, 1.5);
                }
            }
            Stages.recordSince("canvas_paint_ms", t0);
        }
        onPainted: {
            Stages.recordSince("canvas_to_painted_ms", paintStartNs);
            frameMonitor.reportDisplayed(newestCaptureNs);
        }
    }

    Repeater {
        model: SensorBridge.tracks

        delegate: Item {
            id: track

            required property var modelData

            x: cloud.width / 2 + modelData.x * root.pxPerMetre
            y: cloud.height / 2 - modelData.y * root.pxPerMetre

            Rectangle {
                x: -width / 2
                y: -height / 2
                width: track.modelData.length * root.pxPerMetre
                height: track.modelData.width * root.pxPerMetre
                rotation: -track.modelData.heading * 180 / Math.PI
                color: "transparent"
                border.color: "#F8FAFC"
                border.width: 1
            }

            Text {
                x: 8
                y: -20
                color: "#CBD5E1"
                font.pixelSize: 10
                text: "#" + track.modelData.id + "  " + track.modelData.speed.toFixed(1) + " m/s"
            }
        }
    }

    ListView {
        id: log

        x: root.width - root.logWidth
        y: 0
        width: root.logWidth
        height: root.height
        clip: true
        model: SensorBridge.events

        onCountChanged: positionViewAtEnd()

        delegate: Text {
            required property int severity
            required property string source
            required property string message

            width: log.width
            elide: Text.ElideRight
            font.family: "monospace"
            font.pixelSize: 11
            color: severity === 0 ? "#94A3B8" : (severity === 1 ? "#FBBF24" : "#F87171")
            text: source + "  " + message
        }
    }

    Hud {
        x: 12
        y: 12
        monitor: frameMonitor
        heading: "M01  " + AppOptions.sensors + " x " + AppOptions.pointsPerFrame + " pts @ " + AppOptions.lidarRateHz + " Hz, " + AppOptions.objects + " objects, " + AppOptions.eventsPerSecond + " ev/s"
    }

    Connections {
        function onLidarUpdated(sensorId: int, points: var, captureNs: real): void {
            const t0 = Stages.nowNs();
            cloud.clouds[sensorId] = points;
            cloud.newestCaptureNs = Math.max(cloud.newestCaptureNs, captureNs);
            cloud.requestPaint();
            Stages.recordSince("lidar_handler_js_ms", t0);
        }

        target: SensorBridge
    }

    Component.onCompleted: {
        if (AppOptions.bench) {
            const extras = AppOptions.workloadMap();
            extras["app"] = "m01_first";
            frameMonitor.startBenchmark(AppOptions.warmupSec, AppOptions.durationSec, AppOptions.outPath, AppOptions.label, extras);
        }
    }
}
