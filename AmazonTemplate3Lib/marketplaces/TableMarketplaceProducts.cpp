#include "TableMarketplaceProducts.h"

#include <QBrush>
#include <QColor>

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
    // Case-insensitive: Amazon seller-SKU casing can differ from the Temu
    // skuSn casing the rows were built from.
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows.at(i).sku.compare(sku, Qt::CaseInsensitive) == 0) return i;
    return -1;
}

QStringList TableMarketplaceProducts::skus() const
{
    QStringList out;
    out.reserve(m_rows.size());
    for (const Row &r : m_rows)
        out.append(r.sku);
    return out;
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

int TableMarketplaceProducts::estDaysForSku(const QString &sku) const
{
    const int row = _rowForSku(sku);
    return (row >= 0) ? m_rows.at(row).estDays : -1;
}

void TableMarketplaceProducts::setSyncParams(int pctToTarget, int maxTarget, int minDays)
{
    if (m_pctToTarget == pctToTarget && m_maxTarget == maxTarget && m_minDays == minDays)
        return;
    m_pctToTarget = pctToTarget;
    m_maxTarget   = maxTarget;
    m_minDays     = minDays;
    if (!m_rows.isEmpty() && !m_stores.isEmpty())
        emit dataChanged(index(0, k_fixedCols),
                         index(m_rows.size() - 1, columnCount() - 1));
}

int TableMarketplaceProducts::_targetQty(const Row &r) const
{
    if (r.available < 0)
        return -1;
    double corrected = r.available;
    // estDays == kInfiniteDaysOfSupply means no sales: the correction factor
    // (estDays − minDays) / estDays tends to 1, so leave the stock uncorrected.
    if (m_minDays > 0 && r.estDays >= 0 && r.estDays < kInfiniteDaysOfSupply) {
        if (r.estDays <= m_minDays)
            return r.available > 5 && m_pctToTarget > 0 ? 1 : 0;
        else
            corrected = r.available * double(r.estDays - m_minDays) / r.estDays;
    }
    int target = static_cast<int>(corrected * m_pctToTarget / 100.0);
    if (m_maxTarget > 0 && target > m_maxTarget)
        target = m_maxTarget;
    return target;
}

int TableMarketplaceProducts::targetQtyForSku(const QString &sku, const QString &storeId) const
{
    const int row = _rowForSku(sku);
    if (row < 0 || m_rows.at(row).available < 0)
        return -1;
    const Row &r = m_rows.at(row);
    return r.syncOverrides.value(storeId, _targetQty(r));
}

void TableMarketplaceProducts::_recalcEstDays(Row &row) const
{
    row.estDays = estimatedDaysOfSupply(row.available, row.sales90d, 90);
}

void TableMarketplaceProducts::applyInventory(const QList<StockRecord> &records)
{
    for (const auto &s : records) {
        const int row = _rowForSku(s.sku);
        if (row < 0) continue;
        Row &r = m_rows[row];
        r.asin      = s.asin;
        r.available = s.available;
        r.inbound   = s.inbound;
        _recalcEstDays(r);
        // Full row: the per-store Sync Qty columns depend on available/estDays.
        emit dataChanged(index(row, 0), index(row, columnCount() - 1));
    }
}

void TableMarketplaceProducts::applySales(const QString &sku, int units90d)
{
    const int row = _rowForSku(sku);
    if (row < 0) return;
    Row &r = m_rows[row];
    r.sales90d = units90d;
    _recalcEstDays(r);
    emit dataChanged(index(row, 0), index(row, columnCount() - 1));
}

void TableMarketplaceProducts::applyStoreInventory(const QString &storeId,
                                                    const QHash<QString,int> &qtyBySku)
{
    const int si = _storeIndex(storeId);
    if (si < 0) return;
    QHash<QString,int> lower;
    for (auto it = qtyBySku.begin(); it != qtyBySku.end(); ++it)
        lower.insert(it.key().toLower(), it.value());
    const int col = k_fixedCols + si * 3; // Qty column for this store
    for (int i = 0; i < m_rows.size(); ++i) {
        Row &r = m_rows[i];
        const auto it = lower.constFind(r.sku.toLower());
        if (it == lower.constEnd()) continue;
        r.storeQty[storeId] = it.value();
        // Qty + Sync Qty: the Sync Qty foreground colour depends on storeQty.
        emit dataChanged(index(i, col), index(i, col + 1));
    }
}

void TableMarketplaceProducts::applyStoreSales(const QString &storeId,
                                               const QHash<QString,int> &salesBySku)
{
    const int si = _storeIndex(storeId);
    if (si < 0) return;
    QHash<QString,int> lower;
    for (auto it = salesBySku.begin(); it != salesBySku.end(); ++it)
        lower.insert(it.key().toLower(), it.value());
    const int col = k_fixedCols + si * 3 + 2; // Sales column for this store
    for (int i = 0; i < m_rows.size(); ++i) {
        Row &r = m_rows[i];
        const auto it = lower.constFind(r.sku.toLower());
        if (it == lower.constEnd()) continue;
        r.storeSales[storeId] = it.value();
        emit dataChanged(index(i, col), index(i, col));
    }
}

int TableMarketplaceProducts::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

int TableMarketplaceProducts::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : k_fixedCols + m_stores.size() * 3;
}

