#pragma once

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>
#include <QVariantMap>

// Follows the Omarchy theme live and Hyprland's window rounding, so the
// app looks like part of the desktop instead of a guest on it.
//
// The whole palette is read, not just the accent. An Omarchy theme defines
// background, foreground, muted, selection and the ANSI colours, and an app
// that only takes the accent ends up with a hardcoded grey interior that
// clashes with the desktop. Colours are looked up by their theme key, so
// the same name means the same thing in every theme.
class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString accent READ accent NOTIFY changed)
    Q_PROPERTY(QString accentForeground READ accentForeground NOTIFY changed)
    Q_PROPERTY(int radius READ radius NOTIFY changed)
    // The theme's own colour tokens. Keys: background, dark_background,
    // darker_background, lighter_background, foreground, dark_foreground,
    // light_foreground, bright_foreground, muted, selection,
    // selection_foreground, selection_background, red, green, yellow, blue,
    // magenta, cyan, orange, brown and the bright_* variants.
    Q_PROPERTY(QVariantMap colors READ colors NOTIFY changed)
    // A readable version of the theme's muted tone. A theme's own muted can
    // be too dark against its background — the phosphor theme's #426b4b on
    // #020804 is 3.3:1, which is not readable. This lifts the same hue
    // until it clears the contrast floor, so the app is legible in every
    // theme without hardcoding a grey.
    Q_PROPERTY(QString readableMuted READ readableMuted NOTIFY changed)
    // The window colour. A theme's background is often nearly black, and a
    // window painted in it reads as a hole in the desktop rather than as
    // a window. This lifts it to a visible tone while keeping it dark
    // enough for the theme's own foreground to sit on it.
    Q_PROPERTY(QString windowColor READ windowColor NOTIFY changed)
    // The panel colour: one step above the window, so a search field or a
    // card reads as raised rather than as a hole.
    Q_PROPERTY(QString surfaceColor READ surfaceColor NOTIFY changed)

public:
    explicit Theme(const QString &currentDirectory = {}, QObject *parent = nullptr, const QString &hyprctl = {});
    ~Theme() override;

    QString accent() const { return m_accent; }
    QString accentForeground() const;
    int radius() const { return m_radius; }
    QVariantMap colors() const { return m_colors; }
    QString readableMuted() const { return m_readableMuted; }
    QString windowColor() const { return m_windowColor; }
    QString surfaceColor() const { return m_surfaceColor; }

    // The colour for a theme key, or an empty string when the theme does
    // not define it. An empty string means "no opinion", so callers can
    // fall back to their own default.
    QString color(const QString &key) const;

    // Lift a colour until it is legible against a background. Keeps the
    // hue and saturation, so the result still belongs to the theme.
    static QString ensureContrast(const QString &foreground, const QString &background, double minimumRatio);
    // Blend a colour over another, by alpha. Used to build a raised
    // surface from the theme's foreground, so panels work in a theme that
    // has no "lighter" step of its own.
    static QString blend(const QString &foreground, const QString &background, double alpha);

    static QString readAccent(const QString &path);
    // Every `key = "value"` pair in a theme colours file.
    static QHash<QString, QString> readPalette(const QString &path);

signals:
    void changed();

private:
    void reload();
    void refreshRounding();

    QString m_directory;
    QString m_accent = "#FFD60A";
    QString m_readableMuted = "#8b8b96";
    QString m_windowColor = "#101014";
    QString m_surfaceColor = "#17171c";
    QVariantMap m_colors;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QString m_hyprctl;
    int m_radius = 0;
    QProcess m_roundingProcess;
    QTimer m_roundingPoll, m_roundingTimeout;
};
