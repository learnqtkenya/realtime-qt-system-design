import QtQuick
import QtQuick.Controls
import QtQMLPro.Common

Item {
    id: chart

    required property string title
    required property list<real> values
    property real threshold: -1
    property string unit: "ms"
    property int decimals: 1
    property color color: "#38BDF8"

    readonly property real current: values.length > 0 ? values[values.length - 1] : 0
    readonly property bool over: threshold >= 0 && current > threshold

    implicitHeight: 42

    Label {
        id: titleLabel

        text: chart.title + (chart.threshold >= 0 ? "  (budget " + chart.threshold + " " + chart.unit + ")" : "")
        font.pixelSize: 11
        color: "#94A3B8"
    }

    Label {
        anchors.right: parent.right
        text: chart.current.toFixed(chart.decimals) + " " + chart.unit
        font.pixelSize: 12
        font.bold: true
        color: chart.over ? "#F87171" : "#E2E8F0"
    }

    Rectangle {
        anchors.top: titleLabel.bottom
        anchors.topMargin: 4
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        color: "#0B1222"
        radius: 4

        Sparkline {
            anchors.fill: parent
            anchors.margins: 4
            values: chart.values
            threshold: chart.threshold
            color: chart.color
        }
    }
}
