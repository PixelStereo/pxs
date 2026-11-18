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
        if (arg.metaType().id() == QMetaType::QString) return info.choices.contains(arg.toString()) ? arg : QVariant();
        return info.choices.value(arg.toInt(), QString()).isEmpty() ? QVariant() : QVariant(info.choices.value(arg.toInt()));
    case ParamInfo::Type::Text: return arg.toString();
    case ParamInfo::Type::Trigger: return QVariant(true);
    }
    return {};
}

QJsonValue toJson(const ParamInfo &info, const QVariant &v)
{
    switch (info.type) {
    case ParamInfo::Type::Float: return v.toDouble();
    case ParamInfo::Type::Int: return v.toInt();
    case ParamInfo::Type::Bool: return v.toBool();
    case ParamInfo::Type::Choice:
    case ParamInfo::Type::Text: return v.toString();
    case ParamInfo::Type::Trigger: return QJsonValue();
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

Tree::Tree() {}

Node &Tree::add(const QString &path, std::unique_ptr<Parameter> parameter)
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
    if (parameter) node->m_parameter = std::move(parameter);
    return *node;
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

std::vector<Node *> Tree::match(const QString &pattern) const
{
    std::vector<Node *> out;
    collect(const_cast<Node &>(m_root), segments(pattern), 0, out);
    return out;
}

int Tree::apply(const osc::Message &message) const
{
    int written = 0;
    for (Node *node : match(message.address)) {
        Parameter *p = node->parameter();
        if (!p) continue;
        const ParamInfo info = p->info();
        if (!(info.access & ParamInfo::Write)) continue;
        const QVariant v = fromOsc(info, message.args.value(0));
        if (!v.isValid() && info.type != ParamInfo::Type::Trigger) continue;
        if (p->setValue(v)) ++written;
    }
    return written;
}

osc::Message Tree::message(const Node &node)
{
    osc::Message m;
    m.address = node.path();
    if (const Parameter *p = node.parameter()) {
        const ParamInfo info = p->info();
        const QString type = oscType(info);
        if (info.type == ParamInfo::Type::Bool) {
            m.types = p->value().toBool() ? QStringLiteral("T") : QStringLiteral("F");
        } else if (info.type != ParamInfo::Type::Trigger) {
            m.types = type;
            m.args << p->value();
        }
    }
    return m;
}

QJsonObject Tree::query(const QString &path) const
{
    const Node *node = find(path);
    if (!node) return {};
    QJsonObject o;
    o["FULL_PATH"] = node->path();
    if (!node->description.isEmpty()) o["DESCRIPTION"] = node->description;
    if (const Parameter *p = node->parameter()) {
        const ParamInfo info = p->info();
        o["TYPE"] = oscType(info);
        o["ACCESS"] = info.access;                      // 1 read, 2 write, 3 both, as OSCQuery says
        if (info.isNumber()) o["RANGE"] = QJsonArray{QJsonObject{{"MIN", info.min}, {"MAX", info.max}}};
        if (info.type == ParamInfo::Type::Choice) {
            QJsonArray vals;
            for (const QString &key : info.choices) vals.append(key);
            o["RANGE"] = QJsonArray{QJsonObject{{"VALS", vals}}};
        }
        if (info.access & ParamInfo::Read) o["VALUE"] = QJsonArray{toJson(info, p->value())};
    }
    if (!node->children().empty()) {
        QJsonObject contents;
        for (const auto &entry : node->children()) contents[entry.first] = query(entry.second->path());
        o["CONTENTS"] = contents;
    }
    return o;
}

QJsonObject Tree::hostInfo(const QString &name, int oscPort, const QString &transport)
{
    return QJsonObject{{"NAME", name}, {"OSC_PORT", oscPort}, {"OSC_TRANSPORT", transport},
                       {"EXTENSIONS", QJsonObject{{"ACCESS", true}, {"VALUE", true}, {"RANGE", true}, {"DESCRIPTION", true}}}};
}

} // namespace pxs
