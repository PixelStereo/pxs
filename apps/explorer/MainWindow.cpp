#include "MainWindow.h"

#include <QAction>
#include <QShortcut>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenuBar>
#include <QSaveFile>
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

    QMenu *file = menuBar()->addMenu(tr("File"));
    file->addAction(tr("Open…"), QKeySequence::Open, this, &MainWindow::openFile);
    file->addAction(tr("Save"), QKeySequence::Save, this, [this] { saveFile(false); });
    file->addAction(tr("Save As…"), QKeySequence::SaveAs, this, [this] { saveFile(true); });

    auto *bar = addToolBar(tr("Devices"));
    bar->setMovable(false);
    bar->addAction(tr("Add device…"), this, &MainWindow::addByHand);
    m_share = new QLabel;
    m_share->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusBar()->addPermanentWidget(m_share);

    // Cmd/Ctrl+E: the page's edit mode (the page itself takes it when it has the keyboard)
    auto *editKey = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_E), m_tree);
    editKey->setContext(Qt::WidgetShortcut);
    connect(editKey, &QShortcut::activated, this, [this] { m_view->page()->runJavaScript(QStringLiteral("var b=document.getElementById('edit'); if (b && !b.hidden) b.click();")); });

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
            // What is picked (the item right-clicked, when it is not in the selection)
            QList<Target> targets;
            const auto picked = item->isSelected() ? m_tree->selectedItems() : QList<QTreeWidgetItem *>{item};
            for (const QTreeWidgetItem *p : picked)
                if (p->data(0, AddableRole).toBool())
                    targets.append({quint64(p->data(0, DeviceRole).toULongLong()), p->data(0, PathRole).toString()});
            const bool sameDevice = std::all_of(targets.begin(), targets.end(), [&](const Target &t) { return t.first == targets.first().first; });
            const auto widgets = m_explorer.widgetsFor(targets.first().first, targets.first().second);
            const bool numeric = !widgets.isEmpty() && widgets.first().first == QLatin1String("vfader");
            const auto submenu = [&](QMenu *into, const QString &title) {
                QMenu *sub = into->addMenu(title);
                for (const auto &w : widgets) sub->addAction(w.second, [this, targets, w] { addTargets(targets, w.first, false); });
            };
            if (targets.size() == 1) {
                menu.addAction(tr("Add to controls page"), [this, targets] { addTargets(targets); });
                submenu(&menu, tr("Add as"));
            } else if (targets.size() == 2 && sameDevice && numeric) {
                menu.addAction(tr("Add as an XY pad"), [this, targets] { addTargets(targets); });
                submenu(&menu, tr("Add as two"));
            } else {
                menu.addAction(tr("Add %1 controls").arg(targets.size()), [this, targets] { addTargets(targets, QString(), false); });
                if (numeric && sameDevice) submenu(&menu, tr("Add all as"));
            }
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

void MainWindow::addPath(quint64 device, const QString &path) { addTargets({{device, path}}); }

// Controls for these parameters. Two numbers of one device make an XY pad (`pad`), unless a widget is asked for: then each gets it
void MainWindow::addTargets(const QList<Target> &targets, const QString &widget, bool pad)
{
    if (targets.isEmpty()) return;
    const auto numeric = [this](const Target &t) {
        const auto w = m_explorer.widgetsFor(t.first, t.second);
        return !w.isEmpty() && w.first().first == QLatin1String("vfader");
    };
    if (pad && widget.isEmpty() && targets.size() == 2 && targets[0].first == targets[1].first && numeric(targets[0]) && numeric(targets[1])) {
        say(m_explorer.addPad(targets[0].first, targets[0].second, targets[1].second) ? tr("Added an XY pad") : tr("These cannot make a pad"));
        return;
    }
    int added = 0;
    for (const Target &t : targets)
        if (m_explorer.addControl(t.first, t.second, widget)) ++added;
    say(added ? tr("Added %n control(s)", nullptr, added) : tr("Not shown as a control"));
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

static const char *kFilter = "OSCQuery Explorer interface (*.oscpage);;All files (*)";

void MainWindow::setFile(const QString &path)
{
    m_file = path;
    setWindowTitle(path.isEmpty() ? tr("OSCQuery Explorer") : tr("%1 — OSCQuery Explorer").arg(QFileInfo(path).completeBaseName()));
}

void MainWindow::openFile()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Open an interface"), m_file, tr(kFilter));
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return say(tr("Cannot read %1").arg(path));
    QJsonParseError parse;
    const QJsonDocument doc = QJsonDocument::fromJson(f.read(32 << 20), &parse);
    QString why;
    if (parse.error != QJsonParseError::NoError || !m_explorer.fromJson(doc.object(), &why)) return say(tr("%1: %2").arg(QFileInfo(path).fileName(), why.isEmpty() ? parse.errorString() : why));
    setFile(path);
    say(tr("Opened %1").arg(QFileInfo(path).fileName()));
}

bool MainWindow::saveFile(bool as)
{
    QString path = m_file;
    if (as || path.isEmpty()) {
        path = QFileDialog::getSaveFileName(this, tr("Save the interface"), m_file.isEmpty() ? QStringLiteral("Interface.oscpage") : m_file, tr(kFilter));
        if (path.isEmpty()) return false;
        if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".oscpage");
    }
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(m_explorer.toJson()).toJson()) < 0 || !f.commit()) {
        say(tr("Cannot write %1").arg(path));
        return false;
    }
    setFile(path);
    say(tr("Saved %1").arg(QFileInfo(path).fileName()));
    return true;
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
        QList<Target> targets;
        for (const QJsonValue &v : QJsonDocument::fromJson(e->mimeData()->data(AddressTree::kMime)).array()) {
            const QJsonObject o = v.toObject();
            targets.append({o.value("device").toString().toULongLong(), o.value("path").toString()});
        }
        addTargets(targets);
        e->acceptProposedAction();
        return true;
    }
    default: break;
    }
    return QMainWindow::eventFilter(watched, event);
}
