#include "Config.h"
#include "Controller.h"
#include "LampArray.h"
#include "RgbFusion2.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QHash>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTextStream>
#include <QThread>
#include <QTimer>

#include <pwd.h>
#include <unistd.h>

namespace {

// Set by CMake from the project version.
#ifndef GRC_VERSION
#define GRC_VERSION "0.0.0"
#endif

// Must match the installed desktop file's basename: on Wayland the compositor
// matches a window to its .desktop entry - and therefore to its icon and its
// name in the task switcher - through the app ID Qt derives from this.
const QString kDesktopFileName = QStringLiteral("GigabyteRGBController");

// One socket per user, so two people on the same machine do not collide.
QString instanceSocketName()
{
    return QStringLiteral("GigabyteRGBController-%1").arg(::getuid());
}

// A second copy would fight the first over one controller and one config file,
// so the request is handed to whoever is already running.
bool handedOverToRunningInstance()
{
    QLocalSocket sock;
    sock.connectToServer(instanceSocketName());
    if (!sock.waitForConnected(300))
        return false;

    sock.write("raise");
    sock.waitForBytesWritten(300);
    sock.disconnectFromServer();
    return true;
}

// Headless diagnostics: enumerate every hidraw node, show which one matches the
// RGB Fusion lighting collection, and try to talk to it. Useful over SSH and
// when debugging permissions.
int runProbe()
{
    QTextStream out(stdout);

    out << "=== hidraw 节点枚举 ===\n";
    const QVector<HidRawInfo> nodes = HidRawDevice::enumerate();
    if (nodes.isEmpty())
        out << "(没有可见的 hidraw 节点)\n";

    static const QByteArray kLightingUsage  = QByteArray::fromHex("0689FF09CC");
    static const QByteArray kLampArrayUsage = QByteArray::fromHex("055909 01A101");

    for (const HidRawInfo &info : nodes) {
        const bool isIte      = (info.vendorId == RgbFusion2::kVendorId);
        const bool isLighting = info.reportDescriptor.contains(kLightingUsage);
        const bool isLamp     = info.reportDescriptor.startsWith(kLampArrayUsage);

        const char *tag = isLighting ? "<= RGB Fusion 接口 (0xFF89/0xCC)"
                        : isLamp     ? "<= HID LampArray 接口 (0x59)"
                        : isIte      ? "(ITE，其他接口)"
                                     : "";

        out << QString::asprintf("%-16s %04x:%04x  %-34s %s\n",
                                 qPrintable(info.path),
                                 info.vendorId, info.productId,
                                 qPrintable(info.name), tag);
    }

    out << "\n=== 打开灯光接口 ===\n";
    RgbFusion2 rgb;
    QString error;
    if (!rgb.openFirstDevice(&error)) {
        out << "失败: " << error << "\n";
        return 1;
    }
    out << "已打开: " << rgb.devicePath() << "\n";

    if (!rgb.initialize(&error)) {
        out << "初始化失败: " << error << "\n";
        return 1;
    }

    const RgbFusion2::DeviceInfo &info = rgb.deviceInfo();
    out << "产品名称      : " << info.productName << "\n"
        << "固件版本      : " << info.firmwareVersion << "\n"
        << "Product       : " << info.product << "\n"
        << "DeviceNum     : " << info.deviceNum << "\n"
        << "StripDetect   : 0x" << QString::number(info.stripDetect, 16) << "\n"
        << "StripCtrlLen0 : " << info.stripCtrlLength0 << "\n"
        << "StripCtrlLen1 : " << info.stripCtrlLength1 << "\n"
        << "SuppCmdFlag   : 0x" << QString::number(info.suppCmdFlag, 16) << "\n"
        << "ChipId        : 0x" << QString::number(info.chipId, 16) << "\n"
        << "\n原始信息报文:\n" << RgbFusion2::hexDump(info.raw) << "\n";

    out << "\n=== ARGB 可寻址灯带头 ===\n";
    for (int h = 0; h < RgbFusion2::kStripHeaderCount; ++h) {
        RgbFusion2::StripInfo si;
        if (!rgb.scanStripHeader(h, &si, true, &error)) {
            out << "  灯带头 " << h << ": 扫描失败 - " << error << "\n";
            continue;
        }
        if (si.numStrip == 0) {
            out << "  灯带头 " << h << ": 未接灯带\n";
        } else {
            QStringList parts;
            for (uint16_t n : si.ledCounts)
                parts << QString::number(n);
            out << "  灯带头 " << h << ": " << si.numStrip << " 段，灯数 "
                << parts.join(QStringLiteral("+")) << " = " << si.totalLeds << "\n";
        }
    }

    out << "\n=== HID LampArray 接口 ===\n";
    LampArray lamp;
    if (!lamp.openFirstDevice(&error)) {
        out << "未打开: " << error << "\n";
        return 0;
    }
    out << "已打开: " << lamp.devicePath() << "\n";
    if (!lamp.queryAttributes(&error)) {
        out << "读取属性失败: " << error << "\n";
        return 0;
    }
    const LampArray::Attributes &a = lamp.attributes();
    out << "灯数          : " << a.lampCount << "\n"
        << "外框 (µm)     : " << a.boundingBoxWidthUm << " x "
                              << a.boundingBoxHeightUm << " x "
                              << a.boundingBoxDepthUm << "\n"
        << "类型          : " << LampArray::kindName(a.kind) << "\n"
        << "最小刷新间隔  : " << a.minUpdateIntervalUs << " µs\n\n";

    for (const LampArray::LampInfo &l : lamp.lamps()) {
        out << QString::asprintf("  灯 %-4u  位置 (%8u, %8u, %8u) µm  用途 %-12s %s\n",
                                 l.id, l.positionXUm, l.positionYUm, l.positionZUm,
                                 qPrintable(LampArray::purposeNames(l.purposes)),
                                 l.isProgrammable ? "可编程" : "");
    }

    return 0;
}

// Opens and initialises the lighting interface, optionally waiting for it to
// turn up. Resume and login both race the USB stack: the unit used to paper
// over that with a fixed `sleep 3`, which is both too long when the controller
// is already there and too short when it is not.
bool openWithWait(RgbFusion2 *rgb, int waitSeconds, QString *error)
{
    QElapsedTimer clock;
    clock.start();

    for (;;) {
        if (rgb->openFirstDevice(error) && rgb->initialize(error))
            return true;
        rgb->close();

        if (clock.elapsed() >= qint64(waitSeconds) * 1000)
            return false;
        QThread::msleep(500);
    }
}

// Resolves another account's config file. A restore that runs at resume is a
// root system unit, and root's own XDG paths are not where the settings are.
QString configPathForUser(const QString &user)
{
    const struct passwd *pw = ::getpwnam(user.toLocal8Bit().constData());
    if (!pw)
        return QString();
    return QString::fromLocal8Bit(pw->pw_dir)
           + QStringLiteral("/.config/GigabyteRGBController/zones.ini");
}

bool parseMode(const QString &s, RgbFusion2::Mode *mode)
{
    static const QHash<QString, RgbFusion2::Mode> kNames = {
        {QStringLiteral("off"),       RgbFusion2::Mode::Off},
        {QStringLiteral("static"),    RgbFusion2::Mode::Static},
        {QStringLiteral("breathing"), RgbFusion2::Mode::Breathing},
        {QStringLiteral("flash"),     RgbFusion2::Mode::Flash},
        {QStringLiteral("dflash"),    RgbFusion2::Mode::DoubleFlash},
        {QStringLiteral("cycle"),     RgbFusion2::Mode::ColorCycle},
    };
    const auto it = kNames.constFind(s.toLower());
    if (it == kNames.constEnd())
        return false;
    *mode = it.value();
    return true;
}

// Applies a saved profile without opening the UI - the point of profiles for
// anyone who, quite reasonably, does not keep this program running: the effects
// stay in the controller, so a keybinding that runs one command is the whole
// interaction.
int runProfile(const QString &name, int waitSeconds)
{
    QTextStream out(stdout);

    ZoneSetting zones[RgbFusion2::kZoneCount];
    if (!Config::loadProfile(name, zones)) {
        out << "没有名为「" << name << "」的方案。可用方案:\n";
        const QStringList names = Config::profileNames();
        if (names.isEmpty())
            out << "  (无)\n";
        for (const QString &n : names)
            out << "  " << n << "\n";
        return 2;
    }

    RgbFusion2 rgb;
    QString error;
    if (!openWithWait(&rgb, waitSeconds, &error)) {
        out << "失败: " << error << "\n";
        return 1;
    }
    if (!Config::apply(rgb, zones, &error)) {
        out << "应用失败: " << error << "\n";
        return 1;
    }

    // The live state follows, so the GUI and a later --restore agree with what
    // is actually on the board.
    Config::save(zones);
    Config::setActiveProfile(name);

    out << "已应用方案「" << name << "」\n";
    return 0;
}

// Headless control, so the tool is usable from scripts and systemd units.
int runSet(const QStringList &args)
{
    QTextStream out(stdout);

    RgbFusion2::Mode  mode  = RgbFusion2::Mode::Static;
    RgbFusion2::Speed speed = RgbFusion2::Speed::Normal;
    QColor colour(255, 0, 0);
    int zone = -1, brightness = -1, minBright = 0;

    if (args.isEmpty() || !parseMode(args.first(), &mode)) {
        out << "未知模式。可用: off static breathing flash dflash cycle\n";
        return 2;
    }

    for (int i = 1; i < args.size(); ++i) {
        const QString a = args.at(i);
        const QString v = (i + 1 < args.size()) ? args.at(i + 1) : QString();
        if (a == QLatin1String("--color") && !v.isEmpty()) {
            colour = QColor(v);
            if (!colour.isValid()) { out << "颜色无效: " << v << "\n"; return 2; }
            ++i;
        } else if (a == QLatin1String("--zone") && !v.isEmpty()) {
            zone = v.toInt(); ++i;
        } else if (a == QLatin1String("--brightness") && !v.isEmpty()) {
            brightness = v.toInt(); ++i;
        } else if (a == QLatin1String("--min-brightness") && !v.isEmpty()) {
            minBright = v.toInt(); ++i;
        } else if (a == QLatin1String("--speed") && !v.isEmpty()) {
            speed = static_cast<RgbFusion2::Speed>(qBound(0, v.toInt(), 5)); ++i;
        } else {
            out << "未知参数: " << a << "\n";
            return 2;
        }
    }

    if (brightness < 0)
        brightness = RgbFusion2::maxBrightness(mode);

    RgbFusion2 rgb;
    QString error;
    if (!rgb.openFirstDevice(&error) || !rgb.initialize(&error)) {
        out << "失败: " << error << "\n";
        return 1;
    }

    for (int z = 0; z < RgbFusion2::kZoneCount; ++z) {
        if (zone >= 0 && z != zone)
            continue;
        if (!rgb.setZone(z, mode, colour, brightness, speed, minBright, &error)) {
            out << "区域 " << z << " 设置失败: " << error << "\n";
            return 1;
        }
    }
    if (!rgb.apply(&error)) {
        out << "提交失败: " << error << "\n";
        return 1;
    }

    // Merge into the saved config so --restore reproduces this state. Only the
    // effect fields are touched - zone detection results and custom names must
    // survive a --set.
    ZoneSetting saved[RgbFusion2::kZoneCount];
    Config::load(saved);
    for (int z = 0; z < RgbFusion2::kZoneCount; ++z) {
        if (zone >= 0 && z != zone)
            continue;
        ZoneSetting &s = saved[z];
        s.mode          = mode;
        s.colour        = colour;
        s.brightness    = brightness;
        s.minBrightness = minBright;
        s.speed         = speed;
        s.managed       = true;
    }
    Config::save(saved);

    out << "已应用 " << RgbFusion2::modeName(mode);
    if (RgbFusion2::modeUsesColor(mode))
        out << " " << colour.name().toUpper();
    out << "  亮度 " << brightness
        << (zone < 0 ? QStringLiteral("  全部区域") : QStringLiteral("  区域 %1").arg(zone))
        << "\n";
    return 0;
}

// Brings up the Qt Quick UI. With `screenshotPath` set it renders one frame,
// writes it out and exits instead of entering the event loop.
int runGui(int argc, char *argv[], const QString &screenshotPath = QString())
{
    // The software renderer keeps `-platform offscreen` working on machines
    // with no GPU context available, which is the whole point of screenshot
    // mode.
    if (!screenshotPath.isEmpty())
        qputenv("QT_QUICK_BACKEND", "software");

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("GigabyteRGBController"));
    app.setApplicationDisplayName(QStringLiteral("Gigabyte RGB Controller"));
    app.setOrganizationName(QStringLiteral("GigabyteRGBController"));
    app.setApplicationVersion(QStringLiteral(GRC_VERSION));
    app.setDesktopFileName(kDesktopFileName);
    // Wayland takes the icon from the desktop entry above; this is what X11 and
    // the window list on some compositors use. Resolves to nothing when the
    // icon has not been installed, which is harmless.
    app.setWindowIcon(QIcon::fromTheme(kDesktopFileName));

