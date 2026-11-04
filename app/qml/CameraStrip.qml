pragma ComponentBehavior: Bound

import QtQuick
import QtMultimedia
import Viewer

Row {
    id: strip

    // Four tiles across the width the strip is given.
    readonly property real tileWidth: Math.min(288, (width - 3 * spacing) / 4)

    spacing: 6

    Repeater {
        model: Cameras.enabled ? Cameras.feeds : []

        delegate: Rectangle {
            id: tile

            required property CameraFeed modelData

            width: strip.tileWidth
            height: strip.tileWidth * 9 / 16 + 20
            color: "#000000"
            border.color: "#334155"

            VideoOutput {
                id: video

                x: 0
                y: 0
                width: tile.width
                height: tile.width * 9 / 16
                visible: Cameras.path === "forward"
                fillMode: VideoOutput.PreserveAspectFit

                Component.onCompleted: tile.modelData.output = video.videoSink
            }

            CameraImageView {
                x: 0
                y: 0
                width: tile.width
                height: tile.width * 9 / 16
                visible: Cameras.path === "image"
                feed: Cameras.path === "image" ? tile.modelData : null
            }

            Text {
                x: 6
                y: tile.width * 9 / 16 + 2
                color: Math.abs(tile.modelData.skewMs) > 50 ? "#FBBF24" : "#CBD5E1"
                font.pixelSize: 11
                font.family: "monospace"
                text: "cam " + tile.modelData.index + "  " + tile.modelData.presentedFps.toFixed(0) + "/" + tile.modelData.decodedFps.toFixed(0) + " fps  skew " + (tile.modelData.skewMs >= 0 ? "+" : "") + tile.modelData.skewMs.toFixed(0) + " ms"
            }
        }
    }
}
