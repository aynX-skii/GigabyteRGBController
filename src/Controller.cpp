#include "Controller.h"

#include <QDateTime>
#include <QVariantMap>

namespace {

// Presentation order for the effect rail: the useful effects first, "off" last.
// The enum's own order starts with Off because that is the protocol's natural
// zero, which is not what you want at the top of a picker.
const RgbFusion2::Mode kRailOrder[] = {
    RgbFusion2::Mode::Static,      RgbFusion2::Mode::Breathing,
    RgbFusion2::Mode::Flash,       RgbFusion2::Mode::DoubleFlash,
    RgbFusion2::Mode::ColorCycle,  RgbFusion2::Mode::Off,
};

// Icon keys the QML side draws; kept here so the rail stays data-driven.
const char *iconFor(RgbFusion2::Mode m)
{
    switch (m) {
    case RgbFusion2::Mode::Static:      return "sun";
    case RgbFusion2::Mode::Breathing:   return "wave";
    case RgbFusion2::Mode::Flash:       return "spark";
    case RgbFusion2::Mode::DoubleFlash: return "spark2";
    case RgbFusion2::Mode::ColorCycle:  return "cycle";
    case RgbFusion2::Mode::Off:         return "off";
    }
    return "sun";
}

// One line of help per effect, shown under the settings panel heading.
QString hintFor(RgbFusion2::Mode m)
{
    switch (m) {
    case RgbFusion2::Mode::Static:
        return QStringLiteral("固定颜色常亮，不随时间变化。");
    case RgbFusion2::Mode::Breathing:
        return QStringLiteral("在最小与最大亮度之间平滑起伏。");
    case RgbFusion2::Mode::Flash:
        return QStringLiteral("每个周期闪一次，其余时间熄灭。");
    case RgbFusion2::Mode::DoubleFlash:
        return QStringLiteral("每个周期连闪两次，其余时间熄灭。");
    case RgbFusion2::Mode::ColorCycle:
        return QStringLiteral("由控制器自行循环色相，不使用所选颜色。");
    case RgbFusion2::Mode::Off:
        return QStringLiteral("熄灭该区域。设置会被保留，重新选择效果即可恢复。");
    }
    return QString();
}

const QColor kPresets[] = {
    QColor(255,   0,   0), QColor(255, 213,   0), QColor(  0, 255,  60),
    QColor(  0, 211, 255), QColor(  0,  80, 255), QColor(180,   0, 255),
    QColor(255, 102,   0), QColor(255, 255, 255),
};

} // namespace

Controller::Controller(QObject *parent)
    : QObject(parent)
    , m_customColours(Config::kCustomColourSlots)
{
    connect(&m_rgb, &RgbFusion2::traffic, this, &Controller::onTraffic);
    connect(&m_lamp, &LampArray::traffic, this, &Controller::onTraffic);

    // Coalesces a slider drag into a single pair of HID transactions.
    m_flushTimer.setSingleShot(true);
    m_flushTimer.setInterval(120);
    connect(&m_flushTimer, &QTimer::timeout, this, &Controller::flush);

    if (Config::load(m_zones)) {
        log(QStringLiteral("已载入配置 %1").arg(Config::path()));
        // A full power cycle wipes the controller, and it offers no way to read
        // its current effects back - so a loaded config counts as pending until
        // it has actually been pushed, otherwise the apply button would be
        // greyed out exactly when it is needed most.
        for (const ZoneSetting &z : m_zones) {
            if (z.managed) {
                m_pending = true;
                break;
            }
        }
    }
    m_customColours = Config::loadCustomColours();
    if (m_customColours.size() != Config::kCustomColourSlots)
        m_customColours.resize(Config::kCustomColourSlots);

    connectDevice();
    connectLampArray();
}

// ---- device ----------------------------------------------------------------

void Controller::connectDevice()
{
    m_rgb.close();

    QString error;
    if (!m_rgb.openFirstDevice(&error)) {
        setStatus(error, true);
        emit deviceChanged();
        return;
    }
    if (!m_rgb.initialize(&error)) {
        setStatus(QStringLiteral("已打开 %1，但初始化失败：%2")
                      .arg(m_rgb.devicePath(), error), true);
        emit deviceChanged();
        return;
    }

    setStatus(QStringLiteral("RGB Fusion 接口就绪。"));
    emit deviceChanged();
}

