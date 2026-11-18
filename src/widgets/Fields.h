#pragma once

// The number fields of the Pixel Stereo applications (from NyxMap, where they were worked on the most):
//   SliderField  one number, one control: a bar dragged, the value typed at its right end
//   RangeField   two numbers on one bar (the in and out points)
//   NumberBox    a flat number field that is dragged sideways to change it, or clicked to type
//   IntBox       the same, for whole numbers
// They take their colours from the palette (the accent is its Link colour), so a theme change reaches them.

#include <QDoubleSpinBox>
#include <QGradient>
#include <QSpinBox>
#include <QWidget>
#include <vector>

class QCoreApplication;

// One number, one control: a bar that is dragged, graduated with tick marks, and the value shown and typed
// at its right end. Replaces the slider + spin box + separate value label of the earlier panels.
// A click on the bar jumps to that value, a drag follows the cursor, the wheel steps, and the field at the
// right takes a typed value (with its unit). Fine adjustment with Shift.
// The scroll wheel and the trackpad never change a value: a number field, a slider, a menu passes them to what holds it
// (the panel scrolls). Installed once on the application.
void ignoreWheelOnFields(QCoreApplication *app);

class SliderField : public QWidget
{
    Q_OBJECT
public:
    explicit SliderField(QWidget *parent = nullptr);
    void setRange(double min, double max);
    // Widens what the field accepts beyond the bar: the bar covers the useful range, extreme values are typed
    void setTypedRange(double min, double max);
    void setDecimals(int d);
    void setSuffix(const QString &s);  // " %", " s", " ×"
    void setSingleStep(double s);
    void setTicks(int n);              // tick marks drawn on the bar (0: none)
    void setSnaps(const std::vector<double> &v); // values the cursor catches while dragging (0, 100 %…)
    // Paints the bar with this gradient instead of a fill: the value is then shown as a marker on it
    void setGradient(const QGradientStops &stops);
    void setOrigin(double v);          // where the fill starts (0 by default; 0 in the middle for a ± range)
    void setValue(double v);           // no signal
    void setCompact(bool on);          // a lower bar (the channels of a color)
    void setNumberOnly(bool on);       // no bar: the number alone, dragged sideways to change it, double-clicked to type
    double value() const { return m_value; }
    double minimum() const { return m_min; }
    double maximum() const { return m_max; }
    int decimals() const;
    bool isDragging() const { return m_drag; }
    // Being dragged, or its number is being typed or dragged: a live value must not overwrite it meanwhile
    bool isEditing() const;
    void setSpecialValueText(const QString &text); // shown instead of the minimum (an "Auto", an "Off")
    // The notable values (setSnaps) catch the cursor while dragging; an application turns that off in its settings
    static void setSnappingEnabled(bool on);
    static bool snappingEnabled();
    QSize sizeHint() const override { return QSize(220, 20); }

signals:
    void valueEdited(double v);   // continuous while dragging, and on a typed value
    void editingFinished(double v);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

private:
    QRectF bar() const;
    double valueAt(double x) const;
    void apply(double v, bool finished);
    class QDoubleSpinBox *m_spin;
    double m_value = 0, m_min = 0, m_max = 1, m_origin = 0;
    int m_ticks = 0;
    bool m_drag = false, m_numberOnly = false;
    double m_pressX = 0, m_pressValue = 0;
    std::vector<double> m_snaps;
    QGradientStops m_gradient;
};

// Two numbers on one bar: the chosen range is highlighted between two handles, with its bounds typed at
// either end. Used for the in / out points of a video or a sound.
class RangeField : public QWidget
{
    Q_OBJECT
public:
    explicit RangeField(QWidget *parent = nullptr);
    void setRange(double min, double max); // the bounds of the bar
    void setDecimals(int d);
    void setSuffix(const QString &s);
    void setValues(double lo, double hi); // no signal
    double low() const { return m_lo; }
    double high() const { return m_hi; }
    bool isDragging() const { return m_drag != 0; }
    QSize sizeHint() const override { return QSize(260, 20); }

signals:
    void edited(bool low, double v); // continuous while dragging
    void editingFinished();

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

private:
    QRectF bar() const;
    double valueAt(double x) const;
    double xOf(double v) const;
    class QDoubleSpinBox *m_loSpin, *m_hiSpin;
    double m_lo = 0, m_hi = 1, m_min = 0, m_max = 1;
    int m_drag = 0; // 0 none, 1 low, 2 high
};

class NumberBox : public QDoubleSpinBox
{
public:
    explicit NumberBox(QWidget *parent = nullptr);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }
};
class IntBox : public QSpinBox
{
public:
    explicit IntBox(QWidget *parent = nullptr);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }
};

// The application's check box (it replaces QCheckBox everywhere): a small square that fills with the accent when it
// is on, as the switches of MadMapper or Ableton Live. It is a QCheckBox, so the code that reads it is unchanged.
