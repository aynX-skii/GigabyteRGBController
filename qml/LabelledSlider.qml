import QtQuick
import GigabyteRGBController
import QtQuick.Controls.Basic

// A slider with a title above it and end-captions underneath, matching the
// "亮度 / 最小 … 最大" arrangement in the reference layout.
Item {
    id: root

    property string title
    property string leftCaption:  "最小"
    property string rightCaption: "最大"
    property string valueText: ""

    property int from: 0
    property int to:   100
    // Stays a live binding to the backend: `moved` is the only thing that
    // writes back, so the round trip never turns into a loop.
    property int value: 0

    signal moved(int v)

    implicitHeight: header.height + bar.height + captions.height + 14
    implicitWidth: 260

    Item {
        id: header
        width: parent.width
        height: 18

        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: root.title
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
        }

        Text {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: root.valueText
            color: Theme.text
            font.pixelSize: Theme.fontSmall
            font.bold: true
        }
    }

    Slider {
        id: bar
        anchors.top: header.bottom
        anchors.topMargin: 4
        width: parent.width
        height: 20

        from: root.from
        to:   root.to
        stepSize: 1
        value: root.value

        onMoved: root.moved(Math.round(value))

        background: Rectangle {
            x: bar.leftPadding
            y: bar.topPadding + bar.availableHeight / 2 - height / 2
            width: bar.availableWidth
            height: 4
            radius: 2
            color: Theme.border

            Rectangle {
                width: bar.visualPosition * parent.width
                height: parent.height
                radius: 2
                color: root.enabled ? Theme.accent : Theme.textFaint
            }
        }

        handle: Rectangle {
            x: bar.leftPadding + bar.visualPosition * (bar.availableWidth - width)
            y: bar.topPadding + bar.availableHeight / 2 - height / 2
            width: 14
            height: 14
            radius: 7
            color: bar.pressed ? Theme.accent : Theme.text
            border.width: 1
            border.color: Qt.rgba(0, 0, 0, 0.4)

            Behavior on color { ColorAnimation { duration: Theme.anim } }
        }
    }

    Item {
        id: captions
        anchors.top: bar.bottom
        anchors.topMargin: 2
        width: parent.width
        height: 16

        Text {
            anchors.left: parent.left
            text: root.leftCaption
            color: Theme.textFaint
            font.pixelSize: 11
        }
        Text {
            anchors.right: parent.right
            text: root.rightCaption
            color: Theme.textFaint
            font.pixelSize: 11
        }
    }
}
