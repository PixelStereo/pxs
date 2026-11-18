#include "Fields.h"

#include <QAbstractSlider>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QScrollBar>
#include <QSignalBlocker>
#include <algorithm>
#include <cmath>

namespace {
QColor accentColor() { return QApplication::palette().color(QPalette::Link); }
bool g_snapping = true;
} // namespace

namespace {
// Slides the value of a number field, on its text field
class NumberDrag : public QObject
{
public:
    NumberDrag(QAbstractSpinBox *spin, QLineEdit *edit) : QObject(spin), m_spin(spin), m_edit(edit)
    {
        edit->installEventFilter(this);
        spin->installEventFilter(this); // its focus (the text field's is given by it, past the filters)
        edit->setCursor(Qt::SizeHorCursor); // an I-beam only once editing
        // A press must not give the focus by itself (Qt does it before any filter sees the press): the release
        // decides, a click edits the text, a drag does not. Tab still reaches the field.
        spin->setFocusPolicy(Qt::TabFocus);
        edit->setFocusPolicy(Qt::TabFocus);
    }

protected:
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (o == m_spin) {
            if (e->type() == QEvent::FocusIn) m_edit->setCursor(Qt::IBeamCursor);
            else if (e->type() == QEvent::FocusOut) m_edit->setCursor(Qt::SizeHorCursor);
            return false;
        }
        switch (e->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonDblClick:
        case QEvent::MouseMove:
        case QEvent::MouseButtonRelease: break;
        default: return false;
        }
        auto *me = static_cast<QMouseEvent *>(e);
        // Editing the text: the mouse places the cursor and selects, as usual
        if (!m_pressed && (m_spin->hasFocus() || !m_spin->isEnabled() || m_spin->isReadOnly())) return false;
        const QPointF pos = me->globalPosition();
        if (e->type() == QEvent::MouseButtonPress || e->type() == QEvent::MouseButtonDblClick) {
            if (me->button() != Qt::LeftButton) return false;
            m_pressed = true;
            m_dragging = false;
            m_start = pos;
            m_last = pos.x();
            m_carry = 0;
            return true; // neither focus nor text cursor yet
        }
        if (!m_pressed) return false;
        if (e->type() == QEvent::MouseMove) {
            if (!m_dragging) {
                if (std::abs(pos.x() - m_start.x()) < 3) return true;
                m_dragging = true;
                m_last = pos.x();
            }
            slide(pos.x() - m_last, me->modifiers());
            m_last = pos.x();
            return true;
        }
        // Release: a click without moving edits the text, all of it selected; a drag leaves the field as it was
        m_pressed = false;
        if (!m_dragging) {
            m_spin->setFocus(Qt::MouseFocusReason);
            m_spin->selectAll();
        }
        m_dragging = false;
        return true;
    }

private:
    void slide(double dx, Qt::KeyboardModifiers mods)
    {
        double factor = 1.0;
        if (mods & Qt::ShiftModifier) factor = 0.1;
        if (mods & Qt::ControlModifier) factor = 10.0;
        if (auto *d = qobject_cast<QDoubleSpinBox *>(m_spin)) {
            m_carry += dx * d->singleStep() * factor;
            const double unit = std::pow(10.0, -d->decimals());
            const double move = std::trunc(m_carry / unit) * unit; // whole units of what is shown
            if (move != 0) {
                m_carry -= move;
                d->setValue(d->value() + move);
            }
        } else if (auto *i = qobject_cast<QSpinBox *>(m_spin)) {
            m_carry += dx * i->singleStep() * factor;
            const int move = int(std::trunc(m_carry));
            if (move != 0) {
                m_carry -= move;
                i->setValue(i->value() + move);
            }
        }
    }

    QAbstractSpinBox *m_spin;
    QLineEdit *m_edit;
    QPointF m_start;
    double m_last = 0, m_carry = 0;
    bool m_pressed = false, m_dragging = false;
};
} // namespace

