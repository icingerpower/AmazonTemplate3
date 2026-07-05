#ifndef TABLEMARKETPLACEPRODUCTS_H
#define TABLEMARKETPLACEPRODUCTS_H

#include <QAbstractTableModel>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include "MarketplaceTypes.h"

class TableMarketplaceProducts : public QAbstractTableModel
{
    Q_OBJECT
public:
    // Descriptor for one dynamic column group (Qty + Sales 90d).
    struct MarketplaceStore {
        QString id;    // unique key used in QHash (e.g. "temu_FR_Main")
        QString label; // column group header  (e.g. "Temu FR – Main")
    };

    // Fixed Amazon column indices.
    enum Column {
        ColSku      = 0,
        ColAsin     = 1,
        ColEstDays  = 2,
        ColAmazonQty     = 3,
        ColInbound  = 4,
        ColAmazonSales90d = 5,
    };
    static constexpr int k_fixedCols = 6;

    explicit TableMarketplaceProducts(const QStringList &skus,
                                      const QList<MarketplaceStore> &stores = {},
                                      QObject *parent = nullptr);

    // Fulfillment source data (Amazon FBA, Octopia…)
    void applyInventory(const QList<StockRecord> &records);
    void applySales(const QString &sku, int units90d);

    // Per-store data (dynamic columns). storeId must match MarketplaceStore::id.
    void applyStoreInventory(const QString &storeId, const QHash<QString,int> &qtyBySku);
    void applyStoreSales    (const QString &storeId, const QHash<QString,int> &salesBySku);

    // All SKUs shown in the table (row order).
    QStringList skus() const;

    // Returns the Amazon FBA available qty for a SKU, or -1 if not yet loaded.
    int amazonQtyForSku(const QString &sku) const;
    // Estimated days of inventory (from ColEstDays), or -1 if unknown.
    int estDaysForSku(const QString &sku) const;

    // Sync parameters used to compute the per-SKU "Sync Qty" columns.
    // minDays > 0: only count units beyond that many days of inventory —
    // corrected = available × (estDays − minDays) / estDays (0 if estDays ≤ minDays).
    // Then target = corrected × pct / 100, capped at maxTarget (0 = no cap).
    void setSyncParams(int pctToTarget, int maxTarget, int minDays);

    // The quantity "Sync" would upload for this SKU, or -1 if Amazon qty unknown.
    int targetQtyForSku(const QString &sku) const;

    int rowCount   (const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data      (const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

private:
    struct Row {
        QString sku;
        QString asin;
        int available  = -1;
        int inbound    = -1;
        int sales90d   = -1;
        int estDays    = -1;
        QHash<QString,int> storeQty;   // storeId → qty   (-1 via default = unknown)
        QHash<QString,int> storeSales; // storeId → units (-1 via default = unknown)
    };

    QList<Row>              m_rows;
    QList<MarketplaceStore> m_stores;

    int m_pctToTarget = 100;
    int m_maxTarget   = 0;
    int m_minDays     = 0;

    int  _rowForSku   (const QString &sku)     const;
    int  _storeIndex  (const QString &storeId) const;
    void _recalcEstDays(Row &row) const;
    int  _targetQty   (const Row &row) const;
};

#endif // TABLEMARKETPLACEPRODUCTS_H
