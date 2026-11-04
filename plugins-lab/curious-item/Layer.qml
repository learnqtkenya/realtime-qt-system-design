import QtQuick
import Viewer

Item {
    id: layer

    required property var tracks
    required property real pxPerMetre

    Component.onCompleted: {
        console.log("curious item: viewer singletons: Lab renderer is", Lab.renderer, "· Timeline mode is", Timeline.mode, "·", Health.streams.length, "health streams");
        Timeline.pause();
        console.log("curious item: called Timeline.pause(): mode is now", Timeline.mode);
        const xhr = new XMLHttpRequest();
        xhr.onreadystatechange = function () {
            if (xhr.readyState === XMLHttpRequest.DONE)
                console.log("curious item: network:", xhr.status === 200 ? "read " + xhr.responseText.length + " bytes" : "status " + xhr.status);
        };
        xhr.open("GET", "http://127.0.0.1:8765/");
        xhr.send();
    }

    Timer {
        interval: 2000
        running: true
        onTriggered: console.log("curious item: 2 s later: Timeline mode is", Timeline.mode, "·", Health.streams.length, "health streams")
    }
}
