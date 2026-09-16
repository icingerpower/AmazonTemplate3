#pragma once
#include <QAbstractItemView>
#include <QComboBox>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QSet>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QStylePainter>

// Checkbox popup stays open so several types can be selected in one visit.
// An empty selection means All; unavailable saved types remain selectable.
class ProductTypeSelector : public QComboBox {
    Q_OBJECT
  public:
    explicit ProductTypeSelector(QWidget *parent = nullptr)
        : QComboBox(parent), m_items(new QStandardItemModel(this)) {
        setModel(m_items);
        setMinimumContentsLength(18);
        setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        setToolTip(
            tr("Check one or more product types. All clears the restriction. Click outside to close."));
        view()->installEventFilter(this);
        view()->viewport()->installEventFilter(this);
        connect(m_items, &QStandardItemModel::itemChanged, this, [this](QStandardItem *item) {
            auto selected = m_selected;
            if (item->row() == 0)
                selected.clear();
            else if (item->checkState() == Qt::Checked)
                selected << item->text();
            else
                selected.removeAll(item->text());
            setSelectedValues(selected);
        });
        rebuild();
    }
    QStringList selectedValues() const {
        return m_selected;
    }
    void setSelectedValues(QStringList selected) {
        selected.removeAll(QString());
        selected.removeDuplicates();
        selected.sort();
        const bool changed = selected != m_selected;
        m_selected = selected;
        bool missing = false;
        for (const auto &value : selected)
            if (findText(value) < 0)
                missing = true;
        if (missing)
            rebuild();
        else
            syncChecks();
        update();
        if (changed)
            emit selectionChanged();
    }
    void setChoices(const QSet<QString> &choices) {
        m_choices = choices;
        rebuild();
    }
  signals:
    void selectionChanged();

  protected:
    void paintEvent(QPaintEvent *) override {
        QStylePainter painter(this);
        QStyleOptionComboBox option;
        initStyleOption(&option);
        const QString summary = m_selected.isEmpty() ? tr("All") : m_selected.join(" / ");
        option.currentText = fontMetrics().elidedText(summary, Qt::ElideRight, width() - 28);
        painter.drawComplexControl(QStyle::CC_ComboBox, option);
        painter.drawControl(QStyle::CE_ComboBoxLabel, option);
    }
    bool eventFilter(QObject *object, QEvent *event) override {
        if (object == view()->viewport()) {
            if (event->type() == QEvent::MouseButtonPress) {
                auto *mouse = static_cast<QMouseEvent *>(event);
                if (mouse->button() == Qt::LeftButton)
                    return true;
            }
            if (event->type() == QEvent::MouseButtonRelease) {
                auto *mouse = static_cast<QMouseEvent *>(event);
                if (mouse->button() == Qt::LeftButton) {
                    toggle(view()->indexAt(mouse->position().toPoint()));
                    return true;
                }
            }
        }
        if ((object == view() || object == view()->viewport()) && event->type() == QEvent::KeyPress) {
            const int key = static_cast<QKeyEvent *>(event)->key();
            if (key == Qt::Key_Space || key == Qt::Key_Return || key == Qt::Key_Enter) {
                toggle(view()->currentIndex());
                return true;
            }
        }
        return QComboBox::eventFilter(object, event);
    }

  private:
    void toggle(const QModelIndex &index) {
        if (!index.isValid())
            return;
        auto *item = m_items->item(index.row());
        item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
    }
    void syncChecks() {
        const QSignalBlocker blocker(m_items);
        for (int i = 0; i < m_items->rowCount(); ++i)
            m_items->item(i)->setCheckState(
                (i == 0 ? m_selected.isEmpty() : m_selected.contains(m_items->item(i)->text()))
                    ? Qt::Checked
                    : Qt::Unchecked);
    }
    void rebuild() {
        const QSignalBlocker blocker(m_items);
        m_items->clear();
        auto values = m_choices;
        for (const auto &selected : m_selected)
            values.insert(selected);
        values.remove(QString());
        auto sorted = values.values();
        sorted.sort();
        sorted.prepend(tr("All"));
        for (const auto &value : sorted) {
            auto *item = new QStandardItem(value);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
            m_items->appendRow(item);
        }
        syncChecks();
        setCurrentIndex(0);
        update();
    }
    QStandardItemModel *m_items;
    QSet<QString> m_choices;
    QStringList m_selected;
};
