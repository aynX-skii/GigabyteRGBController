import QtQuick
import GigabyteRGBController
import QtCore
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window

ApplicationWindow {
    id: window

    width: 1100
    height: 880
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

    // ---- geometry ----------------------------------------------------------
    //
    // Remembered by hand: the window draws its own frame, so nothing on the
    // desktop side puts it back where it was. Only the windowed rectangle is
    // tracked - restoring a maximised window to its maximised size as a normal
    // window would leave it wedged against the screen edges.
    property rect normalGeometry: Qt.rect(0, 0, 0, 0)

    function rememberGeometry() {
        if (visibility === Window.Windowed)
            normalGeometry = Qt.rect(x, y, width, height);
    }

    onXChanged: rememberGeometry()
    onYChanged: rememberGeometry()
    onWidthChanged: rememberGeometry()
    onHeightChanged: rememberGeometry()

    Component.onCompleted: {
        const g = Ctl.windowGeometry();
        if (g.width > 0 && g.height > 0) {
            width  = g.width;
            height = g.height;
            // X11 honours this; Wayland reserves window placement for the
            // compositor and will ignore it.
            if (g.x > 0 && g.y > 0) {
                x = g.x;
                y = g.y;
            }
        }
        rememberGeometry();
        if (Ctl.windowMaximized())
            showMaximized();
    }

    onClosing: Ctl.saveWindow(normalGeometry, visibility === Window.Maximized)

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
            version: Ctl.appVersion
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
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth: 10
                    implicitHeight: 10
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
                    // Carries the reason the device is not open, which on a
                    // first run is a udev recipe rather than a status - long
                    // enough that the rest lives one hover away.
                    Text {
                        id: deviceDetailText
                        Layout.fillWidth: true
                        text: Ctl.deviceDetail
                        color: Ctl.connected ? Theme.textDim : Theme.danger
                        font.pixelSize: 11
                        elide: Text.ElideRight

                        HoverHandler { id: detailHover }

                        ToolTipBubble {
                            below: true
                            show: detailHover.hovered && deviceDetailText.truncated
                            text: Ctl.deviceDetail
                        }
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

                    // Deliberately not in a Card: the effect panel below is the
                    // one that gives up height when the window is short, and a
                    // card's padding here costs it another 30-odd pixels.
                    ProfileBar {
                        Layout.fillWidth: true
                        onSaveAsRequested: saveProfileDialog.open()
                        onDeleteRequested: (name) => deleteProfileDialog.open(name)
                    }

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
                                Layout.preferredWidth: 1
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

                        // Takes the whole remaining width rather than a fixed
                        // 520: some messages are instructions, not status - the
                        // permission failure hands over three udev commands -
                        // and an ellipsis at 520 px would eat exactly the part
                        // worth reading. Whatever still does not fit is one
                        // hover away.
                        Text {
                            id: statusText
                            Layout.fillWidth: true
                            text: Ctl.statusText
                            color: Ctl.statusIsError ? Theme.danger : Theme.textDim
                            font.pixelSize: Theme.fontSmall
                            horizontalAlignment: Text.AlignRight
                            elide: Text.ElideRight

                            HoverHandler { id: statusHover }

                            ToolTipBubble {
                                show: statusHover.hovered
                                      && statusText.truncated
                                text: Ctl.statusText
                            }
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
                            spacing: 8

                            Text {
                                Layout.alignment: Qt.AlignVCenter
                                text: "HID Feature 报文"
                                color: Theme.textDim
                                font.pixelSize: Theme.fontSmall
                            }

                            // Filter box. The model is rebuilt from the backlog
                            // on every change rather than proxied - 800 lines is
                            // small enough that the simpler thing wins.
                            Rectangle {
                                Layout.alignment: Qt.AlignVCenter
                                Layout.preferredWidth: 220
                                Layout.preferredHeight: 28
                                radius: Theme.radiusSmall
                                color: Theme.sunken
                                border.width: 1
                                border.color: filterField.activeFocus ? Theme.accent
                                                                      : Theme.border

                                TextInput {
                                    id: filterField
                                    anchors.fill: parent
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 8
                                    verticalAlignment: Text.AlignVCenter
                                    color: Theme.text
                                    font.pixelSize: Theme.fontSmall
                                    selectByMouse: true
                                    onTextChanged: logModel.rebuild()
                                }

                                Text {
                                    anchors.fill: parent
                                    anchors.leftMargin: 8
                                    verticalAlignment: Text.AlignVCenter
                                    visible: filterField.text === ""
                                    text: "过滤关键字"
                                    color: Theme.textFaint
                                    font.pixelSize: Theme.fontSmall
                                }
                            }

                            PillButton {
                                id: errorsOnlyButton
                                property bool on: false

                                Layout.alignment: Qt.AlignVCenter
                                text: "只看错误"
                                primary: on
                                onClicked: { on = !on; logModel.rebuild(); }
                            }

                            Item { Layout.fillWidth: true }

                            Text {
                                Layout.alignment: Qt.AlignVCenter
                                text: logModel.count + " 行"
                                color: Theme.textFaint
                                font.pixelSize: 11
                            }

                            PillButton {
                                Layout.alignment: Qt.AlignVCenter
                                text: "复制"
                                onClicked: Ctl.copyLogToClipboard(errorsOnlyButton.on,
                                                                  filterField.text)
                            }
                            PillButton {
                                Layout.alignment: Qt.AlignVCenter
                                text: "导出…"
                                onClicked: logSaveDialog.open()
                            }
                            PillButton {
                                Layout.alignment: Qt.AlignVCenter
                                text: "清空"
                                onClicked: { Ctl.clearLog(); logModel.rebuild(); }
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

    // ---- shortcuts ---------------------------------------------------------
    Shortcut {
        sequences: ["Ctrl+Q", "Ctrl+W"]
        onActivated: window.close()
    }
    Shortcut {
        sequence: "Ctrl+1"
        onActivated: pages.currentIndex = 0
    }
    Shortcut {
        sequence: "Ctrl+2"
        onActivated: pages.currentIndex = 1
    }
    Shortcut {
        sequence: "Ctrl+3"
        onActivated: pages.currentIndex = 2
    }
    Shortcut {
        sequence: "Ctrl+R"
        onActivated: Ctl.rescan()
    }
    // The same commit the apply button performs, for when auto-apply is off.
    Shortcut {
        sequence: "Ctrl+Return"
        enabled: Ctl.connected && Ctl.pending
        onActivated: Ctl.apply()
    }
    Shortcut {
        sequence: "F11"
        onActivated: window.maximized ? window.showNormal() : window.showMaximized()
    }

    // ---- protocol log model ------------------------------------------------
    ListModel {
        id: logModel

        // Refilled from the backend's backlog: on startup, because the device is
        // opened long before this view exists and those first lines are exactly
        // the ones worth seeing, and on every filter change.
        function rebuild() {
            clear();
            const lines = Ctl.filteredLog(errorsOnlyButton.on, filterField.text);
            for (let i = 0; i < lines.length; ++i)
                append(lines[i]);
        }

        Component.onCompleted: rebuild()
    }

    Connections {
        target: Ctl
        function onLogLine(timestamp, text, isError) {
            // A line that the current filter hides must not appear just because
            // it arrived while the filter was on.
            if (!Ctl.logLineMatches(text, isError, errorsOnlyButton.on, filterField.text))
                return;
            logModel.append({ timestamp: timestamp, body: text, isError: isError });
            // Mirrors the cap the backend keeps on its own backlog.
            while (logModel.count > 800)
                logModel.remove(0);
        }
    }

    // Native picker on purpose: a file chooser is one of the few places where
    // the desktop's own dialog beats anything drawn in here.
    FileDialog {
        id: logSaveDialog
        title: "导出协议日志"
        fileMode: FileDialog.SaveFile
        nameFilters: ["文本文件 (*.txt)", "所有文件 (*)"]
        currentFile: "file://" + StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
                     + "/GigabyteRGBController-log.txt"
        onAccepted: Ctl.exportLog(selectedFile, errorsOnlyButton.on, filterField.text)
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

        onDismissed: show = false

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

    // ---- save-as-profile dialog --------------------------------------------
    ModalCard {
        id: saveProfileDialog

        title: "另存为方案"
        subtitle: "把当前八个区域的设置存成一个可随时切回的方案。用已有的名字保存会覆盖它。"

        function open() {
            profileField.text = "";
            show = true;
            profileField.forceActiveFocus();
        }

        function accept() {
            if (profileField.text.trim() === "")
                return;
            Ctl.saveProfileAs(profileField.text);
            show = false;
        }

        onDismissed: show = false

        Rectangle {
            width: parent.width
            height: 34
            radius: Theme.radiusSmall
            color: Theme.sunken
            border.width: 1
            border.color: profileField.activeFocus ? Theme.accent : Theme.border

            TextInput {
                id: profileField
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                verticalAlignment: Text.AlignVCenter
                color: Theme.text
                font.pixelSize: Theme.fontBody
                selectByMouse: true
                maximumLength: 24
                onAccepted: saveProfileDialog.accept()
            }
        }

        Row {
            spacing: 8
            anchors.right: parent.right

            PillButton {
                text: "取消"
                onClicked: saveProfileDialog.show = false
            }
            PillButton {
                text: "保存"
                primary: true
                enabled: profileField.text.trim() !== ""
                onClicked: saveProfileDialog.accept()
            }
        }
    }

    // ---- delete-profile confirmation ---------------------------------------
    ModalCard {
        id: deleteProfileDialog
        property string name: ""

        title: "删除方案「" + name + "」？"
        subtitle: "只删掉这份保存的快照，当前灯效不受影响。"

        function open(n) {
            name = n;
            show = true;
        }

        onDismissed: show = false

        Row {
            spacing: 8
            anchors.right: parent.right

            PillButton {
                text: "取消"
                onClicked: deleteProfileDialog.show = false
            }
            PillButton {
                text: "删除"
                danger: true
                onClicked: {
                    Ctl.deleteProfile(deleteProfileDialog.name);
                    deleteProfileDialog.show = false;
                }
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

        // Escape means "stop" once the probe is running: the zones are lit by
        // the wizard at that point and cancelling is what puts them back.
        onDismissed: {
            confirming = false;
            if (Ctl.detecting)
                Ctl.cancelDetection();
        }

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
