#include "MainWindow.h"

#include <QAction>
#include <QHeaderView>
#include <QStatusBar>
#include <QChildEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QWebEngineView>
#include <QWebEnginePage>

using pxs::oscquery::RemoteNode;

AddressTree::AddressTree(QWidget *parent) : QTreeWidget(parent)
{
    setHeaderLabels({tr("Address"), tr("Type")});
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragOnly);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setContextMenuPolicy(Qt::CustomContextMenu);
    setUniformRowHeights(true);
    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(0, QHeaderView::Stretch);
}

QMimeData *AddressTree::mimeData(const QList<QTreeWidgetItem *> &items) const
{
    QJsonArray list;
    QStringList paths;
    for (const QTreeWidgetItem *item : items) {
        if (!item->data(0, Qt::UserRole + 3).toBool()) continue; // only what a control can show
        list.append(QJsonObject{{"device", item->data(0, Qt::UserRole + 1).toString()}, {"path", item->data(0, Qt::UserRole + 2).toString()}});
        paths << item->data(0, Qt::UserRole + 2).toString();
    }
    if (list.isEmpty()) return nullptr;
    auto *mime = new QMimeData;
    mime->setData(kMime, QJsonDocument(list).toJson(QJsonDocument::Compact));
    mime->setText(paths.join('\n'));
    return mime;
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(tr("OSCQuery Explorer"));
    resize(1280, 800);

    m_tree = new AddressTree;
    m_view = new QWebEngineView;
    m_view->setContextMenuPolicy(Qt::NoContextMenu);
    m_view->page()->setBackgroundColor(QColor(18, 18, 21));
    watchDrops(m_view);

    auto *split = new QSplitter;
    split->addWidget(m_tree);
    split->addWidget(m_view);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setSizes({340, 940});
    setCentralWidget(split);

    auto *bar = addToolBar(tr("Devices"));
    bar->setMovable(false);
    bar->addAction(tr("Add device…"), this, &MainWindow::addByHand);
    m_share = new QLabel;
    m_share->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusBar()->addPermanentWidget(m_share);

    connect(&m_explorer, &Explorer::devicesChanged, this, &MainWindow::rebuildDevices);
    connect(&m_explorer, &Explorer::deviceReady, this, &MainWindow::fillDevice);
    connect(&m_explorer, &Explorer::deviceTreeChanged, this, &MainWindow::fillDevice);
    connect(&m_explorer, &Explorer::deviceFailed, this, [this](quint64 id, const QString &why) {
        const auto *d = m_explorer.device(id);
        say(tr("%1: %2").arg(d ? d->name : QString(), why));
    });

    // A device opens when it is picked or unfolded: its branches are only known then
    connect(m_tree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem *item) {
        if (!item->parent()) m_explorer.openDevice(quint64(item->data(0, DeviceRole).toULongLong()));
    });
    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item) {
        if (!item->parent()) m_explorer.openDevice(quint64(item->data(0, DeviceRole).toULongLong()));
    });
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) {
        if (!item->parent()) return m_explorer.openDevice(quint64(item->data(0, DeviceRole).toULongLong()));
        if (item->data(0, AddableRole).toBool()) addPath(quint64(item->data(0, DeviceRole).toULongLong()), item->data(0, PathRole).toString());
    });
    connect(m_tree, &QWidget::customContextMenuRequested, this, [this](const QPoint &at) {
        QTreeWidgetItem *item = m_tree->itemAt(at);
        if (!item) return;
        QMenu menu;
        if (!item->parent()) {
            menu.addAction(tr("Connect"), [this, item] { m_explorer.openDevice(quint64(item->data(0, DeviceRole).toULongLong())); });
        } else if (item->data(0, AddableRole).toBool()) {
            menu.addAction(tr("Add to controls page"), [this, item] {
                addPath(quint64(item->data(0, DeviceRole).toULongLong()), item->data(0, PathRole).toString());
            });
        } else if (item->childCount()) {
            menu.addAction(tr("Add all its parameters to the page"), [this, item] { addBelow(item); });
        }
        if (!menu.isEmpty()) menu.exec(m_tree->viewport()->mapToGlobal(at));
    });

    // The page is served to the network as it is: no code, whoever is on it can play it
    if (!m_explorer.serve()) say(tr("The page cannot be served: %1").arg(m_explorer.server().status()));
    m_share->setText(tr("Page at %1").arg(m_explorer.networkUrls().value(0)));
    m_view->setUrl(QUrl(m_explorer.pageUrl()));
    if (!m_explorer.startSearch()) say(tr("The network cannot be searched (mDNS): add a device by hand"));
    rebuildDevices();
}

QTreeWidgetItem *MainWindow::deviceItem(quint64 id) const
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        if (quint64(m_tree->topLevelItem(i)->data(0, DeviceRole).toULongLong()) == id) return m_tree->topLevelItem(i);
    return nullptr;
}

void MainWindow::rebuildDevices()
{
    for (const auto &d : m_explorer.devices()) {
        QTreeWidgetItem *item = deviceItem(d->id);
        if (!item) {
            item = new QTreeWidgetItem(m_tree);
            item->setData(0, DeviceRole, QVariant::fromValue<qulonglong>(d->id));
            auto *waiting = new QTreeWidgetItem(item); // so that the triangle opens it: its namespace is asked then
            waiting->setText(0, tr("…"));
            waiting->setFlags(Qt::ItemIsEnabled);
        }
        item->setText(0, d->name.isEmpty() ? QStringLiteral("%1:%2").arg(d->host).arg(d->port) : d->name);
        item->setText(1, d->online ? QString() : tr("offline"));
        item->setToolTip(0, QStringLiteral("%1:%2").arg(d->host).arg(d->port));
        item->setForeground(0, d->online ? QBrush() : QBrush(Qt::gray));
    }
}

