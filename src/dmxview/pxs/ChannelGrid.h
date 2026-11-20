#pragma once
// The 512 channels of a DMX universe as a grid of cells, and the editor that opens beside the clicked ones. Each cell shows its
// number and the level the network carries (a grey, from black to white); a channel whose output is set here has an amber
// border and an amber bar along its cell. Click a channel (Shift-click, Cmd/Ctrl-click or drag for several) and the editor shows
// two bars: the network's level (with a tick for the other devices alone, when they differ) and the level to send, to click,
// drag, scroll, type or take from 0 / 50% / Full. From DMXPulse. The widget holds no output of its own: the application tells
// it what to show (setState) and is told what the user chose (outputChosen, releaseRequested).
#include <QBitArray>
#include <QByteArray>
#include <QPointer>
#include <QVector>
#include <QWidget>
#include <array>

class QTableWidget;
class QStackedWidget;
class QLabel;
class QFrame;
class QSpinBox;
class QPushButton;
class QEvent;

namespace pxs::dmx {

class LevelControl;

class ChannelGrid : public QWidget
{
    Q_OBJECT
public:
    explicit ChannelGrid(QWidget *parent = nullptr);

    // What to show, as often as it changes (each frame is fine: only the cells that changed are repainted)
    struct State {
        QByteArray received;   // 512 levels the network carries; empty: no data yet
        QByteArray others;     // 512 levels of the other devices alone (without this application's output); empty: not known
        QByteArray output;     // 512 levels this application sends
        QBitArray overridden;  // 512: the channels whose output is set here (the others follow the network)
        bool following = true; // the channels not set here follow the network
    };
    void setState(const State &state);

    // Only the channels the network carries above zero, keeping their DMX addresses
    void setActiveOnly(bool on);
    bool activeOnly() const { return m_activeOnly; }
    int activeCount() const { return m_active; } // channels above zero in the last state

    // A monitor: the editor only shows the network's level (no output to set, release or send); on by default
    void setEditable(bool on);
    bool editable() const { return m_editable; }

    int currentChannel() const;                // 1..512
    void setCurrentChannel(int channel);
    QVector<int> selectedChannels() const;     // 1..512, ascending; the current one when nothing is selected
    bool editorOpen() const;
    void closeEditor();

signals:
    void currentChannelChanged(int channel);
    void outputChosen(const QVector<int> &channels, int level); // the user sets these channels' output
    void releaseRequested(const QVector<int> &channels);        // the user lets these follow the network again

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void openEditor();
    void placeEditor();
    void refreshEditor();
    void syncSelection();
    void apply();
    QTableWidget *m_grid;
    QStackedWidget *m_stack;
    QLabel *m_empty;
    QFrame *m_editor;
    QLabel *m_editorTitle, *m_editorState;
    QSpinBox *m_channel, *m_value;
    QPushButton *m_release;
    LevelControl *m_encoder;
    State m_state;
    QVector<int> m_visible; // the channels in the cells, 0-based
    int m_columns = 32;
    QWidget *m_tools;
    bool m_activeOnly = false, m_updating = false, m_pressedOnlyOpen = false, m_editable = true;
    int m_active = 0;
    std::array<int, 512> m_paintedInput{}, m_paintedOutput{};
};

} // namespace pxs::dmx
