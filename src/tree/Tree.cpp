#include <pxs/Tree.h>

#include <QJsonArray>
#include <QStringList>
#include <cmath>

namespace pxs {

namespace {
QStringList segments(const QString &path) { return path.split('/', Qt::SkipEmptyParts); }

// OSC does not allow a space or any of  # * , ? [ ] { }  in the name of a container or a method: they become '_'
QString cleanName(QString name)
{
    for (QChar &c : name)
        if (c == ' ' || c == '#' || c == '*' || c == ',' || c == '?' || c == '[' || c == ']' || c == '{' || c == '}') c = '_';
    return name;
}
QStringList names(const QString &path)
{
    QStringList out = segments(path);
    for (QString &name : out) name = cleanName(name);
    return out;
}

void collect(Node &node, const QStringList &pattern, int at, std::vector<Node *> &out)
{
    if (at == pattern.size()) {
        out.push_back(&node);
        return;
    }
    for (const auto &entry : node.children())
        if (osc::match(pattern[at], entry.first)) collect(*entry.second, pattern, at + 1, out);
}

// An OSC argument as the value of a parameter; an invalid variant when it does not fit
QVariant fromOsc(const ParamInfo &info, const QVariant &arg)
{
    switch (info.type) {
    case ParamInfo::Type::Float: return arg.canConvert<double>() ? QVariant(arg.toDouble()) : QVariant();
    case ParamInfo::Type::Int: return arg.canConvert<double>() ? QVariant(int(std::lround(arg.toDouble()))) : QVariant();
    case ParamInfo::Type::Bool: return arg.metaType().id() == QMetaType::QString ? QVariant() : QVariant(arg.toDouble() != 0 || arg.toBool());
    case ParamInfo::Type::Choice:
        if (arg.metaType().id() == QMetaType::QString) { const QString key = arg.toString().toLower(); return info.choices.contains(key) ? QVariant(key) : QVariant(); }
        return info.choices.value(arg.toInt(), QString()).isEmpty() ? QVariant() : QVariant(info.choices.value(arg.toInt()));
    case ParamInfo::Type::Text: return arg.toString();
    case ParamInfo::Type::Trigger: return QVariant(true);
    }
    return {};
}
} // namespace

QString oscType(const ParamInfo &info)
{
    switch (info.type) {
    case ParamInfo::Type::Float: return QStringLiteral("f");
    case ParamInfo::Type::Int: return QStringLiteral("i");
    case ParamInfo::Type::Bool: return QStringLiteral("T");
    case ParamInfo::Type::Choice:
    case ParamInfo::Type::Text: return QStringLiteral("s");
    case ParamInfo::Type::Trigger: return QStringLiteral("N");
    }
    return {};
}

QString Node::path() const
{
    if (!m_parent) return QStringLiteral("/");
    const QString up = m_parent->path();
    return (up == QLatin1String("/") ? QString() : up) + '/' + m_name;
}

Node *Node::child(const QString &name) const
{
    const auto it = m_children.find(name);
    return it == m_children.end() ? nullptr : it->second.get();
}

Node &Tree::add(const QString &path)
{
    Node *node = &m_root;
    QString made; // the highest node this call makes
    for (const QString &name : names(path)) {
        auto &slot = node->m_children[name];
        if (!slot) {
            slot.reset(new Node);
            slot->m_name = name;
            slot->m_parent = node;
            if (made.isEmpty()) made = slot->path();
            if (!m_pendingLocks.isEmpty() && m_pendingLocks.contains(slot->path())) {
                slot->locked = true;
                m_pendingLocks.removeAll(slot->path());
            }
        }
        node = slot.get();
    }
    if (!made.isEmpty() && m_structure) m_structure(made);
    return *node;
}

Node &Tree::add(const QString &path, std::unique_ptr<Parameter> parameter)
{
    Node &node = add(path);
    Parameter *p = parameter.get();
    node.m_parameter = std::move(parameter);
    const ParamInfo info = p->info();
    // A parameter has a value that can be read. What nobody can read back (a trigger, a setter without getter) is a message.
    const bool readable = info.type != ParamInfo::Type::Trigger && (info.access & ParamInfo::Read);
    node.m_kind = readable ? Kind::Parameter : Kind::Action;
    node.label = info.name();
    if (info.type != ParamInfo::Type::Trigger && info.defaultValue.isValid()) node.reset = [p] { return p->reset(); };
    node.type = oscType(info);
    node.access = info.access;
    if (info.isNumber() && !info.unbounded) {
        node.range = QJsonArray{QJsonObject{{"MIN", info.min}, {"MAX", info.max}}};
        if (info.access & ParamInfo::Write) node.clip = QStringLiteral("both"); // a write is kept within the limits
    }
    if (info.type == ParamInfo::Type::Choice) {
        QJsonArray vals;
        for (const QString &key : info.choices) vals.append(key);
        node.range = QJsonArray{QJsonObject{{"VALS", vals}}};
    }
    if (!readable) {
        node.get = nullptr;
    } else {
        node.get = [p] { return QVariantList{p->value()}; };
    }
    node.set = [p](const QVariantList &args) {
        const ParamInfo now = p->info();
        const QVariant v = fromOsc(now, args.value(0));
        if (!v.isValid() && now.type != ParamInfo::Type::Trigger) return false;
        return p->setValue(v);
    };
    return node;
}

Node &Tree::addAction(const QString &path, const QString &type, std::function<bool(const QVariantList &)> handler,
                       const QString &description)
{
    Node &node = add(path);
    node.m_kind = Kind::Action;
    if (node.label.isEmpty()) node.label = node.name();
    node.type = type.isEmpty() ? QStringLiteral("N") : type;
    node.access = 2;
    node.description = description;
    node.get = nullptr;
    node.set = std::move(handler);
    return node;
}

Node &Tree::addSignal(const QString &path, const QString &type, const QString &description)
{
    Node &node = add(path);
    node.m_kind = Kind::Signal;
    if (node.label.isEmpty()) node.label = node.name();
    node.type = type.isEmpty() ? QStringLiteral("N") : type;
    node.access = 1;
    node.description = description;
    node.get = nullptr;
    node.set = nullptr;
    return node;
}

bool Tree::fire(const QString &path, const QVariantList &args) const
{
    const Node *node = find(path);
    if (!node || node->m_kind != Kind::Signal || node->isContainer()) return false;
    if (m_output) {
        osc::Message m;
        m.address = node->path();
        if (node->type != QLatin1String("N")) {
            m.types = node->type;
            m.args = args;
        }
        m_output(m);
    }
    notify(*node, args);
    return true;
}

bool Tree::changed(const QString &path) const
{
    const Node *node = find(path);
    if (!node || node->kind() != Kind::Parameter) return false;
    if (m_output) m_output(message(*node));
    notify(*node);
    return true;
}

Node *Tree::find(const QString &path) const
{
    const Node *node = &m_root;
    for (const QString &name : names(path)) {
        node = node->child(name);
        if (!node) return nullptr;
    }
    return const_cast<Node *>(node);
}

bool Tree::remove(const QString &path)
{
    Node *node = find(path);
    if (!node || !node->m_parent) return false;
    const QString parent = node->m_parent->path();
    const bool removed = node->m_parent->m_children.erase(node->m_name) > 0;
    if (removed && m_structure) m_structure(parent);
    return removed;
}

void Tree::clear()
{
    m_root.m_children.clear();
    m_root.description.clear();
    if (m_structure) m_structure(QStringLiteral("/"));
}

std::vector<Node *> Tree::match(const QString &pattern) const
{
    std::vector<Node *> out;
    collect(const_cast<Node &>(m_root), segments(pattern), 0, out);
    return out;
}

int Tree::apply(const osc::Message &message) const
{
    int accepted = 0;
    for (Node *node : match(message.address))
        if ((node->access & 2) && !node->locked && node->set) {
            // A write may cause another one (a mapping that drives a second node): each keeps its own state
            const Node *outer = m_applying;
            const bool outerNotified = m_applyNotified;
            m_applying = node;
            m_applyNotified = false;
            const bool ok = node->set(message.args);
            const bool told = m_applyNotified;
            m_applying = outer;
            m_applyNotified = outerNotified;
            if (!ok) continue;
            ++accepted;
            if (!told) notify(*node);
        }
    return accepted;
}

int Tree::addObserver(Observer observer)
{
    auto next = std::make_shared<std::vector<std::pair<int, Observer>>>(m_observers ? *m_observers : std::vector<std::pair<int, Observer>>());
    next->emplace_back(m_nextObserver, std::move(observer));
    m_observers = next;
    return m_nextObserver++;
}

void Tree::removeObserver(int id)
{
    if (!m_observers) return;
    auto next = std::make_shared<std::vector<std::pair<int, Observer>>>();
    for (const auto &entry : *m_observers)
        if (entry.first != id) next->push_back(entry);
    m_observers = next;
}

void Tree::notify(const Node &node, const QVariantList &args) const
{
    if (&node == m_applying) m_applyNotified = true;
    if (!m_observers || m_observers->empty()) return;
    const auto observers = m_observers; // an observer may add or remove observers: it sees the list as it was
    const QVariantList value = args.isEmpty() && node.get && node.kind() == Kind::Parameter ? node.get() : args;
    for (const auto &entry : *observers) entry.second(node, value);
}

bool Tree::setLocked(const QString &path, bool locked)
{
    Node *node = find(path);
    if (!node) return false;
    node->locked = locked;
    notify(*node);
    return true;
}

namespace {
void collectLocked(const Node &node, QJsonArray &out)
{
    if (node.locked) out.append(node.path());
    for (const auto &entry : node.children()) collectLocked(*entry.second, out);
}
}

QJsonArray Tree::lockedToJson() const
{
    QJsonArray out;
    collectLocked(m_root, out);
    return out;
}

void Tree::lockedFromJson(const QJsonArray &paths)
{
    m_pendingLocks.clear();
    for (const QJsonValue &v : paths) {
        const QString path = v.toString();
        if (Node *node = find(path)) node->locked = true;
        else m_pendingLocks.append(path);
    }
}

bool Tree::reset(const QString &path) const
{
    Node *node = find(path);
    if (!node || node->locked || !node->reset || !node->reset()) return false;
    notify(*node);
    return true;
}

osc::Message Tree::message(const Node &node)
{
    osc::Message m;
    m.address = node.path();
    if (node.type == QLatin1String("T") || node.type == QLatin1String("F")) {
        const QVariantList v = node.get ? node.get() : QVariantList();
        m.types = !v.isEmpty() && v[0].toBool() ? QStringLiteral("T") : QStringLiteral("F");
    } else if (node.get && node.type != QLatin1String("N")) {
        m.types = node.type;
        m.args = node.get();
    }
    return m;
}

namespace {
// One OSC argument as OSCQuery writes it (the table of the proposal): numbers, strings, a colour as #RRGGBBAA, a switch as
// a boolean, nothing for what has no value (N I) or no description (blob, MIDI)
QJsonValue scalar(QChar t, const QVariant &v)
{
    if (t == 'T' || t == 'F') return v.toBool();
    if (t == 'i' || t == 'h') return v.toLongLong();
    if (t == 's' || t == 'S' || t == 'c') return v.toString();
    if (t == 'r') return QStringLiteral("#%1").arg(quint32(v.toLongLong()), 8, 16, QLatin1Char('0')).toUpper();
    if (t == 'N' || t == 'I' || t == 'b' || t == 'm') return QJsonValue();
    const double d = v.toDouble();
    return std::isfinite(d) ? d : 0.0;
}

// The values of a type tag string, with its arrays as arrays; `args` holds them flat, in order
QJsonArray valuesOf(const QString &type, const QVariantList &args, int &at, int &used, bool nested)
{
    QJsonArray out;
    while (at < type.size()) {
        const QChar t = type[at++];
        if (t == ']') {
            if (nested) break;
            continue;
        }
        if (t == '[') out.append(valuesOf(type, args, at, used, true));
        else out.append(scalar(t, args.value(used++)));
    }
    return out;
}
} // namespace

QJsonValue Tree::jsonValue(const QString &type, const QVariant &v, int k)
{
    return scalar(k < type.size() ? type[k] : QChar(), v);
}

QJsonObject Tree::json(const QString &path) const
{
    const Node *n = find(path);
    return n ? describe(*n, true) : QJsonObject();
}

QJsonObject Tree::describe(const Node &node, bool contents) const
{
    const Node *n = &node;
    QJsonObject o;
    o["FULL_PATH"] = n->path();
    o["ACCESS"] = n->access;
    if (!n->description.isEmpty()) o["DESCRIPTION"] = n->description;
    if (n->locked) o["PXS_LOCKED"] = true;
    if (!n->type.isEmpty()) {
        o["TYPE"] = n->type;
        if ((n->access & 1) && n->get) {
            const QVariantList v = n->get();
            if (!v.isEmpty()) {
                int at = 0, used = 0;
                o["VALUE"] = valuesOf(n->type, v, at, used, false);
            }
        }
        if (!n->range.isEmpty()) o["RANGE"] = n->range;
        if (!n->clip.isEmpty()) {
            QJsonArray c;
            for (QChar t : n->type)
                if (t != '[' && t != ']') c.append(n->clip);
            o["CLIPMODE"] = c;
        }
    }
    for (const auto &entry : n->attributes) {
        const QJsonValue v = entry.second();
        if (!v.isNull() && !v.isUndefined()) o[entry.first] = v;
    }
    if (n->isContainer() && contents) { // a container always has CONTENTS (even empty)
        QJsonObject children;
        for (const auto &entry : n->children()) children[entry.first] = describe(*entry.second, true);
        o["CONTENTS"] = children;
    }
    return o;
}

bool Tree::attribute(const QString &path, const QString &name, QJsonObject &out) const
{
    const Node *n = find(path);
    if (!n) return false;
    out = QJsonObject();
    const QString key = name.toUpper();
    const QJsonObject node = describe(*n, key == QLatin1String("CONTENTS")); // the subtree only when it is the one asked for
    if (node.contains(key)) out[key] = node.value(key);
    return true;
}

QJsonObject Tree::defaultExtensions()
{
    return QJsonObject{{"ACCESS", true}, {"VALUE", true}, {"RANGE", true}, {"DESCRIPTION", true}, {"CLIPMODE", true}, {"PXS_LOCKED", true}};
}

QJsonObject Tree::hostInfo(const QString &name, int oscPort, const QJsonObject &extensions, const QJsonObject &more)
{
    QJsonObject o{{"NAME", name}, {"OSC_PORT", oscPort}, {"OSC_TRANSPORT", "UDP"}, {"EXTENSIONS", extensions}};
    for (auto it = more.begin(); it != more.end(); ++it) o[it.key()] = it.value();
    return o;
}

} // namespace pxs