namespace {
constexpr int kBoxHeight = 20;

void compactBox(QAbstractSpinBox *s)
{
    s->setButtonSymbols(QAbstractSpinBox::NoButtons);
    s->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    s->setFixedHeight(kBoxHeight);
    s->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    // Keep the native edit-field geometry. Styling the spinbox box model can
    // collapse its NoButtons editor to one pixel on supported Qt versions.
    QPalette palette = s->palette();
    palette.setColor(QPalette::Base, QColor(35, 35, 38));
    s->setPalette(palette);
}

QSize boxHint(const QAbstractSpinBox *s, const QString &longest)
{
    return QSize(std::max(36, s->fontMetrics().horizontalAdvance(longest) + 14), kBoxHeight);
}
} // namespace

NumberBox::NumberBox(QWidget *parent) : QDoubleSpinBox(parent)
{
    new NumberDrag(this, lineEdit());
    compactBox(this);
}

QSize NumberBox::sizeHint() const
{
    const double big = std::max(std::abs(minimum()), std::abs(maximum()));
    return boxHint(this, prefix() + (minimum() < 0 ? QStringLiteral("-") : QString()) + QString::number(big, 'f', decimals())
                             + suffix());
}

IntBox::IntBox(QWidget *parent) : QSpinBox(parent)
{
    new NumberDrag(this, lineEdit());
    compactBox(this);
}

QSize IntBox::sizeHint() const
{
    const long long big = std::max(std::llabs(minimum()), std::llabs(maximum()));
    return boxHint(this, prefix() + (minimum() < 0 ? QStringLiteral("-") : QString()) + QString::number(big) + suffix());
}


namespace {
constexpr int kFieldHeight = 20;
const QColor kTrack(28, 28, 31);
QColor fillColor(bool on) { QColor c = accentColor(); c.setAlpha(on ? 210 : 80); return c; }
const QColor kTick(255, 255, 255, 38), kBorder(0, 0, 0, 90);

// Flat field at the end of a bar: the value, typed, with its unit and no spin buttons.
QDoubleSpinBox *valueField(QWidget *parent, Qt::Alignment align = Qt::AlignRight)
{
    auto *s = new NumberBox(parent);
    s->setButtonSymbols(QAbstractSpinBox::NoButtons);
    s->setAlignment(align | Qt::AlignVCenter);
    s->setKeyboardTracking(false);
    s->setFrame(false);
    s->setFixedHeight(kFieldHeight - 2);
    return s;
}

int fieldWidth(const QDoubleSpinBox *s)
{
    const QString longest = QStringLiteral("-%1%2").arg(std::max(std::abs(s->minimum()), std::abs(s->maximum())),
                                                        0, 'f', s->decimals())
                            + s->suffix();
    return s->fontMetrics().horizontalAdvance(longest) + 16;
}
} // namespace


SliderField::SliderField(QWidget *parent) : QWidget(parent)
{
    setFixedHeight(kFieldHeight);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::SizeHorCursor);
    m_spin = valueField(this);
    m_spin->setRange(0, 1);
    connect(m_spin, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (std::abs(v - m_value) < 1e-12) return;
        m_value = v;
        update();
        emit valueEdited(v);
        emit editingFinished(v);
    });
}

void SliderField::setRange(double min, double max)
{
    m_min = min;
    m_max = std::max(max, min + 1e-9);
    m_spin->setRange(min, max);
    m_origin = std::clamp(m_origin, m_min, m_max);
    resizeEvent(nullptr);
    update();
}
void SliderField::setTypedRange(double min, double max)
{
    m_spin->setRange(min, max);
    resizeEvent(nullptr);
}
void SliderField::setDecimals(int d) { m_spin->setDecimals(d); resizeEvent(nullptr); }
void SliderField::setSuffix(const QString &s) { m_spin->setSuffix(s); resizeEvent(nullptr); }
void SliderField::setSingleStep(double s) { m_spin->setSingleStep(s); }
void SliderField::setTicks(int n) { m_ticks = n; update(); }
void SliderField::setSnaps(const std::vector<double> &v) { m_snaps = v; }
void SliderField::setGradient(const QGradientStops &stops) { m_gradient = stops; update(); }
void SliderField::setOrigin(double v) { m_origin = std::clamp(v, m_min, m_max); update(); }

