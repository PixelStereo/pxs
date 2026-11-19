#pragma once
// The explorer's window: the devices of the network and their parameters on the left, the controls page on the right. Drag an address from the
// tree onto the page (or double-click it, or use its menu) and the page gets a control that is a view of that parameter.
#include "Explorer.h"

#include <QMainWindow>
#include <QStatusBar>
#include <QTreeWidget>

class QWebEngineView;
class QLabel;

// The tree of devices and addresses; what it drags is {"device": id, "path": address}
class AddressTree : public QTreeWidget
{
    Q_OBJECT
public:
    static constexpr const char *kMime = "application/x-pxs-osc-address";
    explicit AddressTree(QWidget *parent = nullptr);

protected:
    QMimeData *mimeData(const QList<QTreeWidgetItem *> &items) const override;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    enum Role { DeviceRole = Qt::UserRole + 1, PathRole, AddableRole };
    void rebuildDevices();
    void fillDevice(quint64 id);
    QTreeWidgetItem *deviceItem(quint64 id) const;
    void addPath(quint64 device, const QString &path);
    void addBelow(QTreeWidgetItem *item);
    void share(bool on);
    void addByHand();
    void say(const QString &message) { statusBar()->showMessage(message, 6000); }
    bool eventFilter(QObject *watched, QEvent *event) override;
    void watchDrops(QObject *object);

    Explorer m_explorer;
    AddressTree *m_tree;
    QWebEngineView *m_view;
    QLabel *m_share;
};
