import QtQuick
import GigabyteRGBController
import QtQuick.Layouts

// The right-hand pane. Which controls appear is driven entirely by what the
// selected effect actually uses - `usesColour`, `usesSpeed` and the per-mode
// brightness ceiling all come from RgbFusion2, so the panel can never offer a
// setting the controller would ignore.
//
// Colour picking and the numeric parameters sit side by side: stacking them
// pushed the speed slider off the bottom of the card on a default-sized window.
ColumnLayout {
    id: root

    property var effect: ({})     // the entry from Ctl.effects for the current mode

    spacing: 20

    // ---- heading -----------------------------------------------------------
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 4

        Text {
            text: root.effect.name !== undefined ? root.effect.name : ""
            color: Theme.text
            font.pixelSize: Theme.fontTitle
            font.bold: true
        }
        Text {
            Layout.fillWidth: true
            text: root.effect.hint !== undefined ? root.effect.hint : ""
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.WordWrap
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 32

        // ---- colour ---------------------------------------------------------
        ColumnLayout {
            Layout.alignment: Qt.AlignTop
            Layout.preferredWidth: 260
            visible: Ctl.usesColour
            spacing: 14

            ColorWheel {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 208
                Layout.preferredHeight: 208
                selected: Ctl.colour
                onPicked: (c) => Ctl.colour = c
            }

            ColorFields {
                Layout.fillWidth: true
                selected: Ctl.colour
                onPicked: (c) => Ctl.colour = c
            }

            Swatches {
                Layout.fillWidth: true
                selected: Ctl.colour
                presets: Ctl.presetColours
                custom:  Ctl.customColours
                onPicked: (c) => Ctl.colour = c
                onSaveRequested: (slot) => Ctl.saveCustomColour(slot, Ctl.colour)
                onClearRequested: (slot) => Ctl.clearCustomColour(slot)
            }
        }

        // ---- numeric parameters ---------------------------------------------
        ColumnLayout {
            Layout.alignment: Qt.AlignTop
            Layout.fillWidth: true
            Layout.maximumWidth: 340
            spacing: 22

            // Colour cycle picks no colour; show what it will do instead.
            ColumnLayout {
                Layout.fillWidth: true
                visible: !Ctl.usesColour && Ctl.mode !== 0
                spacing: 6

                Text {
                    text: "色相由控制器自行循环"
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSmall
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 26
                    radius: Theme.radiusSmall
                    border.width: 1
                    border.color: Theme.border

                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.000; color: "#ff0000" }
                        GradientStop { position: 0.166; color: "#ffff00" }
                        GradientStop { position: 0.333; color: "#00ff00" }
                        GradientStop { position: 0.500; color: "#00ffff" }
                        GradientStop { position: 0.666; color: "#0000ff" }
                        GradientStop { position: 0.833; color: "#ff00ff" }
                        GradientStop { position: 1.000; color: "#ff0000" }
                    }
                }
            }

            LabelledSlider {
                Layout.fillWidth: true
                visible: Ctl.brightnessCap > 0 && Ctl.mode !== 0
                title: "亮度"
                from: 0
                to: Ctl.brightnessCap
                value: Ctl.brightness
                valueText: Ctl.brightness + " / " + Ctl.brightnessCap
                onMoved: (v) => Ctl.brightness = v
            }

            // MinBrightness is the floor a ramping effect dims down to - it only
            // means anything while the effect is actually moving.
            LabelledSlider {
                Layout.fillWidth: true
                visible: Ctl.usesSpeed && Ctl.mode !== 0
                title: "最小亮度"
                leftCaption: "全暗"
                rightCaption: "不变暗"
                from: 0
                to: Math.max(1, Ctl.brightness)
                value: Ctl.minBrightness
                valueText: String(Ctl.minBrightness)
                onMoved: (v) => Ctl.minBrightness = v
            }

            LabelledSlider {
                Layout.fillWidth: true
                visible: Ctl.usesSpeed
                title: "速度"
                leftCaption: "最慢"
                rightCaption: "最快"
                from: 0
                to: 5
                value: Ctl.speed
                valueText: Ctl.speedName
                onMoved: (v) => Ctl.speed = v
            }

            Text {
                Layout.fillWidth: true
                visible: Ctl.mode === 0
                text: "此效果没有可调参数。"
                color: Theme.textFaint
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            }
        }
    }

    Item { Layout.fillHeight: true }
}
