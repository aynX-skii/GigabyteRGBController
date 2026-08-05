import QtQuick
import GigabyteRGBController

// A small hover bubble. Rolled by hand rather than using Controls' ToolTip so
// it can carry multiple lines and match the rest of the surface styling.
//
// Positions itself above whatever it is declared inside, centred on it.
Item {
    id: root

    property string text
    property bool   show: false

    anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
    anchors.bottom: parent ? parent.top : undefined
    anchors.bottomMargin: 8

    implicitWidth: bubble.width
    implicitHeight: bubble.height
    z: 100

    visible: opacity > 0
    opacity: root.show ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: Theme.anim } }

    Rectangle {
        id: bubble
        width: label.implicitWidth + 20
        height: label.implicitHeight + 14
        radius: Theme.radiusSmall
        color: "#0d0f12"
        border.width: 1
        border.color: Theme.border

        Text {
            id: label
            anchors.centerIn: parent
            text: root.text
            color: Theme.text
            font.pixelSize: 11
            lineHeight: 1.25
            horizontalAlignment: Text.AlignHCenter
        }
    }
}
