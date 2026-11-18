#pragma once

// The address space of an application: a tree of named nodes, some of them carrying a Parameter. OSC messages are applied
// to it and the OSCQuery description is derived from it, so a parameter is declared once and nothing else repeats it.
// (The idea comes from Ossia's device tree. What was left out on purpose: bounding modes, units, repetition filters,
// tags, a callback list on every node. A write already tells the others through ChangeFeed.)

#include "OscCodec.h"
#include "Parameter.h"

#include <QJsonObject>
#include <QString>
#include <map>
#include <memory>
#include <vector>

namespace pxs {

class Tree;

class Node {
public:
    const QString &name() const { return m_name; }
    QString path() const; // "/layer/1/opacity"; the root is "/"
    Node *parent() const { return m_parent; }
    Node *child(const QString &name) const;
    const std::map<QString, std::unique_ptr<Node>> &children() const { return m_children; }
    Parameter *parameter() const { return m_parameter.get(); } // null: a container
    QString description;                                       // shown by OSCQuery

private:
    friend class Tree;
    QString m_name;
    Node *m_parent = nullptr;
    std::map<QString, std::unique_ptr<Node>> m_children;
    std::unique_ptr<Parameter> m_parameter;
};

class Tree {
public:
    Tree();
    Node &root() { return m_root; }
    const Node &root() const { return m_root; }

    // The node at this address, with its containers made on the way. A parameter, when given, is attached to it and owned
    // by the tree; its path is the node's address.
    Node &add(const QString &path, std::unique_ptr<Parameter> parameter = {});
    Node *find(const QString &path) const;
    bool remove(const QString &path);

    // Every node whose address matches an OSC address pattern (* ? [abc] {a,b}), containers included
    std::vector<Node *> match(const QString &pattern) const;

    // Applies an OSC message to the parameters its address names, as their access allows; returns how many were written
    int apply(const osc::Message &message) const;
    // The OSC message that says what a parameter holds now (the feedback to send)
    static osc::Message message(const Node &node);

    // OSCQuery: the description of a node and of everything below it (FULL_PATH, CONTENTS, TYPE, ACCESS, RANGE or VALS,
    // VALUE, DESCRIPTION); the HTTP server and the announcement (mDNS) belong to the application
    QJsonObject query(const QString &path = QStringLiteral("/")) const;
    static QJsonObject hostInfo(const QString &name, int oscPort, const QString &transport = QStringLiteral("UDP"));

private:
    Node m_root;
};

// The OSC type tag of a parameter ("f", "i", "T" for a switch, "s", "N" for an action)
QString oscType(const ParamInfo &info);

} // namespace pxs
