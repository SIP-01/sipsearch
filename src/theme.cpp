#include "theme.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <cmath>

Theme::Theme(const QString &directory, QObject *parent, const QString &hyprctl)
    : QObject(parent),
      m_directory(directory.isEmpty() ? QDir::homePath() + "/.local/state/omarchy/current" : directory) {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(80);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_debounce.start(); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_debounce.start(); });
    connect(&m_debounce, &QTimer::timeout, this, &Theme::reload);

    // Corners follow Hyprland's effective rounding, which themes and personal
    // overrides can both set, so ask hyprctl rather than reading its config.
    m_hyprctl = !hyprctl.isEmpty() ? hyprctl
              : directory.isEmpty() && !qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE")
                  ? QStandardPaths::findExecutable("hyprctl") : QString();
    m_roundingTimeout.setSingleShot(true);
    m_roundingTimeout.setInterval(500);
    connect(&m_roundingTimeout, &QTimer::timeout, &m_roundingProcess, &QProcess::kill);
    connect(&m_roundingProcess, &QProcess::errorOccurred, this, [this] { m_roundingTimeout.stop(); });
    connect(&m_roundingProcess, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        m_roundingTimeout.stop();
        const auto value = QJsonDocument::fromJson(m_roundingProcess.readAllStandardOutput()).object().value("int");
        if (code != 0 || status != QProcess::NormalExit || !value.isDouble() || value.toInt(-1) < 0)
            return;
        if (value.toInt() != m_radius) {
            m_radius = value.toInt();
            emit changed();
        }
    });
    m_roundingPoll.setInterval(1000);
    connect(&m_roundingPoll, &QTimer::timeout, this, &Theme::refreshRounding);
    if (!m_hyprctl.isEmpty())
        m_roundingPoll.start();
    reload();
}

Theme::~Theme() {
    if (m_roundingProcess.state() != QProcess::NotRunning) {
        m_roundingProcess.kill();
        m_roundingProcess.waitForFinished(1000);
    }
}

void Theme::refreshRounding() {
    if (m_hyprctl.isEmpty() || m_roundingProcess.state() != QProcess::NotRunning)
        return;
    m_roundingProcess.start(m_hyprctl, {"-j", "getoption", "decoration:rounding"});
    m_roundingTimeout.start();
}

QString Theme::readAccent(const QString &path) {
    return readPalette(path).value(QStringLiteral("accent"), QStringLiteral("#FFD60A"));
}

// Every `key = "value"` pair in a theme colours file. Omarchy themes are
// TOML, but the subset they use is simple enough to read directly — and
// pulling in a TOML parser for six lines of `key = "#hex"` is not worth it.
QHash<QString, QString> Theme::readPalette(const QString &path) {
    QHash<QString, QString> palette;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return palette;
    const QRegularExpression expression(
        R"re(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*["'](#[0-9a-fA-F]{3,8}|[^"']*)["'])re",
        QRegularExpression::MultilineOption);
    auto matches = expression.globalMatch(QString::fromUtf8(file.readAll()));
    while (matches.hasNext()) {
        const auto match = matches.next();
        palette.insert(match.captured(1), match.captured(2));
    }
    return palette;
}

QString Theme::color(const QString &key) const {
    return m_colors.value(key).toString();
}

// WCAG relative luminance.
static double relativeLuminance(const QColor &c) {
    auto channel = [](double v) {
        return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4);
    };
    return .2126 * channel(c.redF()) + .7152 * channel(c.greenF()) + .0722 * channel(c.blueF());
}

static double contrastRatio(const QColor &a, const QColor &b) {
    const double la = relativeLuminance(a), lb = relativeLuminance(b);
    const double hi = qMax(la, lb), lo = qMin(la, lb);
    return (hi + .05) / (lo + .05);
}

// Blend a colour over another, by alpha.
QString Theme::blend(const QString &foreground, const QString &background, double alpha) {
    const QColor fg(foreground), bg(background);
    if (!fg.isValid() || !bg.isValid())
        return foreground;
    const auto mix = [alpha](int a, int b) { return qBound(0, qRound(a * alpha + b * (1.0 - alpha)), 255); };
    return QColor(mix(fg.red(), bg.red()), mix(fg.green(), bg.green()), mix(fg.blue(), bg.blue()))
        .name(QColor::HexRgb);
}

