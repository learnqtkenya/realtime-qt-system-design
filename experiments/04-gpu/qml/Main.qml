pragma ComponentBehavior: Bound

import QtQuick
import QtQMLPro.Common
import M04Gpu

Window {
    id: root

    readonly property real pxPerMetre: 7
    readonly property int logWidth: 360
    readonly property string variant: AppOptions.variant.length > 0 ? AppOptions.variant : "sg"

    width: 1400
    height: 800
    visible: true
    title: "M04 gpu (" + root.variant + ")"
    color: "#0B1020"

    FrameMonitor {
        id: frameMonitor

        window: root
    }

    Item {
        id: cloud

        x: 0
        y: 0
        width: root.width - root.logWidth
        height: root.height

        SgPointCloudView {
            anchors.fill: parent
            visible: root.variant === "sg"
            mailbox: root.variant === "sg" ? Scene.mailbox : null
            pxPerMetre: root.pxPerMetre
        }

        RhiPointCloudView {
            readonly property bool active: root.variant === "rhi" || root.variant === "rhi-ring"

            anchors.fill: parent
            visible: active
            mailbox: active ? Scene.mailbox : null
            bufferRing: root.variant === "rhi-ring" ? 3 : 1
            pxPerMetre: root.pxPerMetre
        }

        // Loaded by URL so the other variants never import Quick 3D.
        Loader {
            anchors.fill: parent
            Component.onCompleted: {
                if (root.variant === "quick3d")
                    setSource("Quick3DPoints.qml", {
                        pxPerMetre: root.pxPerMetre
                    });
            }
        }
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
        heading: "M04  " + AppOptions.sensors + " x " + AppOptions.pointsPerFrame + " pts @ " + AppOptions.lidarRateHz + " Hz, " + AppOptions.objects + " objects, " + AppOptions.eventsPerSecond + " ev/s, " + root.variant
    }

    Component.onCompleted: {
        if (AppOptions.bench) {
            const extras = AppOptions.workloadMap();
            extras["app"] = "m04_gpu";
            frameMonitor.startBenchmark(AppOptions.warmupSec, AppOptions.durationSec, AppOptions.outPath, AppOptions.label, extras);
        }
    }
}
