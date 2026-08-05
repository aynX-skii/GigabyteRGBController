// Minimal Linux hidraw wrapper - talks to /dev/hidrawN through ioctl() directly.
// Deliberately avoids hidapi/libusb so the project builds with nothing but Qt6.
#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

#include <cstdint>

struct HidRawInfo
{
    QString  path;            // /dev/hidrawN
    QString  name;            // HID_NAME from the device uevent
    uint16_t vendorId  = 0;
    uint16_t productId = 0;
    QByteArray reportDescriptor;
};

class HidRawDevice
{
public:
    HidRawDevice() = default;
    ~HidRawDevice();

    HidRawDevice(const HidRawDevice &)            = delete;
    HidRawDevice &operator=(const HidRawDevice &) = delete;

    // Enumerates every /dev/hidrawN the process can see, reading the matching
    // sysfs attributes for vendor/product and the raw report descriptor.
    static QVector<HidRawInfo> enumerate();

    bool open(const QString &path, QString *error = nullptr);
    void close();
    bool isOpen() const { return m_fd >= 0; }

    const QString &path() const { return m_path; }

    // HIDIOCSFEATURE / HIDIOCGFEATURE. `data` must already carry the report ID
    // in byte 0, which is how the hidraw feature ioctls expect it.
    bool sendFeatureReport(const QByteArray &data, QString *error = nullptr);
    bool getFeatureReport(uint8_t reportId, int length, QByteArray *out,
                          QString *error = nullptr);

private:
    int     m_fd = -1;
    QString m_path;
};
