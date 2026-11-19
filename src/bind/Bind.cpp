#include <pxs/Bind.h>

#include "Fields.h"

#include <QAbstractButton>
#include <QComboBox>
#include <QLineEdit>
#include <QPointer>
#include <memory>
#include <optional>

namespace pxs {

namespace {
QString leaf(const QString &path) { return path.section('/', -1); }

// A locked node cannot be written from anywhere: its widget is disabled while it is locked, and given back as it was
void followLock(Tree &tree, const QString &path, QWidget *widget)
{
    auto wasEnabled = std::make_shared<std::optional<bool>>();
    QPointer<QWidget> w(widget);
    const int id = tree.addObserver([path, w, wasEnabled](const Node &node, const QVariantList &) {
        if (!w || node.path() != path) return;
        if (node.locked && !wasEnabled->has_value()) {
            *wasEnabled = w->isEnabled();
            w->setEnabled(false);
        } else if (!node.locked && wasEnabled->has_value()) {
            w->setEnabled(**wasEnabled);
            wasEnabled->reset();
        }
    });
    QObject::connect(widget, &QObject::destroyed, [&tree, id] { tree.removeObserver(id); });
}
}

Node &bindToggle(Tree &tree, const QString &path, QAbstractButton *button, const QString &description)
{
    QPointer<QAbstractButton> w(button);
    auto p = std::make_unique<Parameter>(leaf(path), leaf(path), Parameter::Type::Bool);
    p->byDefault(button->isChecked()); // what it was when exposed
    p->bind([w] { return QVariant(w && w->isChecked()); },
            [w](const QVariant &v) { if (w && w->isCheckable() && w->isEnabled()) w->setChecked(v.toBool()); });
    Node &node = tree.add(path, std::move(p));
    node.description = description;
    followLock(tree, path, button);
    QObject::connect(button, &QAbstractButton::toggled, button, [&tree, path] { tree.changed(path); });
    return node;
}

Node &bindNumber(Tree &tree, const QString &path, SliderField *field, const QString &description)
{
    QPointer<SliderField> w(field);
    const bool whole = field->decimals() == 0;
    auto p = std::make_unique<Parameter>(leaf(path), leaf(path), whole ? Parameter::Type::Int : Parameter::Type::Float);
    p->range(field->minimum(), field->maximum()).byDefault(field->value());
    p->bind([w, whole] { return w ? (whole ? QVariant(int(w->value())) : QVariant(w->value())) : QVariant(); },
            [w](const QVariant &v) {
                if (!w || !w->isEnabled()) return;
                w->setValue(v.toDouble());
                // What the user's own edit does: the application listens to this signal
                QMetaObject::invokeMethod(w, "editingFinished", Qt::DirectConnection, Q_ARG(double, w->value()));
            });
    Node &node = tree.add(path, std::move(p));
    node.description = description;
    followLock(tree, path, field);
    QObject::connect(field, &SliderField::editingFinished, field, [&tree, path] { tree.changed(path); });
    return node;
}

Node &bindChoice(Tree &tree, const QString &path, QComboBox *combo, const QString &description)
{
    QPointer<QComboBox> w(combo);
    QStringList keys;
    for (int i = 0; i < combo->count(); ++i) keys << combo->itemText(i).toLower(); // a Choice's keys are lowercase
    auto p = std::make_unique<Parameter>(leaf(path), leaf(path), Parameter::Type::Choice);
    p->choices(keys).byDefault(combo->currentText().toLower());
    p->bind([w] { return w ? QVariant(w->currentText().toLower()) : QVariant(); },
            [w](const QVariant &v) {
                if (!w || !w->isEnabled()) return;
                const int i = w->findText(v.toString(), Qt::MatchFixedString); // case does not matter
                if (i >= 0) w->setCurrentIndex(i);
            });
    Node &node = tree.add(path, std::move(p));
    node.description = description;
    followLock(tree, path, combo);
    QObject::connect(combo, &QComboBox::currentIndexChanged, combo, [&tree, path] { tree.changed(path); });
    return node;
}

Node &bindText(Tree &tree, const QString &path, QLineEdit *edit, const QString &description)
{
    QPointer<QLineEdit> w(edit);
    auto p = std::make_unique<Parameter>(leaf(path), leaf(path), Parameter::Type::Text);
    p->byDefault(edit->text()).bind([w] { return w ? QVariant(w->text()) : QVariant(); },
            [w](const QVariant &v) {
                if (!w || !w->isEnabled()) return;
                w->setText(v.toString());
                QMetaObject::invokeMethod(w, "editingFinished", Qt::DirectConnection);
            });
    Node &node = tree.add(path, std::move(p));
    node.description = description;
    followLock(tree, path, edit);
    QObject::connect(edit, &QLineEdit::editingFinished, edit, [&tree, path] { tree.changed(path); });
    return node;
}

Node &bindAction(Tree &tree, const QString &path, QAbstractButton *button, const QString &description)
{
    QPointer<QAbstractButton> w(button);
    return tree.addAction(path, "N", [w](const QVariantList &) {
        if (!w || !w->isEnabled()) return false;
        w->click();
        return true;
    }, description);
}

} // namespace pxs
