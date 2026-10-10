#pragma GCC optimize("O1")
#include "AmazonSalesChannel.h"

#include "AmazonMarketplace.h"
#include "apis/AmazonInventoryApi.h"
#include "secrets/CredentialManager.h"
#include <QSettings>

namespace {
class AmazonSalesChannel : public AbstractSalesChannel
{
public:
    AmazonSalesChannel(const AmazonMarketplace &market, const QString &clientId,
                       const QString &secret, const QString &token, const QString &seller)
        : m_name(market.salesChannelName()),
          m_api(clientId, secret, token, seller, market.marketplaceId()) {}
    QString displayName() const override { return m_name; }
    QString lastError() const override { return m_api.lastError(); }
    QCoro::Task<void> fetchOrderCount(QDateTime from, QDateTime to, int *out) override
    {
        co_await m_api.fetchOrderCount(from, to, out);
    }
private:
    QString m_name;
    AmazonInventoryApi m_api;
};
}

QList<AbstractSalesChannel *> AmazonSalesChannelFactory::createSalesChannels(QSettings *settings) const
{
    const auto secret = [settings](const QString &key) {
        return CredentialManager::lookup(QStringLiteral("AmazonTemplate3"), key,
                                         settings->value(key).toString());
    };
    const auto client = settings->value("AmazonApi/lwaClientId").toString();
    const auto clientSecret = secret("AmazonApi/lwaClientSecret");
    QList<AbstractSalesChannel *> channels;
    if (client.isEmpty() || clientSecret.isEmpty())
        return channels;
    for (const QString &region : {QStringLiteral("eu"), QStringLiteral("na"), QStringLiteral("jp")}) {
        const QString prefix = "AmazonApi/" + region + '/';
        const auto token = secret(prefix + "lwaRefreshToken");
        if (token.isEmpty())
            continue;
        const auto seller = settings->value(prefix + "sellerId").toString();
        const auto marketRegion = region == "eu" ? AmazonMarketplace::Region::Europe
            : region == "na" ? AmazonMarketplace::Region::NorthAmerica : AmazonMarketplace::Region::Japan;
        for (const auto *market : AmazonMarketplace::forRegion(marketRegion))
            channels.append(new AmazonSalesChannel(*market, client, clientSecret, token, seller));
    }
    return channels;
}
