#include "LampArray.h"

namespace {

inline uint16_t rdU16(const QByteArray &b, int off)
{
    return static_cast<uint16_t>(static_cast<uint8_t>(b.at(off))
                                 | (static_cast<uint8_t>(b.at(off + 1)) << 8));
}

inline uint32_t rdU32(const QByteArray &b, int off)
{
    return static_cast<uint32_t>(static_cast<uint8_t>(b.at(off)))
         | (static_cast<uint32_t>(static_cast<uint8_t>(b.at(off + 1))) << 8)
         | (static_cast<uint32_t>(static_cast<uint8_t>(b.at(off + 2))) << 16)
         | (static_cast<uint32_t>(static_cast<uint8_t>(b.at(off + 3))) << 24);
}

inline void wrU16(QByteArray &b, int off, uint16_t v)
{
    b[off]     = static_cast<char>(v & 0xFF);
    b[off + 1] = static_cast<char>((v >> 8) & 0xFF);
}

} // namespace

LampArray::LampArray(QObject *parent)
    : QObject(parent)
{
}

QString LampArray::kindName(uint32_t kind)
{
    // HID Usage Tables 1.4, LampArrayKind values (same numbering as the WinRT
    // Windows.Devices.Lights.LampArrayKind enum).
    switch (kind) {
    case 0:  return QStringLiteral("未定义");
    case 1:  return QStringLiteral("键盘");
    case 2:  return QStringLiteral("鼠标");
    case 3:  return QStringLiteral("游戏手柄");
    case 4:  return QStringLiteral("外设");
    case 5:  return QStringLiteral("场景");
    case 6:  return QStringLiteral("通知");
    case 7:  return QStringLiteral("机箱");
    case 8:  return QStringLiteral("穿戴设备");
    case 9:  return QStringLiteral("家具");
    case 10: return QStringLiteral("艺术装置");
    case 11: return QStringLiteral("耳机");
    case 12: return QStringLiteral("麦克风");
    case 13: return QStringLiteral("音箱");
    default: return QStringLiteral("未知(%1)").arg(kind);
    }
}

QString LampArray::purposeNames(uint32_t purposes)
{
    // LampPurposes is a bitmask; values from HID Usage Tables 1.4.
    QStringList parts;
    if (purposes & 0x01) parts << QStringLiteral("控制");
    if (purposes & 0x02) parts << QStringLiteral("氛围点缀");
    if (purposes & 0x04) parts << QStringLiteral("品牌标识");
    if (purposes & 0x08) parts << QStringLiteral("状态指示");
    if (purposes & 0x10) parts << QStringLiteral("照明");
    if (purposes & 0x20) parts << QStringLiteral("演示");
    if (parts.isEmpty())
        return QStringLiteral("-");
    return parts.join(QLatin1Char('/'));
}

bool LampArray::openFirstDevice(QString *error)
{
    // Match on the descriptor prologue:
    //   05 59   Usage Page (Lighting And Illumination)
    //   09 01   Usage (LampArray)
    //   A1 01   Collection (Application)
    static const QByteArray kPrologue = QByteArray::fromHex("055909 01A101");

    const QVector<HidRawInfo> nodes = HidRawDevice::enumerate();
    for (const HidRawInfo &info : nodes) {
        if (!info.reportDescriptor.startsWith(kPrologue))
            continue;
        if (m_dev.open(info.path, error))
            return true;
        return false;
    }

    if (error)
        *error = QStringLiteral("未找到 HID LampArray 接口（Usage Page 0x59）。");
    return false;
}

void LampArray::close()
{
    m_dev.close();
    m_attrs = Attributes();
    m_lamps.clear();
}

bool LampArray::sendReport(const QByteArray &buf, const QString &label, QString *error)
{
    emit traffic(true, label, buf);
    return m_dev.sendFeatureReport(buf, error);
}

