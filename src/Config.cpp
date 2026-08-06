#include "Config.h"

#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace {

QString modeKey(RgbFusion2::Mode m)
{
    switch (m) {
    case RgbFusion2::Mode::Off:         return QStringLiteral("off");
    case RgbFusion2::Mode::Static:      return QStringLiteral("static");
    case RgbFusion2::Mode::Breathing:   return QStringLiteral("breathing");
    case RgbFusion2::Mode::Flash:       return QStringLiteral("flash");
    case RgbFusion2::Mode::DoubleFlash: return QStringLiteral("dflash");
    case RgbFusion2::Mode::ColorCycle:  return QStringLiteral("cycle");
    }
    return QStringLiteral("static");
}

RgbFusion2::Mode modeFromKey(const QString &s)
{
    if (s == QLatin1String("off"))       return RgbFusion2::Mode::Off;
    if (s == QLatin1String("breathing")) return RgbFusion2::Mode::Breathing;
    if (s == QLatin1String("flash"))     return RgbFusion2::Mode::Flash;
    if (s == QLatin1String("dflash"))    return RgbFusion2::Mode::DoubleFlash;
    if (s == QLatin1String("cycle"))     return RgbFusion2::Mode::ColorCycle;
    return RgbFusion2::Mode::Static;
}

// The zone block is written both at the top level (the live state) and inside
// each profile group, so the field list lives in one place.
void writeZones(QSettings &s, const ZoneSetting zones[RgbFusion2::kZoneCount])
{
    for (int i = 0; i < RgbFusion2::kZoneCount; ++i) {
        const ZoneSetting &z = zones[i];
        s.beginGroup(QStringLiteral("zone%1").arg(i));
        s.setValue(QStringLiteral("mode"), modeKey(z.mode));
        s.setValue(QStringLiteral("colour"), z.colour.name());
        s.setValue(QStringLiteral("brightness"), z.brightness);
        s.setValue(QStringLiteral("minBrightness"), z.minBrightness);
        s.setValue(QStringLiteral("speed"), static_cast<int>(z.speed));
        s.setValue(QStringLiteral("managed"), z.managed);
        s.setValue(QStringLiteral("connected"), z.connected);
        s.setValue(QStringLiteral("probed"), z.probed);
        s.setValue(QStringLiteral("name"), z.name);
        s.endGroup();
    }
}

void readZones(QSettings &s, ZoneSetting zones[RgbFusion2::kZoneCount])
{
    for (int i = 0; i < RgbFusion2::kZoneCount; ++i) {
        ZoneSetting &z = zones[i];
        s.beginGroup(QStringLiteral("zone%1").arg(i));
        z.mode   = modeFromKey(s.value(QStringLiteral("mode"),
                                       QStringLiteral("static")).toString());
        const QColor c(s.value(QStringLiteral("colour")).toString());
        if (c.isValid())
            z.colour = c;
        z.brightness    = s.value(QStringLiteral("brightness"), 90).toInt();
        z.minBrightness = s.value(QStringLiteral("minBrightness"), 0).toInt();
        z.speed = static_cast<RgbFusion2::Speed>(
            qBound(0, s.value(QStringLiteral("speed"), 2).toInt(), 5));
        z.managed   = s.value(QStringLiteral("managed"), false).toBool();
        z.connected = s.value(QStringLiteral("connected"), false).toBool();
        z.probed    = s.value(QStringLiteral("probed"), false).toBool();
        z.name      = s.value(QStringLiteral("name")).toString();
        s.endGroup();
    }
}

// Profiles live in numbered groups with the name as a key inside, rather than
// in the group name itself: a profile may be called anything, and INI group
// names cannot hold everything a user might type.
const QString kProfilePrefix = QStringLiteral("profile");

QString profileGroupFor(QSettings &s, const QString &name)
{
    const QStringList groups = s.childGroups();
    for (const QString &g : groups) {
        if (!g.startsWith(kProfilePrefix))
            continue;
        if (s.value(g + QStringLiteral("/name")).toString() == name)
            return g;
    }
    return QString();
}

} // namespace

