import QtQuick
import GigabyteRGBController
import QtQuick.Layouts

// The left column: every effect the controller supports, as a two-wide grid of
// icon buttons. The list is whatever the backend publishes, so adding an effect
// to RgbFusion2 lights it up here with no UI change.
ColumnLayout {
    id: root
    spacing: 16

    Text {
        Layout.alignment: Qt.AlignHCenter
        text: "选择灯效"
        color: Theme.textDim
        font.pixelSize: Theme.fontSmall
    }

    Grid {
        Layout.alignment: Qt.AlignHCenter
        columns: 2
        spacing: 12

        Repeater {
            model: Ctl.effects
            EffectButton {
                required property var modelData
                glyph: modelData.icon
                label: modelData.name
                selected: Ctl.mode === modelData.mode
                onClicked: Ctl.mode = modelData.mode
            }
        }
    }

    Item { Layout.fillHeight: true }
}
