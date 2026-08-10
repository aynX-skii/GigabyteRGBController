// Wire-format tests for the reverse-engineered RGB Fusion 2.0 protocol.
//
// These assert exact byte offsets against the spec recovered from GIGABYTE's
// `DataFormat_8297` / `IT8297_Info` structs and from the real HID report
// descriptor. They need no hardware - that is the point: a wrong offset here
// would otherwise only show up as "the lights look odd".
#include "../src/LampArray.h"
#include "../src/RgbFusion2.h"

#include <QTest>

class ProtocolTest : public QObject
{
    Q_OBJECT

private slots:
    // ---- framing ---------------------------------------------------------

    void reportGeometryMatchesDescriptor()
    {
        // 95 3F -> Report Count (63), plus the report-ID byte the hidraw
        // feature ioctls prepend.
        QCOMPARE(RgbFusion2::kPayloadLen, 63);
        QCOMPARE(RgbFusion2::kBufferLen, 64);
        QCOMPARE(int(RgbFusion2::kReportId), 0xCC);
        QCOMPARE(int(RgbFusion2::kVendorId), 0x048D);
    }

    void everyBuilderProducesFullLengthBuffers()
    {
        QCOMPARE(RgbFusion2::buildInitReport().size(), RgbFusion2::kBufferLen);
        QCOMPARE(RgbFusion2::buildApplyReport().size(), RgbFusion2::kBufferLen);
        QCOMPARE(RgbFusion2::buildCommandReport(0x33).size(), RgbFusion2::kBufferLen);
        QCOMPARE(RgbFusion2::buildZoneReport(0, RgbFusion2::Mode::Static,
                                             Qt::red, 90, RgbFusion2::Speed::Normal, 0)
                     .size(),
                 RgbFusion2::kBufferLen);
    }

    void initAndApplyCommandBytes()
    {
        const QByteArray init = RgbFusion2::buildInitReport();
        QCOMPARE(quint8(init.at(0)), quint8(0xCC));
        QCOMPARE(quint8(init.at(1)), quint8(0x60));

        // Apply is 0xCC 0x28 0xFF - the 0xFF selects every zone.
        const QByteArray apply = RgbFusion2::buildApplyReport();
        QCOMPARE(quint8(apply.at(0)), quint8(0xCC));
        QCOMPARE(quint8(apply.at(1)), quint8(0x28));
        QCOMPARE(quint8(apply.at(2)), quint8(0xFF));
    }

    // ---- taking the zones back off the board's own effects ---------------

    void modeCommandsCarryTheirArgument()
    {
        // 0xCC 0x31 0x00 (audio-beat off) and 0xCC 0x48 0x00 (LampArray off).
        // The argument is byte 2, not byte 3 - swapping them silently leaves
        // the mode on.
        const QByteArray beat = RgbFusion2::buildCommandReport(RgbFusion2::kCmdBeat, 0);
        QCOMPARE(quint8(beat.at(1)), quint8(0x31));
        QCOMPARE(quint8(beat.at(2)), quint8(0x00));

        const QByteArray lamp =
            RgbFusion2::buildCommandReport(RgbFusion2::kCmdLampArray, 1);
        QCOMPARE(quint8(lamp.at(1)), quint8(0x48));
        QCOMPARE(quint8(lamp.at(2)), quint8(0x01));
    }

    void applyMaskIsNarrowOnTheOlderParts()
    {
        // GIGABYTE's MCU_8297.Apply(): byte 2 carries the mask, and byte 3 is
        // only written on IT5711 and later, where it holds the high bits. On a
        // 5702 a stray byte 3 would be a zone selection nobody asked for.
        const QByteArray narrow =
            RgbFusion2::buildApplyReport(RgbFusion2::kApplyAllZones, false);
        QCOMPARE(quint8(narrow.at(1)), quint8(0x28));
        QCOMPARE(quint8(narrow.at(2)), quint8(0xFF));
        QCOMPARE(quint8(narrow.at(3)), quint8(0x00));

        const QByteArray wide =
            RgbFusion2::buildApplyReport(RgbFusion2::kApplyAllZones, true);
        QCOMPARE(quint8(wide.at(2)), quint8(0xFF));
        QCOMPARE(quint8(wide.at(3)), quint8(0x07));
    }

