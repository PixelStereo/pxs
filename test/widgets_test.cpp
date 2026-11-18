#include "ColorPicker.h"
#include "Fields.h"

#include <QApplication>
#include <QMouseEvent>
#include <cstdio>

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "FAILED %s:%d %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    SliderField slider;
    slider.setRange(0, 1);
    slider.setDecimals(2);
    slider.setValue(.25);
    CHECK(qAbs(slider.value() - .25) < 1e-9);
    // A press on the bar jumps to that value and reports it; the field is being edited until the release
    {
        SliderField bar;
        bar.setRange(0, 100);
        bar.setDecimals(0);
        bar.resize(220, 20);
        bar.show();
        double reported = -1;
        QObject::connect(&bar, &SliderField::valueEdited, [&](double v) { reported = v; });
        const QPointF middle(bar.width() * 0.5 - 20, 10); // the number field takes the right end
        QMouseEvent press(QEvent::MouseButtonPress, middle, bar.mapToGlobal(middle), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&bar, &press);
        CHECK(bar.isEditing());
        CHECK(reported > 30 && reported < 70);
        QMouseEvent release(QEvent::MouseButtonRelease, middle, bar.mapToGlobal(middle), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&bar, &release);
        CHECK(!bar.isEditing());
    }
    // The number fields keep a value within their range and say how wide they want to be
    {
        NumberBox number;
        number.setRange(-1, 1);
        number.setDecimals(2);
        number.setValue(5);
        CHECK(qAbs(number.value() - 1) < 1e-9);
        CHECK(number.sizeHint().width() >= 36);
        IntBox whole;
        whole.setRange(0, 10);
        whole.setValue(-4);
        CHECK(whole.value() == 0);
        RangeField range;
        range.setRange(0, 10);
        range.setValues(2, 8);
        CHECK(range.low() == 2 && range.high() == 8);
    }
    ColorPicker picker(true);
    picker.setColor(QColor(10, 20, 30, 200));
    CHECK(picker.color() == QColor(10, 20, 30, 200)); // programmatic updates are exact and silent
    picker.show();
    app.processEvents();
    return failures ? 1 : 0;
}
