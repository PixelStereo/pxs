#include "pxs/FilterEditor.h"
#include "pxs/SignalMeter.h"
#include "pxs/CurveLane.h"
#include "pxs/AdsrCurve.h"

#include <QApplication>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QListWidget>
#include <QMimeData>
#include <QProgressBar>
#include <QtTest/QTest>
#include <cstdio>

using namespace pxs::filterview;

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "FAILED %s:%d %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

static Signal measure()
{
    Signal s;
    s.title = "/face/jawOpen";
    s.text = [](double v) { return QString::number(v, 'f', 3); };
    return s;
}

static std::vector<QProgressBar *> bars(FilterEditor &editor)
{
    std::vector<QProgressBar *> shown;
    for (auto *bar : editor.findChildren<QProgressBar *>("signalValue"))
        if (bar->isVisibleTo(&editor)) shown.push_back(bar);
    return shown;
}

static void barsAndTrace()
{
    FilterEditor editor;
    editor.resize(320, 480);
    editor.show();
    CHECK(!editor.hasSignal() && bars(editor).empty());
    editor.setSignal(measure(), {});
    CHECK(bars(editor).size() == 1); // the signal alone: no filter yet, no bar after it
    FilterChain chain{InteractionStage::made(InteractionFilter::Average), InteractionStage::made(InteractionFilter::Hold)};
    editor.setSignal(measure(), chain);
    CHECK(bars(editor).size() == 2); // the signal in, the signal out
    editor.setTrace(true, 1.0, {0.25, 0.5});
    const auto b = bars(editor);
    CHECK(b.front()->format() == "1.000" && b.front()->value() == 1000);
    CHECK(b.back()->format() == "0.500" && b.back()->value() == 500); // the last stage is what goes out
    editor.setTrace(false, 0, {});
    CHECK(b.front()->format() == QString::fromUtf8("—"));
}

static void sentValue()
{
    Signal s = measure();
    s.sent = [](double v) { return v >= 0.5 ? 1.0 : 0.0; }; // a switch
    s.text = [](double v) { return v >= 0.5 ? QString("on") : QString("off"); };
    FilterEditor editor;
    editor.show();
    editor.setSignal(s, {InteractionStage::made(InteractionFilter::Hold)});
    editor.setTrace(true, 0.7, {0.2});
    const auto b = bars(editor);
    CHECK(b.front()->format() == "on" && b.back()->format() == "off" && b.back()->value() == 0);
}

static void editsAreTold()
{
    FilterEditor editor;
    editor.resize(320, 480);
    editor.show();
    int told = 0;
    FilterChain last;
    QObject::connect(&editor, &FilterEditor::chainChanged, [&](const FilterChain &c, bool) { ++told; last = c; });
    Signal s = measure();
    s.invert = {InteractionStage::made(InteractionFilter::Math)};
    editor.setSignal(s, {InteractionStage::made(InteractionFilter::Average)});
    auto *list = editor.findChild<QListWidget *>();
    CHECK(list && list->count() == 1);
    auto *card = list->itemWidget(list->item(0));
    CHECK(card);
    auto *remove = card->findChild<QWidget *>(); // the first widget of a card is not the button: look for it by accessible name
    for (auto *w : card->findChildren<QWidget *>())
        if (w->accessibleName() == "Remove filter") remove = w;
    QTest::mouseClick(remove, Qt::LeftButton);
    QTest::qWait(50);
    CHECK(told == 1 && last.empty() && list->count() == 0);
}

static void reorder()
{
    FilterEditor editor;
    editor.resize(320, 600);
    editor.show();
    FilterChain chain{InteractionStage::made(InteractionFilter::Average), InteractionStage::made(InteractionFilter::Hold),
                      InteractionStage::made(InteractionFilter::Delay)};
    FilterChain last;
    QObject::connect(&editor, &FilterEditor::chainChanged, [&](const FilterChain &c, bool) { last = c; });
    editor.setSignal(measure(), chain);
    auto *list = editor.findChild<QListWidget *>();
    // The first card dropped below the last one
    QMimeData mime;
    mime.setData("application/x-pxs-filter", "0");
    const QPoint where(list->viewport()->width() / 2, list->viewport()->height() - 2);
    QDragEnterEvent enter(where, Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(list->viewport(), &enter);
    QDragMoveEvent move(where, Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(list->viewport(), &move);
    QDropEvent drop(where, Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(list->viewport(), &drop);
    QTest::qWait(100);
    CHECK(last.size() == 3 && last[2].kind == InteractionFilter::Average && last[0].kind == InteractionFilter::Hold);
}

static void richFilters()
{
    FilterEditor editor;
    editor.resize(360, 900);
    editor.show();
    QStringList texts, merges;
    QObject::connect(&editor, &FilterEditor::chainEdited, [&](const FilterChain &, const QString &t, const QString &m) { texts << t; merges << m; });
    int decorated = 0, extras = 0;
    Signal s = measure();
    s.decorate = [&](int, const QString &, QWidget *label, QWidget *field) { CHECK(label && field); ++decorated; };
    s.extra = [&](int, const InteractionStage &) -> QWidget * { ++extras; return new QWidget; };
    FilterChain chain{InteractionStage::made(InteractionFilter::Curve), InteractionStage::made(InteractionFilter::Envelope),
                      InteractionStage::made(InteractionFilter::Average)};
    editor.setSignal(s, chain);
    CHECK(editor.findChildren<pxs::curveview::CurveLane *>().size() == 1); // the curve editor of the Curve filter
    CHECK(editor.findChild<QWidget *>("adsrCurve") != nullptr); // the envelope in the hand
    CHECK(decorated > 0 && extras == 3);
    // The signal going into the curve marks it
    editor.setTrace(true, 0.4, {0.4, 0.4, 0.4});
    // A name: shown with the type, and told
    editor.renameFilter(2, "Calm");
    CHECK(editor.chain()[2].name == "Calm" && texts.back() == "Rename Filter");
    // A setting edited as a drag: one merge key, and the last step final
    editor.editStage(2, [](InteractionStage &st) { st.a = 0.5; }, "Change Filter", "filter2/0", false);
    CHECK(texts.back() == "Change Filter" && merges.back() == "filter2/0" && editor.chain()[2].a == 0.5);
    // The ranges follow the chain
    FilterChain remapped{InteractionStage::remap(0, 0.3, 0, 1)};
    CHECK(FilterEditor::spanAfter(remapped, 0, 1, 1) == std::make_pair(0.0, 1.0));
    FilterChain clamped{InteractionStage::made(InteractionFilter::Clamp)};
    clamped[0].a = 0.2;
    clamped[0].b = 0.6;
    CHECK(FilterEditor::spanAfter(clamped, 0, 1, 1) == std::make_pair(0.2, 0.6));
    CHECK(FilterEditor::spanAfter(clamped, 0, 1, 0) == std::make_pair(0.0, 1.0)); // before it: as it comes in
    // The bar after the filters follows: a signal at the top of a clamp's range is full
    editor.setSignal(measure(), clamped);
    editor.setTrace(true, 0.6, {0.6});
    CHECK(bars(editor).back()->value() == 1000);
}

static void infos()
{
    for (int k = 0; k < kInteractionFilterCount; ++k) {
        const auto info = filterInfo(InteractionFilter(k));
        CHECK(filterGroups().contains(info.group));
    }
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    barsAndTrace();
    sentValue();
    editsAreTold();
    reorder();
    richFilters();
    infos();
    if (failures) return 1;
    std::printf("filterview passed\n");
    return 0;
}
