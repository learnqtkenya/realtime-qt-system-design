pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Viewer

Rectangle {
    id: bar

    readonly property bool live: Timeline.mode === "live" && !Timeline.replayingFile
    readonly property real fraction: Timeline.length > 0 ? Timeline.position / Timeline.length : 1

    function clock(s: real): string {
        const m = Math.floor(s / 60);
        const r = s - m * 60;
        return m + ":" + (r < 10 ? "0" : "") + r.toFixed(1);
    }

    implicitHeight: 40
    color: "#0F1629"

    component BarButton: Button {
        id: control

        property color accent: "#E2E8F0"
        property bool lit: false

        focusPolicy: Qt.NoFocus
        Layout.preferredHeight: 28
        contentItem: Label {
            text: control.text
            color: control.lit ? "#0B1020" : control.accent
            font.pixelSize: 12
            font.bold: control.lit
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            implicitWidth: 32
            radius: 4
            color: control.lit ? control.accent : (control.hovered ? "#334155" : "#1E293B")
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        spacing: 6

        BarButton {
            text: Timeline.replayingFile ? "Back to live" : "● LIVE"
            accent: "#F87171"
            lit: bar.live
            onClicked: Timeline.goLive()
        }
        BarButton {
            text: "◀|"
            onClicked: Timeline.step(-1)
        }
        BarButton {
            text: Timeline.mode === "paused" ? "▶" : "❚❚"
            onClicked: Timeline.togglePlay()
        }
        BarButton {
            text: "|▶"
            onClicked: Timeline.step(1)
        }

        Item {
            id: scrub

            objectName: "scrubBar"
            Layout.fillWidth: true
            Layout.fillHeight: true

            Rectangle {
                id: track

                x: 0
                y: (parent.height - height) / 2
                width: parent.width
                height: 6
                radius: 3
                color: "#334155"
            }
            Rectangle {
                x: 0
                y: track.y
                width: bar.fraction * track.width
                height: track.height
                radius: 3
                color: Timeline.replayingFile ? "#A78BFA" : "#38BDF8"
            }
            Rectangle {
                x: bar.fraction * track.width - width / 2
                y: (parent.height - height) / 2
                width: 4
                height: 20
                radius: 2
                color: "#F8FAFC"
            }
            MouseArea {
                function seekTo(x: real): void {
                    Timeline.seek(Math.max(0, Math.min(1, x / width)) * Timeline.length);
                }

                anchors.fill: parent
                preventStealing: true
                onPressed: mouse => seekTo(mouse.x)
                onPositionChanged: mouse => {
                    if (pressed)
                        seekTo(mouse.x);
                }
            }
        }

        Label {
            Layout.preferredWidth: 150
            font.family: "monospace"
            font.pixelSize: 12
            color: bar.live ? "#F87171" : "#E2E8F0"
            text: {
                if (Timeline.replayingFile)
                    return bar.clock(Timeline.position) + " / " + bar.clock(Timeline.length);
                if (bar.live)
                    return "live" + (Timeline.delayMs > 0 ? " −" + Timeline.delayMs + " ms" : "") + " · " + Timeline.length.toFixed(0) + " s kept";
                return "−" + Timeline.behindLive.toFixed(2) + " s";
            }
        }
        ComboBox {
            id: rateBox

            readonly property var rates: [0.25, 0.5, 1, 2, 4]

            focusPolicy: Qt.NoFocus
            Layout.preferredWidth: 72
            Layout.preferredHeight: 28
            model: rates.map(r => r + "×")
            currentIndex: rates.indexOf(Timeline.rate)
            onActivated: index => Timeline.rate = rates[index]
        }
        BarButton {
            text: Timeline.recording ? "■ REC " + bar.clock(Timeline.recordSeconds) + " · " + Timeline.recordMb.toFixed(0) + " MB · " + Timeline.recordMbps.toFixed(1) + " MB/s" + (Timeline.recordDropped > 0 ? " · " + Timeline.recordDropped + " dropped" : "") : "● REC"
            accent: "#F87171"
            lit: Timeline.recording
            onClicked: Timeline.toggleRecording()
        }
    }
}
