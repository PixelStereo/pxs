#include "pxs/ChannelGrid.h"
#include "pxs/LevelControl.h"

#include <QtWidgets>
#include <algorithm>

namespace pxs::dmx {

namespace {

constexpr int channelRole = Qt::UserRole;      // the DMX number, 1..512 (invalid: no channel in this cell)
constexpr int outputRole = Qt::UserRole + 1;   // the output level
constexpr int overriddenRole = Qt::UserRole + 2; // set here

class ChannelDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        if (!index.data(channelRole).isValid()) return;
        p->save();
        p->fillRect(option.rect, index.data(Qt::BackgroundRole).value<QBrush>());
        const auto textColor = index.data(Qt::ForegroundRole).value<QBrush>().color();
        QColor channelColor = textColor;
        channelColor.setAlpha(190);
        p->setPen(channelColor);
        const auto lines = index.data().toString().split('\n');
        QRect top = option.rect.adjusted(1, 1, -1, -1);
        top.setHeight(top.height() / 2);
        QRect bottom = top.translated(0, top.height());
        QFont font = option.font;
        font.setPixelSize(qBound(10, top.height() - 2, 14));
        p->setFont(font);
        p->drawText(top, Qt::AlignCenter, lines.value(0));
        font.setBold(true);
        p->setPen(textColor);
        p->setFont(font);
        p->drawText(bottom, Qt::AlignCenter, lines.value(1));
        if (index.data(overriddenRole).toBool()) {
            // This channel's output is set here: an amber bar along the bottom, as long as its level
            const int out = index.data(outputRole).toInt();
            const QColor amber("#d9b36c");
            QRect bar = option.rect.adjusted(1, 0, -1, -1);
            bar.setTop(bar.bottom() - 3);
            bar.setWidth(qMax(2, bar.width() * out / 255));
            p->fillRect(bar, amber);
            p->setPen(QPen(amber, 1));
            p->drawRect(option.rect.adjusted(0, 0, -1, -1));
        }
        if (option.state & QStyle::State_Selected) {
            p->setPen(QPen(QColor("#a8c4b8"), 2));
            p->drawRect(option.rect.adjusted(1, 1, -2, -2));
        }
        p->restore();
    }
};

} // namespace