QString Controller::deviceName() const
{
    if (!m_rgb.isOpen())
        return QStringLiteral("未连接");
    const QString n = m_rgb.deviceInfo().productName;
    return n.isEmpty() ? QStringLiteral("(未命名控制器)") : n;
}

QString Controller::deviceDetail() const
{
    if (!m_rgb.isOpen())
        return QStringLiteral("没有找到 RGB Fusion 接口");

    const RgbFusion2::DeviceInfo &i = m_rgb.deviceInfo();
    return QStringLiteral("固件 %1  ·  芯片 0x%2  ·  %3")
        .arg(i.firmwareVersion)
        .arg(i.chipId, 0, 16)
        .arg(m_rgb.devicePath());
}

void Controller::rescan()
{
    connectDevice();
    connectLampArray();
}

// ---- zones -----------------------------------------------------------------

QVariantList Controller::zones() const
{
    QVariantList out;
    for (int i = 0; i < RgbFusion2::kZoneCount; ++i) {
        const ZoneSetting &z = m_zones[i];
        QVariantMap m;
        m[QStringLiteral("index")]     = i;
        m[QStringLiteral("name")]      = z.name;
        m[QStringLiteral("label")]     = z.name.isEmpty()
                                             ? QStringLiteral("区域 %1").arg(i + 1)
                                             : z.name;
        m[QStringLiteral("colour")]    = z.colour;
        m[QStringLiteral("lit")]       = z.mode != RgbFusion2::Mode::Off;
        m[QStringLiteral("connected")] = z.connected;
        m[QStringLiteral("probed")]    = z.probed;
        m[QStringLiteral("managed")]   = z.managed;
        m[QStringLiteral("modeName")]  = RgbFusion2::modeName(z.mode);
        m[QStringLiteral("command")]   = QStringLiteral("0x%1")
                                             .arg(0x20 + i, 2, 16, QLatin1Char('0'));
        out << m;
    }
    return out;
}

void Controller::setSelectedZone(int zone)
{
    const int clamped = zone < 0 ? -1 : qMin(zone, RgbFusion2::kZoneCount - 1);
    if (clamped == m_selectedZone)
        return;
    m_selectedZone = clamped;
    emit selectedZoneChanged();
    emit settingsChanged();
}

QString Controller::selectionLabel() const
{
    if (m_selectedZone < 0)
        return QStringLiteral("全部区域");
    const QString &n = m_zones[m_selectedZone].name;
    return n.isEmpty() ? QStringLiteral("区域 %1").arg(m_selectedZone + 1) : n;
}

int Controller::representativeZone() const
{
    if (m_selectedZone >= 0)
        return m_selectedZone;
    for (int i = 0; i < RgbFusion2::kZoneCount; ++i) {
        if (m_zones[i].connected)
            return i;
    }
    return 0;
}

template <typename F>
void Controller::forSelectedZones(F fn)
{
    if (m_selectedZone < 0) {
        for (auto &z : m_zones)
            fn(z);
    } else {
        fn(m_zones[m_selectedZone]);
    }
}

void Controller::renameZone(int zone, const QString &name)
{
    if (zone < 0 || zone >= RgbFusion2::kZoneCount)
        return;
    m_zones[zone].name = name.trimmed();
    Config::save(m_zones);
    emit zonesChanged();
    emit selectedZoneChanged();
    setStatus(m_zones[zone].name.isEmpty()
                  ? QStringLiteral("区域 %1 已恢复默认名称。").arg(zone + 1)
                  : QStringLiteral("区域 %1 已命名为「%2」。")
                        .arg(zone + 1).arg(m_zones[zone].name));
}

void Controller::setZoneConnected(int zone, bool connected)
{
    if (zone < 0 || zone >= RgbFusion2::kZoneCount)
        return;
    m_zones[zone].connected = connected;
    m_zones[zone].probed    = true;
    Config::save(m_zones);
    emit zonesChanged();
}

