#pragma once

// The address space of an application: a tree of named nodes. A node is a container, or a method that is read, written or
// both, described the way OSCQuery describes it (type tags, access, range). The OSCQuery answers are derived from the
// tree, and OSC messages are applied to it, so nothing is written twice.
//
// A node can be declared through a Parameter (see parameters.md), or filled in by hand (type, access, get, set) when the
// application already has its own way to read and write the value.
//
// Three kinds of leaf, the same in every application (a container holds them):
//   parameter  has a state: it is read, written, saved, and tells when it changes (a Parameter, or filled by hand)
//   message    has no state: it is received and does something (an action, a trigger, with or without arguments)
//   signal     has no state: the application emits it (a detection, a button pressed, a meter), nobody writes it
// OSCQuery says them with ACCESS (3 or 1 or 2 for a parameter, 2 for a message, 1 for a signal) and, for a message or a
// signal, the extension PXS_KIND ("message", "signal"); neither has a VALUE.
//
// The idea comes from Ossia's device tree. What was left out on purpose: bounding modes, units, repetition filters,
// tags, a callback list on every node. A write already tells the others through ChangeFeed.

#include "OscCodec.h"
#include "Parameter.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVariantList>
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace pxs {

class Tree;

enum class Kind { Container, Parameter, Message, Signal };

class Node {
public:
    const QString &name() const { return m_name; }
    QString path() const; // "/layer/1/opacity"; the root is "/"
    Node *parent() const { return m_parent; }
    Node *child(const QString &name) const;
    const std::map<QString, std::unique_ptr<Node>> &children() const { return m_children; }
    Parameter *parameter() const { return m_parameter.get(); } // when it was declared through a Parameter

    // What it is, as OSCQuery says it
    QString type;        // OSC type tags, one per argument ("f", "ff", "T"); empty for a container or an action without argument
    int access = 0;      // 0 none (a container), 1 read, 2 write, 3 both
    QString description;
    QJsonArray range;    // one entry per argument: {"MIN", "MAX"} or {"VALS"}
    QString clip;        // "both" when the range is enforced (CLIPMODE)
    std::function<QVariantList()> get;                 // the value, one entry per argument
    std::function<bool(const QVariantList &)> set;     // false: refused (a locked layer, a value that does not fit)
    // The application's own attributes (an extension of OSCQuery), asked with ?NAME; empty result: no value
    std::map<QString, std::function<QJsonValue()>> attributes;

    Kind kind() const { return isContainer() ? Kind::Container : m_kind; }
    bool isContainer() const { return !m_children.empty() || (type.isEmpty() && access == 0); }

private:
    friend class Tree;
    Kind m_kind = Kind::Parameter;
    QString m_name;
    Node *m_parent = nullptr;
    std::map<QString, std::unique_ptr<Node>> m_children;
    std::unique_ptr<Parameter> m_parameter;
};

class Tree {
public:
    Tree() = default;
    Node &root() { return m_root; }
    const Node &root() const { return m_root; }

    // The node at this address, with its containers made on the way
    Node &add(const QString &path);
    // The same, declared through a Parameter, which the tree owns: type, access, range, choices and the way to read and
    // write the value come from it
    Node &add(const QString &path, std::unique_ptr<Parameter> parameter);
    // A message: received, no state. `type` is the OSC type tags of its arguments ("N" when it takes none); the handler
    // gets the arguments and returns false to refuse them.
    Node &addMessage(const QString &path, const QString &type, std::function<bool(const QVariantList &)> handler,
                     const QString &description = {});
    // A signal: emitted by the application, no state. Say it with fire().
    Node &addSignal(const QString &path, const QString &type = QStringLiteral("N"), const QString &description = {});
    Node *find(const QString &path) const;
    bool remove(const QString &path);
    void clear();

    // Every node whose address matches an OSC address pattern (* ? [abc] {a,b}), containers included
    std::vector<Node *> match(const QString &pattern) const;

    // Hands an OSC message to every method its address names that can be written; returns how many accepted it
    int apply(const osc::Message &message) const;
    // The OSC message that says what a method holds now (the feedback to send)
    static osc::Message message(const Node &node);

    // Where everything the application says goes (the OSC output, the listeners): a signal emitted, a parameter that
    // changed. Set by whoever sends (the OSC/OSCQuery service); one at a time.
    void setOutput(std::function<void(const osc::Message &)> output) { m_output = std::move(output); }
    // Fires a signal (nothing is sent when the path is not a signal), with the arguments its type tags name
    bool fire(const QString &path, const QVariantList &args = {}) const;
    // Says that a parameter now holds another value (the feedback: its current message goes to the output)
    bool changed(const QString &path) const;

    // OSCQuery: the description of a node and of everything below it (FULL_PATH, CONTENTS, ACCESS, TYPE, VALUE, RANGE,
    // CLIPMODE, DESCRIPTION, and the node's own attributes); empty when there is no such node. The HTTP server, the
    // announcement (mDNS) and the push to listeners belong to the application.
    QJsonObject json(const QString &path = QStringLiteral("/")) const;
    // One attribute of a node (?VALUE, ?TYPE…): false when there is no such node; `out` is empty when the node has no
    // value for it (the HTTP answer is then 204)
    bool attribute(const QString &path, const QString &name, QJsonObject &out) const;

    // The /?HOST_INFO answer; `extensions` says which attributes are supported (the default lists the four that are)
    static QJsonObject hostInfo(const QString &name, int oscPort, const QJsonObject &extensions = defaultExtensions(),
                                const QJsonObject &more = {});
    static QJsonObject defaultExtensions();
    // A value as OSCQuery writes it, by the type tag of its argument
    static QJsonValue jsonValue(const QString &type, const QVariant &value, int argument);

private:
    Node m_root;
    std::function<void(const osc::Message &)> m_output;
};

// The OSC type tag of a parameter ("f", "i", "T" for a switch, "s", "N" for an action)
QString oscType(const ParamInfo &info);

} // namespace pxs
