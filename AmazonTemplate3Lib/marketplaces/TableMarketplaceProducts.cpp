#include "TableMarketplaceProducts.h"

namespace {
constexpr int kInfiniteDays = 999;
}

TableMarketplaceProducts::TableMarketplaceProducts(const QStringList &skus,
                                                   const QList<MarketplaceStore> &stores,
                                                   QObject *parent)
    : QAbstractTableModel(parent)
    , m_stores(stores)
{
    m_rows.reserve(skus.size());
    for (const QString &sku : skus) {
        Row r;
        r.sku = sku;
        m_rows.append(r);
    }
}

int TableMarketplaceProducts::_rowForSku(const QString &sku) const
{
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows.at(i).sku == sku) return i;
    return -1;
}

int TableMarketplaceProducts::_storeIndex(const QString &storeId) const
{
    for (int i = 0; i < m_stores.size(); ++i)
        if (m_stores.at(i).id == storeId) return i;
    return -1;
}

int TableMarketplaceProducts::amazonQtyForSku(const QString &sku) const
{
    const int row = _rowForSku(sku);
    return (row >= 0) ? m_rows.at(row).available : -1;
}

void TableMarketplaceProducts::_recalcEstDays(Row &row) const
{
    if (row.available == 0) {
        row.estDays = 0;
    } else if (row.available > 0 && row.sales90d > 0) {
        const double perDay = row.sales90d / 90.0;
        row.estDays = qRound(row.available / perDay);
    } else if (row.available > 0 && row.sales90d == 0) {
        row.estDays = kInfiniteDays;
    } else {
        row.estDays = -1;
    }
}

void TableMarketplaceProducts::applyInventory(
    const QList<AmazonInventoryApi::InventorySummary> &summaries)
{
    for (const auto &s : summaries) {
        const int row = _rowForSku(s.sku);
        if (row < 0) continue;
        Row &r = m_rows[row];
        r.asin      = s.asin;
        r.available = s.available;
        r.inbound   = s.inbound;
        _recalcEstDays(r);
        emit dataChanged(index(row, 0), index(row, k_fixedCols - 1));
    }
}

void TableMarketplaceProducts::applySales(const QString &sku, int units90d)
{
    const int row = _rowForSku(sku);
    if (row < 0) return;
    Row &r = m_rows[row];
    r.sales90d = units90d;
    _recalcEstDays(r);
    emit dataChanged(index(row, 0), index(row, k_fixedCols - 1));
}

void TableMarketplaceProducts::applyStoreInventory(const QString &storeId,
                                                    const QHash<QString,int> &qtyBySku)
{
    const int si = _storeIndex(storeId);
    if (si < 0) return;
    const int col = k_fixedCols + si * 2; // Qty column for this store
    for (int i = 0; i < m_rows.size(); ++i) {
        Row &r = m_rows[i];
        if (!qtyBySku.contains(r.sku)) continue;
        r.storeQty[storeId] = qtyBySku.value(r.sku);
        emit dataChanged(index(i, col), index(i, col));
    }
}

void TableMarketplaceProducts::applyStoreSales(const QString &storeId,
                                               const QHash<QString,int> &salesBySku)
{
    const int si = _storeIndex(storeId);
    if (si < 0) return;
    const int col = k_fixedCols + si * 2 + 1; // Sales column for this store
    for (int i = 0; i < m_rows.size(); ++i) {
        Row &r = m_rows[i];
        if (!salesBySku.contains(r.sku)) continue;
        r.storeSales[storeId] = salesBySku.value(r.sku);
        emit dataChanged(index(i, col), index(i, col));
    }
}

int TableMarketplaceProducts::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

int TableMarketplaceProducts::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : k_fixedCols + m_stores.size() * 2;
}

QVariant TableMarketplaceProducts::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    if (role != Qt::DisplayRole && role != Qt::EditRole)
        return {};

    const Row &r = m_rows.at(index.row());
    const int col = index.column();

    auto numOrDash = [](int v) -> QVariant {
        return v < 0 ? QVariant(QStringLiteral("-")) : QVariant(QString::number(v));
    };

    // Fixed Amazon columns
    switch (col) {
    case ColSku:   return r.sku;
    case ColAsin:  return r.asin.isEmpty() ? QStringLiteral("-") : r.asin;
    case ColEstDays:
        if (r.estDays < 0)        return QStringLiteral("-");
        if (r.estDays >= kInfiniteDays) return QString::fromUtf8("\xE2\x88\x9E");
        return QString::number(r.estDays);
    case ColAmazonQty:      return numOrDash(r.available);
    case ColInbound:        return numOrDash(r.inbound);
    case ColAmazonSales90d: return numOrDash(r.sales90d);
    default: break;
    }

    // Dynamic store columns
    if (col >= k_fixedCols) {
        const int storeIdx = (col - k_fixedCols) / 2;
        const bool isSales = (col - k_fixedCols) % 2 == 1;
        if (storeIdx >= m_stores.size()) return {};
        const QString &sid = m_stores.at(storeIdx).id;
        const int val = isSales ? r.storeSales.value(sid, -1)
                                : r.storeQty.value(sid, -1);
        return numOrDash(val);
    }
    return {};
}

QVariant TableMarketplaceProducts::headerData(int section, Qt::Orientation orientation,
                                              int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return QAbstractTableModel::headerData(section, orientation, role);

    switch (section) {
    case ColSku:            return QStringLiteral("SKU");
    case ColAsin:           return QStringLiteral("ASIN");
    case ColEstDays:        return QStringLiteral("Est. days");
    case ColAmazonQty:      return QStringLiteral("Amazon Qty");
    case ColInbound:        return QStringLiteral("Inbound");
    case ColAmazonSales90d: return QStringLiteral("Amazon Sales 90d");
    default: break;
    }

    if (section >= k_fixedCols) {
        const int storeIdx = (section - k_fixedCols) / 2;
        const bool isSales = (section - k_fixedCols) % 2 == 1;
        if (storeIdx < m_stores.size()) {
            const QString &lbl = m_stores.at(storeIdx).label;
            return isSales ? lbl + QStringLiteral(" Sales 90d")
                           : lbl + QStringLiteral(" Qty");
        }
    }
    return {};
}

Qt::ItemFlags TableMarketplaceProducts::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsEditable | Qt::ItemIsSelectable;
}
