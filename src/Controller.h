// Backend for the Qt Quick UI.
//
// Everything the QML layer needs to know about the protocol is published from
// here - which effects exist, whether each one uses a colour or a speed, what
// its brightness ceiling is - so the UI never hardcodes protocol facts that
// RgbFusion2 already owns.
//
// The editing state is not kept separately from the zone table: writing `mode`,
// `colour`, `brightness`, `minBrightness` or `speed` mutates the selected zone
// (or every zone, when "all" is selected) straight away. That keeps the UI and
// the model in sync by construction, and makes the optional auto-apply just a
// debounced flush of the same data.
#pragma once

#include "Config.h"
#include "LampArray.h"
#include "RgbFusion2.h"

#include <QColor>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVector>

class Controller : public QObject
{
    Q_OBJECT

    // ---- device -----------------------------------------------------------
    Q_PROPERTY(bool    connected    READ connected    NOTIFY deviceChanged)
    Q_PROPERTY(QString deviceName   READ deviceName   NOTIFY deviceChanged)
    Q_PROPERTY(QString deviceDetail READ deviceDetail NOTIFY deviceChanged)
    Q_PROPERTY(QString devicePath   READ devicePath   NOTIFY deviceChanged)

    // ---- status -----------------------------------------------------------
    Q_PROPERTY(QString statusText    READ statusText    NOTIFY statusChanged)
    Q_PROPERTY(bool    statusIsError READ statusIsError NOTIFY statusChanged)

    // ---- zones ------------------------------------------------------------
    Q_PROPERTY(QVariantList zones        READ zones        NOTIFY zonesChanged)
    Q_PROPERTY(int          selectedZone READ selectedZone WRITE setSelectedZone
                                         NOTIFY selectedZoneChanged)
    Q_PROPERTY(QString      selectionLabel READ selectionLabel
                                           NOTIFY selectedZoneChanged)

    // ---- the effect catalogue (static protocol facts, for the rail) -------
    Q_PROPERTY(QVariantList effects READ effects CONSTANT)

    // ---- current effect settings -----------------------------------------
    Q_PROPERTY(int    mode          READ mode          WRITE setMode
                                    NOTIFY settingsChanged)
    Q_PROPERTY(QColor colour        READ colour        WRITE setColour
                                    NOTIFY settingsChanged)
    Q_PROPERTY(int    brightness    READ brightness    WRITE setBrightness
                                    NOTIFY settingsChanged)
    Q_PROPERTY(int    minBrightness READ minBrightness WRITE setMinBrightness
                                    NOTIFY settingsChanged)
    Q_PROPERTY(int    speed         READ speed         WRITE setSpeed
                                    NOTIFY settingsChanged)

    // Capabilities of the *currently selected* effect, so the settings panel
    // can show only the controls that do something.
    Q_PROPERTY(bool    usesColour    READ usesColour    NOTIFY settingsChanged)
    Q_PROPERTY(bool    usesSpeed     READ usesSpeed     NOTIFY settingsChanged)
    Q_PROPERTY(int     brightnessCap READ brightnessCap NOTIFY settingsChanged)
    Q_PROPERTY(QString speedName     READ speedName     NOTIFY settingsChanged)

    Q_PROPERTY(bool autoApply READ autoApply WRITE setAutoApply
                              NOTIFY autoApplyChanged)

    // True when the edited settings have not been sent to the controller yet.
    // Drives the apply button: nothing pending, nothing to press.
    Q_PROPERTY(bool pending READ pending NOTIFY pendingChanged)

    // ---- custom colour swatches ------------------------------------------
    Q_PROPERTY(QVariantList customColours READ customColours
                                          NOTIFY customColoursChanged)
    Q_PROPERTY(QVariantList presetColours READ presetColours CONSTANT)

    // ---- zone detection wizard -------------------------------------------
    Q_PROPERTY(bool detecting READ detecting NOTIFY detectionChanged)
    Q_PROPERTY(int  detectZone READ detectZone NOTIFY detectionChanged)

    // ---- LampArray page ---------------------------------------------------
    Q_PROPERTY(bool         lampConnected  READ lampConnected  NOTIFY lampChanged)
    Q_PROPERTY(QString      lampInfo       READ lampInfo       NOTIFY lampChanged)
    Q_PROPERTY(QVariantList lamps          READ lamps          NOTIFY lampChanged)
    Q_PROPERTY(QColor       lampColour     READ lampColour     WRITE setLampColour
                                           NOTIFY lampColourChanged)
    Q_PROPERTY(bool         autonomousMode READ autonomousMode WRITE setAutonomousMode
                                           NOTIFY lampChanged)

public:
    explicit Controller(QObject *parent = nullptr);

