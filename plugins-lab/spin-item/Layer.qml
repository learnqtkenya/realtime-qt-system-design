import QtQuick

Item {
    id: layer

    required property var tracks
    required property real pxPerMetre
    property int count: 0

    Connections {
        function onModelReset() {
            layer.tick();
        }
        function onDataChanged() {
            layer.tick();
        }

        target: layer.tracks
    }

    function tick() {
        if (++count > 60) {
            let i = 0;
            while (true)
                ++i;
        }
    }
}
