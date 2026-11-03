pragma ComponentBehavior: Bound

import QtQuick
import QtMultimedia
import Viewer

Row {
    id: strip

    spacing: 6

    Repeater {
        model: Cameras.enabled ? Cameras.feeds : []

        delegate: Rectangle {
            id: tile

            required property CameraFeed modelData

            width: 288
            height: 162 + 20
            color: "#000000"
            border.color: "#334155"

            VideoOutput {
                id: video

                x: 0
                y: 0
                width: tile.width
                height: 162
                visible: Cameras.path === "forward"
                fillMode: VideoOutput.PreserveAspectFit

                Component.onCompleted: tile.modelData.output = video.videoSink
            }

            CameraImageView {
                x: 0
                y: 0
                width: tile.width
                height: 162
                visible: Cameras.path === "image"
                feed: Cameras.path === "image" ? tile.modelData : null
            }

            Text {
                x: 6
                y: 164
                color: Math.abs(tile.modelData.skewMs) > 50 ? "#FBBF24" : "#CBD5E1"
                font.pixelSize: 11
                font.family: "monospace"
                text: "cam " + tile.modelData.index + "  " + tile.modelData.presentedFps.toFixed(0) + "/" + tile.modelData.decodedFps.toFixed(0) + " fps  skew " + (tile.modelData.skewMs >= 0 ? "+" : "") + tile.modelData.skewMs.toFixed(0) + " ms"
            }
        }
    }
}