    // Screenshot mode is a build-time aid and deliberately exempt: it must work
    // while a normal instance is up.
    if (screenshotPath.isEmpty() && handedOverToRunningInstance()) {
        QTextStream(stdout) << "已有实例在运行，已请求其显示窗口。\n";
        return 0;
    }

    Controller controller;
    controller.setSavesWindowState(screenshotPath.isEmpty());

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("Ctl"), &controller);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, []() { QCoreApplication::exit(1); },
                     Qt::QueuedConnection);

    engine.loadFromModule("GigabyteRGBController", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (!window) {
        QTextStream(stdout) << "根对象不是窗口\n";
        return 1;
    }

    // The other half of the single-instance handshake: a later launch connects
    // here instead of opening a second window.
    QLocalServer instanceServer;
    if (screenshotPath.isEmpty()) {
        // A crash leaves the socket file behind and every later launch would
        // then fail to listen; removing it first is the documented remedy.
        QLocalServer::removeServer(instanceSocketName());
        instanceServer.listen(instanceSocketName());
        QObject::connect(&instanceServer, &QLocalServer::newConnection, window, [&]() {
            while (QLocalSocket *conn = instanceServer.nextPendingConnection())
                conn->deleteLater();
            window->show();
            // Wayland only honours this with an activation token from the other
            // side, so treat it as best effort rather than a guarantee. raise()
            // is deliberately not called: no Wayland plugin implements it and
            // it only prints a warning.
            window->requestActivate();
        });
    }

    if (screenshotPath.isEmpty())
        return app.exec();

    // Give the scene graph a moment to lay out and paint before grabbing;
    // grabWindow() on a window that has not settled yields a blank frame.
    int result = 0;
    QTimer::singleShot(600, &app, [&]() {
        const bool ok = window->grabWindow().save(screenshotPath);
        QTextStream(stdout) << (ok ? "已保存 " : "保存失败 ") << screenshotPath << "\n";
        result = ok ? 0 : 1;
        QCoreApplication::quit();
    });

    app.exec();
    return result;
}

} // namespace

