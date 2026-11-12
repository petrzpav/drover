// drover — a floating indicator of your herdr agents, on every workspace.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QGuiApplication>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QStandardPaths>
#include <memory>
#include <QThread>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QtQml/qqml.h>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QUrl>
#include <unistd.h>

#include "asker.h"
#include "backend.h"
#include "hook.h"
#include "surface.h"
#include "terminalview.h"
#include "freshtheme.h"
#include "terminalfont.h"
#include "theme.h"
#include "translator.h"

namespace {

// `drover hook`: Claude Code runs it before a question dialog and with a
// permission prompt (see hook.h). It needs no window, and never fails the
// agent: anything unexpected prints nothing, and Claude asks itself.
int runHook(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    QFile in;
    if (!in.open(stdin, QIODevice::ReadOnly))
        return 0;
    const QByteArray input = in.readAll();
    QHash<QString, QString> env;
    const auto system = QProcessEnvironment::systemEnvironment();
    for (const QString &key : system.keys())
        env.insert(key, system.value(key));
    const QByteArray output = Hook::run(input, env);
    if (!output.isEmpty()) {
        fwrite(output.constData(), 1, output.size(), stdout);
        fflush(stdout);
    }
    return 0;
}

// `drover hooks install|uninstall|status`: drover's hooks in the user's
// Claude settings.
int runHooks(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    const QString action = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QStringLiteral("status");
    const QString settings = qEnvironmentVariable("CLAUDE_CONFIG_DIR", QDir::homePath() + "/.claude") + "/settings.json";
    QString error;
    if (action == "install") {
        error = Hook::install(settings);
        if (error.isEmpty())
            printf("Added drover's hooks to %s. Agents started from now on ask on drover's stage.\n", qPrintable(settings));
    } else if (action == "uninstall") {
        error = Hook::uninstall(settings);
        if (error.isEmpty())
            printf("Took drover's hooks out of %s.\n", qPrintable(settings));
    } else if (action == "status") {
        printf("%s\n", Hook::installed(settings) ? "installed" : "not installed");
    } else {
        fprintf(stderr, "usage: drover hooks [install|uninstall|status]\n");
        return 1;
    }
    if (!error.isEmpty()) {
        fprintf(stderr, "drover: %s\n", qPrintable(error));
        return 1;
    }
    return 0;
}

} // namespace

