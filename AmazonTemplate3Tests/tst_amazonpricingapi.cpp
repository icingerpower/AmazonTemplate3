#include "apis/AmazonPricingApi.h"
#include <QtTest>

class AmazonPricingApiTests : public QObject {
    Q_OBJECT
  private slots:
    void currentPriceOnly_data();
    void currentPriceOnly();
    void productTypeMatchesMarketplace();
    void regularPricePreservesOtherOfferFields();
};

void AmazonPricingApiTests::currentPriceOnly_data() {
    QTest::addColumn<QByteArray>("body");
    QTest::addColumn<double>("expected");
    QTest::newRow("reference-price-is-not-current")
        << QByteArray(R"({"attributes":{"list_price":[{"value":49.99}]}})") << -1.0;
    QTest::newRow("nested-offer-before-reference")
        << QByteArray(
               R"({"offers":[{"marketplaceId":"FR","price":{"listingPrice":{"amount":19.95}}}],"attributes":{"list_price":[{"value":49.99}]}})")
        << 19.95;
    QTest::newRow("direct-offer")
        << QByteArray(R"({"offers":[{"marketplaceId":"FR","listingPrice":{"amount":12.50}}]})") << 12.5;
    QTest::newRow("standard-listings-offer")
        << QByteArray(
               R"({"offers":[{"marketplaceId":"FR","offerType":"B2C","price":{"amount":18.5,"currency":"EUR"}}]})")
        << 18.5;
    QTest::newRow("b2b-is-not-b2c")
        << QByteArray(
               R"({"offers":[{"marketplaceId":"FR","offerType":"B2B","price":{"amount":10}}],"attributes":{"purchasable_offer":[{"marketplace_id":"FR","audience":"B2B","our_price":[{"schedule":[{"value_with_tax":9}]}]}],"list_price":[{"value":99}]}})")
        << -1.0;
    QTest::newRow("explicit-seller-price")
        << QByteArray(
               R"({"attributes":{"purchasable_offer":[{"marketplace_id":"FR","our_price":[{"schedule":[{"value_with_tax":24.99}]}]}],"list_price":[{"value":99}]}})")
        << 24.99;
    QTest::newRow("other-marketplace-price-is-not-current")
        << QByteArray(
               R"({"offers":[{"marketplaceId":"DE","listingPrice":{"amount":12}}],"attributes":{"purchasable_offer":[{"marketplace_id":"DE","our_price":[{"schedule":[{"value_with_tax":20}]}]}],"list_price":[{"value":99}]}})")
        << -1.0;
    QTest::newRow("discount-alone-is-not-regular-price")
        << QByteArray(
               R"({"attributes":{"purchasable_offer":[{"marketplace_id":"FR","discounted_price":[{"schedule":[{"value_with_tax":10}]}]}]}})")
        << -1.0;
    QTest::newRow("invalid-json") << QByteArray("not json") << -1.0;
    QTest::newRow("empty-document") << QByteArray("{}") << -1.0;
}

void AmazonPricingApiTests::currentPriceOnly() {
    QFETCH(QByteArray, body);
    QFETCH(double, expected);
    QCOMPARE(AmazonPricingApi::parseListingPrice(body, "FR"), expected);
}

void AmazonPricingApiTests::productTypeMatchesMarketplace() {
    QString productType = "stale";
    const QByteArray body =
        R"({"summaries":[{"marketplaceId":"DE","productType":"SHIRT"},{"marketplaceId":"FR","productType":"DRESS"}],"attributes":{"list_price":[{"value":99}]}})";
    QCOMPARE(AmazonPricingApi::parseListingPrice(body, "FR", &productType), -1.0);
    QCOMPARE(productType, QString("DRESS"));
    AmazonPricingApi::parseListingPrice(body, "US", &productType);
    QVERIFY(productType.isEmpty());
}

void AmazonPricingApiTests::regularPricePreservesOtherOfferFields() {
    const QJsonObject discounts{{"schedule", QJsonArray{QJsonObject{{"value_with_tax", 12},
                                                                    {"start_at", "2026-09-01T00:00:00Z"},
                                                                    {"end_at", "2026-09-30T00:00:00Z"}}}}};
    const QJsonObject regular{
        {"marketplace_id", "FR"},
        {"currency", "EUR"},
        {"audience", "ALL"},
        {"our_price", QJsonArray{QJsonObject{{"schedule", QJsonArray{QJsonObject{{"value_with_tax", 20}}}}}}},
        {"discounted_price", QJsonArray{discounts}},
        {"minimum_seller_allowed_price", QJsonArray{QJsonObject{{"value", 10}}}},
        {"maximum_seller_allowed_price", QJsonArray{QJsonObject{{"value", 100}}}}};
    QJsonObject business = regular;
    business["audience"] = "B2B";
    const QJsonArray original{regular, business};
    QString error;
    const auto result = AmazonPricingApi::priceOffers("FR", "EUR", 24.999, original, &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(result.size(), 2);
    QCOMPARE(result[1], original[1]);
    auto after = result[0].toObject();
    QCOMPARE(after.value("our_price")
                 .toArray()[0]
                 .toObject()
                 .value("schedule")
                 .toArray()[0]
                 .toObject()
                 .value("value_with_tax")
                 .toDouble(),
             25.0);
    after.remove("our_price");
    auto before = regular;
    before.remove("our_price");
    QCOMPARE(after, before);
    QVERIFY(AmazonPricingApi::priceOffers("FR", "USD", 25, original, &error).isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY(AmazonPricingApi::priceOffers("US", "USD", 25, original, &error).isEmpty());
    QVERIFY(AmazonPricingApi::priceOffers("FR", "EUR", -1, original, &error).isEmpty());
}

QTEST_GUILESS_MAIN(AmazonPricingApiTests)
#include "tst_amazonpricingapi.moc"
