import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import GigabyteRGBController

// The window's own title bar. The window carries Qt.FramelessWindowHint, so
// there is no system decoration: dragging, maximising and closing all start
// here. Left transparent so the window background - and therefore its rounded
// top corners - show through.
Item {
    id: root

    // `var`, not `Window`: the attached property hands back the
    // QQuickWindow itself, which is not the QML Window type.
    readonly property var win: Window.window
    readonly property bool maximized: win && win.visibility === Window.Maximized

    property string title
    property string version

    implicitHeight: 40

    function toggleMaximized() {
        if (!win)
            return;
        if (maximized)
            win.showNormal();
        else
            win.showMaximized();
    }

    // The move is handed to the compositor rather than driven by setting x/y:
    // that is the only form of window dragging Wayland allows, and it gets the
    // snap-to-edge behaviour for free.
    DragHandler {
        target: null
        onActiveChanged: if (active && root.win) root.win.startSystemMove()
    }

    // DragThreshold lets the drag above win as soon as the pointer moves, so a
    // double click still has to be a stationary one.
    TapHandler {
        gesturePolicy: TapHandler.DragThreshold
        onDoubleTapped: root.toggleMaximized()
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.pad
        anchors.rightMargin: 6
        spacing: 10

        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: 12
            Layout.preferredHeight: 12
            radius: 3
            color: Theme.accent
        }

        Text {
            Layout.alignment: Qt.AlignVCenter
            Layout.maximumWidth: root.width - 260
            text: root.title
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
            elide: Text.ElideRight
        }

        Text {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            text: root.version ? "v" + root.version : ""
            color: Theme.textFaint
            font.pixelSize: 11
            elide: Text.ElideRight
        }

        Row {
            Layout.alignment: Qt.AlignVCenter
            spacing: 2

            WindowButton {
                glyph: "min"
                onClicked: if (root.win) root.win.showMinimized()
            }
            WindowButton {
                glyph: root.maximized ? "restore" : "max"
                onClicked: root.toggleMaximized()
            }
            WindowButton {
                glyph: "close"
                destructive: true
                onClicked: if (root.win) root.win.close()
            }
        }
    }
}
