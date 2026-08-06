import QtQuick
import QtQuick.Layouts
import GigabyteRGBController

// Named snapshots of all eight zones. Chips rather than a dropdown: there are
// rarely more than a handful, and this way switching is one click and the whole
// set stays visible - which also matches how the zones above are picked.
RowLayout {
    id: root

    signal saveAsRequested()
    signal deleteRequested(string name)

    spacing: 10

    // Baseline, not centre: the hint below mixes CJK with punctuation that
    // comes from a fallback font, which makes its line box taller than the
    // label's. Centring two boxes of different heights puts their text on two
    // different lines - by two pixels, which is exactly enough to look wrong.
    Text {
        Layout.alignment: Qt.AlignBaseline
        text: "方案"
        color: Theme.textDim
        font.pixelSize: Theme.fontSmall
    }

    Text {
        Layout.alignment: Qt.AlignBaseline
        visible: Ctl.profiles.length === 0
        text: "尚未保存任何方案 — 调好灯效后点「另存为」存一份"
        color: Theme.textFaint
        font.pixelSize: Theme.fontSmall
    }

    Repeater {
        model: Ctl.profiles

        Item {
            id: chip
            required property string modelData

            readonly property bool isActive: Ctl.activeProfile === modelData

            Layout.alignment: Qt.AlignVCenter
            implicitWidth: chipLabel.implicitWidth + 26
            implicitHeight: 30

            Rectangle {
                anchors.fill: parent
                radius: Theme.radiusSmall
                color: chip.isActive ? Theme.accentSoft
                                     : (chipHover.hovered ? Theme.cardAlt : "transparent")
                border.width: chip.isActive ? 1.6 : 1
                border.color: chip.isActive ? Theme.accent : Theme.border

                Behavior on color        { ColorAnimation { duration: Theme.anim } }
                Behavior on border.color { ColorAnimation { duration: Theme.anim } }

                Text {
                    id: chipLabel
                    anchors.centerIn: parent
                    text: chip.modelData
                    font.pixelSize: Theme.fontSmall
                    color: chip.isActive ? Theme.accent : Theme.text
                }
            }

            HoverHandler {
                id: chipHover
                cursorShape: Qt.PointingHandCursor
            }

            TapHandler {
                onTapped: Ctl.selectProfile(chip.modelData)
            }

            // Right-click deletes, the same gesture the custom swatches use.
            // Confirmation is the caller's job: a profile is more work to
            // rebuild than a saved colour.
            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: root.deleteRequested(chip.modelData)
            }

            ToolTipBubble {
                show: chipHover.hovered
                text: (chip.isActive ? "当前方案" : "点击切换到此方案")
                      + "  ·  右键删除"
            }
        }
    }

    // Loading a profile is a click on its chip. That is the whole gesture, and
    // it is not guessable from a row of chips alone - the tooltip only helps
    // once you are already hovering the thing you did not know was a control.
    Text {
        Layout.alignment: Qt.AlignBaseline
        Layout.leftMargin: 2
        visible: Ctl.profiles.length > 0
        text: "点击载入 · 右键删除"
        color: Theme.textFaint
        font.pixelSize: 11
    }

    Item { Layout.fillWidth: true }

    PillButton {
        Layout.alignment: Qt.AlignVCenter
        text: "保存"
        enabled: Ctl.activeProfile !== ""
        onClicked: Ctl.updateActiveProfile()
    }

    PillButton {
        Layout.alignment: Qt.AlignVCenter
        text: "另存为"
        onClicked: root.saveAsRequested()
    }
}
