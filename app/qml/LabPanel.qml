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

    component SectionTitle: Label {
        font.pixelSize: 13
        font.bold: true
        color: "#E2E8F0"
        Layout.topMargin: 6
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 6

        Label {
            text: "Lab"
            font.pixelSize: 20
            font.bold: true
            color: "#F8FAFC"
        }

        SectionTitle {
            text: "Renderer"
        }
        ComboBox {
            id: rendererBox

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
        Loader {
            Layout.fillWidth: true
            Layout.preferredHeight: 300
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

        SectionTitle {
            text: "Events"
        }
        ListView {
            id: log

            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 60
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
    }
}