    void zoneResetIsTheZoneCommandWithAnEmptyPayload()
    {
        const QByteArray reset = RgbFusion2::buildZoneResetReport(2);
        QCOMPARE(reset.size(), RgbFusion2::kBufferLen);
        QCOMPARE(quint8(reset.at(0)), quint8(0xCC));
        QCOMPARE(quint8(reset.at(1)), quint8(0x22));

        // Unlike an effect report this leaves the zone-select byte at 0: it
        // clears the register rather than writing an effect into it.
        for (int i = 2; i < reset.size(); ++i)
            QCOMPARE(quint8(reset.at(i)), quint8(0x00));
    }

    // ---- zone addressing -------------------------------------------------

    void zoneCommandAndBitmask_data()
    {
        QTest::addColumn<int>("zone");
        QTest::addColumn<int>("command");
        QTest::addColumn<int>("bitmask");
        QTest::newRow("zone0") << 0 << 0x20 << 0x01;
        QTest::newRow("zone1") << 1 << 0x21 << 0x02;
        QTest::newRow("zone2") << 2 << 0x22 << 0x04;
        QTest::newRow("zone5") << 5 << 0x25 << 0x20;
        QTest::newRow("zone7") << 7 << 0x27 << 0x80;
    }

    void zoneCommandAndBitmask()
    {
        QFETCH(int, zone);
        QFETCH(int, command);
        QFETCH(int, bitmask);

        const QByteArray b = RgbFusion2::buildZoneReport(
            zone, RgbFusion2::Mode::Static, Qt::white, 90,
            RgbFusion2::Speed::Normal, 0);

        // [1] command byte, [2] low byte of Zone_Sel0.
        QCOMPARE(quint8(b.at(1)), quint8(command));
        QCOMPARE(quint8(b.at(2)), quint8(bitmask));
    }

    // ---- DataFormat_8297 field offsets -----------------------------------

    void colourIsWrittenAsBgr()
    {
        // dwColor0 is a little-endian uint32 at [14..17], so the wire order is
        // B, G, R, 00 - NOT R, G, B.
        const QColor c(0x11, 0x22, 0x33);   // R=0x11 G=0x22 B=0x33
        const QByteArray b = RgbFusion2::buildZoneReport(
            0, RgbFusion2::Mode::Static, c, 90, RgbFusion2::Speed::Normal, 0);

        QCOMPARE(quint8(b.at(14)), quint8(0x33));   // blue
        QCOMPARE(quint8(b.at(15)), quint8(0x22));   // green
        QCOMPARE(quint8(b.at(16)), quint8(0x11));   // red
        QCOMPARE(quint8(b.at(17)), quint8(0x00));   // high byte of the dword
    }

    void brightnessOffsets()
    {
        const QByteArray b = RgbFusion2::buildZoneReport(
            0, RgbFusion2::Mode::Breathing, Qt::red, 80,
            RgbFusion2::Speed::Normal, 30);

        QCOMPARE(quint8(b.at(12)), quint8(80));   // MaxBrightness
        QCOMPARE(quint8(b.at(13)), quint8(30));   // MinBrightness
    }

    void brightnessIsClampedToPerModeCeiling()
    {
        // Static tops out at 90, flash at 100 - sending more is rejected by
        // the controller, so the builder must clamp.
        const QByteArray stat = RgbFusion2::buildZoneReport(
            0, RgbFusion2::Mode::Static, Qt::red, 100, RgbFusion2::Speed::Normal, 0);
        QCOMPARE(quint8(stat.at(12)), quint8(90));

        const QByteArray flash = RgbFusion2::buildZoneReport(
            0, RgbFusion2::Mode::Flash, Qt::red, 100, RgbFusion2::Speed::Normal, 0);
        QCOMPARE(quint8(flash.at(12)), quint8(100));
    }