ChannelGrid::ChannelGrid(QWidget *parent) : QWidget(parent)
{
    m_paintedInput.fill(-2);
    m_paintedOutput.fill(-2);
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_grid = new QTableWidget(512 / m_columns, m_columns);
    m_grid->setObjectName("channelGrid");
    m_grid->horizontalHeader()->hide();
    m_grid->verticalHeader()->hide();
    m_grid->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_grid->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_grid->setItemDelegate(new ChannelDelegate(m_grid));
    m_grid->horizontalHeader()->setMinimumSectionSize(20);
    m_grid->verticalHeader()->setMinimumSectionSize(22);
    m_grid->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_grid->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_grid->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_grid->setStyleSheet("QTableWidget { gridline-color:palette(mid); background:#000; }");
    for (int i = 0; i < 512; ++i) {
        m_visible.append(i);
        auto item = new QTableWidgetItem;
        item->setData(channelRole, i + 1);
        item->setTextAlignment(Qt::AlignCenter);
        m_grid->setItem(i / m_columns, i % m_columns, item);
    }
    m_grid->setCurrentCell(0, 0);
    m_grid->viewport()->installEventFilter(this);

    m_stack = new QStackedWidget;
    m_stack->addWidget(m_grid);
    m_empty = new QLabel;
    m_empty->setObjectName("emptyChannelGrid");
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setWordWrap(true);
    m_stack->addWidget(m_empty);
    layout->addWidget(m_stack, 1);

    // The editor of the clicked channel: a small card beside its cell, with the network's level above the level to send
    m_editor = new QFrame(this);
    m_editor->setObjectName("channelEditor");
    m_editor->setFixedWidth(330);
    m_editor->setStyleSheet("QFrame#channelEditor { background:palette(window); border:1px solid palette(highlight); border-radius:8px; }"
                            "QFrame#channelEditor QLabel { background:transparent; border:0; }"
                            "QFrame#channelEditor QSpinBox { padding:4px 6px; min-height:15px; }");
    m_editor->hide();
    auto editorLayout = new QVBoxLayout(m_editor);
    editorLayout->setContentsMargins(12, 10, 12, 12);
    editorLayout->setSpacing(8);
    auto header = new QHBoxLayout;
    m_editorTitle = new QLabel("Channel");
    m_editorTitle->setObjectName("section");
    QFont bold = m_editorTitle->font();
    bold.setBold(true);
    m_editorTitle->setFont(bold);
    header->addWidget(m_editorTitle);
    m_channel = new QSpinBox;
    m_channel->setObjectName("channel");
    m_channel->setRange(1, 512);
    m_channel->setToolTip("Channel (DMX address)");
    header->addWidget(m_channel);
    header->addStretch();
    m_editorState = new QLabel;
    m_editorState->setObjectName("muted");
    header->addWidget(m_editorState);
    auto closeButton = new QToolButton;
    closeButton->setText("✕");
    closeButton->setToolTip("Close (Esc)");
    closeButton->setCursor(Qt::PointingHandCursor);
    connect(closeButton, &QToolButton::clicked, this, [this] { closeEditor(); });
    header->addWidget(closeButton);
    editorLayout->addLayout(header);
    m_encoder = new LevelControl;
    editorLayout->addWidget(m_encoder);
    auto tools = new QHBoxLayout;
    tools->setSpacing(6);
    m_value = new QSpinBox;
    m_value->setObjectName("outputValue");
    m_value->setRange(0, 255);
    m_value->setToolTip("Output level, 0–255");
    tools->addWidget(m_value);
    for (const auto &preset : QList<QPair<QString, int>>{{"0", 0}, {"50%", 128}, {"Full", 255}}) {
        auto button = new QPushButton(preset.first);
        button->setObjectName(QString("levelPreset%1").arg(preset.second));
        button->setCursor(Qt::PointingHandCursor);
        button->setToolTip(QString("Set output to %1").arg(preset.second));
        button->setStyleSheet("padding:6px 0;min-width:0");
        connect(button, &QPushButton::clicked, this, [this, level = preset.second] { m_value->setValue(level); });
        tools->addWidget(button);
    }
    m_release = new QPushButton("Release");
    m_release->setObjectName("releaseChannel");
    m_release->setToolTip("Let this channel follow the received signal again");
    m_release->setStyleSheet("padding:6px 10px;min-width:0");
    tools->addWidget(m_release);
    editorLayout->addLayout(tools);
    auto closeShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), m_editor);
    closeShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(closeShortcut, &QShortcut::activated, this, [this] { closeEditor(); });

    connect(m_channel, &QSpinBox::valueChanged, this, [this](int number) {
        syncSelection();
        refreshEditor();
        emit currentChannelChanged(number);
    });
    connect(m_grid->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex &current) {
        if (current.isValid() && current.data(channelRole).isValid()) m_channel->setValue(current.data(channelRole).toInt());
    });
    connect(m_grid->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] {
        if (m_editor->isVisible()) refreshEditor();
    });
    connect(m_value, &QSpinBox::valueChanged, this, [this](int level) {
        if (m_updating) return;
        const QVector<int> channels = selectedChannels();
        // Shown at once; the application's state confirms it
        if (m_state.output.size() != 512) m_state.output = QByteArray(512, 0);
        if (m_state.overridden.size() != 512) m_state.overridden = QBitArray(512, false);
        for (int channel : channels) {
            m_state.output[channel - 1] = char(level);
            m_state.overridden.setBit(channel - 1);
        }
        m_encoder->setValue(level);
        emit outputChosen(channels, level);
        apply();
    });
    connect(m_encoder, &LevelControl::valueChanged, m_value, &QSpinBox::setValue);
    connect(m_encoder, &LevelControl::editRequested, this, [this] {
        m_value->setFocus();
        m_value->selectAll();
    });
    connect(m_release, &QPushButton::clicked, this, [this] { emit releaseRequested(selectedChannels()); });
}

void ChannelGrid::setState(const State &state)
{
    m_state = state;
    apply();
}

void ChannelGrid::setActiveOnly(bool on)
{
    if (on == m_activeOnly) return;
    m_activeOnly = on;
    m_grid->verticalHeader()->setSectionResizeMode(on ? QHeaderView::Fixed : QHeaderView::Stretch);
    if (on) m_grid->verticalHeader()->setDefaultSectionSize(48);
    apply();
}

int ChannelGrid::currentChannel() const { return m_channel->value(); }

void ChannelGrid::setCurrentChannel(int channel) { m_channel->setValue(std::clamp(channel, 1, 512)); }

