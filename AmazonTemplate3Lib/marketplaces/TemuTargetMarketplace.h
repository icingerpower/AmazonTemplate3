#ifndef TEMUTARGETMARKETPLACE_H
#define TEMUTARGETMARKETPLACE_H

#pragma GCC optimize("O1")

#include "AbstractTargetMarketplace.h"
#include "AbstractTargetMarketplaceFactory.h"

class TemuInventoryApi;

// One configured Temu store (EU local-seller API). Instances are created by
// TemuTargetMarketplaceFactory from the "TemuApi/stores" settings array.
class TemuTargetMarketplace : public AbstractTargetMarketplace
{
public:
    TemuTargetMarketplace(const QString &appKey, const QString &appSecret,
                          const QString &country, const QString &label,
                          const QString &token,
                          const QString &proxyHost, int proxyPort,
                          const QString &proxyUser, const QString &proxyPassword);
    ~TemuTargetMarketplace() override;

    QString id()            const override; // "temu_{country}_{label}"
    QString displayName()   const override; // "Temu {country} – {label}"
    QString countryCode()   const override { return m_country; }
    QString orderIdPrefix() const override { return QStringLiteral("temu"); }
    QString lastError()     const override;

    QCoro::Task<void> fetchInventory(QStringList skus, QHash<QString,int> *out) override;
    QCoro::Task<void> fetchSales(QStringList skus, int days, QHash<QString,int> *out) override;
    QCoro::Task<void> updateInventory(QHash<QString,int> qtyBySku, ProgressFn onProgress) override;

    QCoro::Task<QList<MarketOrder>> fetchUnshippedOrders() override;
    QCoro::Task<void> fetchOrderAddress(QString orderId, ShippingAddress *out) override;
    QCoro::Task<bool> confirmShipment(MarketOrder order, TrackingInfo tracking,
                                      ProgressFn onProgress) override;

private:
    QString m_country;
    QString m_label;
    TemuInventoryApi *m_api = nullptr; // owned
};

// Registered explicitly in AbstractTargetMarketplaceFactory::getFactories()
// (same pattern as AbstractFiller::ALL_FILLERS_SORTED — a static-lib linker
// drops object files whose static self-registration is never referenced).
class TemuTargetMarketplaceFactory : public AbstractTargetMarketplaceFactory
{
public:
    QString platformId()          const override { return QStringLiteral("temu"); }
    QString platformDisplayName() const override { return QStringLiteral("Temu"); }
    QList<AbstractTargetMarketplace *> createInstances(QSettings *settings) const override;
};

#endif // TEMUTARGETMARKETPLACE_H
