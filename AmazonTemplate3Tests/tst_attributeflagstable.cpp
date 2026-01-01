#include <QtTest>
#include <QCoreApplication>
#include "AttributeFlagsTable.h"
#include "Attribute.h"

class AttributeFlagsTableTests : public QObject
{
    Q_OBJECT

private slots:
    void testGetSizeFieldIds();
};

void AttributeFlagsTableTests::testGetSizeFieldIds()
{
    // Create a temporary directory for the test
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    
    AttributeFlagsTable table(tempDir.path());
    
    // row 1: Size flag, IDs: "id1", "id2"
    QHash<QString, QString> row1;
    row1[Attribute::AMAZON_V01] = "id1";
    row1[Attribute::AMAZON_V02] = "id2";
    table.recordAttribute(row1, Attribute::Size);
    
    // row 2: No flag, IDs: "id3"
    QHash<QString, QString> row2;
    row2[Attribute::AMAZON_V01] = "id3";
    table.recordAttribute(row2, Attribute::NoFlag);
    
    // row 3: Size flag, IDs: "id1" (duplicate), "id4"
    QHash<QString, QString> row3;
    row3[Attribute::AMAZON_V01] = "id1";
    row3[Attribute::TEMU_EN] = "id4";
    table.recordAttribute(row3, Attribute::Size);
    
    QStringList result = table.getSizeFieldIds();
    
    // Expected: id1, id2, id4. (id3 excluded, id1 deduplicated)
    QCOMPARE(result.size(), 3);
    QVERIFY(result.contains("id1"));
    QVERIFY(result.contains("id2"));
    QVERIFY(result.contains("id4"));
    QVERIFY(!result.contains("id3"));
}

QTEST_MAIN(AttributeFlagsTableTests)
#include "tst_attributeflagstable.moc"