    void minBrightnessNeverExceedsMax()
    {
        const QByteArray b = RgbFusion2::buildZoneReport(
            0, RgbFusion2::Mode::Breathing, Qt::red, 40,
            RgbFusion2::Speed::Normal, 99);
        QVERIFY(quint8(b.at(13)) <= quint8(b.at(12)));
        QCOMPARE(quint8(b.at(13)), quint8(40));
    }

    // ---- effect encoding -------------------------------------------------

    void modeEncoding_data()
    {
        QTest::addColumn<RgbFusion2::Mode>("mode");
        QTest::addColumn<int>("modeSel");      // [11]
        QTest::addColumn<int>("cycleCount");   // [30] CtrlVal0
        QTest::addColumn<int>("pulses");       // [31] CtrlVal1
        QTest::addColumn<int>("flashCount");   // [32] CtrlOem0

        QTest::newRow("off")     << RgbFusion2::Mode::Off         << 0x01 << 0 << 0 << 0;
        QTest::newRow("static")  << RgbFusion2::Mode::Static      << 0x01 << 0 << 0 << 0;
        QTest::newRow("breathe") << RgbFusion2::Mode::Breathing   << 0x02 << 0 << 1 << 0;
        QTest::newRow("flash")   << RgbFusion2::Mode::Flash       << 0x03 << 0 << 1 << 1;
        QTest::newRow("dflash")  << RgbFusion2::Mode::DoubleFlash << 0x03 << 0 << 1 << 2;
        QTest::newRow("cycle")   << RgbFusion2::Mode::ColorCycle  << 0x04 << 7 << 0 << 0;
    }

    void modeEncoding()
    {
        QFETCH(RgbFusion2::Mode, mode);
        QFETCH(int, modeSel);
        QFETCH(int, cycleCount);
        QFETCH(int, pulses);
        QFETCH(int, flashCount);

        const QByteArray b = RgbFusion2::buildZoneReport(
            0, mode, Qt::red, 100, RgbFusion2::Speed::Normal, 0);

        QCOMPARE(quint8(b.at(11)), quint8(modeSel));
        QCOMPARE(quint8(b.at(30)), quint8(cycleCount));
        QCOMPARE(quint8(b.at(31)), quint8(pulses));
        QCOMPARE(quint8(b.at(32)), quint8(flashCount));
    }

    void offIsStaticAtZeroBrightness()
    {
        // Off and Static share effect value 0x01; only the brightness differs.
        const QByteArray off = RgbFusion2::buildZoneReport(
            3, RgbFusion2::Mode::Off, Qt::green, 100, RgbFusion2::Speed::Normal, 0);
        QCOMPARE(quint8(off.at(11)), quint8(0x01));
        QCOMPARE(quint8(off.at(12)), quint8(0));
    }

    void flashAndDoubleFlashDifferOnlyInFlashCount()
    {
        QByteArray f = RgbFusion2::buildZoneReport(
            0, RgbFusion2::Mode::Flash, Qt::red, 100, RgbFusion2::Speed::Normal, 0);
        QByteArray d = RgbFusion2::buildZoneReport(
            0, RgbFusion2::Mode::DoubleFlash, Qt::red, 100, RgbFusion2::Speed::Normal, 0);

        QCOMPARE(quint8(f.at(32)), quint8(1));
        QCOMPARE(quint8(d.at(32)), quint8(2));

        // Blank the flash-count and the timing block; everything else must match.
        f[32] = d[32] = 0;
        QCOMPARE(f.mid(0, 22), d.mid(0, 22));
    }

    void colourlessModesSendNoColour()
    {
        // ColorCycle ignores the colour argument entirely.
        const QByteArray b = RgbFusion2::buildZoneReport(
            0, RgbFusion2::Mode::ColorCycle, Qt::magenta, 100,
            RgbFusion2::Speed::Normal, 0);
        QCOMPARE(quint8(b.at(14)), quint8(0));
        QCOMPARE(quint8(b.at(15)), quint8(0));
        QCOMPARE(quint8(b.at(16)), quint8(0));
    }

    // ---- timing block ----------------------------------------------------

