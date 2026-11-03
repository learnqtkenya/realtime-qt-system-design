pragma ComponentBehavior: Bound

import QtQuick
import QtQMLPro.Common

Window {
    id: root

    width: 1000
    height: 640
    visible: true
    title: "M00 harness check"
    color: "#0F172A"

    FrameMonitor {
        id: frameMonitor

        window: root
    }

    Repeater {
        model: AppOptions.objects

        Rectangle {
            id: box

            required property int index

            x: (index * 37) % (root.width - 40)
            y: (index * 53) % (root.height - 40)
            width: 24
            height: 24
            radius: 4
            color: Qt.hsla((index * 0.07) % 1.0, 0.7, 0.55, 1.0)

            RotationAnimation on rotation {
                from: 0
                to: 360
                duration: 2000 + (box.index % 7) * 300
                loops: Animation.Infinite
            }
        }
    }

    Hud {
        x: 12
        y: 12
        monitor: frameMonitor
        heading: "M00 harness check, " + AppOptions.objects + " animated boxes"
    }

    Component.onCompleted: {
        if (AppOptions.bench)
            frameMonitor.startBenchmark(AppOptions.warmupSec, AppOptions.durationSec, AppOptions.outPath, AppOptions.label, AppOptions.workloadMap());
    }
}
