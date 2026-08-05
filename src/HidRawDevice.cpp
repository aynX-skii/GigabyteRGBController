#include "HidRawDevice.h"

#include <QDir>
#include <QFile>

#include <linux/hidraw.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace {

QString errnoText()
{
    return QString::fromLocal8Bit(std::strerror(errno));
}

// The uevent file carries a line like:
//   HID_ID=0003:0000048D:00005702
bool parseHidId(const QString &uevent, uint16_t *vid, uint16_t *pid)
{
    for (const QString &line : uevent.split(QLatin1Char('\n'))) {
        if (!line.startsWith(QLatin1String("HID_ID=")))
            continue;
        const QStringList parts = line.mid(7).split(QLatin1Char(':'));
        if (parts.size() != 3)
            return false;
        bool ok1 = false, ok2 = false;
        const uint32_t v = parts.at(1).toUInt(&ok1, 16);
        const uint32_t p = parts.at(2).toUInt(&ok2, 16);
        if (!ok1 || !ok2)
            return false;
        *vid = static_cast<uint16_t>(v & 0xFFFF);
        *pid = static_cast<uint16_t>(p & 0xFFFF);
        return true;
    }
    return false;
}

QString parseHidName(const QString &uevent)
{
    for (const QString &line : uevent.split(QLatin1Char('\n'))) {
        if (line.startsWith(QLatin1String("HID_NAME=")))
            return line.mid(9).trimmed();
    }
    return QString();
}

QByteArray readFileBytes(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QByteArray();
    return f.readAll();
}

} // namespace

HidRawDevice::~HidRawDevice()
{
    close();
}

QVector<HidRawInfo> HidRawDevice::enumerate()
{
    QVector<HidRawInfo> result;

    QDir classDir(QStringLiteral("/sys/class/hidraw"));
    const QStringList nodes =
        classDir.entryList(QStringList() << QStringLiteral("hidraw*"), QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QString &node : nodes) {
        const QString base = classDir.absoluteFilePath(node);

        const QString uevent = QString::fromLatin1(readFileBytes(base + QStringLiteral("/device/uevent")));
        if (uevent.isEmpty())
            continue;

        HidRawInfo info;
        if (!parseHidId(uevent, &info.vendorId, &info.productId))
            continue;

        info.path             = QStringLiteral("/dev/") + node;
        info.name             = parseHidName(uevent);
        info.reportDescriptor = readFileBytes(base + QStringLiteral("/device/report_descriptor"));

        result.append(info);
    }

    return result;
}

bool HidRawDevice::open(const QString &path, QString *error)
{
    close();

    const int fd = ::open(path.toLocal8Bit().constData(), O_RDWR);
    if (fd < 0) {
        if (error)
            *error = QStringLiteral("打开 %1 失败: %2").arg(path, errnoText());
        return false;
    }

    m_fd   = fd;
    m_path = path;
    return true;
}

void HidRawDevice::close()
{
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
    m_path.clear();
}

bool HidRawDevice::sendFeatureReport(const QByteArray &data, QString *error)
{
    if (m_fd < 0) {
        if (error)
            *error = QStringLiteral("设备未打开");
        return false;
    }

    if (::ioctl(m_fd, HIDIOCSFEATURE(data.size()), data.constData()) < 0) {
        if (error)
            *error = QStringLiteral("HIDIOCSFEATURE 失败: %1").arg(errnoText());
        return false;
    }
    return true;
}

bool HidRawDevice::getFeatureReport(uint8_t reportId, int length, QByteArray *out, QString *error)
{
    if (m_fd < 0) {
        if (error)
            *error = QStringLiteral("设备未打开");
        return false;
    }

    QByteArray buf(length, char(0));
    buf[0] = static_cast<char>(reportId);

    const int n = ::ioctl(m_fd, HIDIOCGFEATURE(length), buf.data());
    if (n < 0) {
        if (error)
            *error = QStringLiteral("HIDIOCGFEATURE 失败: %1").arg(errnoText());
        return false;
    }

    buf.resize(n);
    *out = buf;
    return true;
}
