#include <QtTest>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QSettings>
#include <QFile>

#include "AttributesMandatoryTable.h"
#include "AttributesMandatoryAiTable.h"
#include "OpenAi2.h"

class AttributesMandatoryTableTests : public QObject
{
    Q_OBJECT

public:
    AttributesMandatoryTableTests();
    ~AttributesMandatoryTableTests();

private slots:
    void initTestCase();
    void cleanupTestCase();
    void test_load_and_persistence();
    void test_manual_overrides();
    void test_ai_interaction_flow();
    void test_load_second_if();
    void test_needAiReview();

private:
    QTemporaryDir m_tempDir;
};

AttributesMandatoryTableTests::AttributesMandatoryTableTests()
{
}

AttributesMandatoryTableTests::~AttributesMandatoryTableTests()
{
}

void AttributesMandatoryTableTests::initTestCase()
{
    QVERIFY(m_tempDir.isValid());
}

void AttributesMandatoryTableTests::cleanupTestCase()
{
}

void AttributesMandatoryTableTests::test_load_and_persistence()
{
    QDir dir(m_tempDir.path());
    QString settingsPath = dir.filePath("mandatoryFieldIds.ini");
    QString productType = "shirt";
    QSet<QString> fieldIdMandatory = {"brand_name", "item_name"};
    QSet<QString> prevFieldIdMandatory; // Empty for first load
    QHash<QString, int> fieldId_index;
    fieldId_index["brand_name"] = 0;
    fieldId_index["item_name"] = 1;
    fieldId_index["color_name"] = 2; // Optional initially

    AttributesMandatoryTable table;
    // New signature: load(settingPath, productType, curIds, prevIds, allIds)
    table.load(settingsPath, productType, fieldIdMandatory, prevFieldIdMandatory, fieldId_index);
    
    QCOMPARE(table.rowCount(), 3); // brand_name, item_name, color_name
    QSet<QString> mandatory = table.getMandatoryIds();
    QVERIFY(mandatory.contains("brand_name"));
    QVERIFY(mandatory.contains("item_name"));
    QVERIFY(!mandatory.contains("color_name"));
}

void AttributesMandatoryTableTests::test_manual_overrides()
{
    QTemporaryDir tempDir;
    QString settingsPath = tempDir.filePath("overrides.ini");
    QString productType = "shirt";
    
    QSet<QString> fieldIdMandatory = {"brand_name"};
    QSet<QString> prevFieldIdMandatory;
    QHash<QString, int> fieldId_index;
    fieldId_index["brand_name"] = 0;
    fieldId_index["color_name"] = 1; 

    // 1. Initial Load
    {
        AttributesMandatoryTable table;
        table.load(settingsPath, productType, fieldIdMandatory, prevFieldIdMandatory, fieldId_index);
        QVERIFY(table.getMandatoryIds().contains("brand_name"));
        QVERIFY(!table.getMandatoryIds().contains("color_name"));
        
        // 2. Add manual override (Set color_name as mandatory)
        QModelIndex colorIdx;
        for(int i=0; i<table.rowCount(); ++i) {
            if(table.data(table.index(i, 0)).toString() == "color_name") {
                colorIdx = table.index(i, 1);
                break;
            }
        }
        QVERIFY(colorIdx.isValid());
        table.setData(colorIdx, true, Qt::CheckStateRole);
        
        QVERIFY(table.getMandatoryIds().contains("color_name"));
        
        // 3. Remove manual override (Set brand_name as NOT mandatory)
         QModelIndex brandIdx;
        for(int i=0; i<table.rowCount(); ++i) {
            if(table.data(table.index(i, 0)).toString() == "brand_name") {
                brandIdx = table.index(i, 1);
                break;
            }
        }
        table.setData(brandIdx, false, Qt::CheckStateRole);
        QVERIFY(!table.getMandatoryIds().contains("brand_name"));
    }
    
    // 4. Persistence Reload
    {
        AttributesMandatoryTable table;
        table.load(settingsPath, productType, fieldIdMandatory, prevFieldIdMandatory, fieldId_index);
        
        QVERIFY(table.getMandatoryIds().contains("color_name")); // Should persist
        QVERIFY(!table.getMandatoryIds().contains("brand_name")); // Should persist removal
    }
}

