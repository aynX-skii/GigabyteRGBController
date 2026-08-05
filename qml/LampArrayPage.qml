import QtQuick
import GigabyteRGBController
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The standard HID LampArray interface (Usage Page 0x59) - the same one
// Windows 11 dynamic lighting drives. Host-driven rather than onboard, so this
// page is about inspecting what the controller exposes and painting it live.
ColumnLayout {
    id: root
    spacing: Theme.gap

    Card {
        Layout.fillWidth: true

        ColumnLayout {
            anchors.fill: parent
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Rectangle {
                    width: 10
                    height: 10
                    radius: 5
                    color: Ctl.lampConnected ? Theme.ok : Theme.danger
                }

                Text {
                    Layout.fillWidth: true
                    text: Ctl.lampInfo
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }

                PillButton {
                    text: "读取属性"
                    onClicked: Ctl.lampRescan()
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Theme.borderSoft
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                ColumnLayout {
                    spacing: 2
                    Text {
                        text: "自主模式"
                        color: Theme.text
                        font.pixelSize: Theme.fontBody
                    }
                    Text {
                        text: "开：板载效果自己跑。关：主机接管，灯光更新才会生效。"
                        color: Theme.textDim
                        font.pixelSize: 11
                    }
                }

                Item { Layout.fillWidth: true }

                ToggleSwitch {
                    enabled: Ctl.lampConnected
                    checked: Ctl.autonomousMode
                    onToggled: (v) => Ctl.autonomousMode = v
                }
            }
        }
    }

    Card {
        Layout.fillWidth: true

        RowLayout {
            anchors.fill: parent
            spacing: 12

            Text {
                text: "颜色"
                color: Theme.textDim
                font.pixelSize: Theme.fontSmall
            }

            // ColorFields already leads with a preview swatch, so there is no
            // separate one here.
            ColorFields {
                Layout.fillWidth: true
                selected: Ctl.lampColour
                onPicked: (c) => Ctl.lampColour = c
            }

            PillButton {
                text: "全部设为此色"
                primary: true
                enabled: Ctl.lampConnected
                onClicked: Ctl.lampApplyAll()
            }
        }
    }

    Card {
        Layout.fillWidth: true
        Layout.fillHeight: true

        ColumnLayout {
            anchors.fill: parent
            spacing: 8

            Text {
                text: Ctl.lamps.length > 0
                      ? "已枚举 " + Ctl.lamps.length + " 颗灯"
                      : "尚未枚举灯珠 — 点上方「读取属性」"
                color: Theme.textDim
                font.pixelSize: Theme.fontSmall
            }

            // Header row, matching the delegate's column weights below.
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text { Layout.preferredWidth: 60; text: "灯 ID";  color: Theme.textFaint; font.pixelSize: 11 }
                Text { Layout.fillWidth: true;    text: "X (µm)"; color: Theme.textFaint; font.pixelSize: 11 }
                Text { Layout.fillWidth: true;    text: "Y (µm)"; color: Theme.textFaint; font.pixelSize: 11 }
                Text { Layout.fillWidth: true;    text: "Z (µm)"; color: Theme.textFaint; font.pixelSize: 11 }
                Text { Layout.preferredWidth: 140; text: "用途";   color: Theme.textFaint; font.pixelSize: 11 }
                Text { Layout.preferredWidth: 60; text: "可编程"; color: Theme.textFaint; font.pixelSize: 11 }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: Theme.radiusSmall
                color: Theme.sunken
                border.width: 1
                border.color: Theme.border
                clip: true

                ListView {
                    anchors.fill: parent
                    anchors.margins: 6
                    model: Ctl.lamps
                    clip: true
                    spacing: 1

                    delegate: RowLayout {
                        required property var modelData
                        width: ListView.view.width
                        spacing: 8

                        Text { Layout.preferredWidth: 60;  text: modelData.id;       color: Theme.text;    font.pixelSize: 11; font.family: "monospace" }
                        Text { Layout.fillWidth: true;     text: modelData.x;        color: Theme.textDim; font.pixelSize: 11; font.family: "monospace" }
                        Text { Layout.fillWidth: true;     text: modelData.y;        color: Theme.textDim; font.pixelSize: 11; font.family: "monospace" }
                        Text { Layout.fillWidth: true;     text: modelData.z;        color: Theme.textDim; font.pixelSize: 11; font.family: "monospace" }
                        Text { Layout.preferredWidth: 140; text: modelData.purposes; color: Theme.textDim; font.pixelSize: 11; elide: Text.ElideRight }
                        Text {
                            Layout.preferredWidth: 60
                            text: modelData.programmable ? "是" : "否"
                            color: modelData.programmable ? Theme.ok : Theme.textFaint
                            font.pixelSize: 11
                        }
                    }
                }
            }
        }
    }
}
