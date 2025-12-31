
#include <QtTest>
#include <QCoreApplication>

#include <QCoreApplication>
#include "TemplateFiller.h"
#include "ExceptionTemplate.h"

// Mock AbstractFiller or TemplateFiller if needed.
// AbstractFiller is abstract, but FillerSize inherits from it.
// We can instantiate FillerSize directly and call its helper methods via a friend or just testing public execution?
// Or we can expose helpers for testing?
// Given I made them private, I should only test via 'fill' or use friendly access.
// But 'fill' is complex to setup.
// I can make a testable subclass or just use #define private public trick for testing (ugly but works for legacy codebases)
// Or better, just use the 'fill' method and mock the inputs.

// Let's use #define private public just for this test file to test helpers directly, 
// OR just add friend class in the header (cleaner but requires modifying header again).
// Or just test `fill` which is public.
// Testing `fill` requires a lot of setup (TemplateFiller pointer etc).
// `convertClothingSize` and `convertShoeSize` only use their arguments.
// So the easiest way to test logic is to call them. 
// I'll assume I can add `friend class FillerSizeTests;` to `FillerSize.h` or just use the `#define private public` hack 
// since I don't want to modify the header just for tests if I can avoid it.

#define private public
#include "fillers/FillerSize.h"
#undef private

class FillerSizeTests : public QObject
{
    Q_OBJECT

private slots:
    void testConvertClothingSize_data();
    void testConvertClothingSize();
    
    void testConvertShoeSize_data();
    void testConvertShoeSize();
    void testConvertUnit_data();
    void testConvertUnit();
};

void FillerSizeTests::testConvertClothingSize_data()
{
    QTest::addColumn<QString>("countryFrom");
    QTest::addColumn<QString>("countryTo");
    QTest::addColumn<QString>("langTo");
    QTest::addColumn<int>("gender"); // cast to Gender
    QTest::addColumn<int>("age");    // cast to Age
    QTest::addColumn<QString>("productType");
    QTest::addColumn<QVariant>("origValue");
    QTest::addColumn<QVariant>("expected");

    // Female Adult
    // FR 38 -> UK 10?
    // FR 32 (base) -> UK 4 (base) difference is +2 steps? 
    // Wait, the list is:
    /*
      FR 32 -> UK 4
      FR 34 -> UK 6
      FR 36 -> UK 8
      FR 38 -> UK 10
    */
    
    QTest::newRow("FR38_to_UK_Female_Adult") 
        << "FR" << "UK" << "EN" 
        << (int)AbstractFiller::Female << (int)AbstractFiller::Adult
        << "SHIRT" << QVariant("38") << QVariant(10);

    QTest::newRow("FR38_to_DE_Female_Adult") 
        << "FR" << "DE" << "DE" 
        << (int)AbstractFiller::Female << (int)AbstractFiller::Adult
        << "SHIRT" << QVariant("38") << QVariant(36); // DE starts at 30, FR at 32. 38 FR is +6 from 32. 30 + 6 = 36.

    // Male Adult
    // FR 48 (base) -> UK 38 (48-10)
    QTest::newRow("FR48_to_UK_Male_Adult") 
        << "FR" << "UK" << "EN" 
        << (int)AbstractFiller::Male << (int)AbstractFiller::Adult
        << "SHIRT" << QVariant("48") << QVariant(38);

    QTest::newRow("XL_to_BE_FR")
        << "FR" << "BE" << "FR"
        << (int)AbstractFiller::Male << (int)AbstractFiller::Adult
        << "SHIRT" << QVariant("XL") << QVariant("TG");
}

void FillerSizeTests::testConvertClothingSize()
{
    QFETCH(QString, countryFrom);
    QFETCH(QString, countryTo);
    QFETCH(QString, langTo);
    QFETCH(int, gender);
    QFETCH(int, age);
    QFETCH(QString, productType);
    QFETCH(QVariant, origValue);
    QFETCH(QVariant, expected);

    FillerSize filler;
    QVariant result = filler.convertClothingSize(
        countryFrom, countryTo, langTo, 
        (AbstractFiller::Gender)gender, (AbstractFiller::Age)age, 
        productType, origValue);
        
    QCOMPARE(result, expected);
}

