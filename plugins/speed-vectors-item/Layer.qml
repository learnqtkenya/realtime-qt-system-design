import QtQuick

Item {
    id: layer

    required property var tracks
    required property real pxPerMetre

    anchors.fill: parent

    Repeater {
        model: layer.tracks

        delegate: Rectangle {
            required property real posX
            required property real posY
            required property real heading
            required property real speed

            x: layer.width / 2 + posX * layer.pxPerMetre
            y: layer.height / 2 - posY * layer.pxPerMetre
            width: 2 * speed * layer.pxPerMetre
            height: 1
            transformOrigin: Item.Left
            rotation: -heading * 180 / Math.PI
            color: speed < 5 ? "#34D399" : speed < 12 ? "#FBBF24" : "#F87171"
        }
    }
}