    bool    connected() const { return m_rgb.isOpen(); }
    QString deviceName() const;
    QString deviceDetail() const;
    QString devicePath() const { return m_rgb.devicePath(); }

    QString statusText() const { return m_status; }
    bool    statusIsError() const { return m_statusIsError; }

    QVariantList zones() const;
    int          selectedZone() const { return m_selectedZone; }
    void         setSelectedZone(int zone);
    QString      selectionLabel() const;

    QVariantList effects() const;

    int    mode() const;
    QColor colour() const;
    int    brightness() const;
    int    minBrightness() const;
    int    speed() const;

    void setMode(int mode);
    void setColour(const QColor &c);
    void setBrightness(int v);
    void setMinBrightness(int v);
    void setSpeed(int v);

    bool    usesColour() const;
    bool    usesSpeed() const;
    int     brightnessCap() const;
    QString speedName() const;

    bool autoApply() const { return m_autoApply; }
    void setAutoApply(bool on);

    bool pending() const { return m_pending; }

    QVariantList customColours() const;
    QVariantList presetColours() const;

    bool detecting() const { return m_detecting; }
    int  detectZone() const { return m_detectZone; }

    bool         lampConnected() const { return m_lamp.isOpen(); }
    QString      lampInfo() const { return m_lampInfo; }
    QVariantList lamps() const;
    QColor       lampColour() const { return m_lampColour; }
    void         setLampColour(const QColor &c);
    bool         autonomousMode() const { return m_autonomous; }
    void         setAutonomousMode(bool on);

public slots:
    void rescan();
    void apply();
    void allOff();
    void renameZone(int zone, const QString &name);

    // Toggles a zone between "has LEDs" and "empty" by hand, for people who
    // already know their board and do not want to sit through the wizard.
    void setZoneConnected(int zone, bool connected);

    void saveCustomColour(int slot, const QColor &c);
    void clearCustomColour(int slot);

    // The detection wizard is driven from QML one answer at a time rather than
    // with nested modal dialogs, so the window stays live throughout.
    void beginDetection();
    void answerDetection(bool hasLeds);
    void cancelDetection();

    void lampRescan();
    void lampApplyAll();

    // Everything logged so far. The device is opened in the constructor, well
    // before the QML log view exists, so those lines would otherwise be lost -
    // and they are exactly the ones worth seeing when a device fails to open.
    QVariantList logBacklog() const { return m_logBacklog; }
    void clearLog();

signals:
    void deviceChanged();
    void statusChanged();
    void zonesChanged();
    void selectedZoneChanged();
    void settingsChanged();
    void autoApplyChanged();
    void pendingChanged();
    void customColoursChanged();
    void detectionChanged();
    void lampChanged();
    void lampColourChanged();

    // One formatted line for the protocol log. QML appends these to a
    // ListModel; emitting beats republishing a whole list on every report.
    void logLine(const QString &timestamp, const QString &text, bool isError);

private:
    void onTraffic(bool outgoing, const QString &label, const QByteArray &data);

    void connectDevice();
    void connectLampArray();

    void setStatus(const QString &text, bool isError = false);
    void log(const QString &text, bool isError = false);

    // Which zone the editor reads from. For a single selection that is the zone
    // itself; for "all zones" it is the first zone that actually has LEDs, so
    // the controls reflect what is visible on the board rather than an
    // arbitrary empty zone 0.
    int representativeZone() const;

    // Runs `fn` over every zone the current selection covers.
    template <typename F> void forSelectedZones(F fn);

    // Stages and commits the selection without touching the status line, for
    // the auto-apply path.
    void flush();
    void scheduleFlush();
    void setPending(bool on);

    // Lights exactly one zone white, everything else off - the wizard's probe.
    bool lightOnlyZone(int zone, QString *error);

    void finishDetection();

    RgbFusion2  m_rgb;
    LampArray   m_lamp;
    ZoneSetting m_zones[RgbFusion2::kZoneCount];

    int  m_selectedZone = -1;      // -1 = all zones
    bool m_autoApply    = true;
    bool m_pending      = false;

    QString m_status;
    bool    m_statusIsError = false;

    QVector<QColor> m_customColours;
    QVariantList    m_logBacklog;

    bool m_detecting  = false;
    int  m_detectZone = 0;

    QString m_lampInfo    = QStringLiteral("未连接");
    QColor  m_lampColour  = QColor(0, 128, 255);
    bool    m_autonomous  = true;

    QTimer m_flushTimer;
};
