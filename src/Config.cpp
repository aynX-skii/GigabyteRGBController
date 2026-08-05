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

} // namespace

QString Config::path()
{
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
    s.sync();
}

bool Config::load(ZoneSetting zones[RgbFusion2::kZoneCount])
{
    if (!QFileInfo::exists(path()))
        return false;

    QSettings s(path(), QSettings::IniFormat);

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
    return true;
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

bool Config::apply(RgbFusion2 &rgb, const ZoneSetting zones[RgbFusion2::kZoneCount],
                   QString *error)
{
    int staged = 0;
    for (int i = 0; i < RgbFusion2::kZoneCount; ++i) {
        const ZoneSetting &z = zones[i];
        if (!z.managed)
            continue;
        if (!rgb.setZone(i, z.mode, z.colour, z.brightness, z.speed,
                         z.minBrightness, error))
            return false;
        ++staged;
    }
    if (staged == 0) {
        if (error)
            *error = QStringLiteral("配置里没有已保存的区域");
        return false;
    }
    return rgb.apply(error);
}
