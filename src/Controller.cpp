#include "Controller.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QTextStream>
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
    m_lampFallback  = Config::lampFallback();
    m_customColours = Config::loadCustomColours();
    if (m_customColours.size() != Config::kCustomColourSlots)
        m_customColours.resize(Config::kCustomColourSlots);

    m_profiles      = Config::profileNames();
    m_activeProfile = Config::activeProfile();
    m_window        = Config::loadWindow();

    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(1000);
    connect(&m_saveTimer, &QTimer::timeout, this, &Controller::saveNow);

    m_watchTimer.setInterval(3000);
    connect(&m_watchTimer, &QTimer::timeout, this, &Controller::watchdogTick);
    m_watchTimer.start();

    // Suspend can cut power to the controller, and it has no command to read
    // its effects back - so logind's resume announcement is the cue to push the
    // saved ones again. Fails quietly where there is no system bus or no
    // logind, which costs nothing but this feature.
    const bool sleepSignal = QDBusConnection::systemBus().connect(
        QStringLiteral("org.freedesktop.login1"),
        QStringLiteral("/org/freedesktop/login1"),
        QStringLiteral("org.freedesktop.login1.Manager"),
        QStringLiteral("PrepareForSleep"),
        this, SLOT(onPrepareForSleep(bool)));
    if (!sleepSignal)
        log(QStringLiteral("未能订阅 logind 的睡眠信号，唤醒后不会自动恢复灯效。"));

    connectDevice();
    connectLampArray();
}

Controller::~Controller()
{
    if (m_saveTimer.isActive())
        saveNow();
}

QString Controller::appVersion() const
{
    return QCoreApplication::applicationVersion();
}

void Controller::scheduleSave()
{
    m_saveTimer.start();
}

void Controller::saveNow()
{
    m_saveTimer.stop();
    Config::save(m_zones);
}

// ---- device ----------------------------------------------------------------

