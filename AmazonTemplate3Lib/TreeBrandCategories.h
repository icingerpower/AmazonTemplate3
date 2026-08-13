#ifndef TREEBRANDCATEGORIES_H
#define TREEBRANDCATEGORIES_H

#include <QAbstractItemModel>
#include <QSet>
#include <QStringList>

#include "apis/AmazonCatalogApi.h"

class TreeBrandCategories : public QAbstractItemModel
{
    Q_OBJECT

public:
    enum Column { ColName = 0, ColColumnCount };

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

    // Returns 0=brand, 1=category, 2=gender, 3=age, -1=invalid.
    static int depthOfIndex(const QModelIndex &index);

    QModelIndex index(int row, int column, const QModelIndex &parent) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int         rowCount(const QModelIndex &parent) const override;
    int         columnCount(const QModelIndex &parent) const override;
    QVariant    data(const QModelIndex &index, int role) const override;
    QVariant    headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    struct TreeNode {
        QString          name;
        QStringList      asins;      // all ASINs under this subtree (aggregated)
        QSet<QString>    colorKeys;  // unique color-group keys (color or asin-if-no-color)
        QList<TreeNode*> children;
        TreeNode*        parent = nullptr;

        explicit TreeNode(const QString &n = {}, TreeNode *p = nullptr)
            : name(n), parent(p) {}
        ~TreeNode() { qDeleteAll(children); }
    };

    TreeNode  m_root;

    TreeNode *_findOrCreate(TreeNode *parentNode, const QString &name);
};

#endif // TREEBRANDCATEGORIES_H