void SliderField::setValue(double v)
{
    v = std::clamp(v, m_spin->minimum(), m_spin->maximum());
    if (std::abs(v - m_value) < 1e-12) return;
    m_value = v;
    QSignalBlocker b(m_spin);
    m_spin->setValue(v);
    update();
}

void SliderField::resizeEvent(QResizeEvent *)
{
    const int w = m_numberOnly ? width() : fieldWidth(m_spin);
    m_spin->setFixedHeight(height() - 2);
    m_spin->setGeometry(width() - w, 1, w, height() - 2);
    update();
}

void SliderField::setNumberOnly(bool on)
{
    m_numberOnly = on;
    m_spin->setAttribute(Qt::WA_TransparentForMouseEvents, on);
    if (on) { // the row's colour shows through the number
        QPalette pal = m_spin->palette();
        pal.setColor(QPalette::Base, Qt::transparent);
        pal.setColor(QPalette::Window, Qt::transparent);
        m_spin->setPalette(pal);
        m_spin->setAutoFillBackground(false);
        if (auto *le = m_spin->findChild<QLineEdit *>()) le->setAutoFillBackground(false);
    }
    if (on) connect(m_spin, &QAbstractSpinBox::editingFinished, this, [this] { m_spin->setAttribute(Qt::WA_TransparentForMouseEvents, true); });
    resizeEvent(nullptr);
}

void SliderField::mouseDoubleClickEvent(QMouseEvent *)
{
    if (!m_numberOnly || !isEnabled()) return;
    m_drag = false;
    m_spin->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    m_spin->setFocus(Qt::MouseFocusReason);
    m_spin->selectAll();
}

int SliderField::decimals() const { return m_spin->decimals(); }

QRectF SliderField::bar() const { if (m_numberOnly) return QRectF(); return QRectF(0, 1, width() - m_spin->width() - 4, height() - 2); }

void SliderField::setCompact(bool on) { setFixedHeight(on ? kFieldHeight - 4 : kFieldHeight); }
double SliderField::valueAt(double x) const
{
    const QRectF r = bar();
    return m_min + std::clamp((x - r.left()) / std::max(1.0, r.width()), 0.0, 1.0) * (m_max - m_min);
}

void SliderField::apply(double v, bool finished)
{
    v = std::clamp(v, m_spin->minimum(), m_spin->maximum());
    const double q = std::pow(10, m_spin->decimals());
    v = std::round(v * q) / q;
    if (std::abs(v - m_value) > 1e-12) {
        m_value = v;
        QSignalBlocker b(m_spin);
        m_spin->setValue(v);
        update();
        emit valueEdited(v);
    }
    if (finished) emit editingFinished(m_value);
}

