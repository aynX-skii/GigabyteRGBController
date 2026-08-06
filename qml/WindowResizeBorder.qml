import QtQuick
import QtQuick.Window
import GigabyteRGBController

// A frameless window gets no resize edges from the compositor, so the eight
// grips are ours to provide. Each one hands the drag straight back to the
// compositor via startSystemResize() - the geometry is never driven from here,
// which is what keeps it working under Wayland.
//
// Meant to be parented to the window overlay so it sits above the title bar
// and the content alike.
Item {
    id: root

    // `var`, not `Window`: the attached property hands back the
    // QQuickWindow itself, which is not the QML Window type.
    readonly property var win: Window.window

    // How far in from the edge a press still counts as a resize.
    property int thickness: 6

    // Corner grips are longer than the edges are thick, so hitting a corner
    // does not need pixel accuracy.
    property int corner: 16

    // A maximised window has no edges to drag, and leaving the grips live
    // would put resize cursors along the screen edges for nothing.
    visible: win && win.visibility === Window.Windowed

    component Grip: Item {
        id: grip

        property int edges: 0
        property int cursor: Qt.ArrowCursor

        readonly property var win: Window.window

        HoverHandler {
            cursorShape: grip.cursor
        }

        // Takes the grab off whatever is underneath: the edges overlap the
        // content, and a resize started on an edge must not be swallowed by a
        // slider or a list that happens to sit there.
        DragHandler {
            target: null
            grabPermissions: PointerHandler.CanTakeOverFromAnything
            onActiveChanged: if (active && grip.win) grip.win.startSystemResize(grip.edges)
        }
    }

    // ---- edges -------------------------------------------------------------
    Grip {
        edges: Qt.TopEdge
        cursor: Qt.SizeVerCursor
        height: root.thickness
        anchors { top: parent.top; left: parent.left; right: parent.right
                  leftMargin: root.corner; rightMargin: root.corner }
    }
    Grip {
        edges: Qt.BottomEdge
        cursor: Qt.SizeVerCursor
        height: root.thickness
        anchors { bottom: parent.bottom; left: parent.left; right: parent.right
                  leftMargin: root.corner; rightMargin: root.corner }
    }
    Grip {
        edges: Qt.LeftEdge
        cursor: Qt.SizeHorCursor
        width: root.thickness
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom
                  topMargin: root.corner; bottomMargin: root.corner }
    }
    Grip {
        edges: Qt.RightEdge
        cursor: Qt.SizeHorCursor
        width: root.thickness
        anchors { right: parent.right; top: parent.top; bottom: parent.bottom
                  topMargin: root.corner; bottomMargin: root.corner }
    }

    // ---- corners -----------------------------------------------------------
    Grip {
        edges: Qt.TopEdge | Qt.LeftEdge
        cursor: Qt.SizeFDiagCursor
        width: root.corner
        height: root.corner
        anchors { top: parent.top; left: parent.left }
    }
    Grip {
        edges: Qt.TopEdge | Qt.RightEdge
        cursor: Qt.SizeBDiagCursor
        width: root.corner
        height: root.corner
        anchors { top: parent.top; right: parent.right }
    }
    Grip {
        edges: Qt.BottomEdge | Qt.LeftEdge
        cursor: Qt.SizeBDiagCursor
        width: root.corner
        height: root.corner
        anchors { bottom: parent.bottom; left: parent.left }
    }
    Grip {
        edges: Qt.BottomEdge | Qt.RightEdge
        cursor: Qt.SizeFDiagCursor
        width: root.corner
        height: root.corner
        anchors { bottom: parent.bottom; right: parent.right }
    }
}