// ---- effect catalogue ------------------------------------------------------

QVariantList Controller::effects() const
{
    QVariantList out;
    for (RgbFusion2::Mode m : kRailOrder) {
        QVariantMap e;
        e[QStringLiteral("mode")]       = static_cast<int>(m);
        e[QStringLiteral("name")]       = RgbFusion2::modeName(m);
        e[QStringLiteral("icon")]       = QString::fromLatin1(iconFor(m));
        e[QStringLiteral("hint")]       = hintFor(m);
        e[QStringLiteral("usesColour")] = RgbFusion2::modeUsesColor(m);
        e[QStringLiteral("usesSpeed")]  = RgbFusion2::modeUsesSpeed(m);
        e[QStringLiteral("maxBright")]  = RgbFusion2::maxBrightness(m);
        out << e;
    }
    return out;
}

// ---- current settings ------------------------------------------------------

int    Controller::mode() const { return static_cast<int>(m_zones[representativeZone()].mode); }
QColor Controller::colour() const { return m_zones[representativeZone()].colour; }
int    Controller::brightness() const { return m_zones[representativeZone()].brightness; }
int    Controller::minBrightness() const { return m_zones[representativeZone()].minBrightness; }
int    Controller::speed() const { return static_cast<int>(m_zones[representativeZone()].speed); }

bool Controller::usesColour() const
{
    return RgbFusion2::modeUsesColor(m_zones[representativeZone()].mode);
}

bool Controller::usesSpeed() const
{
    return RgbFusion2::modeUsesSpeed(m_zones[representativeZone()].mode);
}

int Controller::brightnessCap() const
{
    const int cap = RgbFusion2::maxBrightness(m_zones[representativeZone()].mode);
    return cap > 0 ? cap : 100;
}

QString Controller::speedName() const
{
    return RgbFusion2::speedName(m_zones[representativeZone()].speed);
}

void Controller::setMode(int mode)
{
    if (mode < 0 || mode > static_cast<int>(RgbFusion2::Mode::ColorCycle))
        return;
    const auto m = static_cast<RgbFusion2::Mode>(mode);
    if (m == m_zones[representativeZone()].mode)
        return;

    // Each mode has its own brightness ceiling; carrying a higher value across
    // would silently clip at the controller.
    const int cap = RgbFusion2::maxBrightness(m);
    forSelectedZones([&](ZoneSetting &z) {
        z.mode    = m;
        z.managed = true;
        if (cap > 0 && z.brightness > cap)
            z.brightness = cap;
        if (z.minBrightness > z.brightness)
            z.minBrightness = z.brightness;
    });

    emit settingsChanged();
    emit zonesChanged();
    scheduleFlush();
}

void Controller::setColour(const QColor &c)
{
    if (!c.isValid() || c == m_zones[representativeZone()].colour)
        return;
    forSelectedZones([&](ZoneSetting &z) { z.colour = c; z.managed = true; });
    emit settingsChanged();
    emit zonesChanged();
    scheduleFlush();
}

void Controller::setBrightness(int v)
{
    const int clamped = qBound(0, v, brightnessCap());
    if (clamped == m_zones[representativeZone()].brightness)
        return;
    forSelectedZones([&](ZoneSetting &z) {
        z.brightness = clamped;
        z.managed    = true;
        if (z.minBrightness > clamped)
            z.minBrightness = clamped;
    });
    emit settingsChanged();
    scheduleFlush();
}

void Controller::setMinBrightness(int v)
{
    const int clamped = qBound(0, v, brightness());
    if (clamped == m_zones[representativeZone()].minBrightness)
        return;
    forSelectedZones([&](ZoneSetting &z) { z.minBrightness = clamped; z.managed = true; });
    emit settingsChanged();
    scheduleFlush();
}

void Controller::setSpeed(int v)
{
    const int clamped = qBound(0, v, 5);
    if (clamped == static_cast<int>(m_zones[representativeZone()].speed))
        return;
    const auto s = static_cast<RgbFusion2::Speed>(clamped);
    forSelectedZones([&](ZoneSetting &z) { z.speed = s; z.managed = true; });
    emit settingsChanged();
    scheduleFlush();
}

