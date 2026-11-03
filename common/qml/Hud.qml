import QtQuick

Rectangle {
    id: root

    required property FrameMonitor monitor
    property string heading: ""

    function fmt(v: real, digits: int): string {
        return v.toFixed(digits);
    }

    width: lines.implicitWidth + 28
    height: lines.implicitHeight + 12
    radius: 6
    color: "#CC0B0F14"
    border.color: "#334155"

    Column {
        id: lines

        anchors.centerIn: parent

        Line {
            visible: root.heading.length > 0
            text: root.heading
        }
        Line {
            text: `api      ${root.monitor.graphicsApi} @ ${root.fmt(root.monitor.refreshHz, 0)} Hz`
        }
        Line {
            text: `fps      ${root.fmt(root.monitor.fps, 1)}   gpu ${root.monitor.gpuMsMean > 0 ? root.fmt(root.monitor.gpuMsMean, 2) + " ms" : "n/a"}`
        }
        Line {
            text: `frame ms mean ${root.fmt(root.monitor.frameMsMean, 1)}  p99 ${root.fmt(root.monitor.frameMsP99, 1)}  max ${root.fmt(root.monitor.frameMsMax, 1)}`
        }
        Line {
            text: `slow     ${root.monitor.slowFrames} frames > 20 ms`
        }
        Line {
            text: `gui lag  p99 ${root.fmt(root.monitor.lagMsP99, 1)}  max ${root.fmt(root.monitor.lagMsMax, 1)} ms`
        }
        Line {
            text: `latency  mean ${root.fmt(root.monitor.latencyMsMean, 0)}  max ${root.fmt(root.monitor.latencyMsMax, 0)} ms`
        }
        Line {
            text: `cpu      gui ${root.fmt(root.monitor.guiCpuPercent, 0)}%  proc ${root.fmt(root.monitor.processCpuPercent, 0)}%`
        }
        Line {
            text: `rss      ${root.fmt(root.monitor.rssMb, 0)} MB`
        }
    }

    // Keeps the scene asking for a frame every vsync, so a long frame interval means a miss.
    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 6
        width: 8
        height: 8
        color: "#38BDF8"

        RotationAnimation on rotation {
            from: 0
            to: 360
            duration: 1000
            loops: Animation.Infinite
        }
    }

    component Line: Text {
        color: "#E2E8F0"
        font.family: "monospace"
        font.pixelSize: 12
    }
}
