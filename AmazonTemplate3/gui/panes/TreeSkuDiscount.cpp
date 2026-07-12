#include "TreeSkuDiscount.h"

#include <QBrush>
#include <QColor>
#include <QLocale>

TreeSkuDiscount::TreeSkuDiscount(QObject *parent)
    : QAbstractItemModel(parent)
{
}

void TreeSkuDiscount::setProducts(const QList<Product> &products)
{
    beginResetModel();
    m_products = products;
    endResetModel();
}

void TreeSkuDiscount::clear()
{
    beginResetModel();
    m_products.clear();
    endResetModel();
}

QModelIndex TreeSkuDiscount::index(int row, int column, const QModelIndex &parent) const
{
    if (row < 0 || column < 0 || column >= ColumnCount)
        return {};

    if (!parent.isValid()) {                       // top-level product
        if (row >= m_products.size())
            return {};
        return createIndex(row, column, kTopLevel);
    }

    // Children only exist under a product (not under another child).
    if (!isProduct(parent))
        return {};
    const int prodRow = parent.row();
    if (prodRow >= m_products.size()
        || row >= m_products.at(prodRow).countries.size())
        return {};
    return createIndex(row, column, quintptr(prodRow + 1));
}

QModelIndex TreeSkuDiscount::parent(const QModelIndex &child) const
{
    if (!child.isValid() || isProduct(child))
        return {};
    const int prodRow = static_cast<int>(child.internalId()) - 1;
    if (prodRow < 0 || prodRow >= m_products.size())
        return {};
    return createIndex(prodRow, 0, kTopLevel);
}

int TreeSkuDiscount::rowCount(const QModelIndex &parent) const
{
    if (!parent.isValid())
        return m_products.size();
    if (!isProduct(parent))                        // a child row has no children
        return 0;
    const int prodRow = parent.row();
    if (prodRow < 0 || prodRow >= m_products.size())
        return 0;
    return m_products.at(prodRow).countries.size();
}

int TreeSkuDiscount::columnCount(const QModelIndex &) const
{
    return ColumnCount;
}

