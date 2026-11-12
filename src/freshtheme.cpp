#include "freshtheme.h"

#include "theme.h"

#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

#include <cmath>

namespace {

QColor mix(const QColor &a, const QColor &b, double k) {
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * k, a.greenF() + (b.greenF() - a.greenF()) * k,
                            a.blueF() + (b.blueF() - a.blueF()) * k);
}

// WCAG contrast ratio of two colors.
double contrast(const QColor &a, const QColor &b) {
    const auto luminance = [](const QColor &c) {
        const auto linear = [](double v) { return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4); };
        return .2126 * linear(c.redF()) + .7152 * linear(c.greenF()) + .0722 * linear(c.blueF());
    };
    const double x = luminance(a), y = luminance(b);
    return (std::max(x, y) + .05) / (std::min(x, y) + .05);
}

QJsonArray rgb(const QColor &c) {
    return {c.red(), c.green(), c.blue()};
}

QJsonObject styled(const QColor &c, const QStringList &modifiers) {
    return {{"color", rgb(c)}, {"modifier", QJsonArray::fromStringList(modifiers)}};
}

} // namespace

FreshTheme::FreshTheme(Theme *theme, const QString &themesDirectory, const QString &fresh, QObject *parent)
    : QObject(parent), m_theme(theme), m_path(themesDirectory + "/omarchy.json"), m_fresh(fresh) {
    connect(theme, &Theme::changed, this, &FreshTheme::update);
    update();
}

