#pragma once

#include <Interactions.h>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <cmath>
#include <functional>
#include <vector>

class QLabel;
class QListWidget;
class QMenu;
class QProgressBar;
class QPushButton;
class QToolButton;

namespace pxs::curveview {
class CurveLane;
}

namespace pxs::filterview {

class SignalMeter;
using FilterChain = std::vector<InteractionStage>;

// The mini documentation of a filter, to choose one: its group (Smooth, Time, Trigger, Shape), what it does in a line, and when to use it
struct FilterInfo {
  QString group, summary, detail;
};
FilterInfo filterInfo(InteractionFilter kind);
QStringList filterGroups(); // in the order the menus list them

// The filters in a menu: by group, each with what it does in a line and, in its tooltip, when to use it
void addFilterActions(QMenu *menu, const std::vector<InteractionFilter> &kinds, const std::function<void(InteractionFilter)> &chosen);
// A menu made here is handed to this function first: an application shows the tooltips of menu entries in its status bar there
void setMenuWatcher(std::function<void(QMenu *)> watcher);

// What the editor needs to know about the signal it edits
struct Signal {
  QString title;                                  // shown over the bars (an address, a name)
  QString tooltip;                                // what kind of signal it is
  double lo = 0, hi = 1;                          // the range of its bars
  std::function<double(double)> sent;             // what a value becomes when it is sent: a switch is 0 or 1, a count is rounded (none: as is)
  std::function<QString(double)> text;            // a value as text, in the bars
  std::vector<InteractionFilter> offered;         // the filters worth adding to it (empty: all of them)
  // An application can attach to each setting of a card (the key: a, b, c, d): a preset, a link… (the card's number, the name's label and the field)
  std::function<void(int index, const QString &key, QWidget *label, QWidget *field)> decorate;
  // And add something under the settings of a card (it takes the widget)
  std::function<QWidget *(int index, const InteractionStage &stage)> extra;
  double targetLo = NAN, targetHi = NAN;          // what a Remap writes to (default: the range of the signal)
  FilterChain invert;                             // what "Invert" adds, for this signal (empty: no Invert in the menu)
};

// The chain of filters of one signal. A bar over the first filter shows the signal as it comes in and one under the last shows it
// as it goes out, with the incoming one marked on it. "Add filter" puts one at the end, a filter is folded to its name, its settings
// follow, and a card dragged by its handle goes to another place. Every change is told to whoever holds the chain.
class FilterEditor : public QWidget {
  Q_OBJECT
public:
  explicit FilterEditor(QWidget *parent = nullptr);

  // Shows the chain of a signal; clearSignal(): nothing selected
  void setSignal(const Signal &signal, const FilterChain &chain);
  void clearSignal();
  bool hasSignal() const { return active; }
  const FilterChain &chain() const { return filters; }
  // The live value of the signal as it comes in, and after each filter of its chain (`available` false: the signal is not there)
  void setTrace(bool available, double input, const std::vector<double> &afterEach);
  // Changes the chain from outside, as an edit (the signals tell it): `merge` joins a drag's steps into one undo step
  void editStage(int index, const std::function<void(InteractionStage &)> &fn, const QString &text, const QString &merge = {}, bool final = true);
  void renameFilter(int index, const QString &name);
  // The range of the signal after the first `count` filters: a Remap writes its Out range, a Clamp its ends, a curve or a threshold 0 to 1
  static std::pair<double, double> spanAfter(const FilterChain &chain, double lo, double hi, size_t count);

signals:
  // `final`: the end of a change (a drag of a setting is not)
  void chainChanged(const pxs::filterview::FilterChain &chain, bool final);
  // The same, with what was done ("Move Filter", "Edit Curve"…) and a key that joins the steps of a drag: for an undo stack
  void chainEdited(const pxs::filterview::FilterChain &chain, const QString &text, const QString &merge);

protected:
  void resizeEvent(QResizeEvent *event) override;

private:
  void rebuild();
  void addCard(int index);
  void edited(bool final, const QString &text = QStringLiteral("Edit Filters"), const QString &merge = {});
  void updateSpan();
  std::pair<double, double> spanBefore(int index) const;
  void fitItems();
  void addFilter(InteractionFilter kind);
  int levelOf(double value, bool afterFilters = false) const;
  void showValue(QProgressBar *bar, bool available, double value, bool afterFilters = false);

  Signal spec;
  bool active = false;
  FilterChain filters;
  std::vector<bool> open; // which cards are unfolded, in the order of the chain
  QLabel *title;
  QProgressBar *input;
  SignalMeter *output; // after the last filter: what is sent, with the incoming signal marked on it
  QPushButton *add;
  QToolButton *clear;
  QListWidget *list;
  bool rebuilding = false;
  double outLo = 0, outHi = 1; // the range of the bar after the filters
  std::vector<std::pair<int, curveview::CurveLane *>> curveLanes; // a Curve card's editor, with its place in the chain
};

} // namespace pxs::filterview
