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
//   action     has no state: it is received and does something (an action, a trigger, with or without arguments)
//   signal     has no state: the application emits it (a detection, a button pressed, a meter), nobody writes it
// OSCQuery says them with ACCESS and VALUE only, no extension: a parameter always has a VALUE that can be read (ACCESS 1 or
// 3); an action has ACCESS 2 and no VALUE; a signal has ACCESS 1 and no VALUE. So what nobody can read back is never a
// parameter: a setter without getter is declared as an action.
//
// The idea comes from Ossia's device tree. What was left out on purpose: bounding modes, units, repetition filters,
// tags, a callback list on every node. A write already tells the others through ChangeFeed.

#include "OscCodec.h"
#include "Parameter.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace pxs {

class Tree;

enum class Kind { Container, Parameter, Action, Signal };

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
    QString label;       // what an interface shows for it ("Opacity"); empty: its name
    bool locked = false; // refuses every write (OSC, a mapping, the panel) until unlocked; the application's own code is not stopped
    std::function<bool()> reset; // puts the default back (a parameter with a default; empty: nothing to reset)
    QJsonArray range;    // one entry per argument: {"MIN", "MAX"} or {"VALS"}
    QString clip;        // "both" when the range is enforced (CLIPMODE)
    std::function<QVariantList()> get;                 // the value, one entry per argument
    std::function<bool(const QVariantList &)> set;     // false: refused (a locked layer, a value that does not fit)
    // The application's own attributes (an extension of OSCQuery), asked with ?NAME; empty result: no value
    std::map<QString, std::function<QJsonValue()>> attributes;

    QString displayLabel() const { return label.isEmpty() ? m_name : label; }
    bool animatable() const { return m_parameter && m_parameter->info().animatable; } // a timeline can drive it
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
    // An action: received, no state. `type` is the OSC type tags of its arguments ("N" when it takes none); the handler
    // gets the arguments and returns false to refuse them.
    Node &addAction(const QString &path, const QString &type, std::function<bool(const QVariantList &)> handler,
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
    // Told when the structure changes: a node was made (the path of the highest one made), a node was removed (its own path), the
    // tree was cleared ("/"). One at a time, set by whoever tells the clients (OSCQuery's PATH_ADDED, PATH_REMOVED).
    enum class Change { Added, Removed, Cleared };
    void setStructureObserver(std::function<void(const QString &, Change)> observer) { m_structure = std::move(observer); }
    // Who else wants to know, inside the application (a panel that shows the values, the mappings): called when a parameter
    // was written (OSC, a mapping, the panel) or told changed, a signal was fired, a node was locked. Returns an id for
    // removeObserver(). Called on the thread that wrote.
    using Observer = std::function<void(const Node &, const QVariantList &)>; // the value fired, or the one now held
    // A write that goes through apply() and says changed() itself (a bound widget) notifies once.
    int addObserver(Observer observer);
    void removeObserver(int id);
    bool setLocked(const QString &path, bool locked); // false: no such node
    // The locks are saved with what the application saves: the addresses of the locked nodes. A lock read before its node
    // exists (the node is made later) is kept and applied when it is added.
    QJsonArray lockedToJson() const;
    void lockedFromJson(const QJsonArray &paths);
    bool reset(const QString &path) const;            // false: no such node, locked, or nothing to reset
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
    void notify(const Node &node, const QVariantList &args = {}) const;
    QJsonObject describe(const Node &node, bool contents) const; // the OSCQuery object of a node (and its subtree)
    Node m_root;
    QStringList m_pendingLocks;
    std::function<void(const osc::Message &)> m_output;
    std::function<void(const QString &, Change)> m_structure;
    // Copy on write: a fired signal (thousands a second) takes one reference, not a copy of the list
    std::shared_ptr<const std::vector<std::pair<int, Observer>>> m_observers;
    int m_nextObserver = 1;
    mutable const Node *m_applying = nullptr; // the node apply() is writing (a thread at a time: the GUI thread)
    mutable bool m_applyNotified = false;
};

// The OSC type tag of a parameter ("f", "i", "T" for a switch, "s", "N" for an action)
QString oscType(const ParamInfo &info);

} // namespace pxs
