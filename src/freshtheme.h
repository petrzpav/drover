#pragma once

#include <QObject>

class Theme;

// Keeps the Fresh editor in the Omarchy theme: writes a Fresh theme made from
// the theme's colors (themes/omarchy.json in Fresh's config folder) whenever
// the theme changes, and has every running Fresh apply it at once.
class FreshTheme : public QObject {
    Q_OBJECT

public:
    FreshTheme(Theme *theme, const QString &themesDirectory, const QString &fresh, QObject *parent = nullptr);

    // The Fresh theme for these colors, as JSON.
    static QByteArray build(const Theme &theme);

private:
    void update();

    Theme *m_theme;
    QString m_path, m_fresh;
};