void AttributesMandatoryTableTests::test_ai_interaction_flow()
{
    // Validate that we can emulate the flow:
    // 1. AiTable decides X is mandatory.
    // 2. We pass X in 'curTemplateFieldIdsMandatory' (simulating merge) OR
    //    Does Table logic support strictly "Previous Template says it was mandatory"?
    
    // Test: Previous template had "size_name" as mandatory. Current template says Optional.
    // If we pass "size_name" in prevFieldIdMandatory, does it become mandatory?
    
    QTemporaryDir tempDir;
    QString settingsPath = tempDir.filePath("flow.ini");
    QString productType = "shirt";
    QSet<QString> curMandatory = {"brand_name"};
    QSet<QString> prevMandatory = {"brand_name", "size_name"}; // size_name was mandatory before
    QHash<QString, int> fieldId_index;
    fieldId_index["brand_name"] = 0;
    fieldId_index["size_name"] = 1;
    
    AttributesMandatoryTable table;
    table.load(settingsPath, productType, curMandatory, prevMandatory, fieldId_index);
    
    QSet<QString> effective = table.getMandatoryIds();
    QVERIFY(effective.contains("brand_name"));
    // Depending on logic, if previous had it, does it stick?
    // User logic: "Logic for effective mandatory status: (Initial && !Removed) || Added"
    // Initial usually means "Current Template + Previous Template + AI"?
    // Or just Current Template?
    // If the goal is "Don't ask again if AI said yes", then AI results should have been merged into 'curMandatory' or passed somehow.
    // If I cannot change Table, I assume 'prevMandatory' plays a role.
    // Let's assume the table respects 'prevMandatory' as a baseline.
    QVERIFY(effective.contains("size_name")); 
}
void AttributesMandatoryTableTests::test_load_second_if()
{
    QTemporaryDir tempDir;
    QString settingsPath = tempDir.filePath("load_second_if.ini");
    QString productType = "shirt";

    // Current and previous mandatory ids (same set)
    QSet<QString> curMandatory = {"brand_name", "item_name"};
    QSet<QString> prevMandatory = {"brand_name", "item_name"};
    QHash<QString, int> fieldId_index;
    fieldId_index["brand_name"] = 0;
    fieldId_index["item_name"] = 1;
    fieldId_index["color_name"] = 2;

    // First load – file does not exist, should trigger second if block (load from previous template)
    {
        AttributesMandatoryTable table;
        table.load(settingsPath, productType, curMandatory, prevMandatory, fieldId_index);
        QSet<QString> mandatory = table.getMandatoryIds();
        QVERIFY(mandatory == curMandatory);
        QVERIFY(!mandatory.isEmpty());
        // needAiReview should be false because we loaded from previous template
        QVERIFY(!table.needAiReview());
    }

    // Second load – settings now contain saved mandatory ids, should trigger first if block
    {
        AttributesMandatoryTable table2;
        table2.load(settingsPath, productType, curMandatory, prevMandatory, fieldId_index);
        QSet<QString> mandatory2 = table2.getMandatoryIds();
        QVERIFY(mandatory2 == curMandatory);
        QVERIFY(!table2.needAiReview());
    }

    // Cleanup
    QFile::remove(settingsPath);
}

void AttributesMandatoryTableTests::test_needAiReview()
{
    QTemporaryDir tempDir;
    QString settingsPath = tempDir.filePath("need_ai_review.ini");
    QString productType = "shirt";

    QSet<QString> curMandatory; // empty
    QSet<QString> prevMandatory; // empty
    QHash<QString, int> fieldId_index;
    fieldId_index["brand_name"] = 0;
    fieldId_index["item_name"] = 1;

    AttributesMandatoryTable table;
    table.load(settingsPath, productType, curMandatory, prevMandatory, fieldId_index);
    // Since there is no prior data, needAiReview should be true
    QVERIFY(table.needAiReview());

    // Cleanup
    QFile::remove(settingsPath);
}

QTEST_MAIN(AttributesMandatoryTableTests)
#include "tst_attributesmandatorytable.moc"
