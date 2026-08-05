import QtQuick
import GigabyteRGBController
import QtQuick.Layouts

// Zone picker. The protocol always exposes eight zones, but most boards
// populate only a few, so each chip carries three distinct states: never
// checked, checked and empty, checked and lit. Conflating the first two would
// hide the fact that detection has not been run.
ColumnLayout {
    id: root

    signal renameRequested(int zone)
    signal detectRequested()

    spacing: 10

    RowLayout {
        Layout.fillWidth: true
        spacing: 10

        Text {
            text: "区域"
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
        }

        Item { Layout.fillWidth: true }

        PillButton {
            text: "探测有灯区域"
            enabled: Ctl.connected && !Ctl.detecting
            onClicked: root.detectRequested()
        }

        PillButton {
            text: "重命名"
            enabled: Ctl.selectedZone >= 0
            onClicked: root.renameRequested(Ctl.selectedZone)
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 6

        // "All zones" is the default, and the only way to set every zone at
        // once - including the ones with nothing plugged in.
        Rectangle {
            Layout.preferredWidth: 56
            Layout.preferredHeight: 46
            radius: Theme.radiusSmall
            color: Ctl.selectedZone < 0 ? Theme.accentSoft : Theme.cardAlt
            border.width: Ctl.selectedZone < 0 ? 1.6 : 1
            border.color: Ctl.selectedZone < 0 ? Theme.accent : Theme.border

            Behavior on color        { ColorAnimation { duration: Theme.anim } }
            Behavior on border.color { ColorAnimation { duration: Theme.anim } }

            Text {
                anchors.centerIn: parent
                text: "全部"
                color: Ctl.selectedZone < 0 ? Theme.accent : Theme.textDim
                font.pixelSize: Theme.fontSmall
            }

            HoverHandler { cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: Ctl.selectedZone = -1 }
        }

        Repeater {
            model: Ctl.zones

            Item {
                id: chip
                required property var modelData

                readonly property bool isSelected: Ctl.selectedZone === modelData.index
                readonly property bool isEmpty: modelData.probed && !modelData.connected

                Layout.preferredWidth: 46
                Layout.preferredHeight: 46

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusSmall
                    color: chip.isEmpty ? Theme.sunken
                                        : (modelData.lit ? modelData.colour
                                                         : Theme.cardAlt)
                    border.width: 1
                    // A zone that has never been probed gets a dimmer outline,
                    // so "unknown" does not look like "confirmed empty".
                    border.color: modelData.probed ? Theme.border : Theme.borderSoft
                    opacity: modelData.probed ? 1 : 0.75

                    Behavior on color { ColorAnimation { duration: Theme.anim } }

                    // Diagonal strike = checked, nothing there. Kept faint so it
                    // reads as an annotation rather than as content.
                    Rectangle {
                        visible: chip.isEmpty
                        anchors.centerIn: parent
                        width: parent.width * 0.72
                        height: 1.5
                        color: Theme.textFaint
                        opacity: 0.55
                        rotation: -45
                    }

                    Text {
                        anchors.centerIn: parent
                        text: modelData.index + 1
                        font.pixelSize: Theme.fontBody
                        font.bold: true
                        // Pick whichever of black/white stays legible on the
                        // swatch underneath.
                        color: {
                            if (chip.isEmpty || !modelData.lit)
                                return Theme.textDim;
                            const c = modelData.colour;
                            return (c.r * 299 + c.g * 587 + c.b * 114) / 1000 > 0.55
                                   ? "#000000" : "#ffffff";
                        }
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -2.5
                    radius: Theme.radiusSmall + 2
                    color: "transparent"
                    border.width: 2
                    border.color: Theme.accent
                    visible: chip.isSelected
                }

                HoverHandler {
                    id: chipHover
                    cursorShape: Qt.PointingHandCursor
                }
                TapHandler { onTapped: Ctl.selectedZone = chip.modelData.index }

                ToolTipBubble {
                    show: chipHover.hovered
                    text: modelData.label + "  ·  命令 " + modelData.command + "\n"
                          + (modelData.probed
                             ? (modelData.connected ? "已探测：有灯" : "已探测：无灯")
                             : "未探测")
                          + "  ·  " + modelData.modeName
                }
            }
        }

        Item { Layout.fillWidth: true }
    }
}