bool LampArray::queryAttributes(QString *error)
{
    QByteArray resp;
    if (!m_dev.getFeatureReport(kReportAttributes, kLenAttributes, &resp, error))
        return false;
    emit traffic(false, QStringLiteral("LampArrayAttributes (报文1)"), resp);

    if (resp.size() < kLenAttributes) {
        if (error)
            *error = QStringLiteral("属性报文过短: %1 字节").arg(resp.size());
        return false;
    }

    Attributes a;
    a.lampCount           = rdU16(resp, 1);
    a.boundingBoxWidthUm  = rdU32(resp, 3);
    a.boundingBoxHeightUm = rdU32(resp, 7);
    a.boundingBoxDepthUm  = rdU32(resp, 11);
    a.kind                = rdU32(resp, 15);
    a.minUpdateIntervalUs = rdU32(resp, 19);
    m_attrs = a;

    // Walk every lamp: write its ID into report 2, read report 3 back.
    m_lamps.clear();
    m_lamps.reserve(a.lampCount);

    for (uint16_t id = 0; id < a.lampCount; ++id) {
        QByteArray req(kLenAttrRequest, char(0));
        req[0] = static_cast<char>(kReportAttrRequest);
        wrU16(req, 1, id);
        if (!m_dev.sendFeatureReport(req, error))
            return false;

        QByteArray lr;
        if (!m_dev.getFeatureReport(kReportAttrResponse, kLenAttrResponse, &lr, error))
            return false;
        if (lr.size() < kLenAttrResponse)
            continue;

        LampInfo li;
        li.id              = rdU16(lr, 1);
        li.positionXUm     = rdU32(lr, 3);
        li.positionYUm     = rdU32(lr, 7);
        li.positionZUm     = rdU32(lr, 11);
        li.updateLatencyUs = rdU32(lr, 15);
        li.purposes        = rdU32(lr, 19);
        li.redLevels       = static_cast<uint8_t>(lr.at(23));
        li.greenLevels     = static_cast<uint8_t>(lr.at(24));
        li.blueLevels      = static_cast<uint8_t>(lr.at(25));
        li.intensityLevels = static_cast<uint8_t>(lr.at(26));
        li.isProgrammable  = static_cast<uint8_t>(lr.at(27)) != 0;
        li.inputBinding    = static_cast<uint8_t>(lr.at(28));
        m_lamps.append(li);
    }

    emit traffic(false,
                 QStringLiteral("已枚举 %1 个灯").arg(m_lamps.size()),
                 QByteArray());
    return true;
}

bool LampArray::setAutonomousMode(bool enabled, QString *error)
{
    QByteArray buf(kLenControl, char(0));
    buf[0] = static_cast<char>(kReportControl);
    buf[1] = static_cast<char>(enabled ? 1 : 0);

    return sendReport(buf,
                      QStringLiteral("LampArrayControl 自主模式=%1").arg(enabled ? 1 : 0),
                      error);
}

bool LampArray::setLampRange(uint16_t idStart, uint16_t idEnd, const QColor &colour,
                             uint8_t intensity, QString *error)
{
    QByteArray buf(kLenRangeUpdate, char(0));
    buf[0] = static_cast<char>(kReportRangeUpdate);
    buf[1] = static_cast<char>(kUpdateComplete);   // LampUpdateFlags
    wrU16(buf, 2, idStart);
    wrU16(buf, 4, idEnd);
    buf[6] = static_cast<char>(colour.red());
    buf[7] = static_cast<char>(colour.green());
    buf[8] = static_cast<char>(colour.blue());
    buf[9] = static_cast<char>(intensity);

    return sendReport(buf,
                      QStringLiteral("LampRangeUpdate %1-%2 %3")
                          .arg(idStart).arg(idEnd).arg(colour.name()),
                      error);
}

bool LampArray::setLamps(const QVector<QPair<uint16_t, QColor>> &lamps,
                         uint8_t intensity, QString *error)
{
    for (int base = 0; base < lamps.size(); base += kMultiUpdateBatch) {
        const int n = qMin(kMultiUpdateBatch, lamps.size() - base);

        QByteArray buf(kLenMultiUpdate, char(0));
        buf[0] = static_cast<char>(kReportMultiUpdate);
        buf[1] = static_cast<char>(n);                 // LampCount
        buf[2] = static_cast<char>(kUpdateComplete);   // LampUpdateFlags

        for (int i = 0; i < n; ++i) {
            const QPair<uint16_t, QColor> &e = lamps.at(base + i);
            wrU16(buf, 3 + i * 2, e.first);            // LampId[i]

            const int c = 19 + i * 4;                  // RGBI block starts at 19
            buf[c]     = static_cast<char>(e.second.red());
            buf[c + 1] = static_cast<char>(e.second.green());
            buf[c + 2] = static_cast<char>(e.second.blue());
            buf[c + 3] = static_cast<char>(intensity);
        }

        if (!sendReport(buf, QStringLiteral("LampMultiUpdate %1 灯").arg(n), error))
            return false;
    }
    return true;
}

bool LampArray::setAll(const QColor &colour, uint8_t intensity, QString *error)
{
    if (m_attrs.lampCount == 0) {
        if (error)
            *error = QStringLiteral("灯数为 0，请先读取属性。");
        return false;
    }
    return setLampRange(0, static_cast<uint16_t>(m_attrs.lampCount - 1), colour,
                        intensity, error);
}
