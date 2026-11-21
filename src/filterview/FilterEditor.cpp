#include "pxs/FilterEditor.h"

#include "Fields.h"
#include "pxs/SignalMeter.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace pxs::filterview {

namespace {
// The list of cards. A card dragged by its handle (or its name) is dropped between two others, where a line shows it: the list
// does not move the cards itself, it tells where the dragged one was dropped and the chain is rebuilt in its new order.
constexpr const char *kFilterMime = "application/x-pxs-filter";

class FilterList : public QListWidget {
public:
    std::function<void(int from, int insertBefore)> moved;

    FilterList()
    {
        setAcceptDrops(true);
        viewport()->setAcceptDrops(true);
        setDragEnabled(false);
        setDropIndicatorShown(false); // the line is drawn here
    }

protected:
    void dragEnterEvent(QDragEnterEvent *event) override { accept(event); }
    void dragMoveEvent(QDragMoveEvent *event) override
    {
        if (!event->mimeData()->hasFormat(kFilterMime)) return;
        const int y = int(event->position().y());
        if (y < 24) verticalScrollBar()->setValue(verticalScrollBar()->value() - 12); // the list follows the drag at its edges
        else if (y > viewport()->height() - 24) verticalScrollBar()->setValue(verticalScrollBar()->value() + 12);
        dropRow = insertionRow(y);
        viewport()->update();
        event->acceptProposedAction();
    }
    void dragLeaveEvent(QDragLeaveEvent *) override
    {
        dropRow = -1;
        viewport()->update();
    }
    void dropEvent(QDropEvent *event) override
    {
        if (!event->mimeData()->hasFormat(kFilterMime)) return;
        const int from = event->mimeData()->data(kFilterMime).toInt();
        const int before = insertionRow(int(event->position().y()));
        dropRow = -1;
        viewport()->update();
        event->acceptProposedAction();
        if (moved) moved(from, before);
    }
    void paintEvent(QPaintEvent *event) override
    {
        QListWidget::paintEvent(event);
        if (dropRow < 0) return;
        const int y = dropRow >= count() ? (count() ? visualItemRect(item(count() - 1)).bottom() + 2 : 0) : visualItemRect(item(dropRow)).top() - 2;
        QPainter painter(viewport());
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0xe0, 0xb2, 0x5c));
        painter.drawRoundedRect(QRect(2, y - 1, viewport()->width() - 4, 3), 1.5, 1.5);
    }

private:
    void accept(QDragEnterEvent *event)
    {
        if (event->mimeData()->hasFormat(kFilterMime)) event->acceptProposedAction();
    }
    // Between which cards the pointer is: the number of the card it would go before (the count: at the end)
    int insertionRow(int y) const
    {
        for (int row = 0; row < count(); ++row)
            if (y < visualItemRect(item(row)).center().y()) return row;
        return count();
    }
    int dropRow = -1;
};

// What a card is made of: a fold button, its on / off box, its name, a remove button, then its settings
class FilterCard : public QWidget {
public:
    QToolButton *fold, *remove;
    QCheckBox *enabled;
    QLabel *name;
    QWidget *body;
    bool unfolded;

    // The height it takes: added up by hand, the layouts of a card that is not shown yet answer too small
    int height()
    {
        auto *outer = static_cast<QVBoxLayout *>(layout());
        auto *inner = static_cast<QVBoxLayout *>(body->layout());
        int total = outer->contentsMargins().top() + outer->contentsMargins().bottom() + outer->itemAt(0)->sizeHint().height();
        if (unfolded && inner->count() > 0) total += outer->spacing() + int(inner->count()) * 26 + (int(inner->count()) - 1) * inner->spacing();
        return total;
    }

    int index; // its place in the chain: what a drag carries

