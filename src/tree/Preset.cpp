#include <pxs/Preset.h>

#include <QJsonValue>

namespace pxs {

namespace {
// An OSCQuery value as the arguments of an OSC message: nested arrays flattened, as the type tags have them
void flatten(const QJsonValue &v, QVariantList &out)
{
    if (v.isArray()) {
        for (const QJsonValue &e : v.toArray()) flatten(e, out);
    } else if (v.isBool()) {
        out << v.toBool();
    } else if (v.isString()) {
        out << v.toString();
    } else if (v.isDouble()) {
        out << v.toDouble();
    } else {
        out << QVariant();
    }
}

QVariantList arguments(const QJsonArray &a)
{
    QVariantList out;
    for (const QJsonValue &v : a) flatten(v, out);
    return out;
}

// A node a preset may hold: a parameter that is read and written (and, when it has a Parameter, one that is saved)
bool holdable(const Node &n)
{
    if (n.kind() != Kind::Parameter || n.isContainer() || n.access != 3 || !n.get || !n.set) return false;
    return !n.parameter() || n.parameter()->isStored();
}

void collect(const Node &n, QStringList &out)
{
    if (holdable(n)) out << n.path();
    for (const auto &c : n.children()) collect(*c.second, out);
}
} // namespace

QJsonObject Preset::toJson() const
{
    QJsonArray list;
    for (const Action &a : actions) {
        QJsonObject o{{"address", a.address}};
        if (!a.args.isEmpty()) o["args"] = a.args;
        list.append(o);
    }
    QJsonObject o{{"values", values}};
    if (!name.isEmpty()) o["name"] = name;
    if (!list.isEmpty()) o["actions"] = list;
    return o;
}

Preset Preset::fromJson(const QJsonObject &o)
{
    Preset p;
    p.name = o.value("name").toString();
    p.values = o.value("values").toObject();
    for (const QJsonValue &v : o.value("actions").toArray()) {
        const QJsonObject a = v.toObject();
        if (!a.value("address").toString().isEmpty()) p.actions.append({a.value("address").toString(), a.value("args").toArray()});
    }
    return p;
}

Preset capture(const Tree &tree, const QStringList &only, const QList<Preset::Action> &actions)
{
    Preset p;
    QStringList paths = only;
    if (paths.isEmpty()) collect(tree.root(), paths);
    for (const QString &path : paths) {
        const Node *n = tree.find(path);
        if (!n || !holdable(*n)) continue;
        const QJsonArray value = tree.json(path).value("VALUE").toArray();
        if (!value.isEmpty()) p.values.insert(n->path(), value);
    }
    p.actions = actions;
    return p;
}

RecallResult recall(Tree &tree, const Preset &preset)
{
    RecallResult r;
    for (auto it = preset.values.begin(); it != preset.values.end(); ++it) {
        const Node *n = tree.find(it.key());
        if (!n || !holdable(*n)) {
            ++r.missing;
            continue;
        }
        if (n->locked) {
            ++r.locked;
            continue;
        }
        if (tree.apply({n->path(), n->type, arguments(it.value().toArray())}) > 0) ++r.applied;
        else ++r.missing; // refused: a value that does not fit
    }
    for (const Preset::Action &a : preset.actions) {
        const Node *n = tree.find(a.address);
        if (!n || n->kind() != Kind::Action) {
            ++r.missing;
            continue;
        }
        if (n->locked) {
            ++r.locked;
            continue;
        }
        const QVariantList args = arguments(a.args);
        if (tree.apply({n->path(), n->type == QLatin1String("N") ? QString() : n->type, args}) > 0) ++r.actions;
        else ++r.missing;
    }
    return r;
}

} // namespace pxs
