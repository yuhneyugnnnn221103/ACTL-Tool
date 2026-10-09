#include "theme.h"
#include <QApplication>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QGraphicsDropShadowEffect>
#include <QStyleFactory>

namespace ui::theme {

using model::Status;

const Palette &palette()
{
    static const Palette p;
    return p;
}

QColor statusColor(Status s)
{
    switch (s) {
    case Status::NoData:   return QColor(0xE4, 0xE2, 0xDA);
    case Status::Ok:       return QColor(0x7D, 0xB9, 0x3B);
    case Status::Warning:  return QColor(0xEF, 0x9F, 0x27);
    case Status::Trip:     return QColor(0xE2, 0x4B, 0x4A);
    case Status::Lost:     return QColor(0x88, 0x87, 0x80);
    case Status::Updating: return QColor(0x37, 0x8A, 0xDD);
    }
    return {};
}

QString statusMark(Status s)
{
    switch (s) {
    case Status::Ok:       return QStringLiteral("✓");
    case Status::Warning:  return QStringLiteral("▲");
    case Status::Trip:     return QStringLiteral("!");
    case Status::Lost:     return QStringLiteral("✕");
    case Status::Updating: return QStringLiteral("↻");
    default:               return {};
    }
}

QColor statusTextColor(Status s)
{
    return s == Status::NoData ? QColor(0x44, 0x44, 0x41) : QColor(Qt::white);
}

QString styleSheet()
{
    QFile f(QStringLiteral(":/style.qss"));
    if (!f.open(QIODevice::ReadOnly)) return {};
    QString css = QString::fromUtf8(f.readAll());

    const Palette &p = palette();
    const QList<QPair<QString, QColor>> tokens = {
        {"bg", p.bg}, {"surface", p.surface}, {"surfaceAlt", p.surfaceAlt}, {"border", p.border}, {"grid", p.grid}, {"stripe", p.stripe},
        {"text", p.text}, {"textMuted", p.textMuted}, {"accent", p.accent}, {"accentHover", p.accentHover},
        {"accentPressed", p.accentPressed}, {"accentSoft", p.accentSoft}, {"danger", p.danger},
        {"dangerHover", p.dangerHover}, {"dangerPressed", p.dangerPressed}, {"disabledBg", p.disabledBg},
        {"disabledText", p.disabledText},
    };
    for (const auto &t : tokens) css.replace(QLatin1Char('@') + t.first + QLatin1Char('@'), t.second.name());
    return css;
}

void apply(QApplication &app)
{
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    // Font: Segoe UI (Windows) hoặc Roboto; thiếu thì dùng font hệ thống. Cỡ 13px đặt cả ở đây lẫn trong QSS.
    QFont font = app.font();
    for (const QString &family : {QStringLiteral("Segoe UI"), QStringLiteral("Roboto")}) {
        if (QFontDatabase::families().contains(family)) { font.setFamily(family); break; }
    }
    font.setPixelSize(13);
    app.setFont(font);
    app.setStyleSheet(styleSheet());
}

void addShadow(QWidget *card)
{
    auto *shadow = new QGraphicsDropShadowEffect(card);
    shadow->setBlurRadius(15);
    shadow->setOffset(0, 3);
    shadow->setColor(QColor(0, 0, 0, 25));
    card->setGraphicsEffect(shadow);
}

} // namespace ui::theme
