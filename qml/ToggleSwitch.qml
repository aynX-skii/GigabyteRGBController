import QtQuick
import GigabyteRGBController

// A small on/off switch. Hand-rolled rather than styled from Controls so that
// its width is genuinely fixed - a Controls Switch shrinks under layout
// pressure while its indicator keeps drawing at full size, which overlaps
// whatever sits next to it.
Item {
    id: root

    property bool checked: false
    property bool enabled: true

    signal toggled(bool value)

    implicitWidth: 40
    implicitHeight: 22

    opacity: root.enabled ? 1 : 0.4

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: root.checked ? Theme.accent : Theme.cardAlt
        border.width: 1
        border.color: root.checked ? Theme.accent : Theme.border

        Behavior on color        { ColorAnimation { duration: Theme.anim } }
        Behavior on border.color { ColorAnimation { duration: Theme.anim } }

        Rectangle {
            width: parent.height - 6
            height: width
            radius: width / 2
            y: 3
            x: root.checked ? parent.width - width - 3 : 3
            color: "#ffffff"

            Behavior on x { NumberAnimation { duration: Theme.anim } }
        }
    }

    HoverHandler {
        enabled: root.enabled
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        enabled: root.enabled
        onTapped: root.toggled(!root.checked)
    }
}
