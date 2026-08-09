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
#include <QRect>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVector>

class Controller : public QObject
{
    Q_OBJECT

    // ---- application ------------------------------------------------------
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)

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

    // The selection is a bitmask - editing several zones at once is the whole
    // point of the zone bar. `selectedZone` stays for the cases that need
    // exactly one (renaming), and reports -1 whenever that is not the case.
    Q_PROPERTY(int          selectionMask READ selectionMask NOTIFY selectedZoneChanged)
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

    // ---- profiles ---------------------------------------------------------
    Q_PROPERTY(QStringList profiles      READ profiles      NOTIFY profilesChanged)
    Q_PROPERTY(QString     activeProfile READ activeProfile NOTIFY profilesChanged)

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
    ~Controller() override;

    QString appVersion() const;

    // Screenshot mode renders one frame at a fixed size and quits; letting that
    // land in the config would mean a build-time aid moving the user's window.
    void setSavesWindowState(bool on) { m_savesWindow = on; }

    bool    connected() const { return m_rgb.isOpen(); }
    QString deviceName() const;
    QString deviceDetail() const;
    QString devicePath() const { return m_rgb.devicePath(); }

    QString statusText() const { return m_status; }
    bool    statusIsError() const { return m_statusIsError; }

    QVariantList zones() const;
    int          selectionMask() const { return m_selection; }
    int          selectedZone() const;
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

    QStringList profiles() const { return m_profiles; }
    QString     activeProfile() const { return m_activeProfile; }

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

    // ---- selection --------------------------------------------------------
    void selectAllZones();
    void selectOnlyZone(int zone);

    // Ctrl-click: adds or removes one zone. Removing the last one is ignored.
    void toggleZone(int zone);

    // ---- profiles ---------------------------------------------------------
    //
    // Switching loads a snapshot over the live zones without saving the
    // outgoing ones: a profile is what you saved, not where you drifted to.
    void selectProfile(const QString &name);
    void saveProfileAs(const QString &name);
    void updateActiveProfile();
    void deleteProfile(const QString &name);

    void saveCustomColour(int slot, const QColor &c);
    void clearCustomColour(int slot);

    // The detection wizard is driven from QML one answer at a time rather than
    // with nested modal dialogs, so the window stays live throughout.
    void beginDetection();
    void answerDetection(bool hasLeds);
    void cancelDetection();

    // logind's PrepareForSleep: true on the way down, false once back. A slot
    // rather than a lambda because the D-Bus connection is made by signature.
    void onPrepareForSleep(bool sleeping);

    void lampRescan();
    void lampApplyAll();

    // ---- window geometry --------------------------------------------------
    //
    // The window draws its own frame, so no session manager puts it back where
    // it was; QML hands the last windowed rectangle here on close.
    QRect windowGeometry() const { return m_window.geometry; }
    bool  windowMaximized() const { return m_window.maximized; }
    void  saveWindow(const QRect &geometry, bool maximized);

    // Everything logged so far. The device is opened in the constructor, well
    // before the QML log view exists, so those lines would otherwise be lost -
    // and they are exactly the ones worth seeing when a device fails to open.
    QVariantList logBacklog() const { return m_logBacklog; }
    void clearLog();

    // Filtering happens here rather than in a QML model: the backlog is capped
    // at 800 entries, so rebuilding the view's model on a filter change is
    // cheaper than carrying a proxy model around.
    QVariantList filteredLog(bool errorsOnly, const QString &needle) const;

    // True when a line would survive the current filter - the view uses it to
    // decide whether an arriving line belongs on screen.
    bool logLineMatches(const QString &text, bool isError,
                        bool errorsOnly, const QString &needle) const;

    void copyLogToClipboard(bool errorsOnly, const QString &needle);
    void exportLog(const QUrl &file, bool errorsOnly, const QString &needle);

signals:
    void deviceChanged();
    void statusChanged();
    void zonesChanged();
    void selectedZoneChanged();
    void settingsChanged();
    void autoApplyChanged();
    void pendingChanged();
    void customColoursChanged();
    void profilesChanged();
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

    // Runs every few seconds. While connected it checks the hidraw node is
    // still there - a replug or a resume renumbers /dev/hidrawN and the old fd
    // just starts failing - and while disconnected it quietly retries the open,
    // so the connection dot stops being a lie.
    void watchdogTick();

    // Called when a write fails: decides whether that was a transient error,
    // the device going away, or the controller firmware hanging, and drops the
    // handle in the second case.
    void handleIoFailure(const QString &message);

    // Reports the hang and shuts down every write path. Returns true when the
    // caller should give up rather than carry on down its loop.
    bool checkWedged();

    // Pushes the saved effects back out. Both the reconnect path and the
    // resume-from-suspend path need it: either way the controller is in its
    // power-on state whatever the UI shows, and it has no command to read the
    // current effects back.
    void reapplySaved(const QString &reason);

    bool hasManagedZones() const;

    void setStatus(const QString &text, bool isError = false);
    void log(const QString &text, bool isError = false);

    bool isSelected(int zone) const { return m_selection & (1u << zone); }

    // Which zone the editor reads from. For a single selection that is the zone
    // itself; for a wider one it is the first selected zone that actually has
    // LEDs, so the controls reflect what is visible on the board rather than an
    // arbitrary empty zone.
    int representativeZone() const;

    void setSelection(quint8 mask);

    // Runs `fn` over every zone the current selection covers.
    template <typename F> void forSelectedZones(F fn);

    // Stages and commits the selection without touching the status line, for
    // the auto-apply path.
    void flush();
    void scheduleFlush();
    void setPending(bool on);

    // Dragging a slider with auto-apply on used to rewrite the whole INI every
    // 120 ms. The state that matters is the one you stop on, so writes are
    // coalesced - and forced out in the destructor, because quitting right
    // after a change must not lose it.
    void scheduleSave();
    void saveNow();

    // Lights exactly one zone white, everything else off - the wizard's probe.
    bool lightOnlyZone(int zone, QString *error);

    void finishDetection();

    RgbFusion2  m_rgb;
    LampArray   m_lamp;
    ZoneSetting m_zones[RgbFusion2::kZoneCount];

    // Bit i = zone i is being edited. Never zero: with nothing selected every
    // control on the page would edit nothing, which is not a state worth being
    // able to reach.
    quint8 m_selection = 0xFF;
    bool m_autoApply    = true;
    bool m_pending      = false;

    // Set once the controller firmware has hung. Every later write is refused
    // rather than attempted: it is not coming back without losing power, and
    // the old behaviour - auto-apply retrying on every slider move - turned one
    // red line into a screenful of them. Only a rescan clears it.
    bool m_wedged = false;

    QString m_status;
    bool    m_statusIsError = false;
    QString m_lastOpenError;

    QVector<QColor> m_customColours;
    QVariantList    m_logBacklog;

    QStringList m_profiles;
    QString     m_activeProfile;

    bool m_detecting  = false;
    int  m_detectZone = 0;

    QString m_lampInfo    = QStringLiteral("未连接");
    QColor  m_lampColour  = QColor(0, 128, 255);
    bool    m_autonomous  = true;

    Config::WindowState m_window;
    bool                m_savesWindow = true;

    QTimer m_flushTimer;
    QTimer m_watchTimer;
    QTimer m_saveTimer;
};
