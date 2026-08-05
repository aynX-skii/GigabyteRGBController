import QtQuick
import GigabyteRGBController

// Flat rounded button. `primary` gives it the accent fill used for the one
// action on a page that commits something.
Item {
    id: root

    property string text
    property bool   primary: false
    property bool   enabled: true
    property bool   danger:  false

    signal clicked()

    implicitWidth: label.implicitWidth + 30
    implicitHeight: 32

    opacity: root.enabled ? 1 : 0.55

    readonly property color fillColour: {
        // A disabled primary drops the accent entirely rather than just fading
        // it - a translucent orange still reads as "press me".
        if (root.primary)
            return !root.enabled ? Theme.cardAlt
                                 : (hover.hovered ? Qt.lighter(Theme.accent, 1.12)
                                                  : Theme.accent);
        return hover.hovered ? Theme.cardAlt : "transparent";
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusSmall
        color: root.fillColour
        border.width: 1
        border.color: root.primary
                          ? (root.enabled ? "transparent" : Theme.border)
                          : (hover.hovered ? Theme.accent : Theme.border)

        Behavior on color        { ColorAnimation { duration: Theme.anim } }
        Behavior on border.color { ColorAnimation { duration: Theme.anim } }

        scale: tap.pressed ? 0.97 : 1
        Behavior on scale { NumberAnimation { duration: 90 } }

        Text {
            id: label
            anchors.centerIn: parent
            text: root.text
            font.pixelSize: Theme.fontSmall
            font.bold: root.primary
            color: root.primary && root.enabled
                       ? "#ffffff"
                       : (root.danger ? Theme.danger
                                      : (root.enabled ? Theme.text : Theme.textDim))
        }
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