// Built over Fresh's own dark or light theme (by the background), so every
// color it doesn't name still fits. The text is the theme's: its background,
// foreground and accent, and the syntax in its palette, as in Monokai.
QByteArray FreshTheme::build(const Theme &theme) {
    const auto named = [&](const char *key, const QColor &fallback) {
        const QString value = theme.color(QString::fromLatin1(key), QString());
        return value.isEmpty() ? fallback : QColor(value);
    };
    const QStringList palette = theme.terminalPalette();
    const QColor bg(theme.background()), fg(theme.foreground()), accent(theme.accent()), border(theme.border());
    // Comments, line numbers and quiet labels stay readable on any background.
    const QColor muted = contrast(QColor(theme.muted()), bg) >= 3 ? QColor(theme.muted()) : mix(fg, bg, 0.35);
    const QColor red(palette.at(1)), green(palette.at(2)), yellow(palette.at(3)), blue(palette.at(4)),
        magenta(palette.at(5)), cyan(palette.at(6));
    const QColor orange = named("orange", mix(red, yellow, 0.5));
    const bool light = bg.lightnessF() > 0.5;
    // Chrome sits a step darker (or, on a light theme, lighter) than the text.
    const QColor chrome = named("dark_background", mix(bg, Qt::black, light ? 0.06 : 0.18));
    const QColor raised = named("lighter_background", mix(bg, fg, 0.08));
    const QColor selection = named("selection", named("selection_background", mix(bg, accent, 0.28)));
    const QColor dim = named("dark_foreground", mix(fg, bg, 0.4));
    const QColor line = mix(bg, fg, 0.05);

    QJsonObject editor{
        {"bg", rgb(bg)}, {"fg", rgb(fg)}, {"cursor", rgb(accent)}, {"inactive_cursor", rgb(muted)},
        {"selection_bg", rgb(selection)}, {"current_line_bg", rgb(line)},
        {"line_number_fg", rgb(muted)}, {"line_number_bg", rgb(bg)}, {"after_eof_bg", rgb(bg)},
        {"ruler_bg", rgb(raised)}, {"indentation_guide_fg", rgb(mix(bg, fg, 0.12))},
        {"whitespace_indicator_fg", rgb(mix(bg, fg, 0.2))}, {"bracket_match_fg", rgb(accent)},
        {"diff_add_bg", rgb(mix(bg, green, 0.18))}, {"diff_remove_bg", rgb(mix(bg, red, 0.18))},
        {"diff_modify_bg", rgb(mix(bg, yellow, 0.14))},
        {"diff_add_highlight_bg", rgb(mix(bg, green, 0.32))}, {"diff_remove_highlight_bg", rgb(mix(bg, red, 0.32))},
    };
    // Some themes put orange in blue's place; then red takes it.
    const QColor rainbow[6] = {yellow, magenta, cyan, green, orange, blue == orange ? red : blue};
    for (int i = 0; i < 6; ++i) {
        editor.insert(QString("bracket_rainbow_%1").arg(i + 1), rgb(rainbow[i]));
        editor.insert(QString("indent_rainbow_%1").arg(i + 1), rgb(mix(bg, rainbow[i], 0.12)));
    }

    const QJsonObject ui{
        {"tab_active_fg", rgb(fg)}, {"tab_active_bg", rgb(bg)},
        {"tab_inactive_fg", rgb(muted)}, {"tab_inactive_bg", rgb(chrome)},
        {"tab_separator_bg", rgb(chrome)}, {"tab_hover_bg", rgb(raised)}, {"tab_close_hover_fg", rgb(red)},
        {"menu_bg", rgb(chrome)}, {"menu_fg", rgb(dim)}, {"menu_active_bg", rgb(raised)}, {"menu_active_fg", rgb(fg)},
        {"menu_dropdown_bg", rgb(chrome)}, {"menu_dropdown_fg", rgb(fg)},
        {"menu_highlight_bg", rgb(selection)}, {"menu_highlight_fg", rgb(fg)},
        {"menu_border_fg", rgb(raised)}, {"menu_separator_fg", rgb(raised)},
        {"menu_hover_bg", rgb(raised)}, {"menu_hover_fg", rgb(fg)}, {"menu_disabled_fg", rgb(muted)},
        {"status_bar_fg", rgb(dim)}, {"status_bar_bg", rgb(chrome)},
        {"status_palette_fg", rgb(chrome)}, {"status_palette_bg", rgb(accent)},
        {"status_separator_fg", rgb(raised)}, {"status_separator_bg", rgb(chrome)},
        {"prompt_fg", rgb(fg)}, {"prompt_bg", rgb(chrome)},
        {"prompt_selection_fg", rgb(fg)}, {"prompt_selection_bg", rgb(selection)},
        {"popup_border_fg", rgb(border)}, {"popup_bg", rgb(chrome)}, {"popup_text_fg", rgb(fg)},
        {"popup_selection_bg", rgb(selection)}, {"popup_selection_fg", rgb(fg)},
        {"text_input_selection_bg", rgb(selection)},
        {"suggestion_bg", rgb(chrome)}, {"suggestion_fg", rgb(fg)}, {"suggestion_selected_bg", rgb(selection)},
        {"help_bg", rgb(chrome)}, {"help_fg", rgb(fg)}, {"help_key_fg", rgb(accent)},
        {"help_separator_fg", rgb(raised)}, {"help_indicator_fg", rgb(chrome)}, {"help_indicator_bg", rgb(accent)},
        {"inline_code_bg", rgb(raised)},
        {"split_separator_fg", rgb(raised)}, {"split_separator_hover_fg", rgb(border)},
        {"status_lsp_on_fg", rgb(green)}, {"status_lsp_on_bg", rgb(chrome)},
        {"status_lsp_actionable_fg", rgb(fg)}, {"status_lsp_actionable_bg", rgb(raised)},
        {"status_warning_indicator_fg", rgb(chrome)}, {"status_warning_indicator_bg", rgb(orange)},
        {"status_warning_indicator_hover_fg", rgb(chrome)}, {"status_warning_indicator_hover_bg", rgb(mix(orange, fg, 0.15))},
        {"status_error_indicator_fg", rgb(chrome)}, {"status_error_indicator_bg", rgb(red)},
        {"status_error_indicator_hover_fg", rgb(chrome)}, {"status_error_indicator_hover_bg", rgb(mix(red, fg, 0.15))},
        {"scrollbar_track_fg", rgb(bg)}, {"scrollbar_thumb_fg", rgb(raised)},
        {"scrollbar_track_hover_fg", rgb(bg)}, {"scrollbar_thumb_hover_fg", rgb(muted)},
        {"semantic_highlight_bg", rgb(raised)}, {"terminal_bg", rgb(bg)}, {"terminal_fg", rgb(fg)},
        {"settings_selected_bg", rgb(selection)}, {"settings_selected_fg", rgb(fg)},
        {"file_explorer_directory_fg", rgb(fg)},
        {"file_status_added_fg", rgb(green)}, {"file_status_modified_fg", rgb(yellow)},
        {"file_status_deleted_fg", rgb(red)}, {"file_status_renamed_fg", rgb(cyan)},
        {"file_status_untracked_fg", rgb(green)}, {"file_status_conflicted_fg", rgb(red)},
    };
    const QJsonObject search{
        {"match_bg", rgb(mix(bg, accent, 0.3))}, {"match_fg", rgb(fg)},
        {"current_match_bg", rgb(accent)}, {"current_match_fg", rgb(bg)},
        {"label_bg", rgb(accent)}, {"label_fg", rgb(bg)},
    };
    const QJsonObject diagnostic{
        {"error_fg", rgb(red)}, {"error_bg", rgb(mix(bg, red, 0.12))},
        {"warning_fg", rgb(orange)}, {"warning_bg", rgb(mix(bg, orange, 0.12))},
        {"info_fg", rgb(cyan)}, {"info_bg", rgb(mix(bg, cyan, 0.1))},
        {"hint_fg", rgb(muted)}, {"hint_bg", rgb(bg)},
    };
    const QJsonObject syntax{
        {"keyword", rgb(red)}, {"string", rgb(yellow)}, {"comment", styled(muted, {"italic"})},
        {"function", rgb(green)}, {"type", styled(cyan, {"italic"})}, {"variable", rgb(fg)},
        {"variable_builtin", styled(orange, {"italic"})}, {"constant", rgb(magenta)}, {"operator", rgb(red)},
        {"punctuation_bracket", rgb(dim)}, {"punctuation_delimiter", rgb(dim)},
    };
    const QJsonObject root{
        {"name", "omarchy"}, {"extends", light ? "builtin://light" : "builtin://dark"},
        {"editor", editor}, {"ui", ui}, {"search", search}, {"diagnostic", diagnostic}, {"syntax", syntax},
    };
    return QJsonDocument(root).toJson();
}

// Written only when it changed; then every running Fresh reloads its themes
// and applies this one, so open editors follow a theme switch at once.
void FreshTheme::update() {
    const QByteArray json = build(*m_theme);
    QFile file(m_path);
    if (file.open(QIODevice::ReadOnly) && file.readAll() == json)
        return;
    file.close();
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    file.write(json);
    file.close();

    auto *list = new QProcess(this);
    connect(list, &QProcess::finished, this, [this, list] {
        list->deleteLater();
        for (const QString &line : QString::fromUtf8(list->readAllStandardOutput()).split('\n')) {
            // "Running daemons:" then one indented name per line.
            if (!line.startsWith("  ") || line.trimmed().contains(' '))
                continue;
            auto *apply = new QProcess(this);
            connect(apply, &QProcess::finished, apply, &QObject::deleteLater);
            apply->start(m_fresh, {"--cmd", "script", "run", "--session", line.trimmed(), "-"});
            apply->write("editor.reloadThemes(); return editor.applyTheme(\"omarchy\");\n");
            apply->closeWriteChannel();
        }
    });
    list->start(m_fresh, {"--cmd", "daemon", "list"});
}
