#include "RgbFusion2.h"

#include <QThread>

#include <cerrno>

namespace {

// Speed tables. Each entry is three little-endian uint16 durations in
// milliseconds laid out at payload offsets 22..27:
//   [22..23] fade-in / first period
//   [24..25] fade-out / second period
//   [26..27] hold / interval
struct SpeedTriple { uint16_t p0, p1, p2; };

const SpeedTriple kPulseSpeeds[6] = {
    {0x0640, 0x0640, 0x0320}, // slowest
    {0x0578, 0x0578, 0x02bc}, // slower
    {0x04b0, 0x04b0, 0x01f4}, // normal
    {0x03e8, 0x03e8, 0x01f4}, // faster
    {0x0384, 0x0384, 0x01c2}, // fastest
    {0x0320, 0x0320, 0x0190}, // ludicrous
};

const SpeedTriple kFlashSpeeds[6] = {
    {0x0064, 0x0064, 0x0960},
    {0x0064, 0x0064, 0x0890},
    {0x0064, 0x0064, 0x07d0},
    {0x0064, 0x0064, 0x0708},
    {0x0064, 0x0064, 0x0640},
    {0x0064, 0x0064, 0x0578},
};

const SpeedTriple kDoubleFlashSpeeds[6] = {
    {0x0064, 0x0064, 0x0a28},
    {0x0064, 0x0064, 0x0960},
    {0x0064, 0x0064, 0x0890},
    {0x0064, 0x0064, 0x07d0},
    {0x0064, 0x0064, 0x0708},
    {0x0064, 0x0064, 0x0640},
};

const SpeedTriple kColorCycleSpeeds[6] = {
    {0x0578, 0x04b0, 0x0000},
    {0x047e, 0x041a, 0x0000},
    {0x0352, 0x02ee, 0x0000},
    {0x02f8, 0x0294, 0x0000},
    {0x0226, 0x01c2, 0x0000},
    {0x01cc, 0x0168, 0x0000},
};

// Per-mode wire parameters.
struct ModeSpec {
    uint8_t value;         // payload[11]
    bool    pulses;        // payload[31]
    uint8_t flashCount;    // payload[32]
    uint8_t cycleCount;    // payload[30]
    int     maxBrightness;
    bool    takesColor;
    const SpeedTriple *speeds; // nullptr when the mode has no speed control
};

const ModeSpec &specFor(RgbFusion2::Mode mode)
{
    // Note that Static and Off share effect value 0x01 - "off" is simply the
    // static effect at zero brightness. Flash and DoubleFlash likewise share
    // 0x03 and differ only in the flash-count byte.
    static const ModeSpec kOff         {0x01, false, 0, 0,   0, false, nullptr};
    static const ModeSpec kStatic      {0x01, false, 0, 0,  90, true,  nullptr};
    static const ModeSpec kBreathing   {0x02, true,  0, 0,  90, true,  kPulseSpeeds};
    static const ModeSpec kFlash       {0x03, true,  1, 0, 100, true,  kFlashSpeeds};
    static const ModeSpec kDoubleFlash {0x03, true,  2, 0, 100, true,  kDoubleFlashSpeeds};
    static const ModeSpec kColorCycle  {0x04, false, 0, 7, 100, false, kColorCycleSpeeds};

    switch (mode) {
    case RgbFusion2::Mode::Off:         return kOff;
    case RgbFusion2::Mode::Static:      return kStatic;
    case RgbFusion2::Mode::Breathing:   return kBreathing;
    case RgbFusion2::Mode::Flash:       return kFlash;
    case RgbFusion2::Mode::DoubleFlash: return kDoubleFlash;
    case RgbFusion2::Mode::ColorCycle:  return kColorCycle;
    }
    return kOff;
}

inline void putU16LE(QByteArray &buf, int offset, uint16_t v)
{
    buf[offset]     = static_cast<char>(v & 0xFF);
    buf[offset + 1] = static_cast<char>((v >> 8) & 0xFF);
}

} // namespace

// { scan, read } per header, in header order. From RgbMotherboard.dll.
const uint8_t RgbFusion2::kStripCmds[RgbFusion2::kStripHeaderCount][2] = {
    {0x3C, 0x3E}, {0x3D, 0x3F}, {0x38, 0x3A}, {0x39, 0x3B},
};

RgbFusion2::RgbFusion2(QObject *parent)
    : QObject(parent)
{
}

QByteArray RgbFusion2::buildCommandReport(uint8_t command, uint8_t arg0, uint8_t arg1)
{
    QByteArray buf(kBufferLen, char(0));
    buf[0] = static_cast<char>(kReportId);
    buf[1] = static_cast<char>(command);
    buf[2] = static_cast<char>(arg0);
    buf[3] = static_cast<char>(arg1);
    return buf;
}

