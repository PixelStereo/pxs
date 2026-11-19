#pragma once

// A preset by address: the values of some parameters of a Tree, and some actions to do after them. What an application
// keeps as a snapshot, a cue or a scene is this, whatever its parameters are: the tree says which ones there are.
//
// A parameter is held by its value (the VALUE OSCQuery would say). An action is held as "do this when the preset is
// recalled", with its arguments if it has some (Rewind, Start, Go to a cue): an action has no state, so a preset that
// holds one is what makes it part of a snapshot. Which actions a preset holds is its own choice: recalling a preset does
// not do every action of the application, only the ones it was given.
//
// Recalling puts the values first, then does the actions in their order, as a person would: the media is set to play, then
// rewound. A locked parameter keeps its value; an address the tree no longer has is counted, not an error.

#include "Tree.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace pxs {

struct Preset {
    struct Action {
        QString address;
        QJsonArray args; // in OSCQuery's way: numbers, strings, booleans (nested for an array); empty: none
    };

    QString name;
    QJsonObject values;   // address -> its VALUE (an array, one entry per argument)
    QList<Action> actions; // done after the values, in this order

    bool isEmpty() const { return values.isEmpty() && actions.isEmpty(); }
    QJsonObject toJson() const;
    static Preset fromJson(const QJsonObject &o);
};

// What a recall did
struct RecallResult {
    int applied = 0; // values written
    int actions = 0; // actions done
    int locked = 0;  // values and actions refused: the node is locked
    int missing = 0; // addresses the tree does not have (or that cannot be written)
};

// Holds the parameters at these addresses (empty: every parameter that is saved, see Parameter::isStored, and every node filled
// in by hand that is read and written), and these actions
Preset capture(const Tree &tree, const QStringList &only = {}, const QList<Preset::Action> &actions = {});
RecallResult recall(Tree &tree, const Preset &preset);

} // namespace pxs
