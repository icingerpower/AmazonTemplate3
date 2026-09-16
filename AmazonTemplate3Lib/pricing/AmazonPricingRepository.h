#pragma once
#include "AmazonDataCache.h"
#include "TableAmazonPricing.h"
#include <QCoro/QCoroTask>
#include <functional>
#include <memory>

class AmazonInventoryApi;
class AmazonPricingApi;

class AmazonPricingRepository {
  public:
    struct Credentials {
        QString client, secret, tokenEu, tokenNa, sellerEu, sellerNa;
    };
    using Log = std::function<void(const QString &)>;
    using Progress =
        std::function<void(const QString &phase, int completed, int total, const QString &detail)>;
    using InventoryFactory = std::function<std::shared_ptr<AmazonInventoryApi>(const Pricing::Market &)>;
    using PricingFactory = std::function<std::shared_ptr<AmazonPricingApi>()>;
    AmazonPricingRepository(QDir working, Credentials credentials, InventoryFactory inventoryFactory = {},
                            PricingFactory pricingFactory = {});
    QList<Pricing::Row> cachedRows(const QList<Pricing::Market> &markets) const;
    QCoro::Task<void> retrieve(QList<Pricing::Market> markets, PricingFilterProxy::Filter filter,
                               std::shared_ptr<bool> cancelled, Log log,
                               std::function<void(const Pricing::Row &)> onRow, Progress progress = {});
    struct Update {
        QString sku, marketplace, currency, productType;
        double before, after;
    };
    static QString describeUpdate(const Update &update);
    QCoro::Task<void> update(QList<Update> updates, std::shared_ptr<bool> cancelled, Log log,
                             std::function<void(const Update &, bool)> onResult);
    AmazonDataCache &cache() {
        return m_cache;
    }

  private:
    InventoryFactory m_inventoryFactory;
    PricingFactory m_pricingFactory;
    Credentials m_credentials;
    QDir m_working;
    AmazonDataCache m_cache;
};
