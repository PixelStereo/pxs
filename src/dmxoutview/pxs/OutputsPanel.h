#pragma once
// The panel that edits a list of DMX outputs (pxs::dmx::Output): a table (on, name, protocol, what the output is doing) and the
// settings of the selected one: protocol, unicast or the protocol's group, the network card, the addresses, a port, the rate, the
// sACN priority and source name, full or partial frames, and (for an application that routes its universes) the map of its
// universes onto the network's. The same in every application that sends DMX. It keeps no outputs of its own: the application
// hands them over (setOutputs) and is told each change (outputsChanged), with the text an undo step can carry.
#include <QList>
#include <QMetaType>
#include <QWidget>
#include <functional>
#include <pxs/DmxOut.h>

class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;

namespace pxs::dmx {

class OutputsPanel : public QWidget
{
    Q_OBJECT
public:
    explicit OutputsPanel(QWidget *parent = nullptr);

    // The application's outputs (their ids are the application's); the panel shows them, without telling it back
    void setOutputs(const QList<Output> &outputs);
    QList<Output> outputs() const { return m_outputs; }
    // What each output is doing, asked every few hundred milliseconds while the panel is shown (the Sender's `state`)
    void setStateSource(std::function<OutputState(quint64)> source) { m_state = std::move(source); }
    // The universe map (firstSourceUniverse, firstUniverse, universeCount): for an application that uses `route`
    void setMappingShown(bool shown);
    // The sACN source name a new output gets (the application's)
    void setDefaultSourceName(const QString &name) { m_sourceName = name; }

signals:
    // The outputs as they are now, and what was done ("New DMX Output", "Edit DMX Output", "Delete DMX Output"); `field` names a
    // setting a run of edits keeps merging into one step ("rate", "priority"…), empty otherwise
    void outputsChanged(const QList<Output> &outputs, const QString &what, const QString &field);

private:
    void edit(const QString &what, const QString &field, const std::function<void(Output &)> &change);
    void refreshTable();
    void refreshEditor();
    void refreshStates();
    int selectedRow() const;

    QList<Output> m_outputs;
    std::function<OutputState(quint64)> m_state;
    QString m_sourceName = QStringLiteral("pxs");
    bool m_updating = false, m_mapping = false;
    QTableWidget *m_table;
    QWidget *m_editor;
    QFormLayout *m_form;
    QComboBox *m_protocol, *m_mode, *m_interface, *m_transmission;
    QLineEdit *m_addresses, *m_sourceNameEdit;
    QSpinBox *m_port, *m_rate, *m_priority, *m_patchFirst, *m_wireFirst, *m_count;
    QLabel *m_problem;
    QPushButton *m_add, *m_remove, *m_off;
    QTimer *m_timer;
};

} // namespace pxs::dmx

Q_DECLARE_METATYPE(pxs::dmx::Output) // so that the list of outputs travels in a signal
