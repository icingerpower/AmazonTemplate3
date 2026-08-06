#include "AmazonMarketplace.h"

AmazonMarketplace::AmazonMarketplace(QString marketplaceId, QString countryCode,
                                     QString countryName, Region region,
                                     QString endpoint, QString awsRegion)
    : m_marketplaceId(std::move(marketplaceId))
    , m_countryCode(std::move(countryCode))
    , m_countryName(std::move(countryName))
    , m_region(region)
    , m_endpoint(std::move(endpoint))
    , m_awsRegion(std::move(awsRegion))
{}

const QList<AmazonMarketplace> &AmazonMarketplace::all()
{
    static const QList<AmazonMarketplace> s_all = {
        // Europe  (endpoint: sellingpartnerapi-eu.amazon.com, region: eu-west-1)
        {"A1F83G8C2ARO7P", "GB", "United Kingdom", Region::Europe, "sellingpartnerapi-eu.amazon.com", "eu-west-1"},
        {"A1PA6795UKMFR9", "DE", "Germany",         Region::Europe, "sellingpartnerapi-eu.amazon.com", "eu-west-1"},
        {"A13V1IB3VIYZZH", "FR", "France",           Region::Europe, "sellingpartnerapi-eu.amazon.com", "eu-west-1"},
        {"A1RKKUPIHCS9HS", "ES", "Spain",            Region::Europe, "sellingpartnerapi-eu.amazon.com", "eu-west-1"},
        {"APJ6JRA9NG5V4",  "IT", "Italy",            Region::Europe, "sellingpartnerapi-eu.amazon.com", "eu-west-1"},
        {"A1805IZSGTT6HS", "NL", "Netherlands",      Region::Europe, "sellingpartnerapi-eu.amazon.com", "eu-west-1"},
        {"A2NODRKZP88ZB9", "SE", "Sweden",           Region::Europe, "sellingpartnerapi-eu.amazon.com", "eu-west-1"},
        {"A1C3SOZRARQ6R3", "PL", "Poland",           Region::Europe, "sellingpartnerapi-eu.amazon.com", "eu-west-1"},
        {"AMEN7PMS3EDWL",  "BE", "Belgium",          Region::Europe, "sellingpartnerapi-eu.amazon.com", "eu-west-1"},
        {"A28R8C7NBKEWEA", "IE", "Ireland",          Region::Europe, "sellingpartnerapi-eu.amazon.com", "eu-west-1"},
        // North America  (endpoint: sellingpartnerapi-na.amazon.com, region: us-east-1)
        {"ATVPDKIKX0DER",  "US", "United States",    Region::NorthAmerica, "sellingpartnerapi-na.amazon.com", "us-east-1"},
        {"A2EUQ1WTGCTBG2", "CA", "Canada",           Region::NorthAmerica, "sellingpartnerapi-na.amazon.com", "us-east-1"},
        {"A1AM78C64UM0Y8", "MX", "Mexico",           Region::NorthAmerica, "sellingpartnerapi-na.amazon.com", "us-east-1"},
        // Japan  (endpoint: sellingpartnerapi-fe.amazon.com, region: us-west-2)
        {"A1VC38T7YXB528", "JP", "Japan",            Region::Japan, "sellingpartnerapi-fe.amazon.com", "us-west-2"},
    };
    return s_all;
}

const AmazonMarketplace *AmazonMarketplace::forCountryCode(const QString &countryCode)
{
    for (const auto &mp : all())
        if (mp.m_countryCode.compare(countryCode, Qt::CaseInsensitive) == 0)
            return &mp;
    return nullptr;
}

const AmazonMarketplace *AmazonMarketplace::forMarketplaceId(const QString &marketplaceId)
{
    for (const auto &mp : all())
        if (mp.m_marketplaceId == marketplaceId)
            return &mp;
    return nullptr;
}

QList<const AmazonMarketplace *> AmazonMarketplace::forRegion(Region region)
{
    QList<const AmazonMarketplace *> result;
    for (const auto &mp : all())
        if (mp.m_region == region)
            result.append(&mp);
    return result;
}

QList<const AmazonMarketplace *> AmazonMarketplace::europeanUnion()
{
    QList<const AmazonMarketplace *> result;
    for (const auto &mp : all())
        if (mp.m_region == Region::Europe && mp.m_countryCode != QLatin1String("GB"))
            result.append(&mp);
    return result;
}
