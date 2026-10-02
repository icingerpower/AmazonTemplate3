#ifndef TREEDELETEDPRODUCTS_H
#define TREEDELETEDPRODUCTS_H

#include <QStandardItemModel>
#include "StorePlacements.h"

// Deleted originals only. Duplicate placements are deliberately never archived.
class TreeDeletedProducts : public QStandardItemModel
{
public:
    explicit TreeDeletedProducts(QObject *parent = nullptr);
    void record(const QList<StorePlacements::Item> &items);
    bool contains(const QString &asin) const;
    QList<StorePlacements::Item> items() const { return m_items; }
    void forget(const QSet<QString> &asins);
    void reset();
    QSet<QString> asinsForIndexes(const QModelIndexList &indexes) const;
private:
    QList<StorePlacements::Item> m_items;
    void rebuild();
};
#endif
