#include "ViewLayersCombo.h"

#include <QAbstractItemView>
#include <QEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QStandardItemModel>

namespace {

// The key each row carries, so the caller names its layers rather than
// counting rows.
constexpr int kKeyRole = Qt::UserRole + 1;

} // namespace

ViewLayersCombo::ViewLayersCombo(QWidget *parent)
    : QComboBox(parent)
{
    setModel(new QStandardItemModel(this));
    setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    setMinimumContentsLength(10);
    // Nothing is ever "current": the text is a summary, written by hand.
    setCurrentIndex(-1);
    view()->viewport()->installEventFilter(this);
    refreshText();
}

void ViewLayersCombo::addLayer(const QString &key, const QString &label, bool on,
                               const QString &tooltip)
{
    auto *model = qobject_cast<QStandardItemModel *>(this->model());
    if (!model)
        return;
    auto *item = new QStandardItem(label);
    item->setData(key, kKeyRole);
    item->setData(tooltip, Qt::ToolTipRole);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
    item->setCheckState(on ? Qt::Checked : Qt::Unchecked);
    model->appendRow(item);
    setCurrentIndex(-1);
    refreshText();
}

int ViewLayersCombo::rowOf(const QString &key) const
{
    for (int row = 0; row < count(); ++row) {
        if (itemData(row, kKeyRole).toString() == key)
            return row;
    }
    return -1;
}

bool ViewLayersCombo::isOn(const QString &key) const
{
    const int row = rowOf(key);
    return row >= 0 && itemData(row, Qt::CheckStateRole).toInt() == Qt::Checked;
}

void ViewLayersCombo::setOn(const QString &key, bool on)
{
    const int row = rowOf(key);
    if (row < 0 || isOn(key) == on)
        return;
    setItemData(row, on ? Qt::Checked : Qt::Unchecked, Qt::CheckStateRole);
    setCurrentIndex(-1);
    refreshText();
}

void ViewLayersCombo::toggleRow(int row)
{
    if (row < 0 || row >= count())
        return;
    const bool on = itemData(row, Qt::CheckStateRole).toInt() != Qt::Checked;
    setItemData(row, on ? Qt::Checked : Qt::Unchecked, Qt::CheckStateRole);
    setCurrentIndex(-1);
    refreshText();
    emit layerToggled(itemData(row, kKeyRole).toString(), on);
}

bool ViewLayersCombo::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == view()->viewport() && event->type() == QEvent::MouseButtonRelease) {
        const auto *click = static_cast<QMouseEvent *>(event);
        const QModelIndex index = view()->indexAt(click->position().toPoint());
        if (index.isValid()) {
            toggleRow(index.row());
            // Swallowed, so the list stays open for the next one.
            return true;
        }
    }
    return QComboBox::eventFilter(watched, event);
}

void ViewLayersCombo::refreshText()
{
    QStringList on;
    for (int row = 0; row < count(); ++row) {
        if (itemData(row, Qt::CheckStateRole).toInt() == Qt::Checked)
            on << itemText(row);
    }
    // What is on, or how many -- the list is longer than the box on a toolbar,
    // so past a couple of entries it counts instead of naming.
    const QString summary = on.isEmpty()      ? tr("View: nothing")
                            : on.size() == count() ? tr("View: everything")
                            : on.size() <= 2  ? tr("View: %1").arg(on.join(QStringLiteral(", ")))
                                              : tr("View: %1 of %2").arg(on.size()).arg(count());
    setPlaceholderText(summary);
    setToolTip(tr("What the canvas draws: %1").arg(on.isEmpty()
                                                       ? tr("nothing")
                                                       : on.join(QStringLiteral(", "))));
    update();
}