    FilterCard(const InteractionStage &stage, bool open, int place) : unfolded(open), index(place)
    {
        auto *column = new QVBoxLayout(this);
        column->setContentsMargins(6, 4, 6, 4);
        column->setSpacing(4);
        auto *header = new QHBoxLayout;
        auto *grip = new QLabel(QStringLiteral("⋮⋮"));
        grip->setCursor(Qt::OpenHandCursor);
        grip->setToolTip("Drag to put this filter in another place");
        grip->setAttribute(Qt::WA_TransparentForMouseEvents); // the card takes the drag
        setCursor(Qt::ArrowCursor);
        fold = new QToolButton;
        fold->setAutoRaise(true);
        fold->setArrowType(unfolded ? Qt::DownArrow : Qt::RightArrow);
        fold->setToolTip("Show or hide the settings");
        enabled = new QCheckBox;
        enabled->setChecked(stage.enabled);
        enabled->setToolTip("This filter on or off");
        enabled->setAccessibleName("Filter on");
        name = new QLabel(stage.displayName());
        name->setAttribute(Qt::WA_TransparentForMouseEvents); // a drag from the name moves the card
        remove = new QToolButton;
        remove->setText(QStringLiteral("×"));
        remove->setAutoRaise(true);
        remove->setToolTip("Remove this filter");
        remove->setAccessibleName("Remove filter");
        header->addWidget(grip);
        header->addWidget(fold);
        header->addWidget(enabled);
        header->addWidget(name, 1);
        header->addWidget(remove);
        column->addLayout(header);
        body = new QWidget;
        new QVBoxLayout(body);
        body->layout()->setContentsMargins(24, 0, 0, 0);
        body->layout()->setSpacing(4);
        column->addWidget(body);
        body->setVisible(unfolded);
        // What the filter does is said in the status bar, as the pointer goes over its card
        const auto info = filterInfo(stage.kind);
        setToolTip(QStringLiteral("%1. %2").arg(info.summary, info.detail));
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) pressed = event->position().toPoint();
        else pressed = QPoint(-1, -1);
        QWidget::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!(event->buttons() & Qt::LeftButton) || pressed.x() < 0 ||
            (event->position().toPoint() - pressed).manhattanLength() < QApplication::startDragDistance()) {
            QWidget::mouseMoveEvent(event);
            return;
        }
        // The card follows the pointer as a picture; where it would be dropped is shown by a line in the list
        auto *mime = new QMimeData;
        mime->setData(kFilterMime, QByteArray::number(index));
        auto *drag = new QDrag(this);
        drag->setMimeData(mime);
        QPixmap picture = grab();
        QPixmap faded(picture.size());
        faded.setDevicePixelRatio(picture.devicePixelRatio());
        faded.fill(Qt::transparent);
        QPainter painter(&faded);
        painter.setOpacity(0.7);
        painter.drawPixmap(0, 0, picture);
        painter.end();
        drag->setPixmap(faded);
        drag->setHotSpot(pressed);
        pressed = QPoint(-1, -1);
        drag->exec(Qt::MoveAction);
    }

private:
    QPoint pressed{-1, -1};
};
}


QStringList filterGroups() { return {QStringLiteral("Smooth"), QStringLiteral("Time"), QStringLiteral("Trigger"), QStringLiteral("Shape")}; }