void ZoneSetting::setMode(RgbFusion2::Mode m)
{
    mode    = m;
    managed = true;

    const int cap = RgbFusion2::maxBrightness(m);
    if (cap > 0 && brightness > cap)
        brightness = cap;
    // The floor of a pulsing effect can never sit above its ceiling; clamping
    // it here means no caller has to remember the ordering.
    if (minBrightness > brightness)
        minBrightness = brightness;
}

namespace {
QString g_pathOverride;
}

void Config::setPath(const QString &file)
{
    g_pathOverride = file;
}

QString Config::path()
{
    if (!g_pathOverride.isEmpty())
        return g_pathOverride;

    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    // Note: this moved from "gcc-linux" with the rename to GigabyteRGBController.
    // Configs written under the old directory are not read; users who ran the
    // probe wizard before the rename must move the file or re-run the wizard.
    return dir + QStringLiteral("/GigabyteRGBController/zones.ini");
}

void Config::save(const ZoneSetting zones[RgbFusion2::kZoneCount])
{
    QSettings s(path(), QSettings::IniFormat);
    s.setValue(QStringLiteral("version"), 1);
    writeZones(s, zones);
    s.sync();
}

bool Config::load(ZoneSetting zones[RgbFusion2::kZoneCount])
{
    if (!QFileInfo::exists(path()))
        return false;

    QSettings s(path(), QSettings::IniFormat);
    readZones(s, zones);
    return true;
}

// ---- profiles --------------------------------------------------------------

QStringList Config::profileNames()
{
    if (!QFileInfo::exists(path()))
        return {};

    QSettings s(path(), QSettings::IniFormat);
    QStringList out;
    // childGroups() comes back sorted, and profile0..N sorts as text - which is
    // wrong at ten profiles, but the order only has to be stable, not numeric.
    const QStringList groups = s.childGroups();
    for (const QString &g : groups) {
        if (!g.startsWith(kProfilePrefix))
            continue;
        const QString name = s.value(g + QStringLiteral("/name")).toString();
        if (!name.isEmpty())
            out << name;
    }
    return out;
}

void Config::saveProfile(const QString &name,
                         const ZoneSetting zones[RgbFusion2::kZoneCount])
{
    if (name.isEmpty())
        return;

    QSettings s(path(), QSettings::IniFormat);

    QString group = profileGroupFor(s, name);
    if (group.isEmpty()) {
        // First free index, so deleting a profile in the middle does not leave
        // a gap that the next save skips over.
        int n = 0;
        while (s.childGroups().contains(kProfilePrefix + QString::number(n)))
            ++n;
        group = kProfilePrefix + QString::number(n);
    }

    s.beginGroup(group);
    s.setValue(QStringLiteral("name"), name);
    writeZones(s, zones);
    s.endGroup();
    s.sync();
}

bool Config::loadProfile(const QString &name, ZoneSetting zones[RgbFusion2::kZoneCount])
{
    if (!QFileInfo::exists(path()))
        return false;

    QSettings s(path(), QSettings::IniFormat);
    const QString group = profileGroupFor(s, name);
    if (group.isEmpty())
        return false;

    s.beginGroup(group);
    readZones(s, zones);
    s.endGroup();
    return true;
}

void Config::removeProfile(const QString &name)
{
    QSettings s(path(), QSettings::IniFormat);
    const QString group = profileGroupFor(s, name);
    if (group.isEmpty())
        return;
    s.remove(group);
    if (s.value(QStringLiteral("activeProfile")).toString() == name)
        s.remove(QStringLiteral("activeProfile"));
    s.sync();
}

QString Config::activeProfile()
{
    if (!QFileInfo::exists(path()))
        return {};
    QSettings s(path(), QSettings::IniFormat);
    return s.value(QStringLiteral("activeProfile")).toString();
}

