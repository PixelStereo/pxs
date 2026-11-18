#pragma once

#include <QColor>
#include <QString>

class QApplication;
class QPainter;
class QPalette;
class QRectF;

// The look of the Pixel Stereo applications as colour tokens. An application picks a palette (always dark) and gives its
// own style sheet written with the tokens; nothing in a style sheet is a literal colour.
namespace pxs {

struct Palette {
    QColor window, base, alternate, button;    // surfaces: the window, fields and lists, odd rows and hover, buttons
    QColor border, light, midlight, dark, shadow; // lines and bevels, from the brightest to the darkest
    QColor fill;                               // what shows an active control (a checked button, a meter)
    QColor link;                               // focus rings, links, the accent that must stand out
    QColor text, muted, placeholder;           // text, secondary text and disabled controls, hints
};

// Neutral graphite greys: soft rounded controls with light borders, no colour cast.
Palette graphite();
// NyxMap's greys: the same steps, a little cooler, with a light accent.
Palette nyxmap();

// Replaces @text @muted @fill @button @base @alternate @border @focus @window @light @midlight @dark @shadow in a style sheet.
QString resolve(QString styleSheet, const Palette &palette);

// The style sheet every application shares: group boxes named "section", checkable buttons named "toggle", labels named
// "caption", and the tabs (a recessed tab, the selected one raised with the accent underline).
QString baseStyleSheet(const Palette &palette);

// A check box: a dark rounded square with a light edge, and the accent (the palette's Link colour) as a filled square
// inside when it is on, a bar when it is partly on. Every QCheckBox and every checkable list or tree row of the
// application is drawn this way (pxs::apply installs the style that calls it); custom widgets can call it directly.
void paintCheck(QPainter &painter, const QRectF &box, bool on, bool partial, bool hover, bool enabled, const QPalette &palette);

// A radio button: the same family as the check box, round: a dark disc with a light edge, and the accent as a dot inside
// when it is selected.
void paintRadio(QPainter &painter, const QRectF &box, bool on, bool hover, bool enabled, const QPalette &palette);

// Sets the Fusion style (unless the application installed a proxy style of its own), the palette and the style sheet
// (base, then the application's own, tokens resolved).
void apply(QApplication &app, const Palette &palette, const QString &applicationStyleSheet = {});

} // namespace pxs
