#include "pxs/ChannelGrid.h"
#include "pxs/LevelControl.h"

#include <QApplication>
#include <QFrame>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QtTest/QTest>
#include <QWheelEvent>
#include <cstdio>

using namespace pxs::dmx;

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "FAILED %s:%d %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

static void press(QWidget *w, const QPoint &p, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QMouseEvent down(QEvent::MouseButtonPress, p, w->mapToGlobal(p), Qt::LeftButton, Qt::LeftButton, mods);
    QApplication::sendEvent(w, &down);
}
static void release(QWidget *w, const QPoint &p, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QMouseEvent up(QEvent::MouseButtonRelease, p, w->mapToGlobal(p), Qt::LeftButton, Qt::NoButton, mods);
    QApplication::sendEvent(w, &up);
}
static void click(QWidget *w, const QPoint &p, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    press(w, p, mods);
    release(w, p, mods);
    QCoreApplication::processEvents(); // the editor opens once the click ends
}
static void key(QWidget *w, int k)
{
    QKeyEvent e(QEvent::KeyPress, k, Qt::NoModifier);
    QApplication::sendEvent(w, &e);
}

static void levelControl()
{
    LevelControl control;
    control.resize(280, control.height());
    control.show();
    // Bounds
    control.setValue(400);
    CHECK(control.value() == 255);
    control.setValue(-10);
    CHECK(control.value() == 0);
    // The network is read only
    control.setValue(40);
    control.setNetwork(200, 120);
    CHECK(control.network() == 200 && control.others() == 120 && control.value() == 40);
    control.setNetwork(-5);
    CHECK(control.network() == -1 && control.others() == -1);
    // Keys
    control.setFocus();
    control.setValue(0);
    key(&control, Qt::Key_Up);
    CHECK(control.value() == 1);
    key(&control, Qt::Key_PageUp);
    CHECK(control.value() == 11);
    key(&control, Qt::Key_End);
    CHECK(control.value() == 255);
    key(&control, Qt::Key_Home);
    CHECK(control.value() == 0);
    // Click and drag on the lower (output) bar: its left end is zero, its right end full
    const int y = control.height() - 14, left = 74, right = 280 - 34 - 8;
    click(&control, QPoint(left, y));
    CHECK(control.value() == 0);
    click(&control, QPoint(right, y));
    CHECK(control.value() == 255);
    press(&control, QPoint((left + right) / 2, y));
    CHECK(std::abs(control.value() - 128) <= 2);
    QMouseEvent move(QEvent::MouseMove, QPoint(left, y), control.mapToGlobal(QPoint(left, y)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&control, &move);
    release(&control, QPoint(left, y));
    CHECK(control.value() == 0);
    // The wheel: four steps, one with Shift
    control.setValue(100);
    QWheelEvent coarse(QPointF(110, 50), QPointF(110, 50), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
    QCoreApplication::sendEvent(&control, &coarse);
    CHECK(control.value() == 104);
    QWheelEvent fine(QPointF(110, 50), QPointF(110, 50), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::ShiftModifier, Qt::ScrollUpdate, false);
    QCoreApplication::sendEvent(&control, &fine);
    CHECK(control.value() == 105);
}

static ChannelGrid::State stateWith(std::initializer_list<std::pair<int, int>> received)
{
    ChannelGrid::State s;
    s.received = QByteArray(512, 0);
    s.output = QByteArray(512, 0);
    s.overridden = QBitArray(512, false);
    for (auto [channel, level] : received) s.received[channel - 1] = char(level);
    return s;
}

static void channelGrid()
{
    ChannelGrid view;
    view.resize(1100, 600);
    view.show();
    auto *grid = view.findChild<QTableWidget *>("channelGrid");
    auto *editor = view.findChild<QFrame *>("channelEditor");
    auto *encoder = view.findChild<LevelControl *>("outputEncoder");
    auto *channel = view.findChild<QSpinBox *>("channel");
    auto *release = view.findChild<QPushButton *>("releaseChannel");
    CHECK(grid && editor && encoder && channel && release);
    auto cell = [&](int row, int column) { return grid->visualRect(grid->model()->index(row, column)).center(); };
    // Through the window system, so that the modifiers reach the selection model
    auto clickCell = [&](int row, int column, Qt::KeyboardModifiers mods = Qt::NoModifier) {
        QTest::mouseClick(grid->viewport(), Qt::LeftButton, mods, cell(row, column));
        QCoreApplication::processEvents(); // the editor opens once the click ends
    };

    // Nothing received: no data, an empty message is not shown while the 512 cells are
    CHECK(grid->item(0, 0)->text() == "1\n—" || grid->item(0, 0)->text().isEmpty());
    ChannelGrid::State state = stateWith({{1, 231}, {5, 96}, {71, 120}});
    view.setState(state);
    CHECK(grid->item(0, 0)->text() == "1\n231" && grid->item(0, 0)->background().color() == QColor(231, 231, 231));
    CHECK(grid->item(0, 4)->text() == "5\n96");
    CHECK(view.activeCount() == 3);

    // The filter keeps the addresses of the channels above zero
    view.setActiveOnly(true);
    CHECK(grid->rowCount() == 1 && grid->item(0, 0)->text() == "1\n231" && grid->item(0, 1)->text() == "5\n96" && grid->item(0, 2)->text() == "71\n120");
    view.setActiveOnly(false);
    CHECK(grid->rowCount() == 16 && grid->item(2, 6)->text() == "71\n120");

    // Click: the editor opens beside the cell, never over it, and shows the network
    CHECK(!view.editorOpen());
    std::vector<std::pair<QVector<int>, int>> chosen;
    QVector<QVector<int>> released;
    QObject::connect(&view, &ChannelGrid::outputChosen, [&](const QVector<int> &c, int level) { chosen.push_back({c, level}); });
    QObject::connect(&view, &ChannelGrid::releaseRequested, [&](const QVector<int> &c) { released.push_back(c); });
    clickCell(2, 6);
    CHECK(view.editorOpen() && view.currentChannel() == 71 && channel->value() == 71);
    CHECK(encoder->network() == 120);
    const QRect cellRect = grid->visualRect(grid->model()->index(2, 6));
    const QPoint corner = grid->viewport()->mapTo(&view, cellRect.topLeft());
    CHECK(editor->x() >= corner.x() + cellRect.width() || editor->geometry().right() <= corner.x());

    // The level to send: told to the application, and shown at once
    encoder->setValue(200);
    CHECK(chosen.size() == 1 && chosen[0].first == QVector<int>{71} && chosen[0].second == 200);
    CHECK(grid->item(2, 6)->data(Qt::UserRole + 1).toInt() == 200 && grid->item(2, 6)->data(Qt::UserRole + 2).toBool());
    CHECK(!grid->item(0, 4)->data(Qt::UserRole + 2).toBool());
    // The application's state confirms (and says what the others alone send)
    state.output[70] = char(200);
    state.overridden.setBit(70);
    state.others = QByteArray(512, 0);
    state.others[70] = char(60);
    view.setState(state);
    CHECK(encoder->value() == 200 && encoder->others() == 60);

    // Release is the application's to do
    release->click();
    CHECK(released.size() == 1 && released[0] == QVector<int>{71});
    state.overridden.clearBit(70);
    state.output[70] = 0;
    view.setState(state);
    CHECK(!grid->item(2, 6)->data(Qt::UserRole + 2).toBool() && !release->isEnabled());

    // The same cell again closes it; so does closeEditor()
    clickCell(2, 6);
    CHECK(!view.editorOpen());
    clickCell(1, 1);
    CHECK(view.editorOpen());
    view.closeEditor();
    CHECK(!view.editorOpen());

    // Several channels: a range with Shift, one level for all of them
    chosen.clear();
    clickCell(0, 4);
    clickCell(0, 6, Qt::ShiftModifier);
    CHECK(view.editorOpen() && view.selectedChannels() == QVector<int>({5, 6, 7}));
    CHECK(view.findChild<QLabel *>("section")->text() == "3 channels");
    view.findChild<QSpinBox *>("outputValue")->setValue(100);
    CHECK(chosen.size() == 1 && chosen[0].first == QVector<int>({5, 6, 7}) && chosen[0].second == 100);
    CHECK(grid->item(0, 5)->data(Qt::UserRole + 2).toBool() && !grid->item(0, 7)->data(Qt::UserRole + 2).toBool());
    // A plain click goes back to one channel
    clickCell(0, 4);
    CHECK(view.selectedChannels() == QVector<int>({5}));

    // The editor follows the channel spin, and is closed when its channel leaves the filtered view
    channel->setValue(71);
    CHECK(view.currentChannel() == 71);
    view.setActiveOnly(true);
    channel->setValue(2); // not above zero: not in the view
    view.setState(state);
    CHECK(!view.editorOpen());
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    levelControl();
    channelGrid();
    if (failures) return 1;
    std::printf("dmxview passed\n");
    return 0;
}
