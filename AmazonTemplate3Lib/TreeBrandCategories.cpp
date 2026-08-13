#include "TreeBrandCategories.h"

#include <QRegularExpression>

TreeBrandCategories::TreeBrandCategories(QObject *parent)
    : QAbstractItemModel(parent)
{
}

TreeBrandCategories::~TreeBrandCategories() = default;

QString TreeBrandCategories::colorGroupKey(const AmazonCatalogApi::StoreItem &item)
{
    // Derive a per-product "family" key from the SKU so that size/color variants
    // of the SAME product collapse together, without merging unrelated products
    // that happen to share a generic color word (many different robes are all
    // "White", for instance). Two SKU conventions are in use across the catalog:
    //   - "<FAMILY>-<COLOR>-<SIZE>" (color/size spelled out, hyphen-separated)
    //   - "<FAMILY><NN><LL>" (opaque 2-digit + 2-letter dropship variant code)
    // Strip the dropship code first (it never collides with hyphen-separated SKUs),
    // then strip a trailing "-<size>" / "-<color>" if literally present, so what's
    // left identifies the product regardless of which convention it follows.
    static const QRegularExpression dropshipSuffixRe(QStringLiteral(R"(\d{2}[A-Za-z]{2}$)"));
    auto stripTrailingLiteral = [](QString s, const QString &value) {
        if (value.isEmpty()) return s;
        const QRegularExpression re(QStringLiteral("[-_]") + QRegularExpression::escape(value) + QLatin1Char('$'),
                                     QRegularExpression::CaseInsensitiveOption);
        s.remove(re);
        return s;
    };
    // Fallback for SKUs whose embedded size token doesn't literally match
    // sizeValue (e.g. SKU says "-M"/"-XL" while sizeValue is the EU size number
    // "10"/"14") — strip the trailing hyphen segment positionally when it's
    // unambiguously size-shaped, so these still collapse to one family+color.
    static const QRegularExpression sizeTokenRe(
        QStringLiteral(R"(^(?:\d{1,3}|X{0,3}S|[SML]|\d{0,2}X{1,3}L|ONE ?SIZE|OS)$)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression trailingSegmentRe(QStringLiteral(R"([-_]([^-_]+)$)"));

    QString fam = item.sku;
    fam.remove(dropshipSuffixRe);
    const QString beforeSizeStrip = fam;
    fam = stripTrailingLiteral(fam, item.sizeValue);
    if (fam == beforeSizeStrip) {
        const auto m = trailingSegmentRe.match(fam);
        if (m.hasMatch() && sizeTokenRe.match(m.captured(1)).hasMatch())
            fam.chop(m.captured(1).size() + 1);
    }
    fam = stripTrailingLiteral(fam, item.color);
    while (fam.endsWith(QLatin1Char('-')) || fam.endsWith(QLatin1Char('_')))
        fam.chop(1);
    if (fam.isEmpty()) fam = item.sku;

    const QString colorPart = item.color.isEmpty() ? item.asin : item.color;
    return fam + QLatin1Char('\x1f') + colorPart;
}

void TreeBrandCategories::setItems(const QList<AmazonCatalogApi::StoreItem> &items,
                                    const QList<QStringList> &customPaths)
{
    beginResetModel();
    qDeleteAll(m_root.children);
    m_root.children.clear();

    for (const AmazonCatalogApi::StoreItem &item : items) {
        const QString brand    = item.brand.isEmpty()    ? tr("(unknown brand)")    : item.brand;
        const QString category = item.category.isEmpty() ? tr("(unknown category)") : item.category;
        const QString gender   = item.gender.isEmpty()   ? tr("(unknown gender)")   : item.gender;
        const QString age      = item.age.isEmpty()      ? tr("(unknown age)")      : item.age;

        const QString colorKey = colorGroupKey(item);

        TreeNode *brandNode    = _findOrCreate(&m_root,      brand);
        TreeNode *categoryNode = _findOrCreate(brandNode,    category);
        TreeNode *genderNode   = _findOrCreate(categoryNode, gender);
        TreeNode *ageNode      = _findOrCreate(genderNode,   age);

        // Propagate ASIN and color key upward so every level aggregates its subtree.
        ageNode->asins.append(item.asin);      ageNode->colorKeys.insert(colorKey);
        genderNode->asins.append(item.asin);   genderNode->colorKeys.insert(colorKey);
        categoryNode->asins.append(item.asin); categoryNode->colorKeys.insert(colorKey);
        brandNode->asins.append(item.asin);    brandNode->colorKeys.insert(colorKey);
    }

    // Ensure custom paths exist even if no items currently reside there.
    for (const QStringList &path : customPaths) {
        TreeNode *node = &m_root;
        for (const QString &name : path)
            node = _findOrCreate(node, name);
    }

    endResetModel();
}

void TreeBrandCategories::clear()
{
    beginResetModel();
    qDeleteAll(m_root.children);
    m_root.children.clear();
    endResetModel();
}

QStringList TreeBrandCategories::asinsForIndex(const QModelIndex &index) const
{
    if (!index.isValid()) return {};
    return static_cast<const TreeNode *>(index.internalPointer())->asins;
}

int TreeBrandCategories::colorCountForIndex(const QModelIndex &index) const
{
    if (!index.isValid()) return 0;
    return static_cast<const TreeNode *>(index.internalPointer())->colorKeys.size();
}

QString TreeBrandCategories::nodeNameForIndex(const QModelIndex &index) const
{
    if (!index.isValid()) return {};
    return static_cast<const TreeNode *>(index.internalPointer())->name;
}

int TreeBrandCategories::depthOfIndex(const QModelIndex &index)
{
    int depth = 0;
    QModelIndex p = index.parent();
    while (p.isValid()) { ++depth; p = p.parent(); }
    return depth;
}

QModelIndex TreeBrandCategories::index(int row, int col, const QModelIndex &parent) const
{
    if (!hasIndex(row, col, parent)) return {};

    const TreeNode *parentNode = parent.isValid()
        ? static_cast<const TreeNode *>(parent.internalPointer())
        : &m_root;

    if (row >= parentNode->children.size()) return {};
    return createIndex(row, col, parentNode->children.at(row));
}

QModelIndex TreeBrandCategories::parent(const QModelIndex &child) const
{
    if (!child.isValid()) return {};

    const TreeNode *node       = static_cast<const TreeNode *>(child.internalPointer());
    const TreeNode *parentNode = node->parent;
    if (!parentNode || parentNode == &m_root) return {};

    const TreeNode *grandParent = parentNode->parent ? parentNode->parent : &m_root;
    const int row = grandParent->children.indexOf(const_cast<TreeNode *>(parentNode));
    if (row < 0) return {};
    return createIndex(row, 0, const_cast<TreeNode *>(parentNode));
}

int TreeBrandCategories::rowCount(const QModelIndex &parent) const
{
    const TreeNode *node = parent.isValid()
        ? static_cast<const TreeNode *>(parent.internalPointer())
        : &m_root;
    return static_cast<int>(node->children.size());
}

int TreeBrandCategories::columnCount(const QModelIndex &) const
{
    return ColColumnCount;
}

QVariant TreeBrandCategories::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.column() != ColName) return {};

    const TreeNode *node = static_cast<const TreeNode *>(index.internalPointer());

    if (role == Qt::DisplayRole)
        return QStringLiteral("%1  (%2)").arg(node->name).arg(node->colorKeys.size());

    if (role == Qt::ToolTipRole)
        return QStringLiteral("%1 — %2 color group(s), %3 ASIN(s)")
            .arg(node->name).arg(node->colorKeys.size()).arg(node->asins.size());

    return {};
}

QVariant TreeBrandCategories::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    if (section == ColName) return tr("Brand / Category / Gender / Age");
    return {};
}

TreeBrandCategories::TreeNode *TreeBrandCategories::_findOrCreate(
    TreeNode *parentNode, const QString &name)
{
    for (TreeNode *child : parentNode->children) {
        if (child->name == name) return child;
    }
    auto *node = new TreeNode(name, parentNode);
    parentNode->children.append(node);
    return node;
}