int main(int argc, char *argv[]) {
    // Before anything else: the hook runs inside an agent's pane and needs
    // its herdr variables.
    if (argc >= 2 && qstrcmp(argv[1], "hook") == 0)
        return runHook(argc, argv);
    if (argc >= 2 && qstrcmp(argv[1], "hooks") == 0)
        return runHooks(argc, argv);

    // herdr talks to the session of the pane it runs in. Started from inside a
    // herdr pane, drover must still see, and attach to, the whole session.
    for (const char *name : {"HERDR_ENV", "HERDR_PANE_ID", "HERDR_TAB_ID", "HERDR_WORKSPACE_ID"})
        qunsetenv(name);

    QGuiApplication app(argc, argv);
    app.setApplicationName("drover");
    app.setApplicationVersion("0.5.0");

    // Matches the window to drover.desktop, so the compositor (Wayland app_id)
    // and the launcher pick up the installed icon.
    app.setDesktopFileName("drover");
    app.setWindowIcon(QIcon::fromTheme("drover"));

    // One indicator per session: starting drover again (from the launcher, a
    // key binding, or `drover toggle`) opens or closes the running one's
    // panel, and `drover quit` stops it. A bare `drover` (autostart) starts
    // with the panel closed; `open` or `toggle` start with it open.
    const bool bare = argc < 2;
    const QString command = bare ? QStringLiteral("toggle") : QString::fromLocal8Bit(argv[1]);
    if (command != "toggle" && command != "quit" && command != "open" && command != "pick") {
        fprintf(stderr, "usage: drover [open|toggle|pick|quit|hooks]\n");
        return 1;
    }
    const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    const QString serverName = QStringLiteral("drover-%1").arg(getuid());

    // The lock decides which start becomes the indicator, so two starts at the
    // same moment (autostart and a key press) can't both end up running.
    QLockFile lock(runtime + "/drover.lock");
    if (!lock.tryLock(0)) {
        // Another drover runs or is starting; hand it the command once its
        // server listens.
        for (int attempt = 0; attempt < 30; ++attempt) {
            QLocalSocket socket;
            socket.connectToServer(serverName);
            if (socket.waitForConnected(200)) {
                socket.write(command.toUtf8() + '\n');
                socket.waitForBytesWritten(300);
                return 0;
            }
            QThread::msleep(100);
        }
        fprintf(stderr, "drover: another drover holds %s but doesn't answer\n", qPrintable(lock.fileName()));
        return 1;
    }
    if (command == "quit")
        return 0;
    QLocalServer::removeServer(serverName);
    QLocalServer server;
    if (!server.listen(serverName))
        fprintf(stderr, "drover: can't listen on %s, so starting drover again won't reach this one\n",
                qPrintable(serverName));

    // The indicator is the whole app; losing its window for a moment (an
    // output going away) must not end it.
    app.setQuitOnLastWindowClosed(false);

    QQuickStyle::setStyle("Basic");
    // The panel's text rasterized like the terminal's, not as distance fields.
    QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);

    // All text in the terminal's font and size, like the agents' own screens.
    app.setFont(TerminalFont::read(QDir::homePath()));

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    qmlRegisterType<TerminalView>("Drover", 1, 0, "TerminalView");

    Theme theme;
    Backend backend;
    Surface surface;

    // Fresh, the editor beside the agents, wears the Omarchy theme too.
    std::unique_ptr<FreshTheme> freshTheme;
    const QString fresh = QStandardPaths::findExecutable("fresh");
    if (backend.editor() == "fresh" && !fresh.isEmpty())
        freshTheme.reset(new FreshTheme(&theme, QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                                                    + "/fresh/themes", fresh));

    // The agents' questions, on drover's stage.
    Asker asker;
    asker.setHold(Asker::readHold(QDir::homePath() + "/.config/drover/config"));
    asker.setLook(Asker::readLook(QDir::homePath() + "/.config/drover/config"));
    if (!asker.listen(Hook::serverName()))
        fprintf(stderr, "drover: can't listen on %s, so agents ask in their terminals\n", qPrintable(Hook::serverName()));
    QObject::connect(&backend, &Backend::listed, &asker, &Asker::agentsListed);
    QObject::connect(&asker, &Asker::askingChanged, &backend, [&] { backend.setAsking(asker.askingPanes()); });

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("surface", &surface);
    engine.rootContext()->setContextProperty("asker", &asker);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    // The window is created hidden, so it becomes a layer surface before it is
    // ever mapped.
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    surface.attach(window);
    window->show();
    // Whether the user looks at an agent's pane: drover's panel shows it, or
    // the herdr window is in front with that pane focused.
    asker.setWatching([&backend, window](const QString &pane) {
        if (window->property("expanded").toBool() && backend.selected() == pane)
            return true;
        bool focused = false;
        for (const auto &agent : backend.listedAgents())
            focused = focused || (agent.paneId == pane && agent.focused);
        if (!focused)
            return false;
        QProcess hyprctl;
        hyprctl.start("hyprctl", {"activewindow", "-j"});
        if (!hyprctl.waitForFinished(300))
            return false;
        return QString::fromUtf8(hyprctl.readAllStandardOutput()).contains("herdr", Qt::CaseInsensitive);
    });

    if (command == "pick")
        QMetaObject::invokeMethod(window, "openPicker");
    else if (!bare)
        QMetaObject::invokeMethod(window, "setExpanded", Q_ARG(QVariant, true));

    QObject::connect(&server, &QLocalServer::newConnection, &server, [&server, window] {
        while (auto *socket = server.nextPendingConnection()) {
            QObject::connect(socket, &QLocalSocket::readyRead, socket, [socket, window] {
                const QString message = QString::fromUtf8(socket->readAll()).trimmed();
                if (message == "quit")
                    QGuiApplication::quit();
                else if (message == "open")
                    QMetaObject::invokeMethod(window, "setExpanded", Q_ARG(QVariant, true));
                else if (message == "pick")
                    QMetaObject::invokeMethod(window, "openPicker");
                else
                    QMetaObject::invokeMethod(window, "toggle");
            });
            QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        }
    });

    return app.exec();
}
