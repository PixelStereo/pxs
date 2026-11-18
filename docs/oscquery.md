# The address space and OSCQuery

`pxs::tree` is a tree of named nodes. A node is a container, or it carries a `Parameter` (see `parameters.md`). Two things
are derived from it, so nothing is written twice: what an incoming OSC message changes, and what OSCQuery says.

```cpp
pxs::Tree tree;
auto opacity = std::make_unique<Parameter>("/layer/1/opacity", "Opacity", Parameter::Type::Float);
opacity->range(0, 1).bind(get, set);
tree.add("/layer/1/opacity", std::move(opacity)).description = "How opaque the layer is";

tree.apply({"/layer/*/opacity", "f", {0.5}});   // every node the pattern names, as its access and limits allow
tree.query();                                    // the OSCQuery JSON of the whole tree
pxs::Tree::message(*tree.find("/layer/1/enable")); // the OSC message that says what a parameter holds (the feedback)
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

`query(path)` gives `FULL_PATH`, `CONTENTS` (the children), and for a parameter `TYPE` (`f`, `i`, `T`, `s`, `N` for an
action), `ACCESS` (1 read, 2 write, 3 both), `RANGE` (`MIN` and `MAX`, or `VALS` for a choice), `VALUE` (when readable)
and `DESCRIPTION`. `hostInfo(name, port)` gives the `/?HOST_INFO` answer. The HTTP server and the announcement (mDNS)
belong to the application; the tree gives them their content.

## Applying a message

`apply()` writes a parameter when its access allows it and the argument fits its type: a number for `Float` and `Int` (an
`Int` is rounded), a switch for `Bool`, a key (or its index) for `Choice`, a string for `Text`, any message for a
`Trigger`. A value that does not fit is ignored, never half applied; the parameter keeps it within its limits.
