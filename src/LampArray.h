// HID LampArray (HID Usage Tables 1.4, "Lighting And Illumination", usage
// page 0x59) - the standardised per-LED lighting interface.
//
// The Gigabyte IT5702 exposes this on a SEPARATE hidraw node from the vendor
// 0xCC protocol. Decoded from the real report descriptor on a B760M AORUS
// ELITE:
//
//   Report 1  LampArrayAttributesReport   (Feature, get)
//               LampCount                     u16
//               BoundingBoxWidthInMicrometers u32
//               BoundingBoxHeightInMicrometers u32
//               BoundingBoxDepthInMicrometers u32
//               LampArrayKind                 u32
//               MinUpdateIntervalInMicroseconds u32
//
//   Report 2  LampAttributesRequestReport (Feature, set)
//               LampId                        u16
//
//   Report 3  LampAttributesResponseReport(Feature, get)
//               LampId                        u16
//               PositionXInMicrometers        u32
//               PositionYInMicrometers        u32
//               PositionZInMicrometers        u32
//               UpdateLatencyInMicroseconds   u32
//               LampPurposes                  u32
//               RedLevelCount                 u8
//               GreenLevelCount               u8
//               BlueLevelCount                u8
//               IntensityLevelCount           u8
//               IsProgrammable                u8
//               InputBinding                  u8
//
//   Report 4  LampMultiUpdateReport       (Feature, set)
//               LampCount                     u8
//               LampUpdateFlags               u8
//               LampId[8]                     u16 x8
//               (R,G,B,Intensity)[8]          u8  x32
//
//   Report 5  LampRangeUpdateReport       (Feature, set)
//               LampUpdateFlags               u8
//               LampIdStart                   u16
//               LampIdEnd                     u16
//               R,G,B,Intensity               u8 x4
//
//   Report 6  LampArrayControlReport      (Feature, set)
//               AutonomousMode                u8
//
// Note this is the same interface Windows drives through
// `Windows.Devices.Lights.LampArray`, which is what GIGABYTE CONTROL CENTER's
// Dynamic_Lighting_Fun_Lib.dll uses for motherboard dynamic lighting.
#pragma once

#include "HidRawDevice.h"

#include <QColor>
#include <QObject>
#include <QVector>

#include <cstdint>

class LampArray : public QObject
{
    Q_OBJECT

public:
    static constexpr uint8_t kReportAttributes    = 0x01;
    static constexpr uint8_t kReportAttrRequest   = 0x02;
    static constexpr uint8_t kReportAttrResponse  = 0x03;
    static constexpr uint8_t kReportMultiUpdate   = 0x04;
    static constexpr uint8_t kReportRangeUpdate   = 0x05;
    static constexpr uint8_t kReportControl       = 0x06;

    // Sizes include the leading report-ID byte.
    static constexpr int kLenAttributes   = 1 + 2 + 5 * 4;              // 23
    static constexpr int kLenAttrRequest  = 1 + 2;                      // 3
    static constexpr int kLenAttrResponse = 1 + 2 + 5 * 4 + 6;          // 29
    static constexpr int kLenMultiUpdate  = 1 + 1 + 1 + 8 * 2 + 8 * 4;  // 51
    static constexpr int kLenRangeUpdate  = 1 + 1 + 2 + 2 + 4;          // 10
    static constexpr int kLenControl      = 1 + 1;                      // 2

    static constexpr int kMultiUpdateBatch = 8;   // lamps per multi-update report

    // LampUpdateFlags bit 0 - apply this update together with the next one.
    static constexpr uint8_t kUpdateComplete = 0x00;

    struct Attributes {
        uint16_t lampCount             = 0;
        uint32_t boundingBoxWidthUm    = 0;
        uint32_t boundingBoxHeightUm   = 0;
        uint32_t boundingBoxDepthUm    = 0;
        uint32_t kind                  = 0;
        uint32_t minUpdateIntervalUs   = 0;
    };

    struct LampInfo {
        uint16_t id              = 0;
        uint32_t positionXUm     = 0;
        uint32_t positionYUm     = 0;
        uint32_t positionZUm     = 0;
        uint32_t updateLatencyUs = 0;
        uint32_t purposes        = 0;
        uint8_t  redLevels       = 0;
        uint8_t  greenLevels     = 0;
        uint8_t  blueLevels      = 0;
        uint8_t  intensityLevels = 0;
        bool     isProgrammable  = false;
        uint8_t  inputBinding    = 0;
    };

    explicit LampArray(QObject *parent = nullptr);

    bool openFirstDevice(QString *error = nullptr);
    void close();
    bool isOpen() const { return m_dev.isOpen(); }

    QString devicePath() const { return m_dev.path(); }
    const Attributes &attributes() const { return m_attrs; }
    const QVector<LampInfo> &lamps() const { return m_lamps; }

    // Reads report 1 and then walks every lamp through reports 2/3.
    bool queryAttributes(QString *error = nullptr);

    // Report 6. Autonomous mode must be OFF before host updates take effect,
    // and turning it back ON returns control to the board's own effects.
    bool setAutonomousMode(bool enabled, QString *error = nullptr);

    // Report 5 - paints a contiguous lamp-ID range in one transfer.
    bool setLampRange(uint16_t idStart, uint16_t idEnd, const QColor &colour,
                      uint8_t intensity = 255, QString *error = nullptr);

    // Report 4 - up to 8 individually addressed lamps per call; longer vectors
    // are automatically split into batches.
    bool setLamps(const QVector<QPair<uint16_t, QColor>> &lamps,
                  uint8_t intensity = 255, QString *error = nullptr);

    bool setAll(const QColor &colour, uint8_t intensity = 255, QString *error = nullptr);

    static QString kindName(uint32_t kind);
    static QString purposeNames(uint32_t purposes);

signals:
    void traffic(bool outgoing, const QString &label, const QByteArray &data);

private:
    bool sendReport(const QByteArray &buf, const QString &label, QString *error);

    HidRawDevice      m_dev;
    Attributes        m_attrs;
    QVector<LampInfo> m_lamps;
};