int main(int argc, char *argv[])
{
    QStringList args;
    for (int i = 1; i < argc; ++i)
        args << QString::fromLocal8Bit(argv[i]);

    // These apply to every mode, so they are pulled out of the list before the
    // mode is dispatched on args.first(), and may appear at either end.
    int waitSeconds = 0;
    for (int i = 0; i + 1 < args.size();) {
        const QString &opt = args.at(i);
        const QString &val = args.at(i + 1);

        if (opt == QLatin1String("--config")) {
            Config::setPath(val);
        } else if (opt == QLatin1String("--user")) {
            const QString path = configPathForUser(val);
            if (path.isEmpty()) {
                QTextStream(stdout) << "没有这个用户: " << val << "\n";
                return 2;
            }
            Config::setPath(path);
        } else if (opt == QLatin1String("--wait")) {
            waitSeconds = qBound(0, val.toInt(), 300);
        } else {
            ++i;
            continue;
        }

        args.removeAt(i);
        args.removeAt(i);
    }

    if (!args.isEmpty()) {
        if (args.first() == QLatin1String("--probe")) {
            QCoreApplication app(argc, argv);
            return runProbe();
        }
        if (args.first() == QLatin1String("--set")) {
            QCoreApplication app(argc, argv);
            return runSet(args.mid(1));
        }
        if (args.first() == QLatin1String("--restore")) {
            QCoreApplication app(argc, argv);
            QTextStream out(stdout);

            ZoneSetting zones[RgbFusion2::kZoneCount];
            if (!Config::load(zones)) {
                out << "没有已保存的配置 (" << Config::path() << ")\n";
                return 0;
            }

            QString error;

            // With the fallback on, the vendor interface is the one channel
            // known not to work, and going through it would print a success
            // the lighting does not back up. See Config::lampFallback().
            if (Config::lampFallback()) {
                LampArray lamp;
                if (!lamp.openFirstDevice(&error)) {
                    out << "失败: " << error << "\n";
                    return 1;
                }
                if (!Config::applyViaLamp(lamp, zones, &error)) {
                    out << "恢复失败: " << error << "\n";
                    return 1;
                }
                out << "已从 " << Config::path() << " 恢复灯效 (LampArray 通道)\n";
                return 0;
            }

            RgbFusion2 rgb;
            if (!openWithWait(&rgb, waitSeconds, &error)) {
                out << "失败: " << error << "\n";
                return 1;
            }
            if (!Config::apply(rgb, zones, &error)) {
                out << "恢复失败: " << error << "\n";
                return 1;
            }
            out << "已从 " << Config::path() << " 恢复灯效\n";
            return 0;
        }
        if (args.first() == QLatin1String("--profile") && args.size() >= 2) {
            QCoreApplication app(argc, argv);
            return runProfile(args.at(1), waitSeconds);
        }
        if (args.first() == QLatin1String("--list-profiles")) {
            QCoreApplication app(argc, argv);
            QTextStream out(stdout);
            const QStringList names = Config::profileNames();
            const QString active = Config::activeProfile();
            if (names.isEmpty())
                out << "(" << Config::path() << " 里没有保存的方案)\n";
            for (const QString &n : names)
                out << (n == active ? "* " : "  ") << n << "\n";
            return 0;
        }
        // Dev aid: render the window straight to a PNG. Combined with
        // `-platform offscreen` this verifies layout without a display server
        // and produces the screenshots used in the README.
        if (args.first() == QLatin1String("--screenshot") && args.size() >= 2)
            return runGui(argc, argv, args.at(1));
        if (args.first() == QLatin1String("--version") || args.first() == QLatin1String("-v")) {
            QTextStream(stdout)
                << "GigabyteRGBController " << GRC_VERSION
                << "  (Qt " << qVersion() << ")\n";
            return 0;
        }
        if (args.first() == QLatin1String("--help") || args.first() == QLatin1String("-h")) {
            QTextStream(stdout)
                << "用法: GigabyteRGBController [选项]\n"
                << "  (无参数)                  启动图形界面\n"
                << "  --probe                   无界面自检：枚举接口并读取控制器信息\n"
                << "  --set <模式> [参数...]    无界面设置灯效\n"
                << "  --restore                 恢复上次保存的灯效\n"
                << "  --profile <名字>          无界面套用某个方案\n"
                << "  --list-profiles           列出已保存的方案（* 为当前）\n"
                << "  --screenshot <文件>       渲染一帧界面到 PNG 后退出\n"
                << "  --version                 显示版本号\n"
                << "\n全局选项:\n"
                << "  --config <文件>      指定配置文件（默认 "
                << Config::path() << "）\n"
                << "  --user <用户名>      改用该用户的配置文件（root 下恢复他人配置用）\n"
                << "  --wait <秒>          控制器尚未枚举时最多等待多久（默认 0）\n"
                << "\n模式: off static breathing flash dflash cycle\n"
                << "参数:\n"
                << "  --color #RRGGBB      颜色（默认 #FF0000）\n"
                << "  --zone N             区域 0-7（默认全部）\n"
                << "  --brightness N       亮度 0-100（默认为该模式上限）\n"
                << "  --min-brightness N   渐变下限亮度（默认 0）\n"
                << "  --speed N            速度 0-5（默认 2）\n";
            return 0;
        }
    }

    return runGui(argc, argv);
}