FilterInfo filterInfo(InteractionFilter kind)
{
    using F = InteractionFilter;
    const auto make = [](const char *group, const char *summary, const char *detail) {
        return FilterInfo{QString::fromUtf8(group), QString::fromUtf8(summary), QString::fromUtf8(detail)};
    };
    switch (kind) {
    case F::Average: return make("Smooth", "mean of the last moments", "Calms a jittery signal: the longer the window, the smoother and the later. On a switch it is a vote: on when it was on most of the window.");
    case F::WeightedAverage: return make("Smooth", "smooths, reacts at once", "A gentle smoothing that starts moving immediately. The setting is about the time it takes to arrive. The first one to try on an expression.");
    case F::AttackRelease: return make("Smooth", "its own speed up and down", "Rises and falls at different speeds: quick to rise and slow to fall makes a blink or a smile linger. Good for expressions and gestures.");
    case F::Damper: return make("Smooth", "a spring, may overshoot", "Follows like a spring: lively and natural. Damping 1 never overshoots, lower swings past the value.");
    case F::Delay: return make("Time", "the value of N seconds ago", "Waits before passing the value on. Works on switches and counts: a gesture that starts a second later.");
    case F::Hold: return make("Time", "keeps a fall for N seconds", "A rise goes through at once, a fall waits: a switch stays on a moment after it went off, a measure holds its peak. Keeps a lost hand from flickering.");
    case F::Debounce: return make("Time", "ignores what does not last", "A change is followed only once it has stayed for N seconds. Removes the flicker of a switch or a count that the tracking hesitates on.");
    case F::AutoReturn: return make("Time", "back to rest after N seconds", "Goes back to off (or 0, or the value you set) after a while, even if the signal stays up. It is armed again when the signal is at rest.");
    case F::Envelope: return make("Trigger", "a hit becomes a fading pulse", "Each rise starts an attack / decay / sustain / release shape. Turns a gesture or a blink into a flash that fades by itself.");
    case F::PulseDetection: return make("Trigger", "a pulse at each sudden rise", "Fires 1 when the value jumps up quickly, then decays. Reacts to the movement, not to the level.");
    case F::Toggle: return make("Trigger", "flips on / off at each rise", "Each time the value goes up past the threshold the output changes: on, then off at the next one. A gesture as a push button.");
    case F::Threshold: return make("Trigger", "on or off around a level", "Gives 0 or 1 depending on a level, with a margin so it does not flicker around it.");
    case F::Math: return make("Shape", "add, multiply, round, power…", "An operation on the value: x + a, x × a, absolute value, 1 − x, rounding to a step…");
    case F::Remap: return make("Shape", "one range onto another", "Maps In Min..In Max to Out Min..Out Max. Use it to widen a small range (a mouth that barely opens) to 0..1.");
    case F::Clamp: return make("Shape", "keeps between two values", "Cuts what is below Min or above Max.");
    case F::Steps: return make("Shape", "only a few levels", "Rounds the value to N levels between 0 and 1: a staircase instead of a slope.");
    case F::Speed: return make("Shape", "how fast it moves", "Gives 0..1 from the speed of the signal, whatever its level: still is 0, a quick move is 1.");
    case F::Curve: return make("Shape", "a drawn curve", "A curve from 0..1 to 0..1.");
    default: break;
    }
    return make("Shape", "", "");
}

namespace {
std::function<void(QMenu *)> menuWatcher;
}
void setMenuWatcher(std::function<void(QMenu *)> watcher) { menuWatcher = std::move(watcher); }

void addFilterActions(QMenu *menu, const std::vector<InteractionFilter> &kinds, const std::function<void(InteractionFilter)> &chosen)
{
    if (menuWatcher) menuWatcher(menu);
    for (const auto &group : filterGroups()) {
        QMenu *sub = nullptr;
        for (auto kind : kinds) {
            const auto info = filterInfo(kind);
            if (info.group != group) continue;
            if (!sub) {
                sub = menu->addMenu(group);
                if (menuWatcher) menuWatcher(sub);
            }
            auto *action = sub->addAction(interactionFilterName(kind));
            action->setToolTip(QStringLiteral("%1. %2").arg(info.summary, info.detail));
            QObject::connect(action, &QAction::triggered, menu, [chosen, kind] { chosen(kind); });
        }
    }
}

