#ifndef AMAZONMARKETPLACE_H
#define AMAZONMARKETPLACE_H

#include <QString>
#include <QList>

class AmazonMarketplace
{
public:
    enum class Region { Europe, NorthAmerica, Japan };

    // Lookup by country code (ISO 3166-1 alpha-2, e.g. "FR", "DE", "US")
    static const AmazonMarketplace *forCountryCode(const QString &countryCode);
    // Lookup by marketplace ID (e.g. "A13V1IB3VIYZZH")
    static const AmazonMarketplace *forMarketplaceId(const QString &marketplaceId);
    // All marketplaces in a region
    static QList<const AmazonMarketplace *> forRegion(Region region);
    // Full registry
    static const QList<AmazonMarketplace> &all();

    QString marketplaceId() const { return m_marketplaceId; }
    QString countryCode()   const { return m_countryCode; }
    QString countryName()   const { return m_countryName; }
    Region  region()        const { return m_region; }
    // SP-API endpoint host (e.g. "sellingpartnerapi-eu.amazon.com")
    QString endpoint()      const { return m_endpoint; }
    // AWS signing region (e.g. "eu-west-1")
    QString awsRegion()     const { return m_awsRegion; }

private:
    AmazonMarketplace(QString marketplaceId, QString countryCode,
                      QString countryName, Region region,
                      QString endpoint, QString awsRegion);

    QString m_marketplaceId;
    QString m_countryCode;
    QString m_countryName;
    Region  m_region;
    QString m_endpoint;
    QString m_awsRegion;
};

#endif // AMAZONMARKETPLACE_H
