# Common architecture: the tree, the leaves, the mappings

This document fixes what every Pixel Stereo application has in common to expose and drive its settings. It serves as a
contract: an application, a parameter list, a menu or a mapping behaves the same way everywhere. Points marked **to be
validated** are waiting for an answer.

## Vocabulary

| Word | Meaning | In `pxs::tree` |
|---|---|---|
| **Node** | an element of the tree, with a name and a path (`/layer/1/opacity`) | `Node` |
| **Branch** | a node that contains other nodes, with no value or action | `Kind::Container` |
| **Leaf** | a node that contains nothing and does something | `Kind::Parameter`, `Action`, `Signal` |
| **Parameter** | a leaf **with a state**: it is read, written and saved; it says when it changes | `add(path, Parameter)`, or filled in by hand |
| **Action** | a leaf **without a state**: it is received and triggers something | `addAction` |
| **Signal** (controller) | a leaf **without a state**: the application emits it, nobody writes it; its value is not remembered | `addSignal`, `fire` |

A leaf has no children: a node that is given some becomes a branch.
A hardware controller (MIDI, sensor, incoming OSC) is a **signal**.

## The contract: what each kind of node does, the same everywhere

| | Branch | Parameter | Action | Signal |
|---|---|---|---|---|
| **OSC received** | no effect | writes the value (within its limits) | runs the action | refused |
| **OSC sent** | – | the feedback (`changed`) to whoever wrote and to the destinations | – | the stream (`fire`) to the destinations only |
| **OSCQuery** | `CONTENTS` | `TYPE`, `ACCESS` 1 to 3, `RANGE`, `VALUE` | `ACCESS` 2, no `VALUE` | `ACCESS` 1, no `VALUE` |
| **Saved** | no | yes, by address (`parametersToJson`) except `transient` | no | no |
| **Reset** | – | yes, if it has a default value | – | – |
| **Lock** | – | yes: no writes at all (OSC, mapping, interface) | yes | – |
| **Mapping source** | – | no (for now) | – | **yes** |
| **Mapping target** | – | **yes** | **yes** (triggered) | – |
| **Row in the panel** | a folder that can be collapsed | a field according to the type: checkbox, slider, menu, text | a button | an activity indicator |
| **Context menu** | – | Reset · Copy OSC address · Lock · Animate · Link to a signal | Copy OSC address · Lock | Copy OSC address |

A parameter's field comes from its type and its range; the application does not redraw it.
Actions remain buttons that **the application places itself**; there is no automatic action menu.

## Mapping: NyxMap's Interaction, for every application

Mapping already exists in NyxMap: it is the **Interaction** panel (its model is in `Interactions.h`), with MIDI learn. We
reinvent nothing: we **extract** this model into `pxs`, then every application, NyxMap included, uses it from `pxs`.

An **interaction** takes the value of a **source** (keyboard, MIDI, OSC, internal, audio; a controller is a signal),
passes it through a **filter chain** (Remap, Average, Attack Release, Damper, Curve, Math, Threshold, Toggle,
Envelope…), applies the master level, and sends it to the **target**, which receives it in its own units: a number, a
switch (from 0.5), a choice (by its index), an action (triggered when crossing 0.5).
- **Pick-up or jump** when the target has been moved by something else.
- **Action mode**: conditions on the source value, and what happens when they become true or false.
- Colors and XY points: each channel has its own chain.
- **Groups** to organize the list.
- Signals are never remembered; only the interaction is.

**Decisions made**
- **Storage**: `pxs` provides the JSON (`Interaction::toJson` / `fromJson`); **the application decides where it lives**
  (project or settings).
- **Lock**: it is **saved** along with what the application saves. `pxs` provides `lockedToJson` / `lockedFromJson`
  (the list of locked addresses).
- **Animate**: the `animatable` flag and the request (the context menu offers Animate; the application decides what to do
  with it: NyxMap's timeline, nothing elsewhere).

## What exists in NyxMap and moves into `pxs`

Your four ideas are in fact things NyxMap already has. The work is an **extraction**, in steps:

| Step | Content | State |
|---|---|---|
| 1 | the interaction model and filter engine (`Interactions.h`, `Anim.h`, graph): `pxs::interaction` | **done** (tests: filters, graph) |
| 2 | MIDI input (`Midi.h`, RtMidi) and MIDI learn | to do |
| 3 | the Interaction panel and the parameter panel, made generic over the tree | to do |
| 4 | presets (snapshots) by address | **done**: `pxs::Preset` (`capture`, `recall`), see below; NyxMap adopts it in step 6 |
| 5 | the server's `LISTEN` WebSocket | **done in `OscService`** (`LISTEN`/`IGNORE`, binary OSC, `PATH_CHANGED`, `PATH_ADDED`, `PATH_REMOVED`); NyxMap keeps its own until step 6 |
| 6 | NyxMap uses the `pxs` versions and removes its copies | to do, last |

MpOsc's per-signal filter (inversion, attack, release) becomes a `pxs` interaction (step 3 for its interface).

## What is already done, and what comes next

| | State |
|---|---|
| tree, three kinds of leaves, single output, OSCQuery | done (v0.13.0) |
| network service (UDP, HTTP, mDNS) | done (v0.13.0) |
| binding of existing widgets (`pxs::bind`) | done (v0.13.0) |
| label, lock, reset, observers | done, to be released (v0.14.0) |
| interaction model (extracted from NyxMap) | done, to be released |
| MIDI, learn, panels, presets, `LISTEN` | see the steps table |

## Presets by address: `pxs::Preset`

A preset holds parameter **values** (by address, in the format of OSCQuery's `VALUE`) and **actions to run** on recall,
with their arguments. Recall sets the values first, then runs the actions in order: the media is set to play, then
rewound. A locked parameter keeps its value, a locked action is not run, an address the tree no longer has is counted
without error (`RecallResult`).

- By default: every readable, writable and saved parameter (`Parameter::isStored`), never a `transient` parameter (a
  show state) nor an action. A selection of addresses narrows it.
- **Actions are chosen by the preset**, not "all of them": recalling a preset does not run `stop` or `go` everywhere.
- The format is JSON (`Preset::toJson` / `fromJson`); the application decides where it lives. A preset's fade, and what
  is not a parameter (animations, NyxMap's interactions), stay with the application.

**Rewinding is called `rewind`, everywhere**: the action leaf of a media, a shader, a slideshow, a timecode.
"Restart" does not exist: it is `play = true` then `rewind`, which a preset holds with both.

## Other ideas, not yet decided

1. **An "internal" flag** on a node, so that what only serves the interface is not exposed to the outside.
2. **Rate limiting** of the signals sent (for example at most 30 per second per address).
3. **Undo / redo** for changes made in the panel.
