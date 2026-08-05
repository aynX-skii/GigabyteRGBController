import QtQuick
import GigabyteRGBController
import QtQuick.Layouts

// Two rows of round chips: fixed presets on top, user slots below. An empty
// user slot stores the current colour when clicked; a filled one recalls it,
// and right-click empties it again.
ColumnLayout {
    id: root

    property color selected: "#ff0000"
    property var   presets: []
    property var   custom:  []

    signal picked(color c)
    signal saveRequested(int slot)
    signal clearRequested(int slot)

    spacing: 10

    component Chip: Item {
        id: chip
        property color fill: "transparent"
        property bool  filled: true
        property bool  active: false

        signal tapped()
        signal secondaryTapped()

        // The item is sized to hold the selection ring, not just the swatch:
        // a ring drawn larger than its own item overflows the row and gets
        // clipped on the edges. Both sizes are even so the centred swatch
        // lands on whole pixels and the two circles stay concentric.
        implicitWidth: 34
        implicitHeight: 34

        // Ring outside the swatch, so selection reads clearly even on a colour
        // close to white. A sibling rather than a child, so the hover scale
        // below does not drag it around.
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: "transparent"
            border.width: 2
            border.color: Theme.accent
            visible: chip.active
            antialiasing: true
        }

        Rectangle {
            anchors.centerIn: parent
            width: 26
            height: 26
            radius: width / 2
            color: chip.filled ? chip.fill : "transparent"
            border.width: 1
            border.color: chip.filled ? Qt.rgba(0, 0, 0, 0.35) : Theme.border
            antialiasing: true
            scale: chipHover.hovered ? 1.1 : 1

            Behavior on scale        { NumberAnimation { duration: Theme.anim } }
            Behavior on border.color { ColorAnimation { duration: Theme.anim } }
        }

        HoverHandler { id: chipHover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: chip.tapped() }
        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: chip.secondaryTapped()
        }
    }

    RowLayout {
        spacing: 4
        Repeater {
            model: root.presets
            Chip {
                fill: modelData
                active: Qt.colorEqual(modelData, root.selected)
                onTapped: root.picked(modelData)
            }
        }
        Item { Layout.fillWidth: true }
    }

    RowLayout {
        spacing: 4
        Repeater {
            model: root.custom
            Chip {
                required property int index
                required property var modelData

                filled: modelData !== undefined && modelData !== null
                fill: filled ? modelData : "transparent"
                active: filled && Qt.colorEqual(modelData, root.selected)

                onTapped: filled ? root.picked(modelData)
                                 : root.saveRequested(index)
                onSecondaryTapped: if (filled) root.clearRequested(index)
            }
        }
        Item { Layout.fillWidth: true }
    }

    Text {
        text: "自定义色板：点击空位保存当前颜色，右键清除"
        color: Theme.textFaint
        font.pixelSize: 11
    }
}
