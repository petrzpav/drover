// drover — a floating indicator of your herdr agents, on every workspace.

#include <QGuiApplication>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QUrl>
#include <unistd.h>

#include "backend.h"
#include "surface.h"
#include "theme.h"
#include "translator.h"

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("drover");
    app.setApplicationVersion("0.1.0");

    // Matches the window to drover.desktop, so the compositor (Wayland app_id)
    // and the launcher pick up the installed icon.
    app.setDesktopFileName("drover");
    app.setWindowIcon(QIcon::fromTheme("drover"));

    // One indicator per session: starting drover again (from the launcher, or
    // `drover toggle`) opens or closes the running one's panel, and
    // `drover quit` stops it.
    const QString command = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("toggle");
    if (command != "toggle" && command != "quit" && command != "open") {
        fprintf(stderr, "usage: drover [open|toggle|quit]\n");
        return 1;
    }
    const QString serverName = QStringLiteral("drover-%1").arg(getuid());
    {
        QLocalSocket socket;
        socket.connectToServer(serverName);
        if (socket.waitForConnected(300)) {
            socket.write(command.toUtf8() + '\n');
            socket.waitForBytesWritten(300);
            return 0;
        }
    }
    if (command == "quit")
        return 0;
    QLocalServer::removeServer(serverName);
    QLocalServer server;
    if (!server.listen(serverName))
        fprintf(stderr, "drover: can't listen on %s, a second start will open a second indicator\n",
                qPrintable(serverName));

    QQuickStyle::setStyle("Material");

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

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
    if (command == "open")
        QMetaObject::invokeMethod(window, "setExpanded", Q_ARG(QVariant, true));

    QObject::connect(&server, &QLocalServer::newConnection, &server, [&server, window] {
        while (auto *socket = server.nextPendingConnection()) {
            QObject::connect(socket, &QLocalSocket::readyRead, socket, [socket, window] {
                const QString message = QString::fromUtf8(socket->readAll()).trimmed();
                if (message == "quit")
                    QGuiApplication::quit();
                else if (message == "open")
                    QMetaObject::invokeMethod(window, "setExpanded", Q_ARG(QVariant, true));
                else
                    QMetaObject::invokeMethod(window, "setExpanded", Q_ARG(QVariant, !window->property("expanded").toBool()));
            });
            QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        }
    });

    return app.exec();
}