QByteArray RgbFusion2::buildZoneResetReport(int zone)
{
    return buildCommandReport(static_cast<uint8_t>(kZoneCmdBase + zone));
}

QByteArray RgbFusion2::buildInitReport()
{
    return buildCommandReport(kCmdInit);
}

QByteArray RgbFusion2::buildApplyReport()
{
    QByteArray buf = buildCommandReport(kCmdApply);
    buf[2] = static_cast<char>(kApplyAllMask);
    return buf;
}

QByteArray RgbFusion2::buildZoneReport(int zone, Mode mode, const QColor &color,
                                       int brightness, Speed speed, int minBrightness)
{
    const ModeSpec &spec = specFor(mode);

    const int maxBri = qBound(0, brightness, spec.maxBrightness);
    const int minBri = qBound(0, minBrightness, maxBri);

    // Buffer layout, using GIGABYTE's own field names from the
    // `DataFormat_8297` struct in RgbMotherboard.dll. The struct is 32 bytes
    // and starts at buffer offset 2, right after the report ID and the
    // command byte.
    QByteArray buf(kBufferLen, char(0));
    buf[0] = static_cast<char>(kReportId);

    // [1] command byte, [2..5] Zone_Sel0, [6..9] Zone_Sel1.
    // The zone bitmask lives in the low byte of Zone_Sel0.
    buf[1] = static_cast<char>(kZoneCmdBase + zone);
    buf[2] = static_cast<char>(1u << zone);

    // [10] Reserve0, [11] Mode_Sel, [12] MaxBrightness, [13] MinBrightness
    buf[11] = static_cast<char>(spec.value);
    buf[12] = static_cast<char>(maxBri);
    buf[13] = static_cast<char>(minBri);

    // [14..17] dwColor0, little-endian, so the wire order is B G R 00.
    if (spec.takesColor && color.isValid()) {
        buf[14] = static_cast<char>(color.blue());
        buf[15] = static_cast<char>(color.green());
        buf[16] = static_cast<char>(color.red());
    }

    // [18..21] dwColor1 - a second colour, left at 0 for the effects below.

    // [22..23] wTime_base0 (on time), [24..25] wTime_base1 (interval),
    // [26..27] wTime_base2, [28..29] wTime_base3.
    if (spec.speeds) {
        const SpeedTriple &t = spec.speeds[static_cast<int>(speed)];
        putU16LE(buf, 22, t.p0);
        putU16LE(buf, 24, t.p1);
        putU16LE(buf, 26, t.p2);
    }

    // [30] CtrlVal0 (colour-cycle step count), [31] CtrlVal1 (pulse flag),
    // [32] CtrlOem0 (flash count), [33] CtrlOem1.
    buf[30] = static_cast<char>(spec.cycleCount);
    buf[31] = static_cast<char>(spec.pulses ? 1 : 0);
    buf[32] = static_cast<char>(spec.flashCount);

    return buf;
}

RgbFusion2::DeviceInfo RgbFusion2::parseInfoReport(const QByteArray &resp)
{
    DeviceInfo info;
    info.raw = resp;

    if (resp.size() >= 12) {
        info.product         = static_cast<uint8_t>(resp.at(1));
        info.deviceNum       = static_cast<uint8_t>(resp.at(2));
        info.stripDetect     = static_cast<uint8_t>(resp.at(3));
        info.firmwareVersion = QStringLiteral("%1.%2.%3.%4")
                                   .arg(static_cast<uint8_t>(resp.at(4)))
                                   .arg(static_cast<uint8_t>(resp.at(5)))
                                   .arg(static_cast<uint8_t>(resp.at(6)))
                                   .arg(static_cast<uint8_t>(resp.at(7)));
        info.stripCtrlLength0 = static_cast<uint16_t>(
            static_cast<uint8_t>(resp.at(8)) | (static_cast<uint8_t>(resp.at(9)) << 8));
        info.stripCtrlLength1 = static_cast<uint8_t>(resp.at(10));
        info.suppCmdFlag      = static_cast<uint8_t>(resp.at(11));
    }

    // ProductString is a fixed 28-byte field at offset 12 (12..39).
    if (resp.size() >= 40) {
        const QByteArray field = resp.mid(12, 28);
        const int end = field.indexOf(char(0));
        info.productName =
            QString::fromLatin1(end < 0 ? field : field.left(end)).trimmed();
    }

    if (resp.size() >= 60) {
        info.chipId = static_cast<uint32_t>(static_cast<uint8_t>(resp.at(56)))
                    | (static_cast<uint32_t>(static_cast<uint8_t>(resp.at(57))) << 8)
                    | (static_cast<uint32_t>(static_cast<uint8_t>(resp.at(58))) << 16)
                    | (static_cast<uint32_t>(static_cast<uint8_t>(resp.at(59))) << 24);
    }

    return info;
}