void Controller::setAutoApply(bool on)
{
    if (on == m_autoApply)
        return;
    m_autoApply = on;
    emit autoApplyChanged();
}

// ---- applying --------------------------------------------------------------

void Controller::setPending(bool on)
{
    if (on == m_pending)
        return;
    m_pending = on;
    emit pendingChanged();
}

void Controller::scheduleFlush()
{
    setPending(true);
    if (m_autoApply && m_rgb.isOpen() && !m_detecting)
        m_flushTimer.start();
}

void Controller::flush()
{
    if (!m_rgb.isOpen() || m_detecting)
        return;

    QString error;
    for (int i = 0; i < RgbFusion2::kZoneCount; ++i) {
        if (m_selectedZone >= 0 && i != m_selectedZone)
            continue;
        const ZoneSetting &z = m_zones[i];
        if (!m_rgb.setZone(i, z.mode, z.colour, z.brightness, z.speed,
                           z.minBrightness, &error)) {
            setStatus(QStringLiteral("区域 %1 设置失败：%2").arg(i + 1).arg(error), true);
            return;
        }
    }
    if (!m_rgb.apply(&error)) {
        setStatus(QStringLiteral("提交失败：%1").arg(error), true);
        return;
    }
    Config::save(m_zones);
    setPending(false);
}

void Controller::apply()
{
    if (!m_rgb.isOpen()) {
        setStatus(QStringLiteral("设备未连接。"), true);
        return;
    }

    m_flushTimer.stop();

    QString error;
    int sent = 0;
    for (int i = 0; i < RgbFusion2::kZoneCount; ++i) {
        if (m_selectedZone >= 0 && i != m_selectedZone)
            continue;
        const ZoneSetting &z = m_zones[i];
        if (!m_rgb.setZone(i, z.mode, z.colour, z.brightness, z.speed,
                           z.minBrightness, &error)) {
            setStatus(QStringLiteral("区域 %1 设置失败：%2").arg(i + 1).arg(error), true);
            return;
        }
        ++sent;
    }
    if (!m_rgb.apply(&error)) {
        setStatus(QStringLiteral("提交失败：%1").arg(error), true);
        return;
    }

    Config::save(m_zones);
    setPending(false);
    emit zonesChanged();
    setStatus(QStringLiteral("已应用 %1 个区域，配置已保存。").arg(sent));
}

void Controller::allOff()
{
    if (!m_rgb.isOpen()) {
        setStatus(QStringLiteral("设备未连接。"), true);
        return;
    }

    m_flushTimer.stop();

    QString error;
    for (int i = 0; i < RgbFusion2::kZoneCount; ++i) {
        m_zones[i].mode    = RgbFusion2::Mode::Off;
        m_zones[i].managed = true;
        if (!m_rgb.setZone(i, RgbFusion2::Mode::Off, QColor(), 0,
                           RgbFusion2::Speed::Normal, 0, &error)) {
            setStatus(QStringLiteral("区域 %1 关闭失败：%2").arg(i + 1).arg(error), true);
            return;
        }
    }
    if (!m_rgb.apply(&error)) {
        setStatus(QStringLiteral("提交失败：%1").arg(error), true);
        return;
    }

    Config::save(m_zones);
    setPending(false);
    emit zonesChanged();
    emit settingsChanged();
    setStatus(QStringLiteral("所有区域已关闭。"));
}

// ---- custom swatches -------------------------------------------------------

QVariantList Controller::customColours() const
{
    QVariantList out;
    for (const QColor &c : m_customColours)
        out << (c.isValid() ? QVariant(c) : QVariant());
    return out;
}

QVariantList Controller::presetColours() const
{
    QVariantList out;
    for (const QColor &c : kPresets)
        out << c;
    return out;
}

void Controller::saveCustomColour(int slot, const QColor &c)
{
    if (slot < 0 || slot >= m_customColours.size() || !c.isValid())
        return;
    m_customColours[slot] = c;
    Config::saveCustomColours(m_customColours);
    emit customColoursChanged();
}

