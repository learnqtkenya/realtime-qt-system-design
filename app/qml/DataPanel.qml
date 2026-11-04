pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQMLPro.Common
import Viewer

Rectangle {
    id: panel

    readonly property var classNames: ["car", "truck", "cyclist", "pedestrian"]
    // An EventFilterModel or an EventFilterProxy: the same properties.
    readonly property var log: Scene.eventLog

    color: "#0F1629"

    component Caption: Label {
        font.pixelSize: 11
        color: "#94A3B8"
    }

    component Chip: Button {
        id: chip

        focusPolicy: Qt.NoFocus
        checkable: true
        Layout.preferredHeight: 24
        contentItem: Label {
            text: chip.text
            font.pixelSize: 11
            color: chip.checked ? "#0B1020" : "#CBD5E1"
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            implicitWidth: 40
            radius: 4
            color: chip.checked ? "#38BDF8" : "#1E293B"
        }
    }

    component HeaderCell: Label {
        id: header

        required property string key
        required property string title
        property bool numeric: true

        font.pixelSize: 11
        font.bold: Scene.objectTable.sortKey === key
        color: "#E2E8F0"
        horizontalAlignment: numeric ? Text.AlignRight : Text.AlignLeft
        text: title + (Scene.objectTable.sortKey === key ? (Scene.objectTable.descending ? " ▼" : " ▲") : "")

        TapHandler {
            onTapped: {
                if (Scene.objectTable.sortKey === header.key)
                    Scene.objectTable.descending = !Scene.objectTable.descending;
                else
                    Scene.objectTable.sortKey = header.key;
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 4

        RowLayout {
            Layout.fillWidth: true

            Label {
                text: "Objects"
                font.pixelSize: 14
                font.bold: true
                color: "#F8FAFC"
            }
            Item {
                Layout.fillWidth: true
            }
            Caption {
                text: Scene.objectTable.count === Scene.objectTable.total ? Scene.objectTable.total + " objects" : Scene.objectTable.count + " of " + Scene.objectTable.total + " objects"
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 4

            Chip {
                text: "all"
                checked: Scene.objectTable.classFilter < 0
                onClicked: Scene.objectTable.classFilter = -1
            }
            Repeater {
                model: panel.classNames

                delegate: Chip {
                    required property string modelData
                    required property int index

                    text: modelData
                    checked: Scene.objectTable.classFilter === index
                    onClicked: Scene.objectTable.classFilter = index
                }
            }
        }

        // Column widths, shared by the header and the rows.
        Item {
            id: columns

            readonly property real idWidth: 56
            readonly property real classWidth: 80
            readonly property real speedWidth: 64
            readonly property real distanceWidth: 64
            readonly property real headingWidth: width - idWidth - classWidth - speedWidth - distanceWidth

            Layout.fillWidth: true
            Layout.preferredHeight: 18

            Row {
                anchors.fill: parent

                HeaderCell {
                    width: columns.idWidth
                    key: "trackId"
                    numeric: false
                    title: "id"
                }
                HeaderCell {
                    width: columns.classWidth
                    key: "trackClass"
                    numeric: false
                    title: "class"
                }
                HeaderCell {
                    width: columns.speedWidth
                    key: "speed"
                    title: "m/s"
                }
                HeaderCell {
                    width: columns.distanceWidth
                    key: "distance"
                    title: "dist m"
                }
                HeaderCell {
                    width: columns.headingWidth
                    key: "heading"
                    title: "heading °"
                }
            }
        }

        ListView {
            id: table

            objectName: "objectTable"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: 3
            clip: true
            model: Scene.objectTable
            boundsBehavior: Flickable.StopAtBounds
            reuseItems: AppOptions.flag("reuseItems", true)
            ScrollBar.vertical: ScrollBar {}

            delegate: Rectangle {
                id: row

                required property int index
                required property int trackId
                required property int trackClass
                required property real speed
                required property real distance
                required property real heading
                readonly property bool selected: trackId === Selection.trackId

                width: table.width
                height: 20
                color: selected ? "#4D3A12" : (index % 2 ? "#111A2E" : "transparent")

                Component.onCompleted: Stages.gauge("table_row_created", 1)
                ListView.onReused: Stages.gauge("table_row_reused", 1)

                Row {
                    anchors.fill: parent

                    Text {
                        width: columns.idWidth
                        text: "#" + row.trackId
                        color: row.selected ? "#FBBF24" : "#E2E8F0"
                        font.pixelSize: 11
                        font.family: "monospace"
                    }
                    Text {
                        width: columns.classWidth
                        text: panel.classNames[row.trackClass]
                        color: "#CBD5E1"
                        font.pixelSize: 11
                    }
                    Text {
                        width: columns.speedWidth
                        horizontalAlignment: Text.AlignRight
                        text: row.speed.toFixed(1)
                        color: "#CBD5E1"
                        font.pixelSize: 11
                        font.family: "monospace"
                    }
                    Text {
                        width: columns.distanceWidth
                        horizontalAlignment: Text.AlignRight
                        text: row.distance.toFixed(1)
                        color: "#CBD5E1"
                        font.pixelSize: 11
                        font.family: "monospace"
                    }
                    Text {
                        width: columns.headingWidth
                        horizontalAlignment: Text.AlignRight
                        text: ((row.heading * 180 / Math.PI) % 360 + 360) % 360 | 0
                        color: "#CBD5E1"
                        font.pixelSize: 11
                        font.family: "monospace"
                    }
                }
                TapHandler {
                    onTapped: Selection.select(row.trackId)
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 6

            Label {
                text: "Events"
                font.pixelSize: 14
                font.bold: true
                color: "#F8FAFC"
            }
            Item {
                Layout.fillWidth: true
            }
            Caption {
                text: panel.log.count === panel.log.total ? panel.log.total.toLocaleString(Qt.locale("en_US"), "f", 0) + " kept" : panel.log.count.toLocaleString(Qt.locale("en_US"), "f", 0) + " of " + panel.log.total.toLocaleString(Qt.locale("en_US"), "f", 0) + " kept"
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 4

            Repeater {
                model: ["info", "warning", "error"]

                delegate: Chip {
                    required property string modelData
                    required property int index

                    text: modelData
                    checked: panel.log.severities & (1 << index)
                    onClicked: panel.log.severities ^= (1 << index)
                }
            }
            TextField {
                id: search

                Layout.fillWidth: true
                Layout.preferredHeight: 26
                placeholderText: "search"
                font.pixelSize: 11
                onTextChanged: panel.log.text = text
            }
            Chip {
                id: follow

                text: "follow"
                checked: AppOptions.flag("follow", true)
            }
        }
        ListView {
            id: log

            objectName: "eventLog"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: 2
            clip: true
            model: panel.log
            boundsBehavior: Flickable.StopAtBounds
            reuseItems: AppOptions.flag("reuseItems", true)
            ScrollBar.vertical: ScrollBar {}

            // Deferred: called in the middle of a model reset, it costs ~75 ms (results/step-05).
            onCountChanged: if (follow.checked)
                Qt.callLater(log.positionViewAtEnd)

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

                Component.onCompleted: Stages.gauge("log_row_created", 1)
                ListView.onReused: Stages.gauge("log_row_reused", 1)
            }
        }
    }
}
