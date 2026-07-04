#ifndef TABLEMARKETPLACEORDERS_H
#define TABLEMARKETPLACEORDERS_H

#include <QAbstractTableModel>
#include <QList>
#include <QString>
#include <QHash>

class TableMarketplaceOrders : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column {
        ColSource = 0,
        ColSourceOrderId = 1,
        ColTargetOrderId = 2,
        ColTracking = 3,
        ColTargetStore = 4,
        ColCount = 5
    };

    struct OrderRow {
        // Display fields
        QString source;         // e.g. "Amazon"
        QString sourceOrderId;  // e.g. "temu-PO-..."
        QString targetOrderId;  // e.g. "PO-..."
        QString trackingNumber;
        QString targetStore;

        // Hidden fulfillment fields for sync
        QString parentOrderSn;
        QString orderSn;
        QString sku;            // seller SKU (Temu extCode = Amazon SKU)
        qint64 goodsId = 0;
        qint64 skuId = 0;
        int quantity = 0;
        QString temuStoreToken;
        QString temuStoreCountry; // e.g. "FR"
        QString temuProxyHost;
        int temuProxyPort = 0;
        QString temuProxyUser;
        QString temuProxyPass;
    };

    explicit TableMarketplaceOrders(QObject *parent = nullptr);

    void setOrders(const QList<OrderRow> &orders);
    QList<OrderRow> orders() const { return m_orders; }

    // Helpers to update tracking info
    void updateTracking(int row, const QString &source, const QString &sourceOrderId, const QString &tracking);

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

private:
    QList<OrderRow> m_orders;
};

#endif // TABLEMARKETPLACEORDERS_H