void Controller::clearCustomColour(int slot)
{
    if (slot < 0 || slot >= m_customColours.size())
        return;
    m_customColours[slot] = QColor();
    Config::saveCustomColours(m_customColours);
    emit customColoursChanged();
}

// ---- zone detection wizard -------------------------------------------------

bool Controller::lightOnlyZone(int zone, QString *error)
{
    for (int j = 0; j < RgbFusion2::kZoneCount; ++j) {
        const bool on = (j == zone);
        if (!m_rgb.setZone(j, on ? RgbFusion2::Mode::Static : RgbFusion2::Mode::Off,
                           Qt::white, on ? 90 : 0,
                           RgbFusion2::Speed::Normal, 0, error))
            return false;
    }
    return m_rgb.apply(error);
}

void Controller::beginDetection()
{
    if (!m_rgb.isOpen()) {
        setStatus(QStringLiteral("设备未连接。"), true);
        return;
    }

    m_flushTimer.stop();
    m_detecting  = true;
    m_detectZone = 0;

    QString error;
    if (!lightOnlyZone(0, &error)) {
        m_detecting = false;
        setStatus(QStringLiteral("探测中断：%1").arg(error), true);
    }
    emit detectionChanged();
}

void Controller::answerDetection(bool hasLeds)
{
    if (!m_detecting)
        return;

    m_zones[m_detectZone].connected = hasLeds;
    m_zones[m_detectZone].probed    = true;
    emit zonesChanged();

    if (++m_detectZone >= RgbFusion2::kZoneCount) {
        finishDetection();
        return;
    }

    QString error;
    if (!lightOnlyZone(m_detectZone, &error)) {
        setStatus(QStringLiteral("探测中断：%1").arg(error), true);
        finishDetection();
        return;
    }
    emit detectionChanged();
}

void Controller::cancelDetection()
{
    if (m_detecting)
        finishDetection();
}

void Controller::finishDetection()
{
    m_detecting = false;
    Config::save(m_zones);

    // Put the user's own settings back - the probe overwrote every zone.
    QString error;
    if (Config::apply(m_rgb, m_zones, &error))
        setPending(false);
    else
        log(QStringLiteral("探测结束，未恢复灯效：%1").arg(error));

    QStringList names;
    for (int i = 0; i < RgbFusion2::kZoneCount; ++i) {
        if (m_zones[i].connected)
            names << QString::number(i + 1);
    }

    emit detectionChanged();
    emit zonesChanged();
    emit settingsChanged();
    setStatus(names.isEmpty()
                  ? QStringLiteral("探测完成：未发现有灯的区域。")
                  : QStringLiteral("探测完成：%1 个区域有灯（区域 %2）。")
                        .arg(names.size()).arg(names.join(QStringLiteral("、"))));
}

// ---- LampArray -------------------------------------------------------------

void Controller::connectLampArray()
{
    m_lamp.close();

    QString error;
    if (!m_lamp.openFirstDevice(&error)) {
        m_lampInfo = QStringLiteral("未连接：%1").arg(error);
        emit lampChanged();
        return;
    }
    m_lampInfo = QStringLiteral("已打开 %1，点「读取属性」枚举灯珠。")
                     .arg(m_lamp.devicePath());
    emit lampChanged();
}

void Controller::lampRescan()
{
    if (!m_lamp.isOpen())
        connectLampArray();
    if (!m_lamp.isOpen())
        return;

    QString error;
    if (!m_lamp.queryAttributes(&error)) {
        setStatus(QStringLiteral("读取 LampArray 属性失败：%1").arg(error), true);
        return;
    }

    const LampArray::Attributes &a = m_lamp.attributes();
    m_lampInfo = QStringLiteral("%1  ·  灯数 %2  ·  外框 %3×%4×%5 µm  ·  类型 %6  ·  最小刷新间隔 %7 µs")
                     .arg(m_lamp.devicePath())
                     .arg(a.lampCount)
                     .arg(a.boundingBoxWidthUm)
                     .arg(a.boundingBoxHeightUm)
                     .arg(a.boundingBoxDepthUm)
                     .arg(LampArray::kindName(a.kind))
                     .arg(a.minUpdateIntervalUs);

    emit lampChanged();
    setStatus(QStringLiteral("已枚举 %1 颗灯。").arg(m_lamp.lamps().size()));
}

