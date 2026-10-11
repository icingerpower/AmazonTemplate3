#include "GsprManufacturerStore.h"

#include <QtTest>

class GsprManufacturerStoreTests : public QObject
{
    Q_OBJECT
private slots:
    void parseBareEmailInManufacturer();
    void parseEmailAddressLabel();
    void parseStandardLabeledEmail();
    void parseUrlInManufacturer();
    void parseEmailWithAngleBrackets();
    void testSkuPrefixes();
};

void GsprManufacturerStoreTests::parseBareEmailInManufacturer()
{
    const QString raw = QStringLiteral(
        "IŞIKLAR AYAKKABICILIK İMALAT SANAYİ TİCARET LİMİTED ŞİRKETİ - "
        "Anadolu Mah. Aysuna Sk. No: 1 K: 4, Arnavutköy / Istanbul, Turkie - "
        "info@conpaxayakkabi.com - +90 212 619 09 10");

    GsprManufacturerEntry e;
    QVERIFY(GsprManufacturerStore::parseManufacturer(raw, &e));
    QCOMPARE(e.name, QStringLiteral("IŞIKLAR AYAKKABICILIK İMALAT SANAYİ TİCARET LİMİTED ŞİRKETİ"));
    QCOMPARE(e.address, QStringLiteral("Anadolu Mah. Aysuna Sk. No: 1 K: 4, Arnavutköy / Istanbul, Turkie"));
    QCOMPARE(e.email, QStringLiteral("info@conpaxayakkabi.com"));
    QCOMPARE(e.phone, QStringLiteral("+90 212 619 09 10"));
    QVERIFY(e.url.isEmpty());
}

void GsprManufacturerStoreTests::parseEmailAddressLabel()
{
    const QString raw = QStringLiteral(
        "Guangzhou Sinniko Stationery Products Company Limited - "
        "Address: Room 704, No.8, Xingmu Street, Xialiang Gaoqiao West Road, Baiyun District, Guangzhou, China - "
        "Email address: nico@sinniko.cn");

    GsprManufacturerEntry e;
    QVERIFY(GsprManufacturerStore::parseManufacturer(raw, &e));
    QCOMPARE(e.name, QStringLiteral("Guangzhou Sinniko Stationery Products Company Limited"));
    QCOMPARE(e.address, QStringLiteral("Room 704, No.8, Xingmu Street, Xialiang Gaoqiao West Road, Baiyun District, Guangzhou, China"));
    QCOMPARE(e.email, QStringLiteral("nico@sinniko.cn"));
    QVERIFY(e.phone.isEmpty());
    QVERIFY(e.url.isEmpty());
}

void GsprManufacturerStoreTests::parseStandardLabeledEmail()
{
    const QString raw = QStringLiteral(
        "Test Factory - Address: 123 Main St, City, Country - Email: contact@factory.com - Tel: +1234567890");

    GsprManufacturerEntry e;
    QVERIFY(GsprManufacturerStore::parseManufacturer(raw, &e));
    QCOMPARE(e.name, QStringLiteral("Test Factory"));
    QCOMPARE(e.address, QStringLiteral("123 Main St, City, Country"));
    QCOMPARE(e.email, QStringLiteral("contact@factory.com"));
    QCOMPARE(e.phone, QStringLiteral("+1234567890"));
}

void GsprManufacturerStoreTests::parseUrlInManufacturer()
{
    const QString raw = QStringLiteral(
        "Test Factory - Address: 123 Main St - https://factory.com - Tel: +1234567890");

    GsprManufacturerEntry e;
    QVERIFY(GsprManufacturerStore::parseManufacturer(raw, &e));
    QCOMPARE(e.url, QStringLiteral("https://factory.com"));
    QVERIFY(e.email.isEmpty());
}

void GsprManufacturerStoreTests::parseEmailWithAngleBrackets()
{
    const QString raw = QStringLiteral(
        "Test Factory - Address: 123 Main St - Email: <contact@factory.com>");

    GsprManufacturerEntry e;
    QVERIFY(GsprManufacturerStore::parseManufacturer(raw, &e));
    QCOMPARE(e.email, QStringLiteral("contact@factory.com"));
}

void GsprManufacturerStoreTests::testSkuPrefixes()
{
    const QString cell = QStringLiteral("TURK-CROCO-BLACK-PINKSO\nTURK-CLASSY-BLACK-PINKSO");
    const QStringList prefixes = GsprManufacturerStore::skuPrefixes(cell);
    QCOMPARE(prefixes.size(), 2);
    QCOMPARE(prefixes.at(0), QStringLiteral("TURK-CROCO-BLACK-PINKSO"));
    QCOMPARE(prefixes.at(1), QStringLiteral("TURK-CLASSY-BLACK-PINKSO"));
}

QTEST_GUILESS_MAIN(GsprManufacturerStoreTests)
#include "tst_gspr_manufacturer_store.moc"
