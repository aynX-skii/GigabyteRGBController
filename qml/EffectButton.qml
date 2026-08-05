import QtQuick
import GigabyteRGBController

// A circular icon button with its name underneath, as in the reference layout.
Item {
    id: root

    property string glyph
    property string label
    property bool   selected: false
    property bool   enabled:  true

    signal clicked()

    implicitWidth: 74
    implicitHeight: circle.height + caption.height + 8

    opacity: root.enabled ? 1 : 0.35

    readonly property color tint: root.selected ? Theme.accent
                                                : (hover.hovered ? Theme.text
                                                                 : Theme.textDim)

    Rectangle {
        id: circle
        anchors.horizontalCenter: parent.horizontalCenter
        width: 56
        height: 56
        radius: width / 2
        color: root.selected ? Theme.accentSoft
                             : (hover.hovered ? Theme.cardAlt : "transparent")
        border.width: root.selected ? 1.6 : 1
        border.color: root.selected ? Theme.accent
                                    : (hover.hovered ? Theme.border : Theme.borderSoft)

        Behavior on color       { ColorAnimation { duration: Theme.anim } }
        Behavior on border.color { ColorAnimation { duration: Theme.anim } }

        scale: tap.pressed ? 0.94 : 1
        Behavior on scale { NumberAnimation { duration: 90 } }

        EffectGlyph {
            anchors.centerIn: parent
            width: 28
            height: 28
            glyph: root.glyph
            stroke: root.tint
        }
    }

    Text {
        id: caption
        anchors.top: circle.bottom
        anchors.topMargin: 8
        anchors.horizontalCenter: parent.horizontalCenter
        text: root.label
        color: root.tint
        font.pixelSize: Theme.fontSmall
        Behavior on color { ColorAnimation { duration: Theme.anim } }
    }

    HoverHandler {
        id: hover
        enabled: root.enabled
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        id: tap
        enabled: root.enabled
        onTapped: root.clicked()
    }
}
