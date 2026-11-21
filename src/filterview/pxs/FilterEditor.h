#pragma once

#include <Interactions.h>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <functional>
#include <vector>

class QLabel;
class QListWidget;
class QMenu;
class QProgressBar;
class QPushButton;
class QToolButton;

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

signals:
  // `final`: the end of a change (a drag of a setting is not)
  void chainChanged(const pxs::filterview::FilterChain &chain, bool final);

protected:
  void resizeEvent(QResizeEvent *event) override;

private:
  void rebuild();
  void addCard(int index);
  void edited(bool final);
  void fitItems();
  void addFilter(InteractionFilter kind);
  int levelOf(double value) const;
  void showValue(QProgressBar *bar, bool available, double value);

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
};

} // namespace pxs::filterview
