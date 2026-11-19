// OSCQuery Explorer: browse the OSCQuery devices of the network and play them on a page of controls (Pixel Stereo).
#include "MainWindow.h"

#include <pxs/Theme.h>

#include <QApplication>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("OSCQuery Explorer"));
    app.setOrganizationName(QStringLiteral("Pixel Stereo"));
    pxs::apply(app, pxs::graphite());
    MainWindow window;
    window.show();
    return app.exec();
}
