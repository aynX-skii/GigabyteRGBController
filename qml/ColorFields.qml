import QtQuick
import GigabyteRGBController
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Preview swatch plus editable R / G / B / hex entries, as in the reference row
// under the wheel. Every field is a real input, so a colour can be typed in
// exactly rather than only dialled in by hand.
RowLayout {
    id: root

    property color selected: "#ff0000"
    signal picked(color c)

    spacing: 8

    function clamp8(v) { return Math.max(0, Math.min(255, v)); }

    function emitRgb(r, g, b) {
        root.picked(Qt.rgba(clamp8(r) / 255, clamp8(g) / 255, clamp8(b) / 255, 1));
    }

    // ---- live preview ------------------------------------------------------
    Rectangle {
        Layout.preferredWidth: 66
        Layout.preferredHeight: 24
        radius: Theme.radiusSmall
        color: root.selected
        border.width: 1
        border.color: Qt.rgba(0, 0, 0, 0.4)
    }

    component Field: RowLayout {
        id: field
        property string label
        property string text
        property int    maxLen: 3
        property var    validator: null
        signal committed(string value)

        spacing: 5

        Text {
            text: field.label
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 24
            radius: Theme.radiusSmall
            color: Theme.sunken
            border.width: 1
            border.color: input.activeFocus ? Theme.accent : Theme.border

            Behavior on border.color { ColorAnimation { duration: Theme.anim } }

            TextInput {
                id: input
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: Text.AlignHCenter
                color: Theme.text
                font.pixelSize: Theme.fontSmall
                selectByMouse: true
                maximumLength: field.maxLen
                validator: field.validator

                // Only adopt the incoming value while the user is not typing,
                // so an in-progress edit is never yanked out from under them.
                text: field.text
                onTextChanged: if (!activeFocus) cursorPosition = 0

                onEditingFinished: field.committed(text)
                onActiveFocusChanged: if (!activeFocus) text = field.text
            }
        }
    }

    Field {
        Layout.preferredWidth: 76
        Layout.maximumWidth: 76
        label: "R"
        text: String(Math.round(root.selected.r * 255))
        validator: IntValidator { bottom: 0; top: 255 }
        onCommitted: (v) => root.emitRgb(parseInt(v || "0"),
                                         Math.round(root.selected.g * 255),
                                         Math.round(root.selected.b * 255))
    }

    Field {
        Layout.preferredWidth: 76
        Layout.maximumWidth: 76
        label: "G"
        text: String(Math.round(root.selected.g * 255))
        validator: IntValidator { bottom: 0; top: 255 }
        onCommitted: (v) => root.emitRgb(Math.round(root.selected.r * 255),
                                         parseInt(v || "0"),
                                         Math.round(root.selected.b * 255))
    }

    Field {
        Layout.preferredWidth: 76
        Layout.maximumWidth: 76
        label: "B"
        text: String(Math.round(root.selected.b * 255))
        validator: IntValidator { bottom: 0; top: 255 }
        onCommitted: (v) => root.emitRgb(Math.round(root.selected.r * 255),
                                         Math.round(root.selected.g * 255),
                                         parseInt(v || "0"))
    }

    Field {
        Layout.preferredWidth: 96
        Layout.maximumWidth: 96
        label: "#"
        maxLen: 6
        text: root.selected.toString().substring(1).toUpperCase()
        validator: RegularExpressionValidator {
            regularExpression: /[0-9A-Fa-f]{0,6}/
        }
        onCommitted: (v) => {
            if (v.length === 6)
                root.picked("#" + v);
        }
    }

    // Absorbs any slack so the fields keep their size in a wide parent instead
    // of stretching across it.
    Item { Layout.fillWidth: true }
}