QVariantList Controller::lamps() const
{
    QVariantList out;
    for (const LampArray::LampInfo &l : m_lamp.lamps()) {
        QVariantMap m;
        m[QStringLiteral("id")]           = l.id;
        m[QStringLiteral("x")]            = l.positionXUm;
        m[QStringLiteral("y")]            = l.positionYUm;
        m[QStringLiteral("z")]            = l.positionZUm;
        m[QStringLiteral("purposes")]     = LampArray::purposeNames(l.purposes);
        m[QStringLiteral("programmable")] = l.isProgrammable;
        out << m;
    }
    return out;
}

void Controller::setLampColour(const QColor &c)
{
    if (!c.isValid() || c == m_lampColour)
        return;
    m_lampColour = c;
    emit lampColourChanged();
}

void Controller::setAutonomousMode(bool on)
{
    if (!m_lamp.isOpen())
        return;

    QString error;
    if (!m_lamp.setAutonomousMode(on, &error)) {
        setStatus(QStringLiteral("设置自主模式失败：%1").arg(error), true);
        emit lampChanged();   // let the switch snap back
        return;
    }
    m_autonomous = on;
    emit lampChanged();
    setStatus(on ? QStringLiteral("已交还板载效果控制（自主模式开）。")
                 : QStringLiteral("已接管灯光控制（自主模式关）。"));
}

void Controller::lampApplyAll()
{
    if (m_lamp.attributes().lampCount == 0) {
        setStatus(QStringLiteral("请先点「读取属性」。"), true);
        return;
    }

    // Host updates only take effect with autonomous mode off.
    if (m_autonomous)
        setAutonomousMode(false);

    QString error;
    if (!m_lamp.setAll(m_lampColour, 255, &error)) {
        setStatus(QStringLiteral("设置失败：%1").arg(error), true);
        return;
    }
    setStatus(QStringLiteral("已将 %1 颗灯设为 %2。")
                  .arg(m_lamp.attributes().lampCount)
                  .arg(m_lampColour.name().toUpper()));
}

// ---- status and log --------------------------------------------------------

void Controller::onTraffic(bool outgoing, const QString &label, const QByteArray &data)
{
    QString text = QStringLiteral("%1 %2")
                       .arg(outgoing ? QStringLiteral("→ 发送") : QStringLiteral("← 接收"),
                            label);
    if (!data.isEmpty()) {
        // These reports are 64 bytes and mostly zero padding; showing all of it
        // buries the handful of bytes that carry meaning.
        int used = data.size();
        while (used > 0 && data.at(used - 1) == char(0))
            --used;

        text += QStringLiteral("  (%1 字节)\n%2")
                    .arg(data.size())
                    .arg(RgbFusion2::hexDump(data.left(used)));
        if (used < data.size())
            text += QStringLiteral("\n… 其余 %1 字节为 0").arg(data.size() - used);
    }
    log(text);
}

void Controller::setStatus(const QString &text, bool isError)
{
    m_status        = text;
    m_statusIsError = isError;
    emit statusChanged();
    if (!text.isEmpty())
        log(text, isError);
}

void Controller::log(const QString &text, bool isError)
{
    const QString ts =
        QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));

    QVariantMap entry;
    entry[QStringLiteral("timestamp")] = ts;
    entry[QStringLiteral("body")]      = text;
    entry[QStringLiteral("isError")]   = isError;
    m_logBacklog.append(entry);

    // The traffic is verbose; keeping a whole session would grow without bound
    // for no benefit.
    while (m_logBacklog.size() > 800)
        m_logBacklog.removeFirst();

    emit logLine(ts, text, isError);
}

void Controller::clearLog()
{
    m_logBacklog.clear();
    // The QML model is cleared alongside this; logging the fact gives the
    // emptied view a visible starting point.
    log(QStringLiteral("日志已清空。"));
}
