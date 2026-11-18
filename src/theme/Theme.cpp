#include <pxs/Theme.h>

#include <QApplication>
#include <QPainter>
#include <QPalette>
#include <QProxyStyle>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleOption>
#include <algorithm>

namespace pxs {

Palette graphite()
{
    Palette p;
    p.window = "#262628"; p.base = "#1e1e20"; p.alternate = "#323235"; p.button = "#313134";
    p.border = "#7b7b82"; p.light = "#4c4c52"; p.midlight = "#3b3b40"; p.dark = "#141416"; p.shadow = "#0c0c0d";
    p.fill = "#55555c"; p.link = "#d4d4d8";
    p.text = "#f2f2f3"; p.muted = "#bdbdc2"; p.placeholder = "#bdbdc2";
    return p;
}

Palette nyxmap()
{
    Palette p;
    p.window = QColor(40, 40, 44); p.base = QColor(30, 30, 33); p.alternate = QColor(40, 40, 44); p.button = QColor(52, 52, 57);
    p.border = "#62626e"; p.light = "#4c4c59"; p.midlight = "#383841"; p.dark = "#25252b"; p.shadow = QColor(18, 18, 20);
    p.fill = "#4c4c59"; p.link = QColor(214, 214, 219);
    p.text = QColor(225, 225, 228); p.muted = QColor(150, 150, 158); p.placeholder = QColor(130, 130, 135);
    return p;
}

QString resolve(QString s, const Palette &p)
{
    return s.replace("@midlight", p.midlight.name()).replace("@alternate", p.alternate.name())
        .replace("@shadow", p.shadow.name()).replace("@dark", p.dark.name())
        .replace("@window", p.window.name()).replace("@light", p.light.name())
        .replace("@text", p.text.name()).replace("@muted", p.muted.name()).replace("@fill", p.fill.name())
        .replace("@button", p.button.name()).replace("@base", p.base.name())
        .replace("@border", p.border.name()).replace("@focus", p.link.name());
}

QString baseStyleSheet(const Palette &p)
{
    return resolve(QStringLiteral(R"(
QGroupBox#section { border:1px solid @border; border-radius:8px; margin-top:9px; }
QGroupBox#section::title { color:@text; subcontrol-origin:margin; left:11px; padding:0 5px; font-weight:600; }
QLabel#caption { color:@muted; }
QPushButton#toggle { background:@button; color:@text; border:1px solid @border; border-radius:6px; padding:5px 9px; }
QPushButton#toggle:hover, QPushButton#toggle:focus { border-color:@focus; }
QPushButton#toggle:checked { background:@fill; color:@text; border-color:@focus; font-weight:600; }
QPushButton#toggle:disabled { color:@muted; }
QTabWidget::pane { border:1px solid @border; }
QTabBar::tab { background:@base; color:@muted; padding:5px 9px; margin-right:3px; font-weight:600;
    border:1px solid @border; border-bottom:3px solid @midlight; border-top-left-radius:4px; border-top-right-radius:4px; }
QTabBar::tab:selected { background:@light; color:@text; border-bottom-color:@focus; }
QTabBar::tab:hover:!selected { background:@midlight; color:@text; }
QTabBar::tab:disabled { color:@border; border-color:@midlight; }
)"), p);
}

void paintCheck(QPainter &painter, const QRectF &box, bool on, bool partial, bool hover, bool enabled, const QPalette &palette)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    QColor accent = palette.color(QPalette::Link);
    const QColor edge = hover ? palette.color(QPalette::Link).darker(135) : palette.color(QPalette::Mid);
    QColor line = on || partial ? accent : edge;
    if (!enabled) {
        accent.setAlphaF(0.45f);
        line.setAlphaF(0.45f);
    }
    painter.setPen(QPen(line, 1.2));
    painter.setBrush(palette.color(QPalette::Base).darker(115));
    painter.drawRoundedRect(box.adjusted(0.6, 0.6, -0.6, -0.6), 3, 3);
    if (on || partial) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(accent);
        const QRectF inner = box.adjusted(3.2, 3.2, -3.2, -3.2);
        painter.drawRoundedRect(partial && !on ? QRectF(inner.left(), inner.center().y() - 1.5, inner.width(), 3) : inner, 1.5, 1.5);
    }
    painter.restore();
}

namespace {
// Fusion, with the check boxes (and the checkable rows of lists and trees) drawn by paintCheck
class CheckStyle : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;
    void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget = nullptr) const override
    {
        if (element == PE_IndicatorCheckBox || element == PE_IndicatorItemViewItemCheck) {
            const QRectF r(option->rect);
            const double side = std::min(r.width(), r.height());
            const QRectF box(r.center().x() - side / 2, r.center().y() - side / 2, side, side);
            paintCheck(*painter, box, option->state & State_On, option->state & State_NoChange, option->state & State_MouseOver,
                       option->state & State_Enabled, option->palette);
            return;
        }
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }
};
} // namespace

void apply(QApplication &app, const Palette &p, const QString &applicationStyleSheet)
{
    // Fusion draws the same on every platform and follows the palette; the check boxes are ours. Chosen once, the first time: an application that
    // installed a style of its own (a proxy around Fusion: its own check boxes…) keeps it, and a later call only changes
    // the colours.
    if (!app.property("pxs_styled").toBool()) {
        if (!qobject_cast<QProxyStyle *>(app.style()))
            app.setStyle(new CheckStyle(QStyleFactory::create(QStringLiteral("Fusion"))));
        app.setProperty("pxs_styled", true);
    }
    QPalette palette;
    palette.setColor(QPalette::Window, p.window);
    palette.setColor(QPalette::Base, p.base);
    palette.setColor(QPalette::AlternateBase, p.alternate);
    palette.setColor(QPalette::Button, p.button);
    palette.setColor(QPalette::Mid, p.border);
    palette.setColor(QPalette::Light, p.light);
    palette.setColor(QPalette::Midlight, p.midlight);
    palette.setColor(QPalette::Dark, p.dark);
    palette.setColor(QPalette::Shadow, p.shadow);
    palette.setColor(QPalette::Highlight, p.fill);
    palette.setColor(QPalette::Link, p.link);
    palette.setColor(QPalette::LinkVisited, p.muted);
    palette.setColor(QPalette::ToolTipBase, p.base);
    palette.setColor(QPalette::ToolTipText, p.text);
    palette.setColor(QPalette::PlaceholderText, p.placeholder);
    for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText, QPalette::HighlightedText}) {
        palette.setColor(role, p.text);
        palette.setColor(QPalette::Disabled, role, p.muted);
    }
    palette.setColor(QPalette::Disabled, QPalette::Highlight, p.alternate);
    app.setPalette(palette);
    app.setStyleSheet(baseStyleSheet(p) + resolve(applicationStyleSheet, p));
}

} // namespace pxs