void SliderField::paintEvent(QPaintEvent *)
{
    if (m_numberOnly) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = bar();
    p.setPen(Qt::NoPen);
    p.setBrush(kTrack);
    p.drawRoundedRect(r, 3, 3);
    auto xOf = [&](double v) {
        return r.left() + (std::clamp(v, m_min, m_max) - m_min) / (m_max - m_min) * r.width();
    };
    const double x0 = xOf(m_origin), xv = xOf(m_value);
    if (!m_gradient.isEmpty()) {
        // Color bar: the whole track shows the scale, and the value is a marker on it
        QLinearGradient grad(r.topLeft(), r.topRight());
        grad.setStops(m_gradient);
        p.setBrush(grad);
        p.setOpacity(isEnabled() ? 1.0 : 0.4);
        p.drawRoundedRect(r, 3, 3);
        p.setOpacity(1.0);
    } else {
        p.setBrush(fillColor(isEnabled()));
        p.drawRoundedRect(QRectF(QPointF(std::min(x0, xv), r.top()), QPointF(std::max(x0, xv), r.bottom())), 3, 3);
    }
    if (m_ticks > 1) {
        p.setPen(QPen(kTick, 1));
        for (int i = 1; i < m_ticks; ++i) {
            const double x = r.left() + r.width() * i / m_ticks;
            p.drawLine(QPointF(x, r.top() + 3), QPointF(x, r.bottom() - 3));
        }
    }
    if (!m_gradient.isEmpty()) {
        p.setPen(QPen(QColor(20, 20, 22), 3));
        p.drawLine(QPointF(xv, r.top() + 1), QPointF(xv, r.bottom() - 1));
        p.setPen(QPen(Qt::white, 1.4));
        p.drawLine(QPointF(xv, r.top() + 1), QPointF(xv, r.bottom() - 1));
    }
    p.setPen(QPen(kBorder, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
    if (hasFocus()) {
        {
        QColor c = accentColor();
        c.setAlpha(140);
        p.setPen(QPen(c, 1));
    }
        p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
    }
}

void SliderField::mousePressEvent(QMouseEvent *e)
{
    if (m_numberOnly) {
        if (e->button() != Qt::LeftButton || !isEnabled()) return;
        m_drag = true;
        m_pressX = e->position().x();
        m_pressValue = m_value;
        setFocus(Qt::MouseFocusReason);
        return;
    }
    if (e->button() != Qt::LeftButton || !isEnabled() || e->position().x() > bar().right()) return;
    m_drag = true;
    setFocus(Qt::MouseFocusReason);
    apply(valueAt(e->position().x()), false);
}
void SliderField::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_drag) return;
    if (m_numberOnly) { // the whole range over 160 px (ten times finer with Shift)
        const double per = (m_max - m_min) / 160.0 * (e->modifiers() & Qt::ShiftModifier ? 0.1 : 1.0);
        return apply(m_pressValue + (e->position().x() - m_pressX) * per, false);
    }
    const double v = valueAt(e->position().x());
    // Shift: ten times finer, from where the value is
    if (e->modifiers() & Qt::ShiftModifier) return apply(m_value + (v - m_value) * 0.1, false);
    // Otherwise the notable values catch the cursor (magnetism), so they can be hit on a wide range
    const double tol = (m_max - m_min) * 0.012;
    double best = v;
    if (g_snapping)
        for (double snap : m_snaps)
            if (snap >= m_min && snap <= m_max && std::abs(v - snap) < tol) best = snap;
    apply(best, false);
}
void SliderField::mouseReleaseEvent(QMouseEvent *e)
{
    if (!m_drag || e->button() != Qt::LeftButton) return;
    m_drag = false;
    emit editingFinished(m_value);
}
namespace {
class WheelPassThrough : public QObject
{
public:
    using QObject::QObject;
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (e->type() != QEvent::Wheel) return false;
        auto *w = qobject_cast<QWidget *>(o);
        if (!w || !(qobject_cast<QAbstractSpinBox *>(w) || qobject_cast<QComboBox *>(w) ||
                    (qobject_cast<QAbstractSlider *>(w) && !qobject_cast<QScrollBar *>(w))))
            return false;
        // To its parent instead (ignored there, it goes on up to the scroll area)
        if (QWidget *p = w->parentWidget()) QCoreApplication::sendEvent(p, e);
        return true;
    }
};
} // namespace

void ignoreWheelOnFields(QCoreApplication *app)
{
    app->installEventFilter(new WheelPassThrough(app));
}


// --- RangeField

RangeField::RangeField(QWidget *parent) : QWidget(parent)
{
    setFixedHeight(kFieldHeight);
    setCursor(Qt::SizeHorCursor);
    m_loSpin = valueField(this, Qt::AlignLeft);
    m_hiSpin = valueField(this);
    connect(m_loSpin, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (std::abs(v - m_lo) < 1e-12) return;
        m_lo = std::min(v, m_hi);
        update();
        emit edited(true, m_lo);
        emit editingFinished();
    });
    connect(m_hiSpin, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (std::abs(v - m_hi) < 1e-12) return;
        m_hi = std::max(v, m_lo);
        update();
        emit edited(false, m_hi);
        emit editingFinished();
    });
}

void RangeField::setRange(double min, double max)
{
    m_min = min;
    m_max = std::max(max, min + 1e-9);
    for (auto *s : {m_loSpin, m_hiSpin}) s->setRange(min, m_max);
    resizeEvent(nullptr);
    update();
}
void RangeField::setDecimals(int d) { for (auto *s : {m_loSpin, m_hiSpin}) s->setDecimals(d); resizeEvent(nullptr); }
void RangeField::setSuffix(const QString &s) { for (auto *b : {m_loSpin, m_hiSpin}) b->setSuffix(s); resizeEvent(nullptr); }