void FillerSizeTests::testConvertShoeSize_data()
{
    QTest::addColumn<QString>("countryFrom");
    QTest::addColumn<QString>("countryTo");
    QTest::addColumn<int>("gender"); // cast to Gender
    QTest::addColumn<int>("age");    // cast to Age
    QTest::addColumn<QString>("productType");
    QTest::addColumn<QVariant>("origValue");
    QTest::addColumn<QVariant>("expected");

    // Female Adult Shoes
    // EU 37 -> US ?
    // Base: EU 34 -> US 3-1 = 2.
    // 34 (2), 35 (3), 36 (4), 37 (5)?
    // Let's trace loop:
    /*
      curSizeEu = 34
      groupEu = 34
      otherToAdd = 1 (34 not in 40,41,43,44)
      curSizeUs = 2 + 1 = 3
      ... 
      Wait, logic is:
      firstSizeEu = 34, curSizeUs = 2.
      loop 34: map[34] used. otherToAdd=1. curSizeUs becomes 3. mapUS[3] saved.
      Wait, map is saved AFTER increment.
      
      Loop 1 (34):
        Eu=34.
        otherToAdd=1.
        Us=3.
        List << {EU:34, US:3}
      
      Loop 2 (35):
        Eu=35.
        otherToAdd=1.
        Us=4.
        List << {EU:35, US:4}
      
      Loop 3 (36):
        Eu=36.
        otherToAdd=1.
        Us=5.
        List << {EU:36, US:5}

      Loop 4 (37):
        Eu=37.
        otherToAdd=1.
        Us=6.
        List << {EU:37, US:6}
    */
    QTest::newRow("EU37_to_US_Female_Adult") 
        << "FR" << "COM" // FR is in groupEu, COM is in groupUs
        << (int)AbstractFiller::Female << (int)AbstractFiller::Adult
        << "SHOES" << QVariant("37") << QVariant(6.0);
        
   QTest::newRow("EU38_to_US_Male_Adult") 
        << "FR" << "COM" 
        << (int)AbstractFiller::Male << (int)AbstractFiller::Adult
        << "SHOES" << QVariant("42") << QVariant(9.0);
        
    // Logic Male:
    // firstSizeEu = 38
    // curSizeUs = 4.0
    // Loop 38: sizeEu=38. curSizeUse += 1 -> 5.0. map {EU:38, US:5}
    // Loop 39: sizeEu=39. curSizeUse += 1 -> 6.0. map {EU:39, US:6}
    // Loop 40: sizeEu=40. curSizeUse += 1 -> 7.0. map {EU:40, US:7}
    // Loop 41: sizeEu=41. curSizeUse += 1 -> 8.0. map {EU:41, US:8}
    // Loop 42: sizeEu=42. curSizeUse += 1 -> 9.0. map {EU:42, US:9}
}

void FillerSizeTests::testConvertShoeSize()
{
    QFETCH(QString, countryFrom);
    QFETCH(QString, countryTo);
    QFETCH(int, gender);
    QFETCH(int, age);
    QFETCH(QString, productType);
    QFETCH(QVariant, origValue);
    QFETCH(QVariant, expected);
    
    FillerSize filler;
    QVariant result = filler.convertShoeSize(
        countryFrom, countryTo, 
        (AbstractFiller::Gender)gender, (AbstractFiller::Age)age, 
        productType, origValue);
    
    // Use fuzzy compare for doubles if needed, but QVariant comparison should handle it or we cast
    if (result.typeId() == QMetaType::Double && expected.typeId() == QMetaType::Double) {
        QVERIFY(qAbs(result.toDouble() - expected.toDouble()) < 0.0001);
    } else {
        QCOMPARE(result, expected);
    }
}

void FillerSizeTests::testConvertUnit_data()
{
    QTest::addColumn<QString>("countryTo");
    QTest::addColumn<QVariant>("origValue");
    QTest::addColumn<QVariant>("expected");

    QTest::newRow("10cm_to_US") << "US" << QVariant("10 cm") << QVariant("3.94\"");
    QTest::newRow("10cm_to_FR") << "FR" << QVariant("10 cm") << QVariant("10 cm");
    QTest::newRow("5inch_to_FR") << "FR" << QVariant("5\"") << QVariant("12.7 cm");
    QTest::newRow("5inch_to_US") << "US" << QVariant("5 inch") << QVariant("5 inch");
    QTest::newRow("NoUnit_to_US") << "US" << QVariant("10") << QVariant("10");
    QTest::newRow("MixedUnit_to_DE") << "DE" << QVariant("10in") << QVariant("25.4 cm");
    
    // New mixed unit cases
    // Prefer existing value if available
    QTest::newRow("Mixed_CmFirst_TargetInch") << "US" << QVariant("50 cm / 20 inch") << QVariant("20 inch");
    QTest::newRow("Mixed_InchFirst_TargetCm") << "FR" << QVariant("20 inch / 50 cm") << QVariant("50 cm");
    
    // Check that we don't just convert the first one randomly
    // 100 cm is approx 39.37 inch. If we have 1 inch there, and we want inch, we should return 1 inch.
    QTest::newRow("Mixed_DistinguishValues_TargetInch") << "US" << QVariant("100 cm (1 inch)") << QVariant("1 inch");
    QTest::newRow("Mixed_DistinguishValues_TargetCm") << "FR" << QVariant("1 inch (100 cm)") << QVariant("100 cm");
}

void FillerSizeTests::testConvertUnit()
{
    QFETCH(QString, countryTo);
    QFETCH(QVariant, origValue);
    QFETCH(QVariant, expected);

    FillerSize filler;
    QVariant result = filler.convertUnit(countryTo, origValue);

    QCOMPARE(result.toString(), expected.toString());
}

QTEST_MAIN(FillerSizeTests)
#include "tst_fillersize.moc"