static bool addable(const RemoteNode &n)
{
    ParamInfo info;
    return (n.access & 2) && n.toParamInfo(&info);
}

static void fillNode(QTreeWidgetItem *parent, const RemoteNode &node, quint64 device, int depth)
{
    for (const auto &entry : node.children) {
        const RemoteNode &n = entry.second;
        auto *item = new QTreeWidgetItem(parent);
        item->setText(0, n.name);
        item->setData(0, Qt::UserRole + 1, QVariant::fromValue<qulonglong>(device));
        item->setData(0, Qt::UserRole + 2, n.path);
        const bool can = n.isLeaf() && addable(n);
        item->setData(0, Qt::UserRole + 3, can);
        if (n.isLeaf()) {
            item->setText(1, n.type);
            QString tip = n.path;
            if (!n.description.isEmpty()) tip += QStringLiteral("\n") + n.description;
            if (!can) tip += QStringLiteral("\n(read only, or more than one argument: not a control yet)");
            item->setToolTip(0, tip);
            if (!can) item->setForeground(0, Qt::gray);
        }
        Qt::ItemFlags flags = item->flags();
        if (!can) flags &= ~Qt::ItemIsDragEnabled;
        item->setFlags(flags);
        if (depth < 64) fillNode(item, n, device, depth + 1);
    }
}

void MainWindow::fillDevice(quint64 id)
{
    QTreeWidgetItem *item = deviceItem(id);
    const auto *d = m_explorer.device(id);
    if (!item || !d || !d->client) return;
    // The branches open stay open
    QSet<QString> open;
    std::function<void(QTreeWidgetItem *)> remember = [&](QTreeWidgetItem *p) {
        for (int i = 0; i < p->childCount(); ++i) {
            if (p->child(i)->isExpanded()) open.insert(p->child(i)->data(0, PathRole).toString());
            remember(p->child(i));
        }
    };
    remember(item);
    qDeleteAll(item->takeChildren());
    fillNode(item, d->client->tree().root(), id, 0);
    std::function<void(QTreeWidgetItem *)> restore = [&](QTreeWidgetItem *p) {
        for (int i = 0; i < p->childCount(); ++i) {
            if (open.contains(p->child(i)->data(0, PathRole).toString())) p->child(i)->setExpanded(true);
            restore(p->child(i));
        }
    };
    restore(item);
    if (open.isEmpty()) item->setExpanded(true);
    // A device with a single branch shows what is in it
    if (item->childCount() == 1 && open.isEmpty()) item->child(0)->setExpanded(true);
}

void MainWindow::addPath(quint64 device, const QString &path)
{
    if (m_explorer.addControl(device, path)) say(tr("Added %1").arg(path));
    else say(tr("%1 cannot be shown as a control").arg(path));
}

void MainWindow::addBelow(QTreeWidgetItem *item)
{
    int added = 0;
    std::function<void(QTreeWidgetItem *)> walk = [&](QTreeWidgetItem *p) {
        for (int i = 0; i < p->childCount(); ++i) {
            QTreeWidgetItem *c = p->child(i);
            if (c->data(0, AddableRole).toBool() || c->data(0, Qt::UserRole + 3).toBool()) {
                if (m_explorer.addControl(quint64(c->data(0, DeviceRole).toULongLong()), c->data(0, PathRole).toString())) ++added;
            }
            walk(c);
        }
    };
    walk(item);
    say(tr("Added %1 controls").arg(added));
}

void MainWindow::addByHand()
{
    bool ok = false;
    const QString text = QInputDialog::getText(this, tr("Add a device"), tr("Address of its OSCQuery server (host:port):"), QLineEdit::Normal, QStringLiteral("127.0.0.1:"), &ok);
    if (!ok) return;
    const int colon = text.lastIndexOf(QLatin1Char(':'));
    const quint16 port = colon > 0 ? quint16(text.mid(colon + 1).toUInt()) : 0;
    if (!port) return say(tr("Give it as host:port"));
    const QString host = text.left(colon).trimmed();
    const quint64 id = m_explorer.addDevice(QString(), host, port);
    m_explorer.openDevice(id);
}

// Drops on the page: Qt WebEngine's own widgets take them, so each one is watched
void MainWindow::watchDrops(QObject *object)
{
    object->installEventFilter(this);
    if (auto *w = qobject_cast<QWidget *>(object)) w->setAcceptDrops(true);
    for (QObject *child : object->children()) watchDrops(child);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::ChildAdded:
        watchDrops(static_cast<QChildEvent *>(event)->child());
        break;
    case QEvent::DragEnter:
    case QEvent::DragMove: {
        auto *e = static_cast<QDropEvent *>(event);
        if (!e->mimeData()->hasFormat(AddressTree::kMime)) break;
        e->acceptProposedAction();
        return true;
    }
    case QEvent::Drop: {
        auto *e = static_cast<QDropEvent *>(event);
        if (!e->mimeData()->hasFormat(AddressTree::kMime)) break;
        int added = 0;
        for (const QJsonValue &v : QJsonDocument::fromJson(e->mimeData()->data(AddressTree::kMime)).array()) {
            const QJsonObject o = v.toObject();
            if (m_explorer.addControl(o.value("device").toString().toULongLong(), o.value("path").toString())) ++added;
        }
        say(tr("Added %n control(s)", nullptr, added));
        e->acceptProposedAction();
        return true;
    }
    default: break;
    }
    return QMainWindow::eventFilter(watched, event);
}
