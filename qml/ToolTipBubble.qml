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

    // Past this the bubble wraps instead of growing: a long line - the udev
    // hint on a permission failure, say - would otherwise be wider than the
    // window it is trying to explain itself in.
    property int maxWidth: 420

    // Above by default; `below` is for anything near the top of the window,
    // where a bubble hanging upwards would land on the title bar.
    property bool below: false

    anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
    anchors.bottom: (parent && !root.below) ? parent.top : undefined
    anchors.top: (parent && root.below) ? parent.bottom : undefined
    anchors.bottomMargin: 8
    anchors.topMargin: 8

    implicitWidth: bubble.width
    implicitHeight: bubble.height
    z: 100

    visible: opacity > 0
    opacity: root.show ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: Theme.anim } }

    Rectangle {
        id: bubble
        // implicitWidth is the unwrapped width, so this settles in one pass:
        // the height below follows from the width, never the other way round.
        width: Math.min(label.implicitWidth, root.maxWidth) + 20
        height: label.implicitHeight + 14
        radius: Theme.radiusSmall
        color: "#0d0f12"
        border.width: 1
        border.color: Theme.border

        Text {
            id: label
            anchors.centerIn: parent
            width: bubble.width - 20
            text: root.text
            color: Theme.text
            font.pixelSize: 11
            lineHeight: 1.25
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
        }
    }
}
