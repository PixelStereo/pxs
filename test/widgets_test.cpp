#include "ColorPicker.h"
#include "SliderField.h"

#include <QApplication>
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
    ColorPicker picker(true);
    picker.setColor(QColor(10, 20, 30, 200));
    CHECK(picker.color() == QColor(10, 20, 30, 200)); // programmatic updates are exact and silent
    picker.show();
    app.processEvents();
    return failures ? 1 : 0;
}
