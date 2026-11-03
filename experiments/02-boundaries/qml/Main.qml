pragma ComponentBehavior: Bound

import QtQuick
import QtQMLPro.Common
import M02Boundaries

Window {
    id: root

    readonly property real pxPerMetre: 7
    readonly property int logWidth: 360

    width: 1400
    height: 800
    visible: true
    title: "M02 boundaries"
    color: "#0B1020"

    FrameMonitor {
        id: frameMonitor

        window: root
    }

    PointCloudView {
        id: cloud

        x: 0
        y: 0
        width: root.width - root.logWidth
        height: root.height
        store: Scene.store
        pxPerMetre: root.pxPerMetre
    }

    Repeater {
        model: Scene.tracks

        delegate: Item {
            id: track

            required property int trackId
            required property real posX
            required property real posY
            required property real heading
            required property real speed
            required property real boxLength
            required property real boxWidth

            x: cloud.width / 2 + posX * root.pxPerMetre
            y: cloud.height / 2 - posY * root.pxPerMetre

            Rectangle {
                x: -width / 2
                y: -height / 2
                width: track.boxLength * root.pxPerMetre
                height: track.boxWidth * root.pxPerMetre
                rotation: -track.heading * 180 / Math.PI
                color: "transparent"
                border.color: "#F8FAFC"
                border.width: 1
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

    ListView {
        id: log

        x: root.width - root.logWidth
        y: 0
        width: root.logWidth
        height: root.height
        clip: true
        model: Scene.events

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
        heading: "M02  " + AppOptions.sensors + " x " + AppOptions.pointsPerFrame + " pts @ " + AppOptions.lidarRateHz + " Hz, " + AppOptions.objects + " objects, " + AppOptions.eventsPerSecond + " ev/s"
    }

    Component.onCompleted: {
        if (AppOptions.bench) {
            const extras = AppOptions.workloadMap();
            extras["app"] = "m02_boundaries";
            frameMonitor.startBenchmark(AppOptions.warmupSec, AppOptions.durationSec, AppOptions.outPath, AppOptions.label, extras);
        }
    }
}
