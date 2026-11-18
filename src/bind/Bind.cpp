#include <pxs/Bind.h>

#include "Fields.h"

#include <QAbstractButton>
#include <QComboBox>
#include <QLineEdit>
#include <QPointer>

namespace pxs {

namespace {
QString leaf(const QString &path) { return path.section('/', -1); }
}

Node &bindToggle(Tree &tree, const QString &path, QAbstractButton *button, const QString &description)
{
    QPointer<QAbstractButton> w(button);
    auto p = std::make_unique<Parameter>(leaf(path), leaf(path), Parameter::Type::Bool);
    p->bind([w] { return QVariant(w && w->isChecked()); },
            [w](const QVariant &v) { if (w && w->isCheckable() && w->isEnabled()) w->setChecked(v.toBool()); });
    Node &node = tree.add(path, std::move(p));
    node.description = description;
    QObject::connect(button, &QAbstractButton::toggled, button, [&tree, path] { tree.changed(path); });
    return node;
}

Node &bindNumber(Tree &tree, const QString &path, SliderField *field, const QString &description)
{
    QPointer<SliderField> w(field);
    const bool whole = field->decimals() == 0;
    auto p = std::make_unique<Parameter>(leaf(path), leaf(path), whole ? Parameter::Type::Int : Parameter::Type::Float);
    p->range(field->minimum(), field->maximum());
    p->bind([w, whole] { return w ? (whole ? QVariant(int(w->value())) : QVariant(w->value())) : QVariant(); },
            [w](const QVariant &v) {
                if (!w || !w->isEnabled()) return;
                w->setValue(v.toDouble());
                // What the user's own edit does: the application listens to this signal
                QMetaObject::invokeMethod(w, "editingFinished", Qt::DirectConnection, Q_ARG(double, w->value()));
            });
    Node &node = tree.add(path, std::move(p));
    node.description = description;
    QObject::connect(field, &SliderField::editingFinished, field, [&tree, path] { tree.changed(path); });
    return node;
}

Node &bindChoice(Tree &tree, const QString &path, QComboBox *combo, const QString &description)
{
    QPointer<QComboBox> w(combo);
    QStringList keys;
    for (int i = 0; i < combo->count(); ++i) keys << combo->itemText(i).toLower(); // a Choice's keys are lowercase
    auto p = std::make_unique<Parameter>(leaf(path), leaf(path), Parameter::Type::Choice);
    p->choices(keys);
    p->bind([w] { return w ? QVariant(w->currentText().toLower()) : QVariant(); },
            [w](const QVariant &v) {
                if (!w || !w->isEnabled()) return;
                const int i = w->findText(v.toString(), Qt::MatchFixedString); // case does not matter
                if (i >= 0) w->setCurrentIndex(i);
            });
    Node &node = tree.add(path, std::move(p));
    node.description = description;
    QObject::connect(combo, &QComboBox::currentIndexChanged, combo, [&tree, path] { tree.changed(path); });
    return node;
}

Node &bindText(Tree &tree, const QString &path, QLineEdit *edit, const QString &description)
{
    QPointer<QLineEdit> w(edit);
    auto p = std::make_unique<Parameter>(leaf(path), leaf(path), Parameter::Type::Text);
    p->bind([w] { return w ? QVariant(w->text()) : QVariant(); },
            [w](const QVariant &v) {
                if (!w || !w->isEnabled()) return;
                w->setText(v.toString());
                QMetaObject::invokeMethod(w, "editingFinished", Qt::DirectConnection);
            });
    Node &node = tree.add(path, std::move(p));
    node.description = description;
    QObject::connect(edit, &QLineEdit::editingFinished, edit, [&tree, path] { tree.changed(path); });
    return node;
}

Node &bindAction(Tree &tree, const QString &path, QAbstractButton *button, const QString &description)
{
    QPointer<QAbstractButton> w(button);
    return tree.addMessage(path, "N", [w](const QVariantList &) {
        if (!w || !w->isEnabled()) return false;
        w->click();
        return true;
    }, description);
}

} // namespace pxs
