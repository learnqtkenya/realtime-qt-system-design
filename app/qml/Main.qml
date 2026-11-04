pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQMLPro.Common
import Viewer

ApplicationWindow {
    id: root

    width: 1600
    height: 900
    visible: true
    title: "QtQMLPro Perception Viewer"
    color: "#0B1020"

    palette.window: "#0B1020"
    palette.windowText: "#E2E8F0"
    palette.base: "#111A2E"
    palette.alternateBase: "#0F1629"
    palette.text: "#E2E8F0"
    palette.button: "#1E293B"
    palette.buttonText: "#E2E8F0"
    palette.highlight: "#38BDF8"
    palette.highlightedText: "#0B1020"
    palette.light: "#475569"
    palette.midlight: "#334155"
    palette.mid: "#1E293B"
    palette.dark: "#0F172A"

    FrameMonitor {
        id: frameMonitor

        window: root
    }

    Connections {
        function onContentShown(): void {
            Lab.noteContentShown();
        }

        target: frameMonitor
    }

    Shortcut {
        sequence: "Space"
        onActivated: Timeline.togglePlay()
    }
    Shortcut {
        sequence: "Left"
        onActivated: Timeline.step(-1)
    }
    Shortcut {
        sequence: "Right"
        onActivated: Timeline.step(1)
    }
    Shortcut {
        sequence: "L"
        onActivated: Timeline.goLive()
    }
    Shortcut {
        sequence: "R"
        onActivated: Timeline.toggleRecording()
    }
    Shortcut {
        sequence: "F2"
        onActivated: Workspace.dataPanel = !Workspace.dataPanel
    }
    Shortcut {
        sequence: "F3"
        onActivated: Workspace.labPanel = !Workspace.labPanel
    }

    DataPanel {
        id: dataPanel

        x: 0
        y: 0
        width: 360
        height: root.height
        visible: Workspace.dataPanel
    }

    Viewport {
        x: dataPanel.visible ? dataPanel.width : 0
        y: 0
        width: root.width - (panel.visible ? panel.width : 0) - x
        height: root.height
        monitor: frameMonitor
    }

    LabPanel {
        id: panel

        x: root.width - width
        y: 0
        width: 380
        height: root.height
        visible: Workspace.labPanel
        monitor: frameMonitor
    }

    Component.onCompleted: {
        if (AppOptions.bench) {
            const extras = AppOptions.workloadMap();
            extras["app"] = "viewer";
            extras["renderer"] = Lab.renderer;
            frameMonitor.startBenchmark(AppOptions.warmupSec, AppOptions.durationSec, AppOptions.outPath, AppOptions.label, extras);
        }
    }
}