void Controller::connectDevice()
{
    m_rgb.close();
    // A rescan is the one thing that can legitimately follow a power cycle, so
    // it is also the only thing that clears the hang.
    m_wedged = false;

    QString error;
    if (!m_rgb.openFirstDevice(&error)) {
        // Kept so the device bar can say why, not just that. On a first run
        // this string is the whole answer - it carries the udev recipe - and
        // the device bar is where someone looks when the dot is red.
        m_lastOpenError = error;
        setStatus(error, true);
        emit deviceChanged();
        return;
    }
    m_lastOpenError.clear();
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
    if (!m_rgb.isOpen()) {
        return m_lastOpenError.isEmpty()
                   ? QStringLiteral("没有找到 RGB Fusion 接口")
                   : m_lastOpenError;
    }

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

bool Controller::hasManagedZones() const
{
    for (const ZoneSetting &z : m_zones) {
        if (z.managed)
            return true;
    }
    return false;
}

void Controller::watchdogTick()
{
    // The probe drives the device zone by zone and cannot tolerate the handle
    // being pulled out from under it.
    if (m_detecting)
        return;

    if (m_rgb.isOpen()) {
        if (QFileInfo::exists(m_rgb.devicePath()))
            return;

        const QString gone = m_rgb.devicePath();
        m_rgb.close();
        emit deviceChanged();
        // Nothing of the previous state survives in the controller, so whatever
        // is on screen counts as unsent again.
        setPending(hasManagedZones());
        // A hung controller usually takes its node with it a moment later, and
        // "waiting for it to come back" is the wrong thing to leave on screen -
        // it is not coming back on its own. checkWedged() already said so.
        if (!m_wedged)
            setStatus(QStringLiteral("控制器已断开（%1 消失），正在等待重新出现…").arg(gone), true);
        return;
    }

    // Retried quietly: on a machine without the hardware this fires every three
    // seconds forever, and logging each attempt would bury everything else.
    QString error;
    if (!m_rgb.openFirstDevice(&error))
        return;
    if (!m_rgb.initialize(&error)) {
        m_rgb.close();
        return;
    }

    // An INIT that answers means this is a controller that has been through a
    // power cycle, not the hung one wearing the same node.
    m_wedged = false;

    emit deviceChanged();
    reapplySaved(QStringLiteral("控制器已重新连接"));
}

void Controller::reapplySaved(const QString &reason)
{
    if (m_lampFallback) {
        if (!m_lamp.isOpen())
            return;
    } else if (!m_rgb.isOpen() || m_wedged) {
        return;
    }

    if (!hasManagedZones()) {
        setStatus(QStringLiteral("%1：没有已保存的灯效可恢复。").arg(reason));
        return;
    }

    QString error;
    const bool ok = m_lampFallback ? Config::applyViaLamp(m_lamp, m_zones, &error)
                                   : Config::apply(m_rgb, m_zones, &error);
    if (m_autoApply && ok) {
        setPending(false);
        setStatus(QStringLiteral("%1，灯效已恢复。").arg(reason));
        return;
    }

    // With auto-apply off, pushing effects the user did not ask for would be
    // the wrong call - the button lighting up is the whole signal.
    setPending(true);
    setStatus(m_autoApply
                  ? QStringLiteral("%1，但恢复灯效失败：%2").arg(reason, error)
                  : QStringLiteral("%1，点「应用」恢复灯效。").arg(reason),
              m_autoApply);
}

void Controller::onPrepareForSleep(bool sleeping)
{
    if (sleeping) {
        log(QStringLiteral("系统即将进入睡眠。"));
        return;
    }

    // The node can take a moment to come back, and if it went away entirely the
    // watchdog owns the reconnect. This timer covers the other case: the node
    // survived, but the controller lost its state to the power cut.
    QTimer::singleShot(2000, this, [this]() {
        reapplySaved(QStringLiteral("已从睡眠唤醒"));
    });
}

bool Controller::checkWedged()
{
    if (!m_wedged && !RgbFusion2::isFirmwareHang(m_rgb.lastErrno()))
        return false;

    // Restated on every refused write, not just the first: whoever pressed the
    // button wants to know why nothing happened, and the status line by then
    // may be showing something else entirely.
    m_wedged = true;
    m_flushTimer.stop();
    setPending(hasManagedZones());
    setStatus(QStringLiteral(
                  "控制器固件已停止响应，主板断电前无法恢复。"
                  "请关机后切断电源（关闭电源开关或拔掉电源线）约 10 秒再开机；"
                  "重启是不够的，这颗芯片走 +5VSB 供电。"),
              true);
    return true;
}

void Controller::handleIoFailure(const QString &message)
{
    setStatus(message, true);

    // Distinguished from the two cases below because nothing here can fix it:
    // the controller has stopped answering and will keep doing so until the
    // board loses power, so the only useful move is to stop writing and say so.
    if (checkWedged())
        return;

    // A write can fail for a passing reason; only a node that is no longer
    // there means the handle is dead. Dropping it hands the retry to the
    // watchdog instead of leaving every later write to fail the same way.
    if (m_rgb.isOpen() && !QFileInfo::exists(m_rgb.devicePath())) {
        m_rgb.close();
        emit deviceChanged();
        setPending(hasManagedZones());
    }
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

int Controller::selectedZone() const
{
    // Exactly one, or nothing: callers that ask this question - renaming, for
    // one - have no sensible answer for a multi-zone selection.
    int found = -1;
    for (int i = 0; i < RgbFusion2::kZoneCount; ++i) {
        if (!isSelected(i))
            continue;
        if (found >= 0)
            return -1;
        found = i;
    }
    return found;
}

void Controller::setSelection(quint8 mask)
{
    if (mask == 0 || mask == m_selection)
        return;
    m_selection = mask;
    emit selectedZoneChanged();
    emit settingsChanged();
}

void Controller::setSelectedZone(int zone)
{
    if (zone < 0)
        selectAllZones();
    else
        selectOnlyZone(zone);
}

void Controller::selectAllZones()
{
    setSelection(0xFF);
}

void Controller::selectOnlyZone(int zone)
{
    if (zone < 0 || zone >= RgbFusion2::kZoneCount)
        return;
    setSelection(static_cast<quint8>(1u << zone));
}

void Controller::toggleZone(int zone)
{
    if (zone < 0 || zone >= RgbFusion2::kZoneCount)
        return;

    const quint8 bit = static_cast<quint8>(1u << zone);
    // Turning off the last selected zone would leave the editor pointing at
    // nothing; treat it as a no-op rather than as an empty selection.
    if (m_selection == bit)
        return;

    setSelection(static_cast<quint8>(m_selection ^ bit));
}

QString Controller::selectionLabel() const
{
    if (m_selection == 0xFF)
        return QStringLiteral("全部区域");

    const int one = selectedZone();
    if (one >= 0) {
        const QString &n = m_zones[one].name;
        return n.isEmpty() ? QStringLiteral("区域 %1").arg(one + 1) : n;
    }

    int count = 0;
    for (int i = 0; i < RgbFusion2::kZoneCount; ++i)
        count += isSelected(i) ? 1 : 0;
    // No space before the measure word: "应用到3个区域" is the button's whole text.
    return QStringLiteral("%1个区域").arg(count);
}

int Controller::representativeZone() const
{
    // A lit zone says more about what the controls should show than an empty
    // one does, so a connected zone wins over a merely selected one.
    int first = -1;
    for (int i = 0; i < RgbFusion2::kZoneCount; ++i) {
        if (!isSelected(i))
            continue;
        if (first < 0)
            first = i;
        if (m_zones[i].connected)
            return i;
    }
    return first < 0 ? 0 : first;
}

template <typename F>
void Controller::forSelectedZones(F fn)
{
    for (int i = 0; i < RgbFusion2::kZoneCount; ++i) {
        if (isSelected(i))
            fn(m_zones[i]);
    }
}

void Controller::renameZone(int zone, const QString &name)
{
    if (zone < 0 || zone >= RgbFusion2::kZoneCount)
        return;
    m_zones[zone].name = name.trimmed();
    scheduleSave();
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
    scheduleSave();
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

    // ZoneSetting::setMode carries the brightness ceiling with the switch.
    forSelectedZones([&](ZoneSetting &z) { z.setMode(m); });

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
    const bool live = m_lampFallback ? m_lamp.isOpen() : (m_rgb.isOpen() && !m_wedged);
    if (m_autoApply && live && !m_detecting)
        m_flushTimer.start();
}

bool Controller::pushCurrent(QString *error)
{
    if (m_lampFallback)
        return Config::applyViaLamp(m_lamp, m_zones, error);

    for (int i = 0; i < RgbFusion2::kZoneCount; ++i) {
        if (!isSelected(i))
            continue;
        const ZoneSetting &z = m_zones[i];
        if (!m_rgb.setZone(i, z.mode, z.colour, z.brightness, z.speed,
                           z.minBrightness, error)) {
            if (error)
                *error = QStringLiteral("区域 %1 设置失败：%2").arg(i + 1).arg(*error);
            return false;
        }
    }
    if (!m_rgb.apply(error)) {
        if (error)
            *error = QStringLiteral("提交失败：%1").arg(*error);
        return false;
    }
    return true;
}

void Controller::setLampFallback(bool on)
{
    if (on == m_lampFallback)
        return;
    m_lampFallback = on;
    Config::setLampFallback(on);
    emit lampFallbackChanged();

    if (on && !m_lamp.isOpen())
        connectLampArray();

    if (!on) {
        // Leaving the fallback hands the LEDs back to the board's own effect
        // engine. That only does anything once the Fusion path is alive again,
        // but doing it here means one power cycle is all it takes.
        QString error;
        if (m_lamp.isOpen())
            m_lamp.setAutonomousMode(true, &error);
        setStatus(QStringLiteral("已切回硬件效果通道。"));
        setPending(hasManagedZones());
        return;
    }

    QString error;
    if (!pushCurrent(&error)) {
        setStatus(QStringLiteral("已切到 LampArray 通道，但下发失败：%1").arg(error), true);
        setPending(true);
        return;
    }
    setPending(false);
    setStatus(QStringLiteral(
        "已切到 LampArray 通道。八个区会合并成一种颜色，动态效果不可用。"));
}

void Controller::flush()
{
    if (m_detecting || m_wedged)
        return;
    if (m_lampFallback ? !m_lamp.isOpen() : !m_rgb.isOpen())
        return;

    QString error;
    if (!pushCurrent(&error)) {
        handleIoFailure(error);
        return;
    }
    scheduleSave();
    setPending(false);
}

void Controller::apply()
{
    if (m_lampFallback ? !m_lamp.isOpen() : !m_rgb.isOpen()) {
        setStatus(QStringLiteral("设备未连接。"), true);
        return;
    }
    if (!m_lampFallback && checkWedged())
        return;

    m_flushTimer.stop();

    QString error;
    if (!pushCurrent(&error)) {
        handleIoFailure(error);
        return;
    }

    int sent = 0;
    for (int i = 0; i < RgbFusion2::kZoneCount; ++i) {
        if (isSelected(i))
            ++sent;
    }

    scheduleSave();
    setPending(false);
    emit zonesChanged();
    setStatus(m_lampFallback
                  ? QStringLiteral("已通过 LampArray 应用，配置已保存。")
                  : QStringLiteral("已应用 %1 个区域，配置已保存。").arg(sent));
}

void Controller::allOff()
{
    if (m_lampFallback ? !m_lamp.isOpen() : !m_rgb.isOpen()) {
        setStatus(QStringLiteral("设备未连接。"), true);
        return;
    }
    if (!m_lampFallback && checkWedged())
        return;

    m_flushTimer.stop();

    QString error;
    for (int i = 0; i < RgbFusion2::kZoneCount; ++i) {
        // Keeps the stored brightness intact - "off" has a ceiling of 0, and
        // setMode leaves the field alone in that case, which is what makes
        // picking an effect again restore the previous look.
        m_zones[i].setMode(RgbFusion2::Mode::Off);
    }

    if (m_lampFallback) {
        if (!Config::applyViaLamp(m_lamp, m_zones, &error)) {
            handleIoFailure(QStringLiteral("关闭失败：%1").arg(error));
            return;
        }
    } else {
        for (int i = 0; i < RgbFusion2::kZoneCount; ++i) {
            if (!m_rgb.setZone(i, RgbFusion2::Mode::Off, QColor(), 0,
                               RgbFusion2::Speed::Normal, 0, &error)) {
                handleIoFailure(QStringLiteral("区域 %1 关闭失败：%2").arg(i + 1).arg(error));
                return;
            }
        }
        if (!m_rgb.apply(&error)) {
            handleIoFailure(QStringLiteral("提交失败：%1").arg(error));
            return;
        }
    }

    scheduleSave();
    setPending(false);
    emit zonesChanged();
    emit settingsChanged();
    setStatus(QStringLiteral("所有区域已关闭。"));
}

// ---- profiles --------------------------------------------------------------

void Controller::selectProfile(const QString &name)
{
    if (!Config::loadProfile(name, m_zones)) {
        setStatus(QStringLiteral("方案「%1」不存在。").arg(name), true);
        return;
    }

    m_activeProfile = name;
    Config::setActiveProfile(name);
    scheduleSave();

    emit profilesChanged();
    emit zonesChanged();
    emit settingsChanged();

    // Same route as any other edit: auto-apply pushes it, otherwise the apply
    // button lights up and waits.
    setPending(true);
    if (m_autoApply && m_rgb.isOpen() && !m_detecting)
        m_flushTimer.start();

    setStatus(QStringLiteral("已切换到方案「%1」。").arg(name));
}

void Controller::saveProfileAs(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        setStatus(QStringLiteral("方案名不能为空。"), true);
        return;
    }

    const bool existed = m_profiles.contains(trimmed);
    Config::saveProfile(trimmed, m_zones);
    Config::setActiveProfile(trimmed);

    m_activeProfile = trimmed;
    m_profiles      = Config::profileNames();
    emit profilesChanged();

    setStatus(existed ? QStringLiteral("方案「%1」已覆盖。").arg(trimmed)
                      : QStringLiteral("已保存为新方案「%1」。").arg(trimmed));
}

void Controller::updateActiveProfile()
{
    if (m_activeProfile.isEmpty()) {
        setStatus(QStringLiteral("当前没有选中的方案，请先「另存为」。"), true);
        return;
    }
    Config::saveProfile(m_activeProfile, m_zones);
    setStatus(QStringLiteral("方案「%1」已更新为当前设置。").arg(m_activeProfile));
}

void Controller::deleteProfile(const QString &name)
{
    Config::removeProfile(name);

    // Deleting the active profile leaves the live zones exactly as they are -
    // only the name they were saved under goes away.
    if (m_activeProfile == name)
        m_activeProfile.clear();

    m_profiles = Config::profileNames();
    emit profilesChanged();
    setStatus(QStringLiteral("方案「%1」已删除。").arg(name));
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
    scheduleSave();

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

// ---- window geometry -------------------------------------------------------

void Controller::saveWindow(const QRect &geometry, bool maximized)
{
    if (!m_savesWindow)
        return;
    m_window.geometry  = geometry;
    m_window.maximized = maximized;
    Config::saveWindow(m_window);
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

bool Controller::logLineMatches(const QString &text, bool isError,
                                bool errorsOnly, const QString &needle) const
{
    if (errorsOnly && !isError)
        return false;
    return needle.isEmpty()
           || text.contains(needle, Qt::CaseInsensitive);
}

QVariantList Controller::filteredLog(bool errorsOnly, const QString &needle) const
{
    if (!errorsOnly && needle.isEmpty())
        return m_logBacklog;

    QVariantList out;
    for (const QVariant &v : m_logBacklog) {
        const QVariantMap m = v.toMap();
        if (logLineMatches(m[QStringLiteral("body")].toString(),
                           m[QStringLiteral("isError")].toBool(),
                           errorsOnly, needle))
            out << v;
    }
    return out;
}

// Flattens the filtered view the way it reads on screen, timestamps included -
// what gets pasted into a bug report should be what was being looked at.
static QString flattenLog(const QVariantList &lines)
{
    QStringList out;
    out.reserve(lines.size());
    for (const QVariant &v : lines) {
        const QVariantMap m = v.toMap();
        out << QStringLiteral("[%1] %2")
                   .arg(m[QStringLiteral("timestamp")].toString(),
                        m[QStringLiteral("body")].toString());
    }
    return out.join(QLatin1Char('\n'));
}

void Controller::copyLogToClipboard(bool errorsOnly, const QString &needle)
{
    const QVariantList lines = filteredLog(errorsOnly, needle);
    QGuiApplication::clipboard()->setText(flattenLog(lines));
    setStatus(QStringLiteral("已复制 %1 行日志到剪贴板。").arg(lines.size()));
}

void Controller::exportLog(const QUrl &file, bool errorsOnly, const QString &needle)
{
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        setStatus(QStringLiteral("导出失败：%1").arg(f.errorString()), true);
        return;
    }

    const QVariantList lines = filteredLog(errorsOnly, needle);
    QTextStream(&f) << flattenLog(lines) << "\n";
    setStatus(QStringLiteral("已导出 %1 行日志到 %2").arg(lines.size()).arg(path));
}

void Controller::clearLog()
{
    m_logBacklog.clear();
    // The QML model is cleared alongside this; logging the fact gives the
    // emptied view a visible starting point.
    log(QStringLiteral("日志已清空。"));
}
