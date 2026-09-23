#ifndef STOREPLACEMENTS_H
#define STOREPLACEMENTS_H

#include "apis/AmazonCatalogApi.h"
#include <QJsonArray>

// Local category membership, independent of catalog data. An original placement
// follows the item's path; extra placements retain their own path on refresh.
class StorePlacements
{
public:
    using Item = AmazonCatalogApi::StoreItem;
    struct Placement {
        QStringList path;
        bool duplicate = false;
    };

    static QStringList path(const Item &item);
    static bool within(const QStringList &path, const QStringList &prefix);
    QList<Placement> placements(const Item &item) const;
    QList<Item> treeItems(const QList<Item> &items) const;
    bool isDuplicate(const Item &item, const QStringList &scope) const;

    // At aggregate nodes, act on all placements inside that node only.
    void transfer(QList<Item> &items, const QSet<QString> &asins,
                  const QStringList &source, const QStringList &destination, bool duplicate);
    void remove(QList<Item> &items, const QSet<QString> &asins, const QStringList &scope);
    // Original placements retain the existing 'unknown' reassignment behavior;
    // extra placements in the removed subtree disappear.
    void removeCategory(QList<Item> &items, const QStringList &scope);

    void clear() { m_overrides.clear(); }
    bool hasOverride(const QString &asin) const { return m_overrides.contains(asin); }
    QJsonArray toJson(const Item &item) const;
    void load(const QString &asin, const QJsonArray &array);

private:
    QHash<QString, QList<Placement>> m_overrides;
    static void setPath(Item &item, const QStringList &path);
    static QList<Placement> unique(const QList<Placement> &placements);
};

#endif