FilterEditor::FilterEditor(QWidget *parent) : QWidget(parent)
{
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(6, 0, 0, 0);
    title = new QLabel;
    QFont bold = title->font();
    bold.setBold(true);
    title->setFont(bold);
    title->setTextInteractionFlags(Qt::TextSelectableByMouse);
    column->addWidget(title);
    auto *tools = new QHBoxLayout;
    add = new QPushButton(QStringLiteral("Add filter"));
    add->setToolTip("Add a filter at the end of the chain");
    clear = new QToolButton;
    clear->setText(QStringLiteral("Clear"));
    clear->setToolTip("Remove every filter");
    tools->addWidget(add);
    tools->addWidget(clear);
    tools->addStretch();
    column->addLayout(tools);
    input = new QProgressBar;
    input->setObjectName("signalValue");
    input->setFixedHeight(16);
    input->setRange(0, 1000);
    input->setToolTip("The signal as it comes in, before the filters");
    column->addWidget(input);
    auto *cards = new FilterList;
    list = cards;
    list->setSelectionMode(QAbstractItemView::NoSelection);
    list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setSpacing(3);
    column->addWidget(list, 1);
    output = new SignalMeter;
    output->setObjectName("signalValue");
    output->setFixedHeight(16);
    output->setRange(0, 1000);
    output->setToolTip("The signal after the filters: what is sent");
    column->addWidget(output);
    connect(clear, &QToolButton::clicked, this, [this] {
        filters.clear();
        open.clear();
        rebuild();
        edited(true);
    });
    // A card dropped between two others: the chain follows (rebuilt once the drag is over: the card dragged is one of the list's)
    cards->moved = [this](int from, int before) {
        QTimer::singleShot(0, this, [this, from, before] {
            const int to = before > from ? before - 1 : before;
            if (from < 0 || from >= int(filters.size()) || to < 0 || to >= int(filters.size()) || to == from) return;
            const auto stage = filters[size_t(from)];
            const bool unfolded = open[size_t(from)];
            filters.erase(filters.begin() + from);
            open.erase(open.begin() + from);
            filters.insert(filters.begin() + to, stage);
            open.insert(open.begin() + to, unfolded);
            rebuild();
            edited(true);
        });
    };
    clearSignal();
}

void FilterEditor::clearSignal()
{
    active = false;
    spec = {};
    filters.clear();
    open.clear();
    title->clear();
    title->setToolTip({});
    add->setEnabled(false);
    clear->setEnabled(false);
    list->setVisible(false);
    input->setVisible(false);
    rebuild();
}

void FilterEditor::setSignal(const Signal &signal, const FilterChain &chain)
{
    active = true;
    spec = signal;
    filters = chain;
    open.assign(filters.size(), false);
    title->setText(spec.title);
    title->setToolTip(spec.tooltip);
    add->setEnabled(true);
    clear->setEnabled(true);
    list->setVisible(true);
    input->setVisible(true);
    showValue(input, false, 0);
    showValue(output, false, 0);
    auto *menu = new QMenu(add);
    if (!spec.invert.empty()) {
        auto *invert = menu->addAction("Invert", this, [this] {
            for (const auto &stage : spec.invert) {
                filters.push_back(stage);
                open.push_back(false);
            }
            rebuild();
            edited(true);
        });
        invert->setToolTip("Flips the signal");
        if (menuWatcher) menuWatcher(menu);
        menu->addSeparator();
    }
    std::vector<InteractionFilter> kinds = spec.offered;
    if (kinds.empty())
        for (int k = 0; k < kInteractionFilterCount; ++k) kinds.push_back(InteractionFilter(k));
    addFilterActions(menu, kinds, [this](InteractionFilter filter) { addFilter(filter); });
    delete add->menu();
    add->setMenu(menu);
    rebuild();
}

int FilterEditor::levelOf(double value) const
{
    const double sent = spec.sent ? spec.sent(value) : value;
    const double at = spec.hi > spec.lo ? (sent - spec.lo) / (spec.hi - spec.lo) : 0.0;
    return int(std::lround(1000 * std::clamp(at, 0.0, 1.0)));
}

void FilterEditor::showValue(QProgressBar *bar, bool available, double value)
{
    if (!available) {
        bar->setFormat(QStringLiteral("—"));
        bar->setValue(bar->minimum());
        return;
    }
    const double sent = spec.sent ? spec.sent(value) : value;
    bar->setFormat(spec.text ? spec.text(sent) : QString::number(sent, 'f', 3));
    bar->setValue(levelOf(value));
}

void FilterEditor::setTrace(bool available, double value, const std::vector<double> &afterEach)
{
    showValue(input, available, value);
    showValue(output, available, afterEach.empty() ? value : afterEach.back());
    // The signal as it came in, marked on the bar after the filters: how far they have taken it
    output->setReference(available, levelOf(value));
}

void FilterEditor::addFilter(InteractionFilter kind)
{
    filters.push_back(InteractionStage::made(kind));
    open.push_back(true);
    rebuild();
    edited(true);
}

