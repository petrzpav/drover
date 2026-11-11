// drover — a floating indicator of your herdr agents, on every workspace.

#include <QDir>
#include <QGuiApplication>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QStandardPaths>
#include <QThread>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QtQml/qqml.h>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QUrl>
#include <unistd.h>

#include "backend.h"
#include "surface.h"
#include "terminalview.h"
#include "terminalfont.h"
#include "theme.h"
#include "translator.h"

int main(int argc, char *argv[]) {
    // herdr talks to the session of the pane it runs in. Started from inside a
    // herdr pane, drover must still see, and attach to, the whole session.
    for (const char *name : {"HERDR_ENV", "HERDR_PANE_ID", "HERDR_TAB_ID", "HERDR_WORKSPACE_ID"})
        qunsetenv(name);

    QGuiApplication app(argc, argv);
    app.setApplicationName("drover");
    app.setApplicationVersion("0.1.0");

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
        fprintf(stderr, "usage: drover [open|toggle|pick|quit]\n");
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

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("surface", &surface);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    // The window is created hidden, so it becomes a layer surface before it is
    // ever mapped.
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    surface.attach(window);
    window->show();
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
                    QMetaObject::invokeMethod(window, "setExpanded", Q_ARG(QVariant, !window->property("expanded").toBool()));
            });
            QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        }
    });

    return app.exec();
}
