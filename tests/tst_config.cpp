// Tests for the persisted state and for the one rule that guards it.
//
// Every test runs with QStandardPaths in test mode, which redirects the config
// location into a temporary tree - the real ~/.config must never be touched by
// a test run, least of all a test run that writes zone state.
#include "../src/Config.h"

#include <QStandardPaths>
#include <QTest>

class ConfigTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        // Nothing else should be able to reach out of the sandbox; if it can,
        // every later assertion here is meaningless.
        QVERIFY(!Config::path().startsWith(QDir::homePath() + QStringLiteral("/.config/")));
    }

    void init()
    {
        QFile::remove(Config::path());
    }

    // ---- zone state -------------------------------------------------------

    void loadReportsMissingFile()
    {
        ZoneSetting zones[RgbFusion2::kZoneCount];
        QVERIFY(!Config::load(zones));
        // Defaults have to survive the failed load: the caller goes on to use
        // this array either way.
        QCOMPARE(zones[0].brightness, 90);
        QCOMPARE(int(zones[0].mode), int(RgbFusion2::Mode::Static));
    }

    void zoneRoundTrip()
    {
        ZoneSetting out[RgbFusion2::kZoneCount];
        for (int i = 0; i < RgbFusion2::kZoneCount; ++i) {
            out[i].mode          = RgbFusion2::Mode::Breathing;
            out[i].colour        = QColor(10 + i, 20 + i, 30 + i);
            out[i].brightness    = 50 + i;
            out[i].minBrightness = 10 + i;
            out[i].speed         = RgbFusion2::Speed::Faster;
            out[i].managed       = (i % 2 == 0);
            out[i].connected     = (i % 3 == 0);
            out[i].probed        = true;
            out[i].name          = QStringLiteral("区域·%1").arg(i);
        }
        Config::save(out);

        ZoneSetting in[RgbFusion2::kZoneCount];
        QVERIFY(Config::load(in));
        for (int i = 0; i < RgbFusion2::kZoneCount; ++i) {
            QCOMPARE(int(in[i].mode), int(out[i].mode));
            QCOMPARE(in[i].colour, out[i].colour);
            QCOMPARE(in[i].brightness, out[i].brightness);
            QCOMPARE(in[i].minBrightness, out[i].minBrightness);
            QCOMPARE(int(in[i].speed), int(out[i].speed));
            QCOMPARE(in[i].managed, out[i].managed);
            QCOMPARE(in[i].connected, out[i].connected);
            QCOMPARE(in[i].probed, out[i].probed);
            // Non-ASCII names are the normal case here, so the INI codec has to
            // survive the trip.
            QCOMPARE(in[i].name, out[i].name);
        }
    }

    void everyModeSurvivesTheRoundTrip()
    {
        const RgbFusion2::Mode modes[] = {
            RgbFusion2::Mode::Off,   RgbFusion2::Mode::Static,
            RgbFusion2::Mode::Breathing, RgbFusion2::Mode::Flash,
            RgbFusion2::Mode::DoubleFlash, RgbFusion2::Mode::ColorCycle,
        };
        for (RgbFusion2::Mode m : modes) {
            ZoneSetting out[RgbFusion2::kZoneCount];
            out[0].mode = m;
            Config::save(out);

            ZoneSetting in[RgbFusion2::kZoneCount];
            QVERIFY(Config::load(in));
            QCOMPARE(int(in[0].mode), int(m));
        }
    }

    // ---- custom swatches --------------------------------------------------

    void customColoursKeepEmptySlots()
    {
        QVector<QColor> out(Config::kCustomColourSlots);
        out[0] = QColor(255, 0, 0);
        out[3] = QColor(0, 255, 0);
        Config::saveCustomColours(out);

        const QVector<QColor> in = Config::loadCustomColours();
        QCOMPARE(in.size(), Config::kCustomColourSlots);
        QCOMPARE(in[0], QColor(255, 0, 0));
        QCOMPARE(in[3], QColor(0, 255, 0));
        // A blank slot must stay blank rather than shifting the ones after it.
        QVERIFY(!in[1].isValid());
        QVERIFY(!in[7].isValid());
    }

    void swatchesSurviveAZoneSave()
    {
        QVector<QColor> swatches(Config::kCustomColourSlots);
        swatches[2] = QColor(1, 2, 3);
        Config::saveCustomColours(swatches);

        ZoneSetting zones[RgbFusion2::kZoneCount];
        Config::save(zones);

        QCOMPARE(Config::loadCustomColours()[2], QColor(1, 2, 3));
    }

    // ---- profiles ---------------------------------------------------------

    void profilesStartEmpty()
    {
        QVERIFY(Config::profileNames().isEmpty());
        QVERIFY(Config::activeProfile().isEmpty());
    }

    void profileRoundTrip()
    {
        ZoneSetting out[RgbFusion2::kZoneCount];
        out[0].mode       = RgbFusion2::Mode::ColorCycle;
        out[0].brightness = 77;
        out[3].colour     = QColor(9, 8, 7);
        out[3].name       = QStringLiteral("IO 护罩");
        Config::saveProfile(QStringLiteral("夜间"), out);

        QCOMPARE(Config::profileNames(), QStringList{QStringLiteral("夜间")});

        ZoneSetting in[RgbFusion2::kZoneCount];
        QVERIFY(Config::loadProfile(QStringLiteral("夜间"), in));
        QCOMPARE(int(in[0].mode), int(RgbFusion2::Mode::ColorCycle));
        QCOMPARE(in[0].brightness, 77);
        QCOMPARE(in[3].colour, QColor(9, 8, 7));
        QCOMPARE(in[3].name, QStringLiteral("IO 护罩"));
    }

    void savingUnderAnExistingNameOverwritesIt()
    {
        ZoneSetting z[RgbFusion2::kZoneCount];
        z[0].brightness = 10;
        Config::saveProfile(QStringLiteral("同名"), z);
        z[0].brightness = 20;
        Config::saveProfile(QStringLiteral("同名"), z);

        // One profile, not two - the name is the identity.
        QCOMPARE(Config::profileNames().size(), 1);

        ZoneSetting in[RgbFusion2::kZoneCount];
        QVERIFY(Config::loadProfile(QStringLiteral("同名"), in));
        QCOMPARE(in[0].brightness, 20);
    }

    void profilesAreIndependentOfTheLiveState()
    {
        ZoneSetting saved[RgbFusion2::kZoneCount];
        saved[0].brightness = 42;
        Config::saveProfile(QStringLiteral("方案"), saved);

        ZoneSetting live[RgbFusion2::kZoneCount];
        live[0].brightness = 99;
        Config::save(live);

        ZoneSetting in[RgbFusion2::kZoneCount];
        QVERIFY(Config::loadProfile(QStringLiteral("方案"), in));
        QCOMPARE(in[0].brightness, 42);

        ZoneSetting liveBack[RgbFusion2::kZoneCount];
        QVERIFY(Config::load(liveBack));
        QCOMPARE(liveBack[0].brightness, 99);
    }

    void namesMayContainWhateverTheUserTyped()
    {
        // The name is a value, not an INI group name, precisely so these do not
        // have to be escaped or rejected.
        const QStringList awkward = {
            QStringLiteral("a/b"), QStringLiteral("with space"),
            QStringLiteral("方案=1"), QStringLiteral("[brackets]"),
        };
        ZoneSetting z[RgbFusion2::kZoneCount];
        for (const QString &n : awkward)
            Config::saveProfile(n, z);

        const QStringList got = Config::profileNames();
        QCOMPARE(got.size(), awkward.size());
        for (const QString &n : awkward) {
            QVERIFY2(got.contains(n), qPrintable(n));
            QVERIFY2(Config::loadProfile(n, z), qPrintable(n));
        }
    }

    void deletingAProfileLeavesTheOthers()
    {
        ZoneSetting z[RgbFusion2::kZoneCount];
        Config::saveProfile(QStringLiteral("一"), z);
        Config::saveProfile(QStringLiteral("二"), z);
        Config::saveProfile(QStringLiteral("三"), z);

        Config::removeProfile(QStringLiteral("二"));

        const QStringList left = Config::profileNames();
        QCOMPARE(left.size(), 2);
        QVERIFY(left.contains(QStringLiteral("一")));
        QVERIFY(left.contains(QStringLiteral("三")));
        QVERIFY(!Config::loadProfile(QStringLiteral("二"), z));

        // The freed slot gets reused rather than leaving a hole that the index
        // search skips past.
        Config::saveProfile(QStringLiteral("四"), z);
        QCOMPARE(Config::profileNames().size(), 3);
    }

    void deletingTheActiveProfileClearsTheSelection()
    {
        ZoneSetting z[RgbFusion2::kZoneCount];
        Config::saveProfile(QStringLiteral("当前"), z);
        Config::setActiveProfile(QStringLiteral("当前"));
        QCOMPARE(Config::activeProfile(), QStringLiteral("当前"));

        Config::removeProfile(QStringLiteral("当前"));
        QVERIFY(Config::activeProfile().isEmpty());
    }

    void loadingAMissingProfileLeavesZonesAlone()
    {
        ZoneSetting z[RgbFusion2::kZoneCount];
        z[0].brightness = 33;
        QVERIFY(!Config::loadProfile(QStringLiteral("没有这个"), z));
        QCOMPARE(z[0].brightness, 33);
    }

    // ---- window state -----------------------------------------------------

    void windowStateRoundTrip()
    {
        Config::WindowState out;
        out.geometry  = QRect(120, 60, 1024, 768);
        out.maximized = true;
        Config::saveWindow(out);

        const Config::WindowState in = Config::loadWindow();
        QCOMPARE(in.geometry, out.geometry);
        QCOMPARE(in.maximized, out.maximized);
    }

    void windowStateIsAbsentUntilSaved()
    {
        ZoneSetting zones[RgbFusion2::kZoneCount];
        Config::save(zones);

        // An invalid rectangle is how the QML side knows to keep its own
        // default size instead of resizing to nothing.
        QVERIFY(!Config::loadWindow().geometry.isValid());
    }

    // ---- the brightness ceiling ------------------------------------------

    void switchingModeClampsBrightnessToTheModeCeiling()
    {
        ZoneSetting z;
        z.brightness = 100;              // legal for Flash
        z.setMode(RgbFusion2::Mode::Flash);
        QCOMPARE(z.brightness, 100);

        // Static caps at 90 in the controller's Mode_Sel table; carrying 100
        // across would be clipped silently by the hardware instead.
        z.setMode(RgbFusion2::Mode::Static);
        QCOMPARE(z.brightness, 90);
        QVERIFY(z.managed);
    }

    void theFloorNeverOutrunsTheCeiling()
    {
        ZoneSetting z;
        z.brightness    = 100;
        z.minBrightness = 100;
        z.setMode(RgbFusion2::Mode::Breathing);   // ceiling 90
        QCOMPARE(z.brightness, 90);
        QCOMPARE(z.minBrightness, 90);
    }

    void turningOffKeepsTheBrightnessForLater()
    {
        ZoneSetting z;
        z.brightness = 75;
        z.setMode(RgbFusion2::Mode::Off);
        // "Off" has a ceiling of 0, but zeroing the stored value would break
        // the promise the UI makes: pick an effect again and it comes back.
        QCOMPARE(z.brightness, 75);
        QCOMPARE(int(z.mode), int(RgbFusion2::Mode::Off));
    }
};

QTEST_MAIN(ConfigTest)
#include "tst_config.moc"