QVariant TreeSkuDiscount::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};

    const int col = index.column();

    // ---- Product (parent) row ----
    if (isProduct(index)) {
        if (index.row() >= m_products.size())
            return {};
        const Product &p = m_products.at(index.row());
        if (role == Qt::DisplayRole || role == Qt::EditRole) {
            switch (col) {
            case ColSku:   return p.sku;
            case ColAsin:  return p.asin;
            case ColTitle:     return p.title;
            case ColUnits:     return p.available;   // shared pan-EU pool
            case ColAgedUnits: return p.agedUnits;
            case ColSales90:   return p.unitsSold90 >= 0 ? QVariant(p.unitsSold90)
                                                         : QVariant();
            default:           return {};   // prices live on the child rows
            }
        }
        if (role == Qt::TextAlignmentRole) {
            switch (col) {
            case ColUnits:
            case ColAgedUnits:
            case ColSales90:
                return int(Qt::AlignRight | Qt::AlignVCenter);
            default:
                break;
            }
        }
        if (role == Qt::ToolTipRole && col == ColAgedUnits && p.minMonths > 0)
            return tr("%1 of %2 units have been stored for at least %3 month(s).")
                       .arg(p.agedUnits).arg(p.available).arg(p.minMonths);
        // Checkbox on the product row: untick to exclude it from Apply.
        if (role == Qt::CheckStateRole && col == ColSku)
            return p.checked ? Qt::Checked : Qt::Unchecked;
        if (role == Qt::ToolTipRole && col == ColSku)
            return tr("Untick to skip this product when applying discounts.");
        return {};
    }

    // ---- Country (child) row ----
    const int prodRow = static_cast<int>(index.internalId()) - 1;
    if (prodRow < 0 || prodRow >= m_products.size())
        return {};
    const Product &p = m_products.at(prodRow);
    if (index.row() >= p.countries.size())
        return {};
    const CountryRow &c = p.countries.at(index.row());

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (col) {
        case ColSku:          return c.countryCode;   // echo country under the SKU column
        case ColAsin:         return {};
        case ColTitle:        return c.title;
        case ColUnits:        return c.units;
        // Blank when the price is unknown (not listed / Amazon-managed) rather
        // than a misleading 0.00 or negative sentinel.
        case ColUnitPriceEur: return c.unitPriceEur > 0.0
                                     ? QLocale().toString(c.unitPriceEur, 'f', 2)
                                     : QVariant();
        case ColCurrencyEur:  return c.unitPriceEur > 0.0 ? QStringLiteral("EUR") : QVariant();
        case ColOrigAmount:   return c.origAmount > 0.0
                                     ? QLocale().toString(c.origAmount, 'f', 2)
                                     : QVariant();
        case ColOrigCurrency: return c.origCurrency;
        case ColNewPrice:     return c.eligible
                                     ? QLocale().toString(c.newPrice, 'f', 2)
                                     : QVariant();
        default:              return {};
        }
    }

    // Indicator checkbox: ticked when Apply will erase a min/max price here.
    if (role == Qt::CheckStateRole && col == ColEraseMinMax)
        return c.eraseMinMax() ? Qt::Checked : Qt::Unchecked;

    // Explain a blank price so the user can tell "not listed" from
    // "listed but price managed by Amazon".
    if (role == Qt::ToolTipRole && c.origAmount <= 0.0) {
        if (!c.listed)
            return tr("Not listed on this marketplace (no listing record).");
        return tr("Listed, but no seller price is set — the price is managed by "
                  "Amazon (Build International Listings). "
                  "Not available via the Listings API.");
    }

    // Dark red New-price cell for eligible rows (white text for contrast).
    if (c.eligible && col == ColNewPrice) {
        if (role == Qt::BackgroundRole)
            return QBrush(QColor(0x8B, 0x00, 0x00));   // dark red
        if (role == Qt::ForegroundRole)
            return QBrush(Qt::white);
    }

    // Dark red background on the erase-min/max checkbox when it is ticked.
    if (role == Qt::BackgroundRole && col == ColEraseMinMax && c.eraseMinMax())
        return QBrush(QColor(0x8B, 0x00, 0x00));

    if (role == Qt::ToolTipRole && col == ColEraseMinMax && c.eraseMinMax()) {
        QStringList parts;
        if (c.minPrice > 0.0)
            parts << tr("min %1").arg(QLocale().toString(c.minPrice, 'f', 2));
        if (c.maxPrice > 0.0)
            parts << tr("max %1").arg(QLocale().toString(c.maxPrice, 'f', 2));
        return tr("Applying the discount will erase the seller price %1 (%2) on this marketplace.")
                   .arg(parts.join(QStringLiteral(" / ")), c.origCurrency);
    }

    if (role == Qt::TextAlignmentRole) {
        switch (col) {
        case ColUnits:
        case ColUnitPriceEur:
        case ColOrigAmount:
        case ColNewPrice:
            return int(Qt::AlignRight | Qt::AlignVCenter);
        default:
            break;
        }
    }
    return {};
}

QVariant TreeSkuDiscount::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case ColSku:          return QStringLiteral("SKU / Country");
    case ColAsin:         return QStringLiteral("ASIN");
    case ColTitle:        return QStringLiteral("Title");
    case ColUnits:        return QStringLiteral("Units");
    case ColAgedUnits:    return QStringLiteral("Aged");
    case ColSales90:      return QStringLiteral("Sales 90d");
    case ColUnitPriceEur: return QStringLiteral("Unit price (EUR)");
    case ColCurrencyEur:  return QStringLiteral("Cur.");
    case ColOrigAmount:   return QStringLiteral("Orig amount");
    case ColOrigCurrency: return QStringLiteral("Orig cur.");
    case ColNewPrice:     return QStringLiteral("New price");
    case ColEraseMinMax:  return QStringLiteral("Erase min/max");
    default:              return {};
    }
}

Qt::ItemFlags TreeSkuDiscount::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    // The erase-min/max column is a read-only indicator checkbox — not editable
    // (no editor, not user-toggleable).
    if (index.column() == ColEraseMinMax)
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
    // Product (parent) rows: first column carries a user checkbox to include/
    // exclude the product from Apply.
    if (isProduct(index) && index.column() == ColSku)
        f |= Qt::ItemIsUserCheckable;
    return f;
}

bool TreeSkuDiscount::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (role == Qt::CheckStateRole && index.isValid() && isProduct(index)
        && index.column() == ColSku && index.row() < m_products.size()) {
        m_products[index.row()].checked = (value.toInt() == Qt::Checked);
        emit dataChanged(index, index, {Qt::CheckStateRole});
        return true;
    }
    return false;   // all other edits are read-only (copy-only cells)
}
