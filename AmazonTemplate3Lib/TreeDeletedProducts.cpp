#include "TreeDeletedProducts.h"
#include <functional>

TreeDeletedProducts::TreeDeletedProducts(QObject *parent) : QStandardItemModel(parent)
{
    rebuild();
}

bool TreeDeletedProducts::contains(const QString &asin) const
{
    for (const auto &item : m_items) if (item.asin == asin) return true;
    return false;
}

void TreeDeletedProducts::record(const QList<StorePlacements::Item> &items)
{
    QSet<QString> known;
    for (const auto &item : m_items) known.insert(item.asin);
    for (const auto &item : items) {
        if (item.asin.isEmpty() || known.contains(item.asin)) continue;
        m_items.append(item);
        known.insert(item.asin);
    }
    rebuild();
}

void TreeDeletedProducts::forget(const QSet<QString> &asins)
{
    m_items.removeIf([&](const auto &item) { return asins.contains(item.asin); });
    rebuild();
}

void TreeDeletedProducts::reset()
{
    m_items.clear();
    rebuild();
}

void TreeDeletedProducts::rebuild()
{
    clear();
    setHorizontalHeaderLabels({tr("Category / product"), tr("ASIN"), tr("SKU")});
    for (const auto &item : m_items) {
        auto *parent = invisibleRootItem();
        for (const auto &part : StorePlacements::path(item)) {
            const QString name = part.isEmpty() ? tr("(unknown)") : part;
            QStandardItem *child = nullptr;
            for (int row = 0; row < parent->rowCount(); ++row)
                if (parent->child(row)->text() == name) { child = parent->child(row); break; }
            if (!child) {
                child = new QStandardItem(name);
                parent->appendRow(child);
            }
            parent = child;
        }
        auto *product = new QStandardItem(item.title);
        product->setData(item.asin, Qt::UserRole);
        parent->appendRow({product, new QStandardItem(item.asin), new QStandardItem(item.sku)});
    }
}

QSet<QString> TreeDeletedProducts::asinsForIndexes(const QModelIndexList &indexes) const
{
    QSet<QString> result;
    std::function<void(QModelIndex)> visit = [&](QModelIndex index) {
        const QString asin = index.data(Qt::UserRole).toString();
        if (!asin.isEmpty()) result.insert(asin);
        for (int row = 0; row < rowCount(index); ++row) visit(this->index(row, 0, index));
    };
    for (const auto &index : indexes) if (index.isValid()) visit(index.siblingAtColumn(0));
    return result;
}
