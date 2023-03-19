#include <QCommandLineParser>
#include <QSystemTrayIcon>
#include <QActionGroup>
#include <QApplication>
#include <QDirIterator>
#include <QProcess>
#include <QDebug>
#include <QMenu>

#include <QDBusPendingCall>
#include <QDBusConnection>
#include <QDBusMessage>

using namespace std;

constexpr auto g_windowScalingFactorPath = "/Gdk/WindowScalingFactor";
constexpr auto g_unscaledDpiPath = "/Gdk/UnscaledDPI";
constexpr auto g_dpiPath = "/Xft/DPI";
constexpr auto g_cursorSizePath = "/Gtk/CursorThemeSize";

constexpr auto g_xfwm4ThemePath = "/general/theme";

constexpr auto g_dpiReference = 96;

static unique_ptr<QProcess> getXsettingValueAsync(const QString &path)
{
    auto process = make_unique<QProcess>();
    process->start("xfconf-query", {"-c", "xsettings", "-p", path});
    return process;
}
static int getIntValueFromAsyncProcess(const unique_ptr<QProcess> &process)
{
    if (!process->waitForFinished())
        return 0;
    if (process->exitCode() != 0)
        return 0;
    return process->readAllStandardOutput().toInt();
}
static int getXsettingIntValue(const QString &path)
{
    return getIntValueFromAsyncProcess(getXsettingValueAsync(path));
}

static QString getXfwm4Theme()
{
    QProcess process;
    process.start("xfconf-query", {"-c", "xfwm4", "-p", g_xfwm4ThemePath});
    if (!process.waitForFinished())
        return QString();
    if (process.exitCode() != 0)
        return QString();
    return process.readAllStandardOutput().trimmed();
}
static void maybeSetXfwm4Theme(const QString &theme)
{
    QDirIterator allThemesIt(DATA_DIR "/themes", QDir::Dirs | QDir::NoDotAndDotDot);
    while (allThemesIt.hasNext())
    {
        const auto themeDir = allThemesIt.nextFileInfo();
        QDirIterator themeIt(themeDir.filePath(), QDir::Dirs | QDir::NoDotAndDotDot);
        while (themeIt.hasNext())
        {
            if (themeIt.nextFileInfo().fileName() == QStringLiteral("xfwm4") && themeDir.fileName() == theme)
            {
                QProcess::startDetached("xfconf-query", {"-c", "xfwm4", "-p", g_xfwm4ThemePath, "-s", theme});
                break;
            }
        }
    }
}

static void setXsettingIntValue(const QString &path, int value)
{
    QProcess::startDetached("xfconf-query", {"-c", "xsettings", "-n", "-p", path, "-t", "int", "-s", QString::number(value)});
}
static void resetXsettingIntValue(const QString &path)
{
    QProcess::startDetached("xfconf-query", {"-c", "xsettings", "-p", path, "-r"});
}

static inline int getScale(int dpi)
{
    return qRound(dpi * 100.0 / g_dpiReference);
}

static inline int getDpi(int scale)
{
    return qRound(g_dpiReference * scale / 100.0);
}
static inline int getWindowScalingFactor(int scale)
{
    return qRound(scale / 100.0);
}
static inline int getUnscaledDpi(int dpi, int windowScalingFactor)
{
    return qRound(dpi * 1000.0 / windowScalingFactor);
}
static inline int getCursorSize(int dpi)
{
    const int size = qCeil(dpi / 4.0);
    if (size & 1)
        return size + 1;
    return size;
}

static int getCurrentScale()
{
    const auto currDpiProcess = getXsettingValueAsync(g_dpiPath);
    const auto currWindowScalingFactorProcess = getXsettingValueAsync(g_windowScalingFactorPath);
    const auto currUnscaledDpiProcess = getXsettingValueAsync(g_unscaledDpiPath);
    const auto currCursorSizeProcess = getXsettingValueAsync(g_cursorSizePath);

    const int currDpi = getIntValueFromAsyncProcess(currDpiProcess);
    const int currWindowScalingFactor = getIntValueFromAsyncProcess(currWindowScalingFactorProcess);
    const int currUnscaledDpi = getIntValueFromAsyncProcess(currUnscaledDpiProcess);
    const int currCursorSize = getIntValueFromAsyncProcess(currCursorSizeProcess);

    if (currDpi > 0 && currWindowScalingFactor > 0 && currCursorSize > 0)
    {
        const int scale = getScale(currDpi);
        const int windowScalingFactor = getWindowScalingFactor(scale);
        const int unscaledDpi = getUnscaledDpi(currDpi, windowScalingFactor);
        const int cursorSize = getCursorSize(currDpi);

        const bool hasSameWindowScalingFactor = (windowScalingFactor == currWindowScalingFactor);
        const bool hasSameUnscaledDpi = ((currUnscaledDpi == 0 && unscaledDpi == g_dpiReference * 1000) || (currUnscaledDpi > 0 && unscaledDpi == currUnscaledDpi));
        const bool hasSameCursorSize = (currCursorSize == cursorSize);

        if (hasSameWindowScalingFactor && hasSameUnscaledDpi && hasSameCursorSize)
        {
            return scale;
        }
    }

    return 0;
}

