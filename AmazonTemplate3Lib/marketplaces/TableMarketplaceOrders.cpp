#include "TableMarketplaceOrders.h"

TableMarketplaceOrders::TableMarketplaceOrders(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void TableMarketplaceOrders::setOrders(const QList<OrderRow> &orders)
{
    beginResetModel();
    m_orders = orders;
    endResetModel();
}

void TableMarketplaceOrders::updateTracking(int row, const QString &source, const QString &sourceOrderId, const QString &tracking)
{
    if (row < 0 || row >= m_orders.size()) return;
    m_orders[row].source = source;
    m_orders[row].sourceOrderId = sourceOrderId;
    m_orders[row].trackingNumber = tracking;
    emit dataChanged(index(row, 0), index(row, ColCount - 1));
}

int TableMarketplaceOrders::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_orders.size();
}

int TableMarketplaceOrders::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return ColCount;
}

QVariant TableMarketplaceOrders::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) return {};
    int row = index.row();
    int col = index.column();
    if (row < 0 || row >= m_orders.size()) return {};

    const auto &order = m_orders[row];

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (col) {
            case ColSource:         return order.source;
            case ColSourceOrderId:  return order.sourceOrderId;
            case ColTargetOrderId:  return order.targetOrderId;
            case ColTracking:       return order.trackingNumber;
            case ColTargetStore:    return order.targetStore;
            default: break;
        }
    }
    return {};
}

QVariant TableMarketplaceOrders::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        switch (section) {
            case ColSource:         return tr("Source");
            case ColSourceOrderId:  return tr("Source Order ID");
            case ColTargetOrderId:  return tr("Target Order ID");
            case ColTracking:       return tr("Tracking Number");
            case ColTargetStore:    return tr("Target Store");
            default: break;
        }
    }
    return {};
}

Qt::ItemFlags TableMarketplaceOrders::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    return Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable;
}
