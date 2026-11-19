#pragma once
// What an OSCQuery device says of itself (its / answer), kept as a tree the application can browse and read: each node's address, type
// tags, access, range, description and last value. A node that holds one argument can be seen as a ParamInfo (pxs::params), which is
// how a control becomes a view of a remote parameter (RemoteSource.h).
#include <Parameter.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <map>

namespace pxs::oscquery {

struct RemoteNode {
    QString name;        // its segment ("opacity"); the root's is empty
    QString path;        // "/layer/1/opacity"; the root's is "/"
    QString type;        // OSC type tags, one per argument; empty for a container
    QString description;
    QString kind;        // PXS_KIND: "message", "signal", or empty
    int access = 0;      // 0 none (a container), 1 read, 2 write, 3 both
    QJsonArray range;    // one entry per argument: {"MIN","MAX"} or {"VALS"}
    QJsonArray value;    // the last one known, one entry per argument (empty: none known)
    std::map<QString, RemoteNode> children;

    bool isContainer() const { return type.isEmpty() && access == 0; }
    bool isLeaf() const { return !type.isEmpty() || access != 0; }
    // As one parameter: false when it has no type or several arguments (a leaf of one argument only)
    bool toParamInfo(ParamInfo *out) const;
};

class RemoteTree {
public:
    // Replaces everything with a device's answer to GET /; false when it is not a node
    bool load(const QJsonObject &root);
    void clear() { m_root = RemoteNode(); m_loaded = false; }
    bool loaded() const { return m_loaded; }
    const RemoteNode &root() const { return m_root; }
    const RemoteNode *find(const QString &path) const;
    // A value the device told (LISTEN, ?VALUE, a message); false: no such node
    bool setValue(const QString &path, const QJsonArray &value);
    // The addresses of every leaf, in order
    QStringList leaves() const;

private:
    RemoteNode m_root;
    bool m_loaded = false;
};

} // namespace pxs::oscquery