static void scalingActionTriggered(int scale)
{
    const int dpi = getDpi(scale);
    const int windowScalingFactor = getWindowScalingFactor(scale);
    const int unscaledDpi = getUnscaledDpi(dpi, windowScalingFactor);
    const int cursorSize = getCursorSize(dpi);

    // Restart Xfce4 panel if window scaling has changed
    if (auto sessionBus = QDBusConnection::sessionBus(); sessionBus.isConnected() && windowScalingFactor != getXsettingIntValue(g_windowScalingFactorPath))
    {
        sessionBus.asyncCall(
            QDBusMessage::createMethodCall(
                "org.xfce.Panel",
                "/org/xfce/Panel",
                "org.xfce.Panel",
                "Terminate"
            ) << true // Restart (argument)
        );
    }

    setXsettingIntValue(g_windowScalingFactorPath, windowScalingFactor);
    if (unscaledDpi == g_dpiReference * 1000)
        resetXsettingIntValue(g_unscaledDpiPath);
    else
        setXsettingIntValue(g_unscaledDpiPath, unscaledDpi);
    setXsettingIntValue(g_dpiPath, dpi);
    setXsettingIntValue(g_cursorSizePath, cursorSize);
    setXsettingIntValue("/Xfce/LastCustomDPI", dpi);

    // Set Xfwm4 style
    if (const auto theme = getXfwm4Theme(); !theme.isEmpty())
    {
        auto newTheme = theme;
        newTheme.remove(QStringLiteral("-hdpi"));
        newTheme.remove(QStringLiteral("-xhdpi"));
        if (windowScalingFactor == 2)
            newTheme.append(QStringLiteral("-hdpi"));
        else if (windowScalingFactor > 2)
            newTheme.append(QStringLiteral("-xhdpi"));
        if (theme != newTheme)
            maybeSetXfwm4Theme(newTheme);
    }
}

int main(int argc, char *argv[])
{
    qInstallMessageHandler([](QtMsgType t, const QMessageLogContext &c, const QString &s) {
        fprintf(stderr, "%s\n", qUtf8Printable(qFormatLogMessage(t, c, s)));
        fflush(stderr);
    });

    QApplication app(argc, argv);
    app.setApplicationName("Display scaling");

    QCommandLineParser parser;
    parser.addPositionalArgument("scale", "Set scaling");
    parser.addHelpOption();
    parser.process(app);
    if (const auto positionalArgs = parser.positionalArguments(); positionalArgs.size() == 1)
    {
        const int scale = positionalArgs[0].toInt();
        if (scale >= 100 && scale <= 300)
        {
            scalingActionTriggered(scale);
            return 0;
        }
    }

    QMenu menu;
    QActionGroup scalingGroup(&menu);

    bool blockToggled = false;

    QObject::connect(&menu, &QMenu::aboutToShow, [&menu, &blockToggled] {
        const int currentScale = getCurrentScale();
        if (currentScale <= 0)
            return;

        const auto actions = menu.actions();
        for (QAction *act : actions)
        {
            const int scale = act->data().toInt();
            if (scale == currentScale)
            {
                blockToggled = true;
                act->setChecked(true);
                blockToggled = false;
            }
        }
    });

    const int scales[] {
        100,
        125,
        170,
        200,
        300,
    };
    for (int scale : scales)
    {
        auto act = menu.addAction(QString("%1%").arg(scale));
        act->setCheckable(true);
        act->setData(scale);
        scalingGroup.addAction(act);
        QObject::connect(act, &QAction::toggled, [scale, &blockToggled](bool checked) {
            if (!blockToggled && checked)
                scalingActionTriggered(scale);
        });
    }

    QProcess dispSettings;
    dispSettings.setProgram("xfce4-display-settings");

    QSystemTrayIcon tray;
    tray.setIcon(QIcon::fromTheme("preferences-desktop-display"));
    tray.setContextMenu(&menu);
    QObject::connect(&tray, &QSystemTrayIcon::activated, [&dispSettings](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger)
        {
            if (dispSettings.state() == QProcess::NotRunning)
                dispSettings.start();
        }
    });
    tray.show();

    return app.exec();
}