QVector<int> ChannelGrid::selectedChannels() const
{
    QVector<int> numbers;
    for (const auto &index : m_grid->selectionModel()->selectedIndexes())
        if (index.data(channelRole).isValid()) numbers << index.data(channelRole).toInt();
    std::sort(numbers.begin(), numbers.end());
    if (numbers.isEmpty()) numbers << m_channel->value();
    return numbers;
}

bool ChannelGrid::editorOpen() const { return m_editor->isVisible(); }

void ChannelGrid::closeEditor()
{
    m_editor->hide();
    m_grid->setFocus();
}

void ChannelGrid::openEditor()
{
    m_editor->show();
    m_editor->raise();
    placeEditor();
    m_encoder->setFocus();
    refreshEditor();
}

void ChannelGrid::syncSelection()
{
    const QSignalBlocker blocker(m_grid->selectionModel());
    const int position = m_visible.indexOf(m_channel->value() - 1);
    if (position < 0) {
        m_grid->clearSelection();
        m_grid->setCurrentIndex(QModelIndex());
        return;
    }
    const QModelIndex index = m_grid->model()->index(position / m_columns, position % m_columns);
    if (m_grid->selectionModel()->isSelected(index)) // inside a selection: keep it
        m_grid->selectionModel()->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
    else
        m_grid->setCurrentIndex(index);
}