bool RgbFusion2::parseStripInfo(const QByteArray &resp, int header, StripInfo *out)
{
    if (resp.size() < 32)
        return false;

    StripInfo info;
    info.header   = header;
    info.numStrip = static_cast<uint8_t>(resp.at(1));

    for (int i = 0; i < info.numStrip && (3 + i * 2) < resp.size(); ++i) {
        const uint16_t n = static_cast<uint16_t>(
            static_cast<uint8_t>(resp.at(2 + i * 2))
            | (static_cast<uint8_t>(resp.at(3 + i * 2)) << 8));
        info.ledCounts.append(n);
        info.totalLeds = static_cast<uint16_t>(info.totalLeds + n);
    }

    *out = info;
    return true;
}

QString RgbFusion2::modeName(Mode mode)
{
    switch (mode) {
    case Mode::Off:         return QStringLiteral("关闭");
    case Mode::Static:      return QStringLiteral("静态");
    case Mode::Breathing:   return QStringLiteral("呼吸");
    case Mode::Flash:       return QStringLiteral("闪烁");
    case Mode::DoubleFlash: return QStringLiteral("双闪");
    case Mode::ColorCycle:  return QStringLiteral("色彩循环");
    }
    return QString();
}

QString RgbFusion2::speedName(Speed speed)
{
    switch (speed) {
    case Speed::Slowest:   return QStringLiteral("最慢");
    case Speed::Slower:    return QStringLiteral("较慢");
    case Speed::Normal:    return QStringLiteral("正常");
    case Speed::Faster:    return QStringLiteral("较快");
    case Speed::Fastest:   return QStringLiteral("最快");
    case Speed::Ludicrous: return QStringLiteral("极速");
    }
    return QString();
}

int  RgbFusion2::maxBrightness(Mode mode) { return specFor(mode).maxBrightness; }
bool RgbFusion2::modeUsesColor(Mode mode) { return specFor(mode).takesColor; }
bool RgbFusion2::modeUsesSpeed(Mode mode) { return specFor(mode).speeds != nullptr; }

QString RgbFusion2::hexDump(const QByteArray &data)
{
    QString out;
    for (int i = 0; i < data.size(); ++i) {
        if (i && i % 16 == 0)
            out += QLatin1Char('\n');
        else if (i)
            out += QLatin1Char(' ');
        out += QString::asprintf("%02X", static_cast<uint8_t>(data.at(i)));
    }
    return out;
}

bool RgbFusion2::isFirmwareHang(int err)
{
    // EPROTO is what the xHCI driver reports once the MCU stops driving the bus
    // correctly, and ETIMEDOUT is the transfer that catches it going under.
    // ENODEV follows when the kernel gives up on the port entirely - by then it
    // will not even answer a SET_ADDRESS, so re-enumerating cannot bring it
    // back and asking the kernel to try only loses the node as well.
    return err == EPROTO || err == ETIMEDOUT || err == ENODEV;
}

bool RgbFusion2::openFirstDevice(QString *error)
{
    const QVector<HidRawInfo> nodes = HidRawDevice::enumerate();

    // The lighting collection is identified by the byte sequence
    //   06 89 FF   Usage Page (0xFF89)
    //   09 CC      Usage (0xCC)
    // inside the report descriptor. The sibling interface uses Usage 0x10 and
    // must be skipped.
    static const QByteArray kLightingUsage =
        QByteArray::fromHex("0689FF09CC");

    QStringList seen;
    for (const HidRawInfo &info : nodes) {
        if (info.vendorId != kVendorId)
            continue;
        seen << QStringLiteral("%1 (%2:%3)")
                    .arg(info.path)
                    .arg(info.vendorId, 4, 16, QLatin1Char('0'))
                    .arg(info.productId, 4, 16, QLatin1Char('0'));

        if (!info.reportDescriptor.contains(kLightingUsage))
            continue;

        if (m_dev.open(info.path, error)) {
            m_dev.setMinInterval(kMinIoIntervalMs);
            return true;
        }
        return false; // found it but could not open - surface the errno
    }

    if (error) {
        if (seen.isEmpty()) {
            *error = QStringLiteral(
                "未找到 ITE (048d) RGB 控制器。请确认主板带 RGB Fusion 2.0 控制芯片。");
        } else {
            *error = QStringLiteral("找到 ITE 设备 %1，但其中没有 0xFF89/0xCC 灯光接口。")
                         .arg(seen.join(QStringLiteral(", ")));
        }
    }
    return false;
}

