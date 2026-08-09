// RGB Fusion 2.0 USB-HID protocol (ITE IT5702 / IT8297 family).
//
// Wire format confirmed against the real HID report descriptor read from
// /sys/class/hidraw/hidrawN/device/report_descriptor on a Gigabyte
// B760M AORUS ELITE:
//
//     06 89 FF   Usage Page (Vendor-Defined 0xFF89)
//     09 CC      Usage (0xCC)
//     A1 01      Collection (Application)
//     85 CC        Report ID (0xCC)
//     75 08        Report Size (8 bits)
//     95 3F        Report Count (63)
//     B1 00        Feature (Data,Var,Abs)
//     C0         End Collection
//
// So: feature reports, report ID 0xCC, 63 payload bytes (64 including the
// report ID byte that the hidraw feature ioctls prepend).
//
// The same device exposes a second vendor collection (Usage Page 0xFF89,
// Usage 0x10, Report ID 0x5A, 16 bytes) on a different hidraw node - that one
// is NOT the lighting interface, which is why the device must be selected by
// its report descriptor rather than by a hardcoded /dev/hidrawN path.
//
// Field offsets follow the liquidctl `rgb_fusion2` driver
// (https://github.com/liquidctl/liquidctl, GPL-3.0), the most complete public
// reverse-engineering of this controller.
#pragma once

#include "HidRawDevice.h"

#include <QColor>
#include <QObject>
#include <QString>
#include <QVector>

#include <cstdint>

class RgbFusion2 : public QObject
{
    Q_OBJECT

public:
    // ---- protocol constants ---------------------------------------------
    static constexpr uint16_t kVendorId = 0x048D;   // ITE Tech. Inc.

    static constexpr uint8_t kReportId      = 0xCC;
    static constexpr int     kPayloadLen    = 63;   // Report Count from the descriptor
    static constexpr int     kBufferLen     = kPayloadLen + 1; // + report ID byte

    static constexpr uint8_t kCmdInit       = 0x60; // request device info
    static constexpr uint8_t kCmdApply      = 0x28; // commit staged zone settings
    static constexpr uint8_t kZoneCmdBase   = 0x20; // zone i -> command 0x20 + i
    static constexpr uint8_t kApplyAllMask  = 0xFF;

    // Global modes the board can come out of a cold boot already in, both of
    // which leave the controller painting zones by itself. Names and values
    // from OpenRGB's `RGBFusion2USBController` (EnableBeat / EnableLampArray).
    static constexpr uint8_t kCmdBeat       = 0x31; // hardware audio-beat mode
    static constexpr uint8_t kCmdLampArray  = 0x48; // HID LampArray (MSDL) mode

    // SuppCmdFlag bit that says 0x48 is understood. Sending it blind to a
    // controller that does not support it is what OpenRGB guards against here.
    static constexpr uint8_t kSuppLampArray = 0x02;

    static constexpr int kZoneCount = 8;            // led1 .. led8

    // Minimum gap between two feature reports. The controller is an MCU on the
    // board's +5VSB rail with no queue worth the name: a full apply used to go
    // out as ~18 reports about a millisecond apart, which it survives from a
    // cold boot but not when it comes up carrying state from GIGABYTE CONTROL
    // CENTER. There it stops answering partway through and every later transfer
    // fails EPROTO until the board actually loses power. OpenRGB paces the same
    // chip at tens of milliseconds; 20 keeps a full apply well under a second.
    static constexpr int kMinIoIntervalMs = 20;

    enum class Mode {
        Off,
        Static,
        Breathing,   // "pulse"
        Flash,
        DoubleFlash,
        ColorCycle,
    };

    enum class Speed {
        Slowest, Slower, Normal, Faster, Fastest, Ludicrous,
    };

    // Mirrors GIGABYTE's own `IT8297_Info` struct (64 bytes), recovered from
    // RgbMotherboard.dll:
    //   [0] ReportId  [1] Product  [2] DeviceNum  [3] StripDetect
    //   [4..7] FW_Ver  [8..9] Strip_Ctrl_Length0  [10] Strip_Ctrl_Length1
    //   [11] SuppCmdFlag  [12..39] ProductString[28]
    //   [40..55] CalStrip3/0/1/2  [56..59] ChipId  [60..63] CalStrip4
    struct DeviceInfo {
        QString    productName;
        QString    firmwareVersion;
        uint8_t    product          = 0;
        uint8_t    deviceNum        = 0;
        uint8_t    stripDetect      = 0;   // addressable-strip detect bitmask
        uint16_t   stripCtrlLength0 = 0;
        uint8_t    stripCtrlLength1 = 0;
        uint8_t    suppCmdFlag      = 0;   // supported-command bitmask
        uint32_t   chipId           = 0;
        QByteArray raw;
    };

    explicit RgbFusion2(QObject *parent = nullptr);

