// Persistence for the per-zone lighting setup.
//
// The controller keeps its settings across warm reboots but loses them on a
// full power cycle, and there is no vendor service on Linux to put them back.
// Saving the last applied state lets `GigabyteRGBController --restore` (wired
// to a systemd user unit) reapply it at login.
#pragma once

#include "RgbFusion2.h"

#include <QColor>
#include <QString>
#include <QVector>

struct ZoneSetting {
    RgbFusion2::Mode  mode          = RgbFusion2::Mode::Static;
    QColor            colour        = QColor(255, 0, 0);
    int               brightness    = 90;
    int               minBrightness = 0;
    RgbFusion2::Speed speed         = RgbFusion2::Speed::Normal;

    // False until this zone has actually been set by the user. The controller
    // offers no way to read a zone's current effect back, so restoring a zone
    // we never wrote would clobber it with invented defaults - `apply()`
    // therefore skips unmanaged zones.
    bool managed = false;

    // Whether this zone drives any physical LEDs on this board. The protocol
    // always exposes eight, but most boards populate only a few and the
    // controller happily ACKs writes to empty ones - so this can only be
    // established by lighting each zone and looking. Populated by the
    // detection wizard, or toggled by hand.
    bool connected = false;

    // True once the user has actually answered for this zone, so the UI can
    // tell "known to be empty" apart from "never checked".
    bool probed = false;

    // Optional label, e.g. "IO 护罩". Empty means "区域 N".
    QString name;
};

namespace Config {

// ~/.config/GigabyteRGBController/zones.ini (honours XDG_CONFIG_HOME).
QString path();

void save(const ZoneSetting zones[RgbFusion2::kZoneCount]);

// Returns false when no config has been written yet, leaving `zones` at its
// defaults.
bool load(ZoneSetting zones[RgbFusion2::kZoneCount]);

// Stages every zone and commits once.
bool apply(RgbFusion2 &rgb, const ZoneSetting zones[RgbFusion2::kZoneCount],
           QString *error = nullptr);

// The GUI's user-defined colour swatches. Stored in the same file under
// [custom]; kept apart from the zone state because they are a UI convenience
// and must survive `--set`, which rewrites only the effect fields.
inline constexpr int kCustomColourSlots = 8;

void          saveCustomColours(const QVector<QColor> &colours);
QVector<QColor> loadCustomColours();

} // namespace Config