void RgbFusion2::close()
{
    m_dev.close();
    m_info = DeviceInfo();
}

bool RgbFusion2::sendReport(const QByteArray &buf, const QString &label, QString *error)
{
    emit traffic(true, label, buf);
    return m_dev.sendFeatureReport(buf, error);
}

bool RgbFusion2::initialize(QString *error)
{
    if (!sendReport(buildInitReport(), QStringLiteral("INIT (0x60)"), error))
        return false;

    QByteArray resp;
    if (!m_dev.getFeatureReport(kReportId, kBufferLen, &resp, error))
        return false;

    emit traffic(false, QStringLiteral("INFO 应答"), resp);

    m_info = parseInfoReport(resp);
    return true;
}

bool RgbFusion2::setZone(int zone, Mode mode, const QColor &color, int brightness,
                         Speed speed, int minBrightness, QString *error)
{
    if (zone < 0 || zone >= kZoneCount) {
        if (error)
            *error = QStringLiteral("区域索引越界: %1").arg(zone);
        return false;
    }

    const QByteArray buf =
        buildZoneReport(zone, mode, color, brightness, speed, minBrightness);

    const QString label = QStringLiteral("SET 区域%1 %2").arg(zone + 1).arg(modeName(mode));
    return sendReport(buf, label, error);
}

bool RgbFusion2::scanStripHeader(int header, StripInfo *out, bool rescan, QString *error)
{
    if (header < 0 || header >= kStripHeaderCount) {
        if (error)
            *error = QStringLiteral("灯带头索引越界: %1").arg(header);
        return false;
    }

    if (rescan) {
        QByteArray req(kBufferLen, char(0));
        req[0] = static_cast<char>(kReportId);
        req[1] = static_cast<char>(kStripCmds[header][0]);
        if (!sendReport(req, QStringLiteral("SCAN 灯带头%1 (0x%2)")
                                 .arg(header)
                                 .arg(kStripCmds[header][0], 2, 16, QLatin1Char('0')),
                        error))
            return false;
        QThread::msleep(750);   // the controller needs ~700 ms to walk the bus
    }

    QByteArray req(kBufferLen, char(0));
    req[0] = static_cast<char>(kReportId);
    req[1] = static_cast<char>(kStripCmds[header][1]);
    if (!sendReport(req, QStringLiteral("READ 灯带头%1 (0x%2)")
                             .arg(header)
                             .arg(kStripCmds[header][1], 2, 16, QLatin1Char('0')),
                    error))
        return false;

    QByteArray resp;
    if (!m_dev.getFeatureReport(kReportId, kBufferLen, &resp, error))
        return false;
    emit traffic(false, QStringLiteral("灯带头%1 信息").arg(header), resp);

    if (!parseStripInfo(resp, header, out)) {
        if (error)
            *error = QStringLiteral("灯带信息报文过短: %1 字节").arg(resp.size());
        return false;
    }
    return true;
}

bool RgbFusion2::apply(QString *error)
{
    return sendReport(buildApplyReport(), QStringLiteral("APPLY (0x28 0xFF)"), error);
}

bool RgbFusion2::takeOverZones(const QVector<int> &zones, QString *error)
{
    if (zones.isEmpty())
        return true;

    // Audio-beat mode makes the controller drive zones from the analogue input,
    // and LampArray mode hands them to the Windows dynamic-lighting engine.
    // Neither is anything this program uses, and either one left on would fight
    // the effects staged below.
    //
    // Both were suspected of being what leaves the controller ignoring every
    // Fusion report after a Windows boot, since they arrived in 9d01535 and
    // that is roughly when restoring stopped working. tools/ab-global-modes.c
    // cleared them on a mains-cut cold boot: sending each one and painting
    // after it, the paint still shows. They stay.
    if (!sendReport(buildCommandReport(kCmdBeat, 0),
                    QStringLiteral("BEAT off (0x31)"), error))
        return false;
    if (m_info.suppCmdFlag & kSuppLampArray) {
        if (!sendReport(buildCommandReport(kCmdLampArray, 0),
                        QStringLiteral("LAMPARRAY off (0x48)"), error))
            return false;
    }

    for (int zone : zones) {
        if (zone < 0 || zone >= kZoneCount)
            continue;
        if (!sendReport(buildZoneResetReport(zone),
                        QStringLiteral("RESET 区域%1 (0x%2)")
                            .arg(zone + 1)
                            .arg(kZoneCmdBase + zone, 2, 16, QLatin1Char('0')),
                        error))
            return false;
    }

    if (!apply(error))
        return false;

    // The controller needs a moment to act on a mode change before it will take
    // effects again; OpenRGB waits the same 50 ms after its own mode commands.
    QThread::msleep(50);
    return true;
}