    void timingsAreLittleEndianAtOffset22()
    {
        // Breathing at "normal" is 1200 / 1200 / 500 ms.
        const QByteArray b = RgbFusion2::buildZoneReport(
            0, RgbFusion2::Mode::Breathing, Qt::red, 90,
            RgbFusion2::Speed::Normal, 0);

        auto u16 = [&b](int off) {
            return quint16(quint8(b.at(off)) | (quint8(b.at(off + 1)) << 8));
        };
        QCOMPARE(u16(22), quint16(1200));
        QCOMPARE(u16(24), quint16(1200));
        QCOMPARE(u16(26), quint16(500));
    }

    void speedChangesTheTimingBlock()
    {
        const QByteArray slow = RgbFusion2::buildZoneReport(
            0, RgbFusion2::Mode::Breathing, Qt::red, 90,
            RgbFusion2::Speed::Slowest, 0);
        const QByteArray fast = RgbFusion2::buildZoneReport(
            0, RgbFusion2::Mode::Breathing, Qt::red, 90,
            RgbFusion2::Speed::Ludicrous, 0);
        QVERIFY(slow.mid(22, 6) != fast.mid(22, 6));
    }

    void modesWithoutSpeedLeaveTimingsZero()
    {
        const QByteArray b = RgbFusion2::buildZoneReport(
            0, RgbFusion2::Mode::Static, Qt::red, 90, RgbFusion2::Speed::Fastest, 0);
        QCOMPARE(b.mid(22, 8), QByteArray(8, char(0)));
    }

    // ---- IT8297_Info parsing ---------------------------------------------

    void parsesRealInfoReport()
    {
        // Captured verbatim from a B760M AORUS ELITE (IT5701).
        const QByteArray raw = QByteArray::fromHex(
            "CC010007 03050F00 00000003 49543537"
            "30312D47 49474142 59544520 56332E35"
            "2E31352E 30000000 00010200 02000100"
            "02000100 00010200 00010157 00000000");
        QCOMPARE(raw.size(), 64);

        const RgbFusion2::DeviceInfo info = RgbFusion2::parseInfoReport(raw);

        QCOMPARE(info.productName, QStringLiteral("IT5701-GIGABYTE V3.5.15.0"));
        QCOMPARE(info.firmwareVersion, QStringLiteral("3.5.15.0"));
        QCOMPARE(int(info.product), 1);
        QCOMPARE(int(info.deviceNum), 0);
        QCOMPARE(int(info.stripDetect), 0x07);
        QCOMPARE(int(info.suppCmdFlag), 0x03);
        QCOMPARE(info.chipId, 0x57010100u);
    }

    void infoParserToleratesShortReports()
    {
        // Must not read out of bounds when the device answers with less.
        const RgbFusion2::DeviceInfo a = RgbFusion2::parseInfoReport(QByteArray());
        QCOMPARE(a.productName, QString());

        const RgbFusion2::DeviceInfo b =
            RgbFusion2::parseInfoReport(QByteArray::fromHex("CC010007"));
        QCOMPARE(int(b.product), 0);   // too short to trust any field
    }

    void productStringStopsAtTheFixedFieldWidth()
    {
        // ProductString is 28 bytes at [12..39] with no NUL when full; the
        // parser must not run into CalStrip3 at [40].
        QByteArray raw(64, char(0));
        raw[0] = char(0xCC);
        raw.replace(12, 28, QByteArray(28, 'A'));
        raw.replace(40, 4, QByteArray::fromHex("DEADBEEF"));

        const RgbFusion2::DeviceInfo info = RgbFusion2::parseInfoReport(raw);
        QCOMPARE(info.productName, QString(28, QLatin1Char('A')));
    }

    // ---- gen2_strip_info parsing -----------------------------------------