QVariant TableMarketplaceProducts::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};

    const Row &r = m_rows.at(index.row());
    const int col = index.column();

    if (role == Qt::ToolTipRole && col >= k_fixedCols
        && (col - k_fixedCols) % 3 == 1) {
        return tr("Double-click to edit the quantity to sync for this store. "
                  "Manual quantities last until the next Load. "
                  "Red also indicates that the automatic quantity would reduce the store's stock.");
    }

    // Preserve the automatic decrease warning even after a manual override.
    if (role == Qt::BackgroundRole) {
        if (col >= k_fixedCols && (col - k_fixedCols) % 3 == 1) {
            const int storeIdx = (col - k_fixedCols) / 3;
            if (storeIdx < m_stores.size()) {
                const QString &sid = m_stores.at(storeIdx).id;
                const int target   = _targetQty(r);
                const int editedTarget = r.syncOverrides.value(sid, target);
                const int storeQty = r.storeQty.value(sid, -1);
                if (target >= 0 && storeQty >= 0
                    && (target < storeQty || editedTarget < storeQty))
                    return QBrush(QColor(139, 0, 0)); // dark red
            }
        }
        return {};
    }

    if (role != Qt::DisplayRole && role != Qt::EditRole)
        return {};

    auto numOrDash = [](int v) -> QVariant {
        return v < 0 ? QVariant(QStringLiteral("-")) : QVariant(QString::number(v));
    };

    // Fixed Amazon columns
    switch (col) {
    case ColSku:   return r.sku;
    case ColAsin:  return r.asin.isEmpty() ? QStringLiteral("-") : r.asin;
    case ColEstDays:
        if (r.estDays < 0)        return QStringLiteral("-");
        if (r.estDays >= kInfiniteDaysOfSupply) return QString::fromUtf8("\xE2\x88\x9E");
        return QString::number(r.estDays);
    case ColAmazonQty:      return numOrDash(r.available);
    case ColInbound:        return numOrDash(r.inbound);
    case ColAmazonSales90d: return numOrDash(r.sales90d);
    default: break;
    }

    // Dynamic store columns: Qty / Sync Qty / Sales 90d
    if (col >= k_fixedCols) {
        const int storeIdx = (col - k_fixedCols) / 3;
        const int sub      = (col - k_fixedCols) % 3;
        if (storeIdx >= m_stores.size()) return {};
        const QString &sid = m_stores.at(storeIdx).id;
        int val = -1;
        switch (sub) {
        case 0: val = r.storeQty.value(sid, -1);   break;
        case 1: val = r.available < 0 ? -1 : r.syncOverrides.value(sid, _targetQty(r)); break;
        case 2: val = r.storeSales.value(sid, -1); break;
        }
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
        const int storeIdx = (section - k_fixedCols) / 3;
        const int sub      = (section - k_fixedCols) % 3;
        if (storeIdx < m_stores.size()) {
            const QString &lbl = m_stores.at(storeIdx).label;
            switch (sub) {
            case 0: return lbl + QStringLiteral(" Qty");
            case 1: return lbl + QStringLiteral(" Sync Qty");
            case 2: return lbl + QStringLiteral(" Sales 90d");
            }
        }
    }
    return {};
}

Qt::ItemFlags TableMarketplaceProducts::flags(const QModelIndex &index) const
{
    if (!index.isValid() || index.model() != this
        || index.row() >= m_rows.size() || index.column() >= columnCount())
        return Qt::NoItemFlags;
    Qt::ItemFlags result = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    // Open a text editor for SKU selection/copying, but reject writes in setData.
    if (index.column() == ColSku
        || (index.column() >= k_fixedCols && (index.column() - k_fixedCols) % 3 == 1
            && m_rows.at(index.row()).available >= 0))
        result |= Qt::ItemIsEditable;
    return result;
}

bool TableMarketplaceProducts::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (role != Qt::EditRole || !(flags(index) & Qt::ItemIsEditable)
        || index.column() == ColSku)
        return false;
    bool ok = false;
    const int quantity = value.toString().toInt(&ok);
    if (!ok || quantity < 0)
        return false;
    Row &r = m_rows[index.row()];
    const QString &sid = m_stores.at((index.column() - k_fixedCols) / 3).id;
    r.syncOverrides.insert(sid, quantity);
    emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole, Qt::BackgroundRole});
    return true;
}
