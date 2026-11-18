#include <pxs/Tree.h>

#include <QJsonArray>
#include <QStringList>
#include <cmath>

namespace pxs {

namespace {
QStringList segments(const QString &path) { return path.split('/', Qt::SkipEmptyParts); }

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
    for (const QString &name : segments(path)) {
        auto &slot = node->m_children[name];
        if (!slot) {
            slot.reset(new Node);
            slot->m_name = name;
            slot->m_parent = node;
        }
        node = slot.get();
    }
    return *node;
}

Node &Tree::add(const QString &path, std::unique_ptr<Parameter> parameter)
{
    Node &node = add(path);
    Parameter *p = parameter.get();
    node.m_parameter = std::move(parameter);
    node.m_kind = p->info().type == ParamInfo::Type::Trigger ? Kind::Message : Kind::Parameter;
    const ParamInfo info = p->info();
    node.type = oscType(info);
    node.access = info.access;
    if (info.isNumber() && !info.unbounded) node.range = QJsonArray{QJsonObject{{"MIN", info.min}, {"MAX", info.max}}};
    if (info.type == ParamInfo::Type::Choice) {
        QJsonArray vals;
        for (const QString &key : info.choices) vals.append(key);
        node.range = QJsonArray{QJsonObject{{"VALS", vals}}};
    }
    if (info.type == ParamInfo::Type::Trigger) {
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

Node &Tree::addMessage(const QString &path, const QString &type, std::function<bool(const QVariantList &)> handler,
                       const QString &description)
{
    Node &node = add(path);
    node.m_kind = Kind::Message;
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
    return true;
}

bool Tree::changed(const QString &path) const
{
    const Node *node = find(path);
    if (!node || node->kind() != Kind::Parameter) return false;
    if (m_output) m_output(message(*node));
    return true;
}

Node *Tree::find(const QString &path) const
{
    const Node *node = &m_root;
    for (const QString &name : segments(path)) {
        node = node->child(name);
        if (!node) return nullptr;
    }
    return const_cast<Node *>(node);
}

bool Tree::remove(const QString &path)
{
    Node *node = find(path);
    if (!node || !node->m_parent) return false;
    return node->m_parent->m_children.erase(node->m_name) > 0;
}

void Tree::clear() { m_root.m_children.clear(); m_root.description.clear(); }

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
        if ((node->access & 2) && node->set && node->set(message.args)) ++accepted;
    return accepted;
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

QJsonValue Tree::jsonValue(const QString &type, const QVariant &v, int k)
{
    const QChar t = k < type.size() ? type[k] : QChar();
    if (t == 'T' || t == 'F') return v.toBool();
    if (t == 'i' || t == 'h') return v.toLongLong();
    if (t == 's') return v.toString();
    if (t == 'N' || t == 'I') return QJsonValue();
    const double d = v.toDouble();
    return std::isfinite(d) ? d : 0.0;
}

QJsonObject Tree::json(const QString &path) const
{
    const Node *n = find(path);
    if (!n) return {};
    QJsonObject o;
    o["FULL_PATH"] = n->path();
    o["ACCESS"] = n->access;
    if (!n->description.isEmpty()) o["DESCRIPTION"] = n->description;
    if (n->m_kind == Kind::Message && !n->isContainer()) o["PXS_KIND"] = "message";
    if (n->m_kind == Kind::Signal && !n->isContainer()) o["PXS_KIND"] = "signal";
    if (!n->type.isEmpty()) {
        o["TYPE"] = n->type;
        if ((n->access & 1) && n->get) {
            const QVariantList v = n->get();
            if (!v.isEmpty()) {
                QJsonArray a;
                for (int k = 0; k < v.size(); ++k) a.append(jsonValue(n->type, v[k], k));
                o["VALUE"] = a;
            }
        }
        if (!n->range.isEmpty()) o["RANGE"] = n->range;
        if (!n->clip.isEmpty()) {
            QJsonArray c;
            for (int k = 0; k < n->type.size(); ++k) c.append(n->clip);
            o["CLIPMODE"] = c;
        }
    }
    for (const auto &entry : n->attributes) {
        const QJsonValue v = entry.second();
        if (!v.isNull() && !v.isUndefined()) o[entry.first] = v;
    }
    if (n->isContainer()) { // a container always has CONTENTS (even empty)
        QJsonObject contents;
        for (const auto &entry : n->children()) contents[entry.first] = json(entry.second->path());
        o["CONTENTS"] = contents;
    }
    return o;
}

bool Tree::attribute(const QString &path, const QString &name, QJsonObject &out) const
{
    const QJsonObject node = json(path);
    if (node.isEmpty()) return false;
    out = QJsonObject();
    const QString key = name.toUpper();
    if (node.contains(key)) out[key] = node.value(key);
    return true;
}

QJsonObject Tree::defaultExtensions()
{
    return QJsonObject{{"ACCESS", true}, {"VALUE", true}, {"RANGE", true}, {"DESCRIPTION", true}};
}

QJsonObject Tree::hostInfo(const QString &name, int oscPort, const QJsonObject &extensions, const QJsonObject &more)
{
    QJsonObject o{{"NAME", name}, {"OSC_PORT", oscPort}, {"OSC_TRANSPORT", "UDP"}, {"EXTENSIONS", extensions}};
    for (auto it = more.begin(); it != more.end(); ++it) o[it.key()] = it.value();
    return o;
}

} // namespace pxs
