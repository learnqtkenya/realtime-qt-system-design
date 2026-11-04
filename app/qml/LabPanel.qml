pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQMLPro.Common
import Viewer

Rectangle {
    id: panel

    required property FrameMonitor monitor

    // Log scale: 0 -> 1k points, 1 -> 1M points.
    function toSlider(points: int): real {
        return Math.log(Math.max(points, 1000) / 1000) / Math.log(1000);
    }
    function fromSlider(v: real): int {
        const raw = 1000 * Math.pow(1000, v);
        const magnitude = Math.pow(10, Math.floor(Math.log10(raw)) - 1);
        return Math.round(raw / magnitude) * magnitude;
    }

    color: "#0F1629"

    component Caption: Label {
        font.pixelSize: 11
        color: "#94A3B8"
    }

    component LabSlider: Slider {
        id: control

        Layout.fillWidth: true
        background: Rectangle {
            x: control.leftPadding
            y: control.topPadding + control.availableHeight / 2 - height / 2
            width: control.availableWidth
            height: 4
            radius: 2
            color: "#334155"

            Rectangle {
                width: control.visualPosition * parent.width
                height: parent.height
                radius: 2
                color: "#38BDF8"
            }
        }
        handle: Rectangle {
            x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
            y: control.topPadding + control.availableHeight / 2 - height / 2
            width: 16
            height: 16
            radius: 8
            color: control.pressed ? "#7DD3FC" : "#E2E8F0"
            border.color: "#0B1020"
        }
    }

    component ToggleButton: Button {
        id: toggle

        Layout.fillWidth: true
        checkable: true
        contentItem: Label {
            text: toggle.text
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            color: toggle.checked ? "#0B1020" : "#E2E8F0"
            font.bold: toggle.checked
        }
        background: Rectangle {
            implicitHeight: 30
            radius: 4
            color: toggle.checked ? "#38BDF8" : (toggle.hovered ? "#334155" : "#1E293B")
        }
    }

    component LabCheck: CheckBox {
        id: check

        indicator: Rectangle {
            x: check.leftPadding
            y: (check.height - height) / 2
            implicitWidth: 16
            implicitHeight: 16
            radius: 3
            color: check.checked ? "#38BDF8" : "transparent"
            border.color: check.checked ? "#38BDF8" : "#94A3B8"
            opacity: check.enabled ? 1 : 0.4
        }
        contentItem: Label {
            leftPadding: check.indicator.width + 8
            text: check.text
            font.pixelSize: 12
            color: check.enabled ? "#E2E8F0" : "#64748B"
            verticalAlignment: Text.AlignVCenter
        }
    }

    component SectionTitle: Label {
        font.pixelSize: 13
        font.bold: true
        color: "#E2E8F0"
        Layout.topMargin: 6
    }

    Flickable {
        id: flick

        anchors.fill: parent
        anchors.margins: 12
        contentHeight: column.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: column

            width: flick.width
            spacing: 4

            Label {
                text: "Lab"
                font.pixelSize: 20
                font.bold: true
                color: "#F8FAFC"
            }

            SectionTitle {
                text: "View"
            }
            RowLayout {
                Layout.fillWidth: true

                ToggleButton {
                    text: "Top-down"
                    checked: Lab.viewMode === "top"
                    onClicked: Lab.viewMode = "top"
                }
                ToggleButton {
                    text: "3D orbit"
                    checked: Lab.viewMode === "orbit"
                    onClicked: Lab.viewMode = "orbit"
                }
            }
            LabCheck {
                visible: Lab.viewMode === "orbit"
                text: "Level of detail (thin points when far)"
                checked: Lab.lodEnabled
                onToggled: Lab.lodEnabled = checked
            }

            SectionTitle {
                text: "Cameras"
            }
            LabCheck {
                text: "Show four camera panels"
                enabled: Cameras.available
                checked: Cameras.enabled
                onToggled: Cameras.enabled = checked
            }
            RowLayout {
                Layout.fillWidth: true
                visible: Cameras.enabled

                ToggleButton {
                    text: "Forward frames"
                    checked: Cameras.path === "forward"
                    onClicked: Cameras.path = "forward"
                }
                ToggleButton {
                    text: "Convert to image"
                    checked: Cameras.path === "image"
                    onClicked: Cameras.path = "image"
                }
            }
            RowLayout {
                Layout.fillWidth: true
                visible: Cameras.enabled

                ToggleButton {
                    text: "Latest frame"
                    checked: Cameras.sync === "latest"
                    onClicked: Cameras.sync = "latest"
                }
                ToggleButton {
                    text: "Aligned to lidar"
                    checked: Cameras.sync === "aligned"
                    onClicked: Cameras.sync = "aligned"
                }
            }
            Caption {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: !Cameras.available ? "No test videos: run scripts/make_videos.sh" : "Decoder: " + Cameras.decoder + (Cameras.enabled && Cameras.frameInfo.length > 0 ? " · frames: " + Cameras.frameInfo : "")
            }

            SectionTitle {
                text: "Time"
            }
            Caption {
                text: "Presentation delay: " + Timeline.delayMs + " ms (every stream shown at now − delay)"
            }
            LabSlider {
                from: 0
                to: 400
                stepSize: 10
                snapMode: Slider.SnapAlways
                value: Timeline.delayMs
                onMoved: Timeline.delayMs = value
            }
            Caption {
                text: "Simulated lidar processing: " + Lab.lidarDelayMs + " ms"
            }
            LabSlider {
                from: 0
                to: 300
                stepSize: 10
                snapMode: Slider.SnapAlways
                value: Lab.lidarDelayMs
                onMoved: Lab.lidarDelayMs = value
            }
            Caption {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: "History in memory: " + Timeline.historyMb.toFixed(0) + " of " + Timeline.historyBudgetMb.toFixed(0) + " MB (" + Timeline.historyShape + "), " + Timeline.historySeconds.toFixed(1) + " s"
            }
            Caption {
                Layout.fillWidth: true
                visible: Timeline.status.length > 0
                wrapMode: Text.WordWrap
                color: "#C4B5FD"
                text: Timeline.status
            }
            Repeater {
                model: Timeline.recordings.slice(0, 4)

                delegate: RowLayout {
                    id: recordingRow

                    required property var modelData

                    Layout.fillWidth: true

                    Caption {
                        Layout.fillWidth: true
                        elide: Text.ElideMiddle
                        text: recordingRow.modelData.name + "  " + recordingRow.modelData.mb.toFixed(0) + " MB"
                    }
                    Button {
                        text: "Open"
                        focusPolicy: Qt.NoFocus
                        onClicked: Timeline.openRecording(recordingRow.modelData.path)
                    }
                }
            }

            SectionTitle {
                visible: Network.role !== "off"
                text: Network.role === "server" ? "Viewers connected" : "Connected to"
            }
            Caption {
                visible: Network.role !== "off"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: Network.lines.length > 0 ? Network.lines.join("\n") : (Network.role === "server" ? "none yet" : "")
            }

            SectionTitle {
                text: "Sensor health"
            }
            Repeater {
                model: Health.streams

                delegate: RowLayout {
                    id: healthRow

                    required property var modelData
                    readonly property string status: modelData.status

                    Layout.fillWidth: true
                    spacing: 6

                    Rectangle {
                        Layout.preferredWidth: 9
                        Layout.preferredHeight: 9
                        radius: 4.5
                        color: {
                            switch (healthRow.status) {
                            case "ok":
                                return "#34D399";
                            case "degraded":
                            case "late":
                                return "#FBBF24";
                            case "corrupt":
                            case "stale":
                                return "#F87171";
                            default:
                                return "#475569";
                            }
                        }
                    }
                    Caption {
                        Layout.preferredWidth: 70
                        color: "#E2E8F0"
                        text: healthRow.modelData.name
                    }
                    Caption {
                        Layout.preferredWidth: 56
                        text: healthRow.status
                    }
                    Caption {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: healthRow.status === "off" ? "" : healthRow.modelData.hz.toFixed(0) + " Hz · " + healthRow.modelData.ageMs.toFixed(0) + " ms old" + (healthRow.modelData.dropPct > 0 ? " · " + healthRow.modelData.dropPct.toFixed(0) + "% lost" : "") + (healthRow.modelData.rejected > 0 ? " · " + healthRow.modelData.rejected + " rejected" : "") + (healthRow.modelData.reordered > 0 ? " · " + healthRow.modelData.reordered + " out of order" : "")
                    }
                }
            }

            SectionTitle {
                text: "Layers"
            }
            Caption {
                text: "Plugins found at start · viewer API " + Plugins.api
            }
            Repeater {
                model: Plugins.layers

                delegate: ColumnLayout {
                    id: layerRow

                    required property var modelData
                    readonly property string status: modelData.status

                    Layout.fillWidth: true
                    spacing: 0

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        LabCheck {
                            Layout.fillWidth: true
                            text: layerRow.modelData.name
                            checked: layerRow.modelData.enabled
                            enabled: layerRow.status !== "refused"
                            onToggled: Plugins.setEnabled(layerRow.modelData.id, checked)
                        }
                        Rectangle {
                            Layout.preferredWidth: 9
                            Layout.preferredHeight: 9
                            radius: 4.5
                            color: {
                                switch (layerRow.status) {
                                case "ok":
                                    return "#34D399";
                                case "slow":
                                case "starting":
                                    return "#FBBF24";
                                case "off":
                                    return "#475569";
                                default:
                                    return "#F87171";
                                }
                            }
                        }
                        Caption {
                            text: layerRow.modelData.kind
                        }
                    }
                    Caption {
                        visible: text !== ""
                        Layout.fillWidth: true
                        Layout.leftMargin: 24
                        wrapMode: Text.WordWrap
                        text: {
                            const d = layerRow.modelData;
                            if (d.status === "off")
                                return "";
                            const parts = [d.status + (d.detail ? ": " + d.detail : "")];
                            if (d.cost)
                                parts.push(d.cost);
                            return parts.join(" · ");
                        }
                    }
                }
            }

            SectionTitle {
                text: "Diagnostics"
            }
            Caption {
                id: diagnosticsText

                readonly property var d: Diagnostics.snapshot
                readonly property var p: d.process ?? {}
                readonly property var st: d.startup ?? {}

                function clock(s: real): string {
                    const h = Math.floor(s / 3600);
                    const m = Math.floor(s / 60) % 60;
                    return h + ":" + String(m).padStart(2, "0") + ":" + String(Math.floor(s) % 60).padStart(2, "0");
                }

                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: (Application.version + " · " + (d.frames?.graphicsApi ?? "") + "\n") + "up " + clock(p.uptimeS ?? 0) + " · RSS " + (p.rssMb ?? 0).toFixed(0) + " MB · heap " + (p.heapInUseMb ?? 0).toFixed(0) + " MB · " + (p.threads ?? 0) + " threads · " + (p.openFiles ?? 0) + " files\n" + "first frame " + (st.firstFrameMs ?? 0).toFixed(0) + " ms after start (main at " + (st.mainMs ?? 0).toFixed(0) + " ms)\n" + "layout: " + Workspace.status + "  (F2, F3: panels)" + (Diagnostics.metricsUrl ? "\nmetrics: " + Diagnostics.metricsUrl : "")
            }
            RowLayout {
                Layout.fillWidth: true

                Button {
                    text: "Save diagnostic bundle"
                    onClicked: bundleSaved.text = "saved " + Diagnostics.saveBundle()
                }
            }
            Caption {
                id: bundleSaved

                visible: text !== ""
                Layout.fillWidth: true
                wrapMode: Text.WrapAnywhere
            }

            SectionTitle {
                visible: Network.role !== "client"
                text: "Faults"
            }
            RowLayout {
                visible: Network.role !== "client"
                Layout.fillWidth: true

                ComboBox {
                    id: faultStream

                    readonly property var streams: [0, 1, 2, 3, 4, 10]

                    Layout.fillWidth: true
                    focusPolicy: Qt.NoFocus
                    model: ["lidar 0", "lidar 1", "lidar 2", "lidar 3", "tracks", "GUI thread"]
                }
                Caption {
                    text: Lab.faults[String(faultStream.streams[faultStream.currentIndex])] ?? "no fault"
                }
            }
            Flow {
                visible: Network.role !== "client"
                Layout.fillWidth: true
                spacing: 4

                Repeater {
                    model: faultStream.currentIndex === 5 ? [["block 300 ms", "gui300"], ["clear", "clear"]] : [["drop 50%", "drop50"], ["delay 300 ms", "delay300"], ["jitter ±100", "jitter100"], ["corrupt", "corrupt"], ["stall", "stall"], ["clear", "clear"]]

                    delegate: Button {
                        required property var modelData

                        text: modelData[0]
                        focusPolicy: Qt.NoFocus
                        font.pixelSize: 11
                        onClicked: Lab.injectFault(faultStream.streams[faultStream.currentIndex], modelData[1])
                    }
                }
            }

            SectionTitle {
                text: Lab.viewMode === "orbit" ? "Renderer (3D orbit always uses Quick 3D)" : "Renderer"
            }
            ComboBox {
                id: rendererBox

                enabled: Lab.viewMode === "top"
                Layout.fillWidth: true
                model: Lab.renderers
                textRole: "name"
                valueRole: "id"
                currentIndex: Lab.renderers.findIndex(r => r.id === Lab.renderer)
                onActivated: index => Lab.renderer = Lab.renderers[index].id
            }
            Caption {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: rendererBox.currentIndex >= 0 ? Lab.renderers[rendererBox.currentIndex].detail : ""
            }

            SectionTitle {
                text: "Load"
            }
            Caption {
                text: "Points per sensor: " + Lab.pointsPerSensor.toLocaleString(Qt.locale("en_US"), "f", 0) + "  (max " + Lab.maxPoints.toLocaleString(Qt.locale("en_US"), "f", 0) + " at this rate)"
            }
            LabSlider {
                from: 0
                to: panel.toSlider(Lab.maxPoints)
                value: panel.toSlider(Lab.pointsPerSensor)
                onMoved: Lab.pointsPerSensor = panel.fromSlider(value)
            }
            Caption {
                text: "Sensor rate: " + Lab.rateHz + " Hz"
            }
            LabSlider {
                from: 5
                to: 30
                stepSize: 5
                snapMode: Slider.SnapAlways
                value: Lab.rateHz
                onMoved: Lab.rateHz = value
            }
            Caption {
                text: "Tracked objects: " + Lab.objects
            }
            LabSlider {
                from: 0
                to: 800
                stepSize: 10
                snapMode: Slider.SnapAlways
                value: Lab.objects
                onMoved: Lab.objects = value
            }
            Caption {
                text: "Events per second: " + Lab.eventsPerSecond
            }
            LabSlider {
                from: 0
                to: 2000
                stepSize: 50
                snapMode: Slider.SnapAlways
                value: Lab.eventsPerSecond
                onMoved: Lab.eventsPerSecond = value
            }
            Button {
                text: "Reset to nominal"
                onClicked: Lab.resetToNominal()
            }

            // Same height with charts off, so the event log (whose cost grows with visible rows) stays the same size.
            SectionTitle {
                text: "Selected object"
            }
            Caption {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Selection.trackId >= 0 ? "#E2E8F0" : "#94A3B8"
                text: {
                    const i = Selection.info;
                    if (Selection.trackId < 0 || i.id === undefined)
                        return "Click an object in the view to inspect it.";
                    return "#" + i.id + "  " + i.cls + "\n" + i.speed.toFixed(1) + " m/s, heading " + i.heading.toFixed(0) + "°\n" + "at (" + i.x.toFixed(1) + ", " + i.y.toFixed(1) + ") m, " + i.distance.toFixed(1) + " m away\n" + i.length.toFixed(1) + " × " + i.width.toFixed(1) + " × " + i.height.toFixed(1) + " m";
                }
            }

            Loader {
                Layout.fillWidth: true
                Layout.preferredHeight: 340
                active: Lab.chartsEnabled

                sourceComponent: ColumnLayout {
                    spacing: 6

                    SectionTitle {
                        text: "Live (last 60 s)"
                    }
                    MetricChart {
                        Layout.fillWidth: true
                        title: "Frame time p99"
                        values: panel.monitor.historyFrameP99
                        threshold: 20
                    }
                    MetricChart {
                        Layout.fillWidth: true
                        title: "GUI thread lag p99"
                        values: panel.monitor.historyLagP99
                        threshold: 4
                        color: "#34D399"
                    }
                    MetricChart {
                        Layout.fillWidth: true
                        title: "Sensor-to-pixel latency"
                        values: panel.monitor.historyLatency
                        threshold: 100
                        decimals: 0
                        color: "#FBBF24"
                    }
                    MetricChart {
                        Layout.fillWidth: true
                        title: "Input to photon"
                        values: panel.monitor.historyInput
                        threshold: 50
                        decimals: 0
                        color: "#7DD3FC"
                    }
                    MetricChart {
                        Layout.fillWidth: true
                        title: "Camera minus lidar time on screen"
                        values: panel.monitor.historySkew
                        decimals: 0
                        color: "#FB923C"
                    }
                    MetricChart {
                        Layout.fillWidth: true
                        title: "Process CPU"
                        values: panel.monitor.historyCpu
                        unit: "%"
                        decimals: 0
                        color: "#F472B6"
                    }
                    MetricChart {
                        Layout.fillWidth: true
                        title: "GPU time per frame"
                        values: panel.monitor.historyGpu
                        decimals: 2
                        color: "#A78BFA"
                    }
                }
            }
        }
    }
}