    // Finds the lighting interface among all hidraw nodes and opens it.
    bool openFirstDevice(QString *error = nullptr);
    void close();
    bool isOpen() const { return m_dev.isOpen(); }

    QString devicePath() const { return m_dev.path(); }
    const DeviceInfo &deviceInfo() const { return m_info; }

    // errno behind the last failed transfer. See HidRawDevice::lastErrno().
    int lastErrno() const { return m_dev.lastErrno(); }

    // True once the controller has stopped answering altogether - see
    // kMinIoIntervalMs for what that state is. Nothing the host can send clears
    // it; only cutting power to the board does.
    static bool isFirmwareHang(int err);

    // Sends 0xCC 0x60 and reads back the info report.
    bool initialize(QString *error = nullptr);

    // Stages one zone. Nothing lights up until apply() is called.
    // `minBrightness` is the floor a pulsing effect dims down to; GIGABYTE's
    // own client always sends 0, but the field is honoured by the controller.
    bool setZone(int zone, Mode mode, const QColor &color, int brightness,
                 Speed speed, int minBrightness = 0, QString *error = nullptr);

    // 0xCC 0x28 0xFF - tells the controller to latch everything staged so far.
    bool apply(QString *error = nullptr);

    // Hands `zones` over from whatever the board is running on its own.
    //
    // Out of a cold boot the controller is still painting the BIOS's effects,
    // and staging a zone does not reliably take that over - the chipset LED on
    // a B760M AORUS ELITE keeps cycling through the boot rainbow while every
    // other zone obeys. GIGABYTE's client and OpenRGB both clear the zone
    // registers first, which is what this does: the global audio-beat and
    // LampArray modes off, then every listed zone's register zeroed and
    // latched. Call it before staging the effects you actually want.
    //
    // Zones not listed are left alone - a restore must not blank a zone the
    // user never handed to us.
    bool takeOverZones(const QVector<int> &zones, QString *error = nullptr);

    // ---- addressable (Gen2 ARGB) strip headers ---------------------------
    //
    // Each header is scanned with one command and its result read back with
    // another; the pairs come from RgbMotherboard.dll:
    //
    //   header 0: scan 0x3C, read 0x3E      header 2: scan 0x38, read 0x3A
    //   header 1: scan 0x3D, read 0x3F      header 3: scan 0x39, read 0x3B
    //
    // The reply is GIGABYTE's `gen2_strip_info`:
    //   [0] ReportId  [1] numStrip  [2..31] LedCountOfStrip0..14 (u16 each)
    struct StripInfo {
        int      header    = 0;
        uint8_t  numStrip  = 0;
        QVector<uint16_t> ledCounts;   // one entry per detected strip
        uint16_t totalLeds = 0;
    };

    static constexpr int kStripHeaderCount = 4;

    // { scan, read } command pair per header.
    static const uint8_t kStripCmds[kStripHeaderCount][2];

    // Triggers a rescan (which the controller needs ~700 ms for) and reads the
    // result. Returns false only on I/O failure - a header with nothing
    // plugged in reports numStrip == 0.
    bool scanStripHeader(int header, StripInfo *out, bool rescan = true,
                         QString *error = nullptr);

    // ---- pure packet builders / parsers ----------------------------------
    //
    // Split out from the send path so the wire format can be unit-tested
    // without a device present. Every builder returns a full kBufferLen
    // buffer with the report ID already in byte 0.

    static QByteArray buildZoneReport(int zone, Mode mode, const QColor &color,
                                      int brightness, Speed speed, int minBrightness);
    static QByteArray buildApplyReport();
    static QByteArray buildInitReport();
    static QByteArray buildCommandReport(uint8_t command, uint8_t arg0 = 0,
                                         uint8_t arg1 = 0);

    // A zone command with an all-zero payload: clears the register rather than
    // writing an effect into it. Note the zone-select byte stays 0 here, which
    // is what OpenRGB's ResetController() sends.
    static QByteArray buildZoneResetReport(int zone);

    // Decodes an info report into DeviceInfo. Exposed for testing against
    // captured payloads.
    static DeviceInfo parseInfoReport(const QByteArray &resp);

    // Decodes a gen2_strip_info reply.
    static bool parseStripInfo(const QByteArray &resp, int header, StripInfo *out);

    static QString modeName(Mode mode);
    static QString speedName(Speed speed);
    static int     maxBrightness(Mode mode);
    static bool    modeUsesColor(Mode mode);
    static bool    modeUsesSpeed(Mode mode);

    // Exposed so the GUI can show exactly what went over the wire.
    static QString hexDump(const QByteArray &data);

signals:
    // Emitted for every feature report in either direction. `outgoing` is true
    // for HIDIOCSFEATURE, false for HIDIOCGFEATURE.
    void traffic(bool outgoing, const QString &label, const QByteArray &data);

private:
    bool sendReport(const QByteArray &buf, const QString &label, QString *error);

    HidRawDevice m_dev;
    DeviceInfo   m_info;
};
