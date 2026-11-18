# The address space and OSCQuery

`pxs::tree` is a tree of named nodes. A node is a container, or it carries a `Parameter` (see `parameters.md`). Two things
are derived from it, so nothing is written twice: what an incoming OSC message changes, and what OSCQuery says.

```cpp
pxs::Tree tree;
auto opacity = std::make_unique<Parameter>("/layer/1/opacity", "Opacity", Parameter::Type::Float);
opacity->range(0, 1).bind(get, set);
tree.add("/layer/1/opacity", std::move(opacity)).description = "How opaque the layer is";

tree.apply({"/layer/*/opacity", "f", {0.5}});   // every node the pattern names, as its access and limits allow
tree.json();                                     // the OSCQuery JSON of the whole tree
tree.attribute("/layer/1/opacity", "VALUE", out);  // one attribute (?VALUE); out is empty when it has no value
pxs::Tree::message(*tree.find("/layer/1/enable")); // the OSC message that says what a parameter holds (the feedback)
```

## Three kinds of leaf, the same in every application

| Kind | State | Made with | OSCQuery |
|---|---|---|---|
| **parameter** | yes: read, written, saved, tells when it changes | `add(path, Parameter)`, or filled by hand | ACCESS 1 to 3, `VALUE`, `RANGE` |
| **message** (action) | none: received, does something | `addMessage(path, "i", handler)` | ACCESS 2, `TYPE`, `PXS_KIND: "message"`, no `VALUE` |
| **signal** (controller output) | none: the application emits it, nobody writes it | `addSignal(path, "f")`, then `fire(path, {args})` | ACCESS 1, `TYPE`, `PXS_KIND: "signal"`, no `VALUE` |

```cpp
tree.addMessage("/show/go", "i", [&](const QVariantList &a) { goTo(a.value(0).toInt()); return true; }, "Go to a cue");
tree.addSignal("/detect/peak", "f", "A peak was heard");
tree.setOutput([&](const osc::Message &m) { sendToClients(m); });  // set once, by the service that sends
tree.fire("/detect/peak", {0.8});     // a signal goes to the output
tree.changed("/layer/1/opacity");     // a parameter's new value goes to the output (the feedback)
```

A message with no argument has the type `N`. Everything an application says (a signal, a parameter's feedback) goes
through the same output, so a client that listens hears all three the same way. A `Trigger` parameter is a message.

A node can also be filled in by hand, for an application that already has its own way to read and write a value:

```cpp
auto &xy = tree.add("/layer/1/position");
xy.type = "ff"; xy.access = 3; xy.description = "Position";
xy.range = {...}; xy.get = [&] { return QVariantList{x, y}; }; xy.set = [&](const QVariantList &v) { ...; return true; };
xy.attributes["MYAPP_DEFAULT"] = [] { return QJsonValue(...); };   // an extension of OSCQuery, asked with ?MYAPP_DEFAULT
```

## What it takes from Ossia

Ossia (libossia) holds a device as a tree of nodes with a parameter on the leaves, and exposes it through protocols
(OSC, OSCQuery). That is kept: the tree is the single source, OSCQuery and OSC are views of it, an address is a path, a
pattern names several nodes.

## What it leaves out, on purpose

Ossia has more than these applications use, and a part of it is complexity that costs more than it gives:

- **Bounding modes** (clip, wrap, fold…): a parameter's limits keep the value within them, and `ramp` already says how a
  fade goes round an angle.
- **Units** (a colour as RGB or HSV, an angle in degrees or radians): a colour is its components
  (`colorComponent`, `pointComponent`), the label says the unit.
- **Repetition filters and callbacks on every node:** a write tells the others once, through `ChangeFeed`; the one
  listener (the feedback) reads what was named.
- **Tags, priorities, critical values.**

## The OSCQuery description

`json(path)` gives `FULL_PATH`, `CONTENTS` (the children), and for a parameter `TYPE` (`f`, `i`, `T`, `s`, `N` for an
action), `ACCESS` (1 read, 2 write, 3 both), `RANGE` (`MIN` and `MAX`, or `VALS` for a choice), `VALUE` (when readable)
and `DESCRIPTION`. `hostInfo(name, port, extensions)` gives the `/?HOST_INFO` answer (each application says which extensions it supports). The HTTP server and the announcement (mDNS)
belong to the application; the tree gives them their content.

## Applying a message

`apply()` writes a parameter when its access allows it and the argument fits its type: a number for `Float` and `Int` (an
`Int` is rounded), a switch for `Bool`, a key (or its index) for `Choice`, a string for `Text`, any message for a
`Trigger`. A value that does not fit is ignored, never half applied; the parameter keeps it within its limits.

## Making an application reachable: `pxs::oscquery` and `pxs::bind`

Every application is reachable the same way. `OscService` opens the UDP port (what arrives is applied to the tree), the
OSCQuery HTTP side and the mDNS announcement. What the application says goes back over UDP: a parameter's feedback
(`changed`) to the senders that wrote to it and to the targets, a signal (`fire`) to the targets only (`addTarget()`:
the application's own destinations), so that writing a parameter never subscribes anyone to a stream.

```cpp
pxs::Tree tree;
pxs::OscService service(&tree, "MyApp");
service.start(oscPort, httpPort);                 // 0: any free port
```

An interface that already has its widgets exposes them in one line each (`pxs/Bind.h`); writing from outside goes
through the widget, so it does what a click does, and a user's change tells the tree (the feedback):

```cpp
pxs::bindToggle(tree, "/osc/send", sendButton, "Send the signals");     // parameter
pxs::bindNumber(tree, "/detection/confidence", field, "Confidence");    // parameter, range from the field
pxs::bindChoice(tree, "/analysis/size", combo);                         // parameter, keys = item texts in lowercase
pxs::bindAction(tree, "/video/stop", stopButton);                       // message
tree.addSignal("/face/head/yaw", "f");                                  // signal, then tree.fire(...)
```

What an application exposes is its choice, but never less than what a user can do in the interface: every control is a
parameter or a message, every value the application computes for the outside is a signal.