// Lift a colour until it is legible against a background. The hue and
// saturation are kept, so the result still belongs to the theme — only the
// lightness moves. A theme's muted tone is meant to recede, and on a very
// dark background it can recede past the point of being readable.
QString Theme::ensureContrast(const QString &foreground, const QString &background, double minimumRatio) {
    const QColor fg(foreground), bg(background);
    if (!fg.isValid() || !bg.isValid())
        return foreground;
    if (contrastRatio(fg, bg) >= minimumRatio)
        return foreground;

    QColor hsl = fg.toHsl();
    // Walk the lightness up until the ratio is met, then stop. A binary
    // search would be tidier but the lightness range is short and this
    // keeps the exact point where the ratio is first met.
    for (int step = 1; step <= 100; ++step) {
        const double lightness = qMin(1.0, hsl.lightnessF() + step / 100.0);
        QColor lifted = QColor::fromHslF(hsl.hue(), hsl.saturation(), lightness);
        if (contrastRatio(lifted, bg) >= minimumRatio)
            return lifted.name(QColor::HexRgb);
    }
    // Nothing in the theme's own hue reaches the ratio — fall back to the
    // theme's foreground, which always does.
    return background == bg.name(QColor::HexRgb) ? QStringLiteral("#ffffff") : foreground;
}

// Black or white, whichever reads on the accent (WCAG relative luminance).
QString Theme::accentForeground() const {
    const QColor c(m_accent);
    auto linear = [](double v) { return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4); };
    const double luminance = .2126 * linear(c.redF()) + .7152 * linear(c.greenF()) + .0722 * linear(c.blueF());
    return luminance > .179 ? "black" : "white";
}

void Theme::reload() {
    refreshRounding();
    const auto paths = m_watcher.files() + m_watcher.directories();
    if (!paths.isEmpty())
        m_watcher.removePaths(paths);

    // A theme switch replaces symlinks and files, so watch the parents too and
    // re-arm after every change. Watching the nearest existing ancestor means
    // installing Omarchy later works without restarting the app.
    QString ancestor = m_directory;
    while (!QFileInfo::exists(ancestor) && ancestor != "/")
        ancestor = QFileInfo(ancestor).absolutePath();
    QStringList candidates{ancestor, QFileInfo(ancestor).absolutePath(), m_directory,
                           m_directory + "/theme", m_directory + "/theme/colors.toml"};
    candidates.removeDuplicates();
    for (const auto &path : candidates)
        if (QFileInfo::exists(path))
            m_watcher.addPath(path);

    const auto accent = readAccent(m_directory + "/theme/colors.toml");
    const auto palette = readPalette(m_directory + "/theme/colors.toml");

    QVariantMap colors;
    for (auto it = palette.cbegin(); it != palette.cend(); ++it)
        colors.insert(it.key(), it.value());
    if (colors != m_colors) {
        m_colors = colors;
        emit changed();
    }

    // The muted tone has to be legible against the app's own background.
    // A theme's muted is meant to recede, and on a very dark background it
    // can recede past being readable — the phosphor theme's #426b4b on
    // #020804 is 3.3:1, below even the 4.5:1 AA floor. The floor here is
    // 7:1, the AAA body-text ratio, because this tone carries the result
    // snippets and the status line at 11-12 pixels, where 4.5:1 reads as
    // a smudge rather than as text.
    const QString background = m_colors.value("background").toString().isEmpty()
        ? QStringLiteral("#101014") : m_colors.value("background").toString();
    const QString foreground = m_colors.value("foreground").toString().isEmpty()
        ? QStringLiteral("#e6e6e9") : m_colors.value("foreground").toString();
    const QString muted = m_colors.value("muted").toString().isEmpty()
        ? QStringLiteral("#8b8b96") : m_colors.value("muted").toString();

    // The window colour. A theme's background is often nearly black, and a
    // window painted in it reads as a hole in the desktop rather than as a
    // window — the text floats on nothing and the panel edges vanish.
    // The window takes the theme's lighter step when it has one, and
    // otherwise the background is lifted slightly toward the foreground.
    QString window = m_colors.value("lighter_background").toString();
    if (window.isEmpty() || window == background)
        window = blend(foreground, background, 0.06);
    if (window != m_windowColor) {
        m_windowColor = window;
        emit changed();
    }

    // The panel colour: one step above the window.
    QString surface = m_colors.value("lighter_background").toString();
    if (surface.isEmpty() || surface == background || surface == window)
        surface = blend(foreground, window, 0.07);
    if (surface != m_surfaceColor) {
        m_surfaceColor = surface;
        emit changed();
    }

    // The tone is measured against the window colour, which is what the
    // text actually sits on.
    QString readable = ensureContrast(muted, m_windowColor, 7.0);
    if (readable == muted)
        readable = ensureContrast(muted, background, 7.0);
    if (readable != m_readableMuted) {
        m_readableMuted = readable;
        emit changed();
    }

    if (accent != m_accent) {
        m_accent = accent;
        emit changed();
    }
}