    void stripScanCommandPairs()
    {
        // header 0: 0x3C/0x3E, 1: 0x3D/0x3F, 2: 0x38/0x3A, 3: 0x39/0x3B
        QCOMPARE(int(RgbFusion2::kStripCmds[0][0]), 0x3C);
        QCOMPARE(int(RgbFusion2::kStripCmds[0][1]), 0x3E);
        QCOMPARE(int(RgbFusion2::kStripCmds[1][0]), 0x3D);
        QCOMPARE(int(RgbFusion2::kStripCmds[1][1]), 0x3F);
        QCOMPARE(int(RgbFusion2::kStripCmds[2][0]), 0x38);
        QCOMPARE(int(RgbFusion2::kStripCmds[2][1]), 0x3A);
        QCOMPARE(int(RgbFusion2::kStripCmds[3][0]), 0x39);
        QCOMPARE(int(RgbFusion2::kStripCmds[3][1]), 0x3B);
    }

    void parsesEmptyStripHeader()
    {
        // What an unpopulated header actually returns on real hardware.
        QByteArray raw(64, char(0));
        raw[0] = char(0xCC);

        RgbFusion2::StripInfo si;
        QVERIFY(RgbFusion2::parseStripInfo(raw, 2, &si));
        QCOMPARE(si.header, 2);
        QCOMPARE(int(si.numStrip), 0);
        QCOMPARE(si.totalLeds, quint16(0));
        QVERIFY(si.ledCounts.isEmpty());
    }

    void parsesPopulatedStripHeader()
    {
        // numStrip = 3, LED counts 32 / 8 / 16 as little-endian u16.
        QByteArray raw(64, char(0));
        raw[0] = char(0xCC);
        raw[1] = char(3);
        raw[2] = char(32); raw[3] = char(0);
        raw[4] = char(8);  raw[5] = char(0);
        raw[6] = char(16); raw[7] = char(0);

        RgbFusion2::StripInfo si;
        QVERIFY(RgbFusion2::parseStripInfo(raw, 0, &si));
        QCOMPARE(int(si.numStrip), 3);
        QCOMPARE(si.ledCounts, QVector<quint16>({32, 8, 16}));
        QCOMPARE(si.totalLeds, quint16(56));
    }

    void stripParserRejectsShortReports()
    {
        RgbFusion2::StripInfo si;
        QVERIFY(!RgbFusion2::parseStripInfo(QByteArray(31, char(0)), 0, &si));
    }

    // ---- LampArray report geometry ---------------------------------------

    void lampArrayReportLengths()
    {
        // Derived from the descriptor's Report Size / Report Count pairs.
        QCOMPARE(LampArray::kLenAttributes,   1 + 2 + 5 * 4);
        QCOMPARE(LampArray::kLenAttrRequest,  1 + 2);
        QCOMPARE(LampArray::kLenAttrResponse, 1 + 2 + 5 * 4 + 6);
        QCOMPARE(LampArray::kLenMultiUpdate,  1 + 1 + 1 + 8 * 2 + 8 * 4);
        QCOMPARE(LampArray::kLenRangeUpdate,  1 + 1 + 2 + 2 + 4);
        QCOMPARE(LampArray::kLenControl,      1 + 1);

        QCOMPARE(LampArray::kLenMultiUpdate, 51);
        QCOMPARE(LampArray::kLenRangeUpdate, 10);
    }

    void lampArrayKindNames()
    {
        // 7 is Chassis, which is what a motherboard reports. Getting this
        // table wrong is easy and silently mislabels the device.
        QCOMPARE(LampArray::kindName(7), QStringLiteral("机箱"));
        QCOMPARE(LampArray::kindName(1), QStringLiteral("键盘"));
        QCOMPARE(LampArray::kindName(13), QStringLiteral("音箱"));
    }

    void lampArrayPurposeBits()
    {
        // Bit 1 (value 2) is Accent, not Status - Status is bit 3.
        QCOMPARE(LampArray::purposeNames(0x02), QStringLiteral("氛围点缀"));
        QCOMPARE(LampArray::purposeNames(0x08), QStringLiteral("状态指示"));
        QCOMPARE(LampArray::purposeNames(0x01 | 0x10),
                 QStringLiteral("控制/照明"));
        QCOMPARE(LampArray::purposeNames(0), QStringLiteral("-"));
    }
};

QTEST_MAIN(ProtocolTest)
#include "tst_protocol.moc"