void RangeField::setValues(double lo, double hi)
{
    m_lo = std::clamp(lo, m_min, m_max);
    m_hi = std::clamp(std::max(hi, m_lo), m_min, m_max);
    QSignalBlocker a(m_loSpin), b(m_hiSpin);
    m_loSpin->setValue(m_lo);
    m_hiSpin->setValue(m_hi);
    update();
}

void RangeField::resizeEvent(QResizeEvent *)
{
    const int wl = fieldWidth(m_loSpin), wh = fieldWidth(m_hiSpin);
    m_loSpin->setGeometry(0, 1, wl, kFieldHeight - 2);
    m_hiSpin->setGeometry(width() - wh, 1, wh, kFieldHeight - 2);
    update();
}

QRectF RangeField::bar() const
{
    return QRectF(QPointF(m_loSpin->width() + 4, 1), QPointF(width() - m_hiSpin->width() - 4, kFieldHeight - 1));
}
double RangeField::xOf(double v) const
{
    const QRectF r = bar();
    return r.left() + (v - m_min) / (m_max - m_min) * r.width();
}
double RangeField::valueAt(double x) const
{
    const QRectF r = bar();
    return m_min + std::clamp((x - r.left()) / std::max(1.0, r.width()), 0.0, 1.0) * (m_max - m_min);
}

void RangeField::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = bar();
    p.setPen(Qt::NoPen);
    p.setBrush(kTrack);
    p.drawRoundedRect(r, 3, 3);
    { QColor c = accentColor(); c.setAlpha(isEnabled() ? 110 : 45); p.setBrush(c); }
    p.drawRect(QRectF(QPointF(xOf(m_lo), r.top()), QPointF(xOf(m_hi), r.bottom())));
    p.setPen(QPen(kBorder, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
    // Handles: a triangle pointing into the range
    p.setPen(Qt::NoPen);
    p.setBrush(isEnabled() ? accentColor().lighter(115) : QColor(150, 150, 155));
    for (int k = 0; k < 2; ++k) {
        const double x = k ? xOf(m_hi) : xOf(m_lo);
        const double d = k ? -5 : 5;
        QPainterPath path;
        path.moveTo(x, r.top());
        path.lineTo(x + d, r.top());
        path.lineTo(x, r.top() + 6);
        path.closeSubpath();
        p.drawPath(path);
        QPainterPath low;
        low.moveTo(x, r.bottom());
        low.lineTo(x + d, r.bottom());
        low.lineTo(x, r.bottom() - 6);
        low.closeSubpath();
        p.drawPath(low);
        p.fillRect(QRectF(x - 0.5, r.top(), 1, r.height()), p.brush());
    }
}

void RangeField::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton || !isEnabled()) return;
    const double x = e->position().x();
    if (x < bar().left() || x > bar().right()) return;
    m_drag = std::abs(x - xOf(m_lo)) <= std::abs(x - xOf(m_hi)) ? 1 : 2;
    mouseMoveEvent(e);
}
void RangeField::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_drag) return;
    const double v = valueAt(e->position().x());
    if (m_drag == 1) {
        m_lo = std::min(v, m_hi);
        QSignalBlocker b(m_loSpin);
        m_loSpin->setValue(m_lo);
        emit edited(true, m_lo);
    } else {
        m_hi = std::max(v, m_lo);
        QSignalBlocker b(m_hiSpin);
        m_hiSpin->setValue(m_hi);
        emit edited(false, m_hi);
    }
    update();
}
void RangeField::mouseReleaseEvent(QMouseEvent *e)
{
    if (!m_drag || e->button() != Qt::LeftButton) return;
    m_drag = 0;
    emit editingFinished();
}

bool SliderField::isEditing() const
{
    return m_drag || m_spin->hasFocus() || [this] {
        const auto *edit = m_spin->findChild<QLineEdit *>();
        return edit && edit->hasFocus();
    }();
}
void SliderField::setSpecialValueText(const QString &text) { m_spin->setSpecialValueText(text); resizeEvent(nullptr); }
void SliderField::setSnappingEnabled(bool on) { g_snapping = on; }
bool SliderField::snappingEnabled() { return g_snapping; }
