pragma Singleton

import QtQuick

// One place for every colour and metric in the UI. GIGABYTE CONTROL CENTER is a
// dark, flat, orange-accented interface; this mirrors that character without
// pretending to be the real thing.
QtObject {
    // ---- surfaces ----------------------------------------------------------
    readonly property color background: "#141619"
    readonly property color card:       "#1e2126"
    readonly property color cardAlt:    "#24272d"
    readonly property color sunken:     "#17191d"
    readonly property color border:     "#31353c"
    readonly property color borderSoft: "#282c32"

    // ---- ink ---------------------------------------------------------------
    readonly property color text:     "#e8eaed"
    readonly property color textDim:  "#8d939b"
    readonly property color textFaint:"#5f656d"

    // ---- accents -----------------------------------------------------------
    readonly property color accent:     "#f36d21"   // AORUS orange
    readonly property color accentSoft: "#f36d2133"
    readonly property color danger:     "#e5534b"
    readonly property color ok:         "#3fb950"

    // ---- metrics -----------------------------------------------------------
    readonly property int radius:      10
    readonly property int radiusSmall: 6
    readonly property int pad:         16
    readonly property int gap:         12

    readonly property int fontSmall:  12
    readonly property int fontBody:   13
    readonly property int fontTitle:  15

    readonly property int anim: 140
}
