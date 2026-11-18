#include <pxs/Theme.h>

#include <QApplication>
#include <QPalette>
#include <QProxyStyle>
#include <QStyle>
#include <QStyleFactory>
#include <cstdio>

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "FAILED %s:%d %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    for (const auto &palette : {pxs::graphite(), pxs::nyxmap()}) {
        // Always dark: the window is darker than the text, and every token is set
        CHECK(palette.window.lightness() < palette.text.lightness());
        CHECK(palette.base.isValid() && palette.fill.isValid() && palette.link.isValid() && palette.border.isValid());
        const QString sheet = pxs::resolve("QLabel { color:@text; background:@base; border:1px solid @border; } QLabel:focus { color:@focus; }", palette);
        CHECK(!sheet.contains('@'));
        CHECK(sheet.contains(palette.text.name()));
        pxs::apply(app, palette, "QLineEdit { background:@alternate; }");
        CHECK(app.palette().color(QPalette::Window) == palette.window);
        CHECK(app.styleSheet().contains(palette.alternate.name()));
        CHECK(app.styleSheet().contains("QTabBar::tab:selected")); // the tabs are part of the shared sheet
    }
    // An application's own proxy style survives, even when the colours are applied again
    app.setProperty("pxs_styled", false);
    auto *own = new QProxyStyle(QStyleFactory::create("Fusion"));
    app.setStyle(own);
    pxs::apply(app, pxs::graphite());
    pxs::apply(app, pxs::nyxmap());
    CHECK(qobject_cast<QProxyStyle *>(QApplication::style()) == nullptr || QApplication::style() == own);
    return failures ? 1 : 0;
}
