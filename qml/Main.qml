import QtQuick
import GigabyteRGBController
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window

ApplicationWindow {
    id: window

    width: 1100
    height: 820
    minimumWidth: 900
    minimumHeight: 700
    visible: true
    title: "Gigabyte RGB Controller — 主板灯光控制"

    // The whole frame is ours: system decorations would put a light, rounded,
    // desktop-themed bar on top of a flat dark app. TitleBar and
    // WindowResizeBorder below replace what the compositor stops drawing.
    flags: Qt.Window | Qt.FramelessWindowHint

    // The window background is painted by `background` instead, so that the
    // corners can be rounded - anything the rounded rectangle does not cover
    // has to stay transparent.
    color: "transparent"

    readonly property bool maximized: visibility === Window.Maximized

    background: Rectangle {
        color: Theme.background
        // A maximised window is flush with the screen edges; rounding it there
        // would only cut holes in the corners.
        radius: window.maximized ? 0 : Theme.radius
        border.width: 1
        border.color: Theme.border
    }

    // Everything lives in one column rather than in ApplicationWindow's
    // `header`, so that the title bar, the device bar and the pages share a
    // single stacking order - the resize grips at the bottom of this file have
    // to end up above all three.
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- title bar -----------------------------------------------------
        TitleBar {
            Layout.fillWidth: true
            title: window.title
        }

        // ---- device bar ----------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            color: Theme.card

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.borderSoft
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.pad
                anchors.rightMargin: Theme.pad
                spacing: Theme.gap

                // A dot that carries the connection state without a line of text.
                Rectangle {
                    width: 10
                    height: 10
                    radius: 5
                    color: Ctl.connected ? Theme.ok : Theme.danger
                    Behavior on color { ColorAnimation { duration: Theme.anim } }
                }

                // Elides rather than pushing the controls off the right edge: a
                // long product string must not cost us the buttons.
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Text {
                        Layout.fillWidth: true
                        text: Ctl.deviceName
                        color: Theme.text
                        font.pixelSize: Theme.fontTitle
                        font.bold: true
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: Ctl.deviceDetail
                        color: Theme.textDim
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }

                RowLayout {
                    spacing: 8

                    Text {
                        text: "自动应用"
                        color: Theme.textDim
                        font.pixelSize: Theme.fontSmall
                    }

                    // Auto-apply pushes each change to the controller as it is
                    // made; the explicit button stays for when it is switched off.
                    ToggleSwitch {
                        checked: Ctl.autoApply
                        onToggled: (v) => Ctl.autoApply = v
                    }

                    PillButton {
                        text: "重新扫描"
                        onClicked: Ctl.rescan()
                    }
                }
            }
        }

        // ---- tabs ----------------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Theme.pad
            spacing: Theme.gap

                RowLayout {
                Layout.fillWidth: true
                spacing: 4

                Repeater {
                    model: ["硬件效果", "标准接口 (LampArray)", "协议日志"]

                    Item {
                        required property int index
                        required property string modelData

                        implicitWidth: tabLabel.implicitWidth + 28
                        implicitHeight: 34

                        Rectangle {
                            anchors.fill: parent
                            radius: Theme.radiusSmall
                            color: pages.currentIndex === index ? Theme.card : "transparent"
                            Behavior on color { ColorAnimation { duration: Theme.anim } }

                            Text {
                                id: tabLabel
                                anchors.centerIn: parent
                                text: modelData
                                font.pixelSize: Theme.fontBody
                                color: pages.currentIndex === index ? Theme.text : Theme.textDim
                            }

                            Rectangle {
                                anchors.bottom: parent.bottom
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: pages.currentIndex === index ? parent.width - 20 : 0
                                height: 2
                                radius: 1
                                color: Theme.accent
                                Behavior on width { NumberAnimation { duration: Theme.anim } }
                            }
                        }

                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: pages.currentIndex = index }
                    }
                }

                Item { Layout.fillWidth: true }
            }

            StackLayout {
                id: pages
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: 0

                // ============ page 1: hardware effects ==========================
                ColumnLayout {
                    spacing: Theme.gap

                    Card {
                        Layout.fillWidth: true

                        ZoneBar {
                            anchors.fill: parent
                            onRenameRequested: (zone) => renameDialog.open(zone)
                            onDetectRequested: detectDialog.open()
                        }
                    }

                    // The effect picker and its settings, laid out as one panel.
                    Card {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        padding: 0

                        RowLayout {
                            anchors.fill: parent
                            spacing: 0

                            EffectRail {
                                Layout.fillHeight: true
                                Layout.preferredWidth: 200
                                Layout.margins: Theme.pad + 6
                            }

                            Rectangle {
                                Layout.fillHeight: true
                                Layout.topMargin: Theme.pad
                                Layout.bottomMargin: Theme.pad
                                width: 1
                                color: Theme.borderSoft
                            }

                            ScrollView {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                contentWidth: availableWidth
                                clip: true

                                EffectSettings {
                                    width: parent.width - 2 * (Theme.pad + 8)
                                    x: Theme.pad + 8
                                    y: Theme.pad + 6
                                    effect: {
                                        const list = Ctl.effects;
                                        for (let i = 0; i < list.length; ++i) {
                                            if (list[i].mode === Ctl.mode)
                                                return list[i];
                                        }
                                        return ({});
                                    }
                                }
                            }
                        }
                    }

                    // ---- action row --------------------------------------------
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        // Greys out once everything on screen has reached the
                        // controller, and lights up again on the next change - so
                        // its state answers "is there anything left to send?".
                        PillButton {
                            text: Ctl.pending ? "应用到" + Ctl.selectionLabel : "已应用"
                            primary: true
                            enabled: Ctl.connected && Ctl.pending
                            onClicked: Ctl.apply()
                        }

                        PillButton {
                            text: "全部关闭"
                            enabled: Ctl.connected
                            onClicked: Ctl.allOff()
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            Layout.maximumWidth: 520
                            text: Ctl.statusText
                            color: Ctl.statusIsError ? Theme.danger : Theme.textDim
                            font.pixelSize: Theme.fontSmall
                            elide: Text.ElideRight
                        }
                    }
                }

                // ============ page 2: LampArray =================================
                LampArrayPage {}

                // ============ page 3: protocol log ==============================
                Card {
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 10

                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                text: "HID Feature 报文"
                                color: Theme.textDim
                                font.pixelSize: Theme.fontSmall
                            }
                            Item { Layout.fillWidth: true }
                            PillButton {
                                text: "清空"
                                onClicked: { logModel.clear(); Ctl.clearLog(); }
                            }
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
                                id: logView
                                anchors.fill: parent
                                anchors.margins: 8
                                model: logModel
                                spacing: 2
                                clip: true

                                // Follow the tail only while the user is already
                                // there, so scrolling back to read is not fought.
                                property bool atTail: true
                                onContentYChanged: atTail = atYEnd
                                onCountChanged: if (atTail) positionViewAtEnd()

                                delegate: Text {
                                    required property string timestamp
                                    required property string body
                                    required property bool   isError

                                    width: logView.width
                                    text: "[" + timestamp + "] " + body
                                    color: isError ? Theme.danger : Theme.textDim
                                    font.family: "monospace"
                                    font.pixelSize: 11
                                    wrapMode: Text.WrapAnywhere
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Above everything, dialogs included: the window edge has to stay grabbable
    // no matter what is on screen.
    WindowResizeBorder {
        anchors.fill: parent
        z: 2000
    }

    // ---- protocol log model ------------------------------------------------
    ListModel {
        id: logModel

        // The device is opened before this view exists, so pick up whatever the
        // backend already recorded rather than starting blank.
        Component.onCompleted: {
            clear();
            const backlog = Ctl.logBacklog();
            for (let i = 0; i < backlog.length; ++i)
                append(backlog[i]);
        }
    }

    Connections {
        target: Ctl
        function onLogLine(timestamp, text, isError) {
            logModel.append({ timestamp: timestamp, body: text, isError: isError });
            // Mirrors the cap the backend keeps on its own backlog.
            while (logModel.count > 800)
                logModel.remove(0);
        }
    }

    // ---- rename dialog -----------------------------------------------------
    ModalCard {
        id: renameDialog
        property int zone: 0

        title: "重命名区域 " + (zone + 1)
        subtitle: "留空可恢复默认名称。"

        function open(z) {
            zone = z;
            nameField.text = Ctl.zones[z].name;
            show = true;
            nameField.forceActiveFocus();
        }

        function accept() {
            Ctl.renameZone(zone, nameField.text);
            show = false;
        }

        Rectangle {
            width: parent.width
            height: 34
            radius: Theme.radiusSmall
            color: Theme.sunken
            border.width: 1
            border.color: nameField.activeFocus ? Theme.accent : Theme.border

            TextInput {
                id: nameField
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                verticalAlignment: Text.AlignVCenter
                color: Theme.text
                font.pixelSize: Theme.fontBody
                selectByMouse: true
                maximumLength: 24
                onAccepted: renameDialog.accept()
            }
        }

        Row {
            spacing: 8
            anchors.right: parent.right

            PillButton {
                text: "取消"
                onClicked: renameDialog.show = false
            }
            PillButton {
                text: "确定"
                primary: true
                onClicked: renameDialog.accept()
            }
        }
    }

    // ---- zone detection wizard ---------------------------------------------
    //
    // Driven one answer at a time instead of a stack of modal dialogs, so the
    // window stays live and the board is visible while answering.
    ModalCard {
        id: detectDialog

        // Two states share one dialog: the confirmation before starting, and
        // the per-zone question afterwards. `confirming` is local because
        // `Ctl.detecting` only becomes true once the first zone is lit.
        property bool confirming: false

        show: confirming || Ctl.detecting

        title: Ctl.detecting
               ? "探测中 (" + (Ctl.detectZone + 1) + "/8)"
               : "探测有灯区域"
        subtitle: Ctl.detecting
                  ? "区域 " + (Ctl.detectZone + 1) + " 现在应当亮起白色。主板上有灯亮起吗？"
                  : "将依次把每个区域单独点亮为白色，其余熄灭。过程会覆盖当前灯效，结束后自动恢复。"

        function open() { confirming = true; }

        Row {
            spacing: 8
            anchors.right: parent.right

            PillButton {
                text: Ctl.detecting ? "停止" : "取消"
                onClicked: {
                    detectDialog.confirming = false;
                    if (Ctl.detecting)
                        Ctl.cancelDetection();
                }
            }
            PillButton {
                visible: Ctl.detecting
                text: "没有"
                onClicked: Ctl.answerDetection(false)
            }
            PillButton {
                text: Ctl.detecting ? "有灯" : "开始"
                primary: true
                onClicked: {
                    if (Ctl.detecting) {
                        Ctl.answerDetection(true);
                    } else {
                        detectDialog.confirming = false;
                        Ctl.beginDetection();
                    }
                }
            }
        }
    }
}
