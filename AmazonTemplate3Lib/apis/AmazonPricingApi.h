#ifndef AMAZONPRICINGAPI_H
#define AMAZONPRICINGAPI_H

#include <QDateTime>
#include <QObject>
#include <QString>

#include <QCoro/QCoroTask>

class QNetworkAccessManager;

class AmazonPricingApi : public QObject
{
    Q_OBJECT
public:
    explicit AmazonPricingApi(const QString &lwaClientId,
                              const QString &lwaClientSecret,
                              const QString &lwaRefreshTokenEu,
                              const QString &lwaRefreshTokenNa,
                              const QString &sellerIdEu,
                              const QString &sellerIdNa,
                              QObject *parent = nullptr);

    // Fetch the B2C listing price for a SKU on a given marketplace.
    // *priceOut       : listing price in the marketplace's currency (-1.0 if no price data).
    // *existsOut      : true when the SKU has an active listing (HTTP 200).
    // *productTypeOut : SP-API product type string from summaries (may be empty); pass nullptr to ignore.
    // *minPriceOut    : seller's minimum_seller_allowed_price from purchasable_offer (-1.0 if none); nullptr to ignore.
    // *maxPriceOut    : seller's maximum_seller_allowed_price from purchasable_offer (-1.0 if none); nullptr to ignore.
    // GCC 13 ICE workaround: params passed by value.
    QCoro::Task<void> fetchListingPrice(QString marketplaceId, QString sku,
                                        double *priceOut, bool *existsOut,
                                        QString *productTypeOut = nullptr,
                                        double *minPriceOut = nullptr,
                                        double *maxPriceOut = nullptr);

    // PATCH the purchasable_offer attribute to set a new B2C price.
    // currency: the marketplace currency code (e.g. "EUR", "GBP").
    // newPrice: the new listing price (rounded to 2 decimal places before sending).
    // *success: true on HTTP 200/202 with no INVALID status from Amazon.
    // GCC 13 ICE workaround: params passed by value.
    QCoro::Task<void> patchListingPrice(QString marketplaceId, QString sku,
                                        QString productType, QString currency,
                                        double newPrice, bool *success);

    // PATCH purchasable_offer to schedule a time-boxed sale (strike-through)
    // price. listPrice keeps our_price (the regular price); discountedPrice is
    // the sale price applied between startAt and endAt (inclusive). Both are in
    // the marketplace currency. This is the closest the SP-API gets to a
    // "promotion" — it is a scheduled discounted price, not a coupon/deal.
    // *success: true on HTTP 200/202 with no INVALID status from Amazon.
    // GCC 13 ICE workaround: params passed by value.
    // This op replaces the whole purchasable_offer, so both
    // minimum_seller_allowed_price and maximum_seller_allowed_price are
    // deliberately NOT written — i.e. any existing price floor/ceiling is
    // removed (a floor could block the discounted price; the ceiling is dropped
    // by request too).
    QCoro::Task<void> patchListingDiscount(QString marketplaceId, QString sku,
                                           QString productType, QString currency,
                                           double listPrice, double discountedPrice,
                                           QDateTime startAt, QDateTime endAt,
                                           bool *success);

    QString lastError() const { return m_lastError; }

private:
    static QString endpointForMarketplace (const QString &marketplaceId);
    static QString lwaRegionForMarketplace(const QString &marketplaceId);
    QString        sellerIdForMarketplace (const QString &marketplaceId) const;

    QCoro::Task<void>     _getAccessToken(QString lwaRegion, QString *out);
    QCoro::Task<void>     _rateLimit();
    QNetworkAccessManager *_nam();

    QString m_lwaClientId;
    QString m_lwaClientSecret;
    QString m_lwaRefreshTokenEu;
    QString m_lwaRefreshTokenNa;
    QString m_sellerIdEu;
    QString m_sellerIdNa;

    QString   m_accessTokenEu;   QDateTime m_accessTokenExpiryEu;
    QString   m_accessTokenNa;   QDateTime m_accessTokenExpiryNa;

    QNetworkAccessManager *m_nam = nullptr;
    QDateTime              m_lastRequestTime;
    QString                m_lastError;
};

#endif // AMAZONPRICINGAPI_H
