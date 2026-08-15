#ifndef TREEBRANDCATEGORIES_H
#define TREEBRANDCATEGORIES_H

#include <QAbstractItemModel>
#include <QHash>
#include <QSet>
#include <QStringList>

#include "apis/AmazonCatalogApi.h"

class TreeBrandCategories : public QAbstractItemModel
{
    Q_OBJECT

public:
    enum Column { ColName = 0, ColEnglishName, ColColumnCount };

    explicit TreeBrandCategories(QObject *parent = nullptr);
    ~TreeBrandCategories() override;

    // customPaths: extra paths to ensure exist in the tree even if no items reside there.
    // Each entry is an ordered list of node names from root, e.g. {"Nike","Platform Heels"}.
    void setItems(const QList<AmazonCatalogApi::StoreItem> &items,
                  const QList<QStringList> &customPaths = {});
    void clear();

    // Returns all ASINs for the node at index (aggregated across children).
    QStringList asinsForIndex(const QModelIndex &index) const;

    // Returns the number of unique color groups under this node (what the tree displays).
    int colorCountForIndex(const QModelIndex &index) const;

    // Groups an item by (product family derived from its SKU) + color, so that
    // size variants of ONE product collapse together without merging different
    // products that happen to share a generic color word ("White", "Black"…).
    // Single source of truth for this grouping — used by this tree's own counts
    // AND by PaneStore's table-row grouping and Move/Remove ASIN-set expansion,
    // so they can never drift apart from each other again.
    static QString colorGroupKey(const AmazonCatalogApi::StoreItem &item);

    // Returns the raw node name (without count suffix).
    QString nodeNameForIndex(const QModelIndex &index) const;

    // Returns the node's "English name" (ColEnglishName), regardless of which
    // column index refers to — unlike nodeNameForIndex, callers don't need to
    // build a sibling index at the right column first.
    QString englishNameForIndex(const QModelIndex &index) const;

    // Full replace of the persisted English-name map, keyed by _pathKeyFor().
    // Must be called BEFORE the first setItems() so the freshly built nodes
    // pick up their names — setItems() looks up this map but does not reset it,
    // so English names survive every subsequent tree rebuild (merge/move/etc.)
    // without needing to be reloaded from disk each time.
    void setEnglishNames(const QHash<QString, QString> &names);
    // Current map (path key -> English name), for the owner to persist to disk.
    QHash<QString, QString> englishNames() const { return m_englishNames; }

    // Returns 0=brand, 1=category, 2=gender, 3=age, -1=invalid.
    static int depthOfIndex(const QModelIndex &index);

    QModelIndex index(int row, int column, const QModelIndex &parent) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int         rowCount(const QModelIndex &parent) const override;
    int         columnCount(const QModelIndex &parent) const override;
    QVariant    data(const QModelIndex &index, int role) const override;
    bool        setData(const QModelIndex &index, const QVariant &value, int role) override;
    QVariant    headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

private:
    struct TreeNode {
        QString          name;
        QString          englishName; // user-filled, persisted via m_englishNames
        QStringList      asins;      // all ASINs under this subtree (aggregated)
        QSet<QString>    colorKeys;  // unique color-group keys (color or asin-if-no-color)
        QList<TreeNode*> children;
        TreeNode*        parent = nullptr;

        explicit TreeNode(const QString &n = {}, TreeNode *p = nullptr)
            : name(n), parent(p) {}
        ~TreeNode() { qDeleteAll(children); }
    };

    TreeNode  m_root;
    // path key (root->node names joined by '\x1f') -> user-filled English name.
    // Kept alive across setItems() rebuilds — see setEnglishNames().
    QHash<QString, QString> m_englishNames;

    TreeNode *_findOrCreate(TreeNode *parentNode, const QString &name);
    QString   _pathKeyFor(const TreeNode *node) const;
};

#endif // TREEBRANDCATEGORIES_H
