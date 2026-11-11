#pragma once

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QProcess>
#include <QTimer>

class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString accent READ accent NOTIFY changed)
    Q_PROPERTY(QString accentForeground READ accentForeground NOTIFY changed)
    Q_PROPERTY(int radius READ radius NOTIFY changed)
    // The rest of the palette, so the app looks like the Omarchy shell under
    // any theme. Each falls back to the shell's own default.
    Q_PROPERTY(QString background READ background NOTIFY changed)
    // The Hyprland active-window border, which the Omarchy shell also draws
    // around its popups and menus; the accent when the theme doesn't set one.
    Q_PROPERTY(QString border READ border NOTIFY changed)
    Q_PROPERTY(QString foreground READ foreground NOTIFY changed)
    Q_PROPERTY(QString muted READ muted NOTIFY changed)
    Q_PROPERTY(QString urgent READ urgent NOTIFY changed)
    Q_PROPERTY(QString success READ success NOTIFY changed)

public:
    explicit Theme(const QString &currentDirectory = {}, QObject *parent = nullptr, const QString &hyprctl = {});
    ~Theme() override;

    QString accent() const { return m_accent; }
    QString accentForeground() const;
    int radius() const { return m_radius; }
    QString background() const { return color("background", "#101315"); }
    QString border() const { return color("hyprland_active_border", m_accent); }
    QString foreground() const { return color("foreground", "#cacccc"); }
    QString muted() const { return color("muted", "#707880"); }
    QString urgent() const { return color("red", "#a55555"); }
    QString success() const { return color("green", "#8ab27a"); }

    static QString readAccent(const QString &path);
    static QHash<QString, QString> readColors(const QString &path);

signals:
    void changed();

private:
    void reload();
    void refreshRounding();

    QString m_directory;
    QString color(const QString &key, const QString &fallback) const { return m_colors.value(key, fallback); }

    QString m_accent = "#FFD60A";
    QHash<QString, QString> m_colors;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QString m_hyprctl;
    int m_radius = 0;
    QProcess m_roundingProcess;
    QTimer m_roundingPoll, m_roundingTimeout;
};