bool ChannelGrid::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_grid->viewport()) {
        auto *mouse = dynamic_cast<QMouseEvent *>(event);
        if (mouse && mouse->button() == Qt::LeftButton) {
            if (event->type() == QEvent::MouseButtonPress) {
                // Clicking the only channel already being edited will close its editor
                const QModelIndex under = m_grid->indexAt(mouse->position().toPoint());
                const QVector<int> numbers = selectedChannels();
                m_pressedOnlyOpen = m_editor->isVisible() && under.isValid() && mouse->modifiers() == Qt::NoModifier &&
                                    numbers.size() == 1 && numbers.first() == under.data(channelRole).toInt();
            } else if (event->type() == QEvent::MouseButtonRelease) {
                QTimer::singleShot(0, this, [this] {
                    if (m_pressedOnlyOpen && selectedChannels().size() == 1) closeEditor();
                    else if (m_grid->selectionModel()->hasSelection()) openEditor();
                    m_pressedOnlyOpen = false;
                });
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ChannelGrid::placeEditor()
{
    if (!m_editor->isVisible()) return;
    const int position = m_visible.indexOf(m_channel->value() - 1);
    if (position < 0) { // filtered out of the view: nothing to stand beside
        closeEditor();
        return;
    }
    const QRect cell = m_grid->visualRect(m_grid->model()->index(position / m_columns, position % m_columns));
    const QPoint corner = m_grid->viewport()->mapTo(this, cell.topLeft());
    m_editor->adjustSize();
    const int gap = 8;
    int x = corner.x() + cell.width() + gap;
    if (x + m_editor->width() > width()) x = corner.x() - m_editor->width() - gap;
    int y = corner.y() + cell.height() / 2 - m_editor->height() / 2;
    x = qBound(0, x, qMax(0, width() - m_editor->width()));
    y = qBound(0, y, qMax(0, height() - m_editor->height()));
    m_editor->move(x, y);
}

void ChannelGrid::refreshEditor()
{
    // The output of the current channel is always kept in the editor's fields (a closed editor opens on it), without telling
    // the application it changed
    const int current = m_channel->value();
    const int out = m_state.output.size() == 512 ? quint8(m_state.output[current - 1]) : 0;
    m_updating = true;
    m_value->setValue(out);
    m_encoder->setValue(out);
    m_updating = false;
    if (!m_editor->isVisible()) return;
    const QVector<int> numbers = selectedChannels();
    const bool several = numbers.size() > 1;
    const bool hasInput = m_state.received.size() == 512;
    const bool hasOthers = m_state.others.size() == 512;
    const auto overridden = [this](int channel) { return m_state.overridden.size() == 512 && m_state.overridden.testBit(channel - 1); };
    m_channel->setVisible(!several);
    m_editorTitle->setText(several ? QString("%1 channels").arg(numbers.size()) : QString("Channel"));
    // The network's level: the same on every selected channel, or none to show
    int net = hasInput ? quint8(m_state.received[numbers.first() - 1]) : -1;
    int others = hasOthers ? quint8(m_state.others[numbers.first() - 1]) : -1;
    bool any = false, all = true;
    for (int number : numbers) {
        if (hasInput && quint8(m_state.received[number - 1]) != net) net = -1;
        if (hasOthers && quint8(m_state.others[number - 1]) != others) others = -1;
        const bool set = overridden(number);
        any |= set;
        all &= set;
    }
    m_encoder->setNetwork(net, others);
    m_editorState->setText(several && net < 0 && hasInput ? "Levels differ"
                           : all                          ? "● Set here"
                           : any                          ? "● Partly set"
                           : m_state.following            ? "Follows network"
                                                          : "Not following");
    m_release->setEnabled(any);
    placeEditor();
}

void ChannelGrid::apply()
{
    const bool hasInput = m_state.received.size() == 512;
    const bool hasOutput = m_state.output.size() == 512;
    const bool hasMask = m_state.overridden.size() == 512;
    const int displayColumns = m_activeOnly ? 16 : 32;
    int active = 0;
    QVector<int> shown;
    shown.reserve(512);
    for (int i = 0; i < 512; ++i) {
        const bool aboveZero = hasInput && quint8(m_state.received[i]) > 0;
        if (aboveZero) ++active;
        if (!m_activeOnly || aboveZero) shown.append(i);
    }
    m_active = active;
    if (shown != m_visible || m_columns != displayColumns) {
        // Repack only when channel membership changes, keeping the inspector's real DMX address even when that channel
        // disappears from the filtered view
        const QSignalBlocker blocker(m_grid->selectionModel());
        m_grid->setUpdatesEnabled(false);
        m_visible = shown;
        m_columns = displayColumns;
        m_grid->setColumnCount(m_columns);
        m_grid->setRowCount((shown.size() + m_columns - 1) / m_columns);
        for (int position = 0; position < m_grid->rowCount() * m_columns; ++position) {
            auto item = m_grid->item(position / m_columns, position % m_columns);
            if (!item) {
                item = new QTableWidgetItem;
                item->setTextAlignment(Qt::AlignCenter);
                m_grid->setItem(position / m_columns, position % m_columns, item);
            }
            if (position < shown.size()) {
                item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                item->setData(channelRole, shown[position] + 1);
            } else {
                item->setFlags(Qt::NoItemFlags);
                item->setData(channelRole, QVariant());
                item->setText({});
                item->setToolTip({});
                item->setBackground(Qt::black);
            }
        }
        m_paintedInput.fill(-2);
        m_paintedOutput.fill(-2);
        syncSelection();
        m_grid->setUpdatesEnabled(true);
    }
    m_empty->setText(hasInput ? "No channels above zero in this universe." : "Waiting for received DMX data.");
    m_stack->setCurrentWidget(shown.isEmpty() ? static_cast<QWidget *>(m_empty) : static_cast<QWidget *>(m_grid));
    for (int position = 0; position < shown.size(); ++position) {
        const int i = shown[position];
        const int receivedValue = hasInput ? quint8(m_state.received[i]) : -1;
        const int out = hasOutput ? quint8(m_state.output[i]) : 0;
        const bool overridden = hasMask && m_state.overridden.testBit(i);
        const int outState = out | (overridden ? 256 : 0);
        auto item = m_grid->item(position / m_columns, position % m_columns);
        if (m_paintedOutput[size_t(i)] != outState) {
            item->setData(outputRole, out);
            item->setData(overriddenRole, overridden);
        }
        if (m_paintedInput[size_t(i)] != receivedValue) {
            item->setText(QString("%1\n%2").arg(i + 1).arg(receivedValue < 0 ? QString("—") : QString::number(receivedValue)));
            const int gray = qMax(0, receivedValue);
            item->setBackground(QColor(gray, gray, gray));
            item->setForeground(gray < 128 ? QColor(Qt::white) : QColor(Qt::black));
        }
        if (m_paintedInput[size_t(i)] != receivedValue || m_paintedOutput[size_t(i)] != outState)
            item->setToolTip(QString("Channel %1\nReceived: %2\nOutput: %3")
                                 .arg(i + 1)
                                 .arg(receivedValue < 0 ? QString("No data") : QString::number(receivedValue))
                                 .arg(out));
        m_paintedInput[size_t(i)] = receivedValue;
        m_paintedOutput[size_t(i)] = outState;
    }
    refreshEditor();
}

} // namespace pxs::dmx