void FilterEditor::edited(bool final)
{
    emit chainChanged(filters, final);
}

void FilterEditor::fitItems()
{
    for (int row = 0; row < list->count(); ++row) {
        auto *item = list->item(row);
        auto *card = dynamic_cast<FilterCard *>(list->itemWidget(item));
        if (!card) continue;
        const int width = std::max(120, list->viewport()->width() - 2 * list->spacing());
        item->setSizeHint(QSize(width, card->height()));
    }
}

void FilterEditor::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    fitItems();
}

void FilterEditor::rebuild()
{
    rebuilding = true;
    list->clear();
    for (size_t i = 0; i < filters.size(); ++i) addCard(int(i));
    fitItems();
    output->setVisible(active && !filters.empty());
    rebuilding = false;
}

void FilterEditor::addCard(int index)
{
    auto *item = new QListWidgetItem(list);
    item->setData(Qt::UserRole, index);
    auto *card = new FilterCard(filters[size_t(index)], open[size_t(index)], index);
    card->setObjectName("filterCard");
    card->setAttribute(Qt::WA_StyledBackground);
    card->setStyleSheet("QWidget#filterCard { border:1px solid palette(mid); border-radius:5px; }");
    list->setItemWidget(item, card);
    connect(card->fold, &QToolButton::clicked, this, [this, card, index] {
        const bool unfolded = !card->unfolded;
        card->unfolded = unfolded;
        open[size_t(index)] = unfolded;
        card->body->setVisible(unfolded);
        card->fold->setArrowType(unfolded ? Qt::DownArrow : Qt::RightArrow);
        card->layout()->activate();
        fitItems();
    });
    connect(card->enabled, &QCheckBox::toggled, this, [this, index](bool on) {
        filters[size_t(index)].enabled = on;
        edited(true);
    });
    connect(card->remove, &QToolButton::clicked, this, [this, index] {
        QTimer::singleShot(0, this, [this, index] {
            filters.erase(filters.begin() + index);
            open.erase(open.begin() + index);
            rebuild();
            edited(true);
        });
    });
    auto &stage = filters[size_t(index)];
    auto *body = static_cast<QVBoxLayout *>(card->body->layout());
    const auto addRow = [&](const QString &label, QWidget *field) {
        auto *row = new QHBoxLayout;
        auto *text = new QLabel(label);
        text->setMinimumWidth(64);
        row->addWidget(text);
        row->addWidget(field, 1);
        body->addLayout(row);
    };
    if (stage.kind == InteractionFilter::Math) {
        auto *operation = new QComboBox;
        for (int op = 0; op < kMathOpCount; ++op) operation->addItem(mathOpName(MathOp(op)), op);
        operation->setCurrentIndex(int(stage.op));
        addRow("Operation", operation);
        connect(operation, &QComboBox::activated, this, [this, index, operation](int) {
            filters[size_t(index)].op = MathOp(operation->currentData().toInt());
            edited(true);
            QTimer::singleShot(0, this, [this] { rebuild(); }); // its value is there only for the operations that use one
        });
    }
    for (int k = 0; k < kFilterSettingCount; ++k) {
        const auto setting = interactionFilterSetting(stage.kind, k);
        if (setting.name.isEmpty()) continue;
        if (stage.kind == InteractionFilter::Math && !mathOpUsesValue(stage.op)) continue;
        auto *field = new SliderField;
        field->setDecimals(3);
        field->setRange(setting.min, setting.max);
        if (setting.suffix.trimmed() == "s") field->setTypedRange(setting.min, std::max(setting.max, 600.0));
        field->setSingleStep((setting.max - setting.min) / 100);
        field->setSuffix(setting.suffix);
        field->setValue(stage.setting(k));
        addRow(setting.name, field);
        connect(field, &SliderField::valueEdited, this, [this, index, k](double v) {
            filters[size_t(index)].setting(k) = v;
            edited(false);
        });
        connect(field, &SliderField::editingFinished, this, [this, index, k](double v) {
            filters[size_t(index)].setting(k) = v;
            edited(true);
        });
    }
    card->layout()->activate();
}

} // namespace pxs::filterview