void Config::setActiveProfile(const QString &name)
{
    QSettings s(path(), QSettings::IniFormat);
    if (name.isEmpty())
        s.remove(QStringLiteral("activeProfile"));
    else
        s.setValue(QStringLiteral("activeProfile"), name);
    s.sync();
}

void Config::saveCustomColours(const QVector<QColor> &colours)
{
    QSettings s(path(), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("custom"));
    for (int i = 0; i < kCustomColourSlots; ++i) {
        const QColor c = i < colours.size() ? colours.at(i) : QColor();
        // An invalid slot is an empty one; write it blank rather than dropping
        // the key, so slot indices stay stable.
        s.setValue(QStringLiteral("slot%1").arg(i),
                   c.isValid() ? c.name() : QString());
    }
    s.endGroup();
    s.sync();
}

QVector<QColor> Config::loadCustomColours()
{
    QVector<QColor> out(kCustomColourSlots);
    if (!QFileInfo::exists(path()))
        return out;

    QSettings s(path(), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("custom"));
    for (int i = 0; i < kCustomColourSlots; ++i) {
        const QString v = s.value(QStringLiteral("slot%1").arg(i)).toString();
        if (!v.isEmpty())
            out[i] = QColor(v);
    }
    s.endGroup();
    return out;
}

void Config::saveWindow(const WindowState &w)
{
    QSettings s(path(), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("window"));
    // A maximised window's own geometry is the screen, which is not what should
    // come back on restore - callers pass the last windowed rectangle instead.
    if (w.geometry.isValid()) {
        s.setValue(QStringLiteral("x"), w.geometry.x());
        s.setValue(QStringLiteral("y"), w.geometry.y());
        s.setValue(QStringLiteral("width"), w.geometry.width());
        s.setValue(QStringLiteral("height"), w.geometry.height());
    }
    s.setValue(QStringLiteral("maximized"), w.maximized);
    s.endGroup();
    s.sync();
}

Config::WindowState Config::loadWindow()
{
    WindowState w;
    if (!QFileInfo::exists(path()))
        return w;

    QSettings s(path(), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("window"));
    const int width  = s.value(QStringLiteral("width"), 0).toInt();
    const int height = s.value(QStringLiteral("height"), 0).toInt();
    if (width > 0 && height > 0) {
        w.geometry = QRect(s.value(QStringLiteral("x"), 0).toInt(),
                           s.value(QStringLiteral("y"), 0).toInt(),
                           width, height);
    }
    w.maximized = s.value(QStringLiteral("maximized"), false).toBool();
    s.endGroup();
    return w;
}

bool Config::apply(RgbFusion2 &rgb, const ZoneSetting zones[RgbFusion2::kZoneCount],
                   QString *error)
{
    QVector<int> managed;
    for (int i = 0; i < RgbFusion2::kZoneCount; ++i) {
        if (zones[i].managed)
            managed.append(i);
    }
    if (managed.isEmpty()) {
        if (error)
            *error = QStringLiteral("配置里没有已保存的区域");
        return false;
    }

    // This is the path a cold boot takes, and out of a cold boot the controller
    // is still running the BIOS's own effects - staging an effect on top of one
    // of those does not reliably displace it. Clear the registers first.
    if (!rgb.takeOverZones(managed, error))
        return false;

    // Twice, because there is no way to read a zone back and confirm: the
    // controller silently drops the occasional feature report when it is busy,
    // which right after boot leaves one zone stuck on whatever it was showing.
    // Restoring runs once per boot, so a second identical pass is free.
    for (int pass = 0; pass < 2; ++pass) {
        for (int i : managed) {
            const ZoneSetting &z = zones[i];
            if (!rgb.setZone(i, z.mode, z.colour, z.brightness, z.speed,
                             z.minBrightness, error))
                return false;
        }
        if (!rgb.apply(error))
            return false;
    }
    return true;
}
