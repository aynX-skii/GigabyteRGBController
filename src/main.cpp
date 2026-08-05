#include "Config.h"
#include "Controller.h"
#include "LampArray.h"
#include "RgbFusion2.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QHash>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTextStream>
#include <QTimer>

namespace {

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

    Controller controller;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("Ctl"), &controller);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, []() { QCoreApplication::exit(1); },
                     Qt::QueuedConnection);

    engine.loadFromModule("GigabyteRGBController", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;

    if (screenshotPath.isEmpty())
        return app.exec();

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (!window) {
        QTextStream(stdout) << "根对象不是窗口\n";
        return 1;
    }

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

            RgbFusion2 rgb;
            QString error;
            if (!rgb.openFirstDevice(&error) || !rgb.initialize(&error)) {
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
        // Dev aid: render the window straight to a PNG. Combined with
        // `-platform offscreen` this verifies layout without a display server
        // and produces the screenshots used in the README.
        if (args.first() == QLatin1String("--screenshot") && args.size() >= 2)
            return runGui(argc, argv, args.at(1));
        if (args.first() == QLatin1String("--help") || args.first() == QLatin1String("-h")) {
            QTextStream(stdout)
                << "用法: GigabyteRGBController [选项]\n"
                << "  (无参数)                  启动图形界面\n"
                << "  --probe                   无界面自检：枚举接口并读取控制器信息\n"
                << "  --set <模式> [参数...]    无界面设置灯效\n"
                << "  --restore                 恢复上次保存的灯效\n"
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
