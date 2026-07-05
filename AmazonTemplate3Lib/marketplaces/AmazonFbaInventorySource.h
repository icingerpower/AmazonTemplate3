#ifndef AMAZONFBAINVENTORYSOURCE_H
#define AMAZONFBAINVENTORYSOURCE_H

#pragma GCC optimize("O1")

#include "AbstractInventorySource.h"
#include "AbstractInventorySourceFactory.h"

class AmazonInventoryApi;

// Amazon FBA (EU pool) as fulfillment/inventory source. Created by
// AmazonFbaInventorySourceFactory from the "AmazonApi/*" settings keys.
class AmazonFbaInventorySource : public AbstractInventorySource
{
public:
    AmazonFbaInventorySource(const QString &lwaClientId, const QString &lwaClientSecret,
                             const QString &lwaRefreshToken, const QString &sellerId);
    ~AmazonFbaInventorySource() override;

    QString id()          const override { return QStringLiteral("amazon_fba_eu"); }
    QString displayName() const override { return QStringLiteral("Amazon FBA EU"); }
    QString lastError()   const override;
    void    clearLastError()    override;

    QCoro::Task<void> fetchAllInventory(QStringList filterSkus, QList<StockRecord> *out,
                                        ProgressFn onProgress) override;
    QCoro::Task<void> fetchInventory(QStringList skus, QList<StockRecord> *out,
                                     ProgressFn onProgress) override;
    QCoro::Task<void> fetchSalesUnits(QString sku, int days, int *out) override;

    QCoro::Task<void> fetchTracking(QString fulfillmentOrderId, TrackingInfo *out) override;
    QJsonObject previewFulfillmentOrder(FulfillmentRequest request) const override;
    QCoro::Task<bool> createFulfillmentOrder(FulfillmentRequest request) override;

private:
    AmazonInventoryApi *m_api = nullptr; // owned
};

// Registered explicitly in AbstractInventorySourceFactory::getFactories()
// (same pattern as AbstractFiller::ALL_FILLERS_SORTED — a static-lib linker
// drops object files whose static self-registration is never referenced).
class AmazonFbaInventorySourceFactory : public AbstractInventorySourceFactory
{
public:
    QString platformId()          const override { return QStringLiteral("amazon_fba"); }
    QString platformDisplayName() const override { return QStringLiteral("Amazon FBA"); }
    QList<AbstractInventorySource *> createInstances(QSettings *settings) const override;
};

#endif // AMAZONFBAINVENTORYSOURCE_H
