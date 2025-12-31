#include <QtTest>
#include <QCoreApplication>

#include "AttributeValueReplacedTable.h"

class AttributeValueReplacedTableTests : public QObject
{
    Q_OBJECT

public:
    AttributeValueReplacedTableTests();
    ~AttributeValueReplacedTableTests();

private slots:
    void initTestCase();
    void cleanupTestCase();
    void test_recordAndContains();
    void test_replaceIfContains();

private:
    QString m_workingDir;
};

AttributeValueReplacedTableTests::AttributeValueReplacedTableTests()
{

}

AttributeValueReplacedTableTests::~AttributeValueReplacedTableTests()
{

}

void AttributeValueReplacedTableTests::initTestCase()
{
    m_workingDir = QDir::currentPath() + "/test_data";
    QDir dir(m_workingDir);
    if (dir.exists())
        dir.removeRecursively();
    dir.mkpath(".");
}

void AttributeValueReplacedTableTests::cleanupTestCase()
{
     QDir dir(m_workingDir);
     if (dir.exists())
         dir.removeRecursively();
}

void AttributeValueReplacedTableTests::test_recordAndContains()
{
    AttributeValueReplacedTable table(m_workingDir);
    
    QString marketplace = "Amazon.com";
    QString country = "US";
    QString lang = "en";
    QString productType = "SHIRT";
    QString fieldId = "brand";
    QString valFrom = "Nike";
    QString valTo = "Adidas";

    QVERIFY(!table.contains(marketplace, country, lang, productType, fieldId, valFrom));

    table.recordAttribute(marketplace, country, lang, productType, fieldId, valFrom, valTo);

    QVERIFY(table.contains(marketplace, country, lang, productType, fieldId, valFrom));
}

void AttributeValueReplacedTableTests::test_replaceIfContains()
{
    AttributeValueReplacedTable table(m_workingDir);

    QString marketplace = "Amazon.fr";
    QString country = "FR";
    QString lang = "fr";
    QString productType = "PANTS";
    QString fieldId = "color";
    QString valFrom = "Rouge";
    QString valTo = "Red";

    table.recordAttribute(marketplace, country, lang, productType, fieldId, valFrom, valTo);

    QString val = "Rouge";
    QVERIFY(table.replaceIfContains(marketplace, country, lang, productType, fieldId, val));
    QCOMPARE(val, valTo);

    val = "Bleu";
    QVERIFY(!table.replaceIfContains(marketplace, country, lang, productType, fieldId, val));
    QCOMPARE(val, QString("Bleu"));
    
    // Test set replacement
    QSet<QString> values;
    values.insert("Rouge");
    values.insert("Bleu");
    
    table.replaceIfContains(marketplace, country, lang, productType, fieldId, values);
    QVERIFY(values.contains("Red"));
    QVERIFY(values.contains("Bleu"));
    QVERIFY(!values.contains("Rouge"));
}

QTEST_MAIN(AttributeValueReplacedTableTests)

#include "tst_attributevaluereplacedtable.moc"
