#include <QtTest>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QSet>
#include <QFile>

#include "AttributeEquivalentTable.h"

class AttributeEquivalentTableTests : public QObject
{
    Q_OBJECT

public:
    AttributeEquivalentTableTests();
    ~AttributeEquivalentTableTests();

private slots:
    void initTestCase();
    void cleanupTestCase();
    void test_sorted_insertion();

private:
    QTemporaryDir m_tempDir;
};

AttributeEquivalentTableTests::AttributeEquivalentTableTests()
{
}

AttributeEquivalentTableTests::~AttributeEquivalentTableTests()
{
}

void AttributeEquivalentTableTests::initTestCase()
{
    QVERIFY(m_tempDir.isValid());
}

void AttributeEquivalentTableTests::cleanupTestCase()
{
}

void AttributeEquivalentTableTests::test_sorted_insertion()
{
    QDir dir(m_tempDir.path());
    // Ensure we start fresh
    QString workDir = dir.absolutePath();
    
    AttributeEquivalentTable table(workDir);
    
    // Insert "Zebra"
    table.recordAttribute("Zebra", {"Stripes"});
    QCOMPARE(table.rowCount(), 1);
    QCOMPARE(table.data(table.index(0, 0)).toString(), QString("Zebra"));
    
    // Insert "Apple" - should be first
    table.recordAttribute("Apple", {"Fruit"});
    QCOMPARE(table.rowCount(), 2);
    // This expects "Apple" to be at 0, "Zebra" at 1.
    // With current implementation (insert at 0), "Apple" will be at 0, "Zebra" at 1. 
    // Wait, inserting at 0 pushes existing down.
    // 1. Empty. 
    // 2. Insert Zebra at 0 -> [Zebra]
    // 3. Insert Apple at 0 -> [Apple, Zebra]
    // So this check passes even with current implementation!
    
    QCOMPARE(table.data(table.index(0, 0)).toString(), QString("Apple"));
    QCOMPARE(table.data(table.index(1, 0)).toString(), QString("Zebra"));
    
    // Insert "Mouse" - should be middle
    table.recordAttribute("Mouse", {"Squeak"});
    QCOMPARE(table.rowCount(), 3);
    
    // Desired: [Apple, Mouse, Zebra]
    // Current (insert 0): [Mouse, Apple, Zebra]
    
    QCOMPARE(table.data(table.index(0, 0)).toString(), QString("Apple"));
    QCOMPARE(table.data(table.index(1, 0)).toString(), QString("Mouse"));
    QCOMPARE(table.data(table.index(2, 0)).toString(), QString("Zebra"));
}

QTEST_MAIN(AttributeEquivalentTableTests)
#include "tst_attributeequivalenttable.moc"
