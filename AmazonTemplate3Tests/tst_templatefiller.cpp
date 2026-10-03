#include <QtTest>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QSet>
#include "xlsxdocument.h"
#include "TemplateFiller.h"
#include "AttributesMandatoryTable.h"

class TemplateFillerTests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void test_getAllFieldIds();
    void test_getFieldIdsToProcess();

private:
    QTemporaryDir m_tempDir;
    QString createTemplateFile(const QString &fileName, const QStringList &headers);
};

void TemplateFillerTests::initTestCase()
{
    QVERIFY(m_tempDir.isValid());
}

void TemplateFillerTests::cleanupTestCase()
{
}

QString TemplateFillerTests::createTemplateFile(const QString &fileName, const QStringList &headers)
{
    QString filePath = m_tempDir.filePath(fileName);
    QXlsx::Document doc(filePath);
    doc.addSheet("Template"); 
    doc.selectSheet("Template");

    // Write headers at row 3 (standard Amazon template V02 location usually)
    for (int i = 0; i < headers.size(); ++i) {
        doc.write(3, i + 1, headers[i]);
    }
    
    int feedProductTypeIdx = headers.indexOf("feed_product_type");
    if (feedProductTypeIdx != -1) {
        doc.write(5, feedProductTypeIdx + 1, "shirt");
    }
    int productTypeValIdx = headers.indexOf("product_type#1.value");
    if (productTypeValIdx != -1) {
        doc.write(7, productTypeValIdx + 1, "shirt");
    }
    
    // Also need to ensure it's recognized as a valid template if strict checks exist.
    // TemplateFiller checks _get_productType(doc).
    // _get_productType looks at row 1, col 1 roughly or "Product Type" header.
    // Let's assume standard layout.
    
    doc.write(1, 1, "TemplateType=fptcustom"); // V02 signature often
    doc.write(1, 2, "shirt"); // Product Type
    
    // Add "Data Definitions" sheet for mandatory attributes check
    doc.addSheet("Data Definitions");
    doc.selectSheet("Data Definitions");
    // Headers at row 2
    doc.write(2, 2, "Field Name"); // col 2 (C)
    doc.write(2, 3, "Mandatory");  // col 3 (D) - Last one will be picked as colIndMandatory
    
    // Data at row 3+
    doc.write(3, 2, "sku");             // col 1 (B) is Field ID. Indices in code are 0-based for variable but cellAt is 1-based?
    // _get_fieldIdMandatory uses: colIndFieldId = 1 (index, so 2nd col) which is B.
    // doc.cellAt(i+1, colIndFieldId + 1) -> doc.cellAt(row, 2). Correct.
    doc.write(3, 2, "sku"); // B3
    doc.write(3, 3, "Part Number"); // C3
    doc.write(3, 4, "Required"); // D3 (matches colIndMandatory which is 3 -> 4th col)
    
    doc.write(3, 4, "Required"); // D3 (matches colIndMandatory which is 3 -> 4th col)
    
    // Add "Valid Values" sheet
    doc.addSheet("Valid Values");
    
    // Switch back to Template sheet
    doc.selectSheet("Template");

    doc.save();
    return filePath;
}

void TemplateFillerTests::test_getAllFieldIds() {
    // using item_sku and feed_product_type as per V02/Standard expectations to avoid assertions if checked
    QString fromPath = createTemplateFile("original.xlsx", {"item_sku", "brand_name", "item_name", "main_image_url", "feed_product_type"});
    
    // Create 'to' paths
    QString toPath1 = createTemplateFile("target1.xlsx", {"item_sku", "brand_name", "color_name", "size_name"});
    QString toPath2 = createTemplateFile("target2.xlsx", {"item_sku", "external_product_id", "item_name"});
    
    QStringList toPaths;
    toPaths << toPath1 << toPath2;
    
    // Setup TemplateFiller args
    QString workingDirCommon = m_tempDir.path();
    QStringList sourcePaths;
    QMap<QString, QString> skuPattern;
    
    // We need real files for TemplateFiller constructor to pass checks
    
    qDebug() << "Constructing TemplateFiller...";
    TemplateFiller filler(workingDirCommon, fromPath, toPaths, sourcePaths, skuPattern);
    qDebug() << "TemplateFiller constructed.";
    
    qDebug() << "Calling getAllFieldIds...";
    QSet<QString> fieldIds = filler.getAllFieldIds();
    qDebug() << "getAllFieldIds returned.";
    
    QSet<QString> expected;
    expected << "item_sku" << "brand_name" << "item_name" << "main_image_url" 
             << "color_name" << "size_name" << "external_product_id" << "feed_product_type";
             
    // Debug output
    if (fieldIds != expected) {
        qDebug() << "Got:" << fieldIds;
        qDebug() << "Expected:" << expected;
    }

    QVERIFY(fieldIds.contains("item_sku"));
    QVERIFY(fieldIds.contains("brand_name"));
    QVERIFY(fieldIds.contains("item_name"));
    QVERIFY(fieldIds.contains("main_image_url"));
    QVERIFY(fieldIds.contains("color_name"));
    QVERIFY(fieldIds.contains("size_name"));
    QVERIFY(fieldIds.contains("external_product_id"));
    QCOMPARE(fieldIds.size(), expected.size());
}

void TemplateFillerTests::test_getFieldIdsToProcess() {
    QTemporaryDir testDir;
    QVERIFY(testDir.isValid());

    // Create attributeFlags.csv
    QFile flagsFile(testDir.filePath("attributeFlags.csv"));
    QVERIFY(flagsFile.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream out(&flagsFile);
    out << "Amazon V01,Amazon V02,Temu,ChildOnly,ChildSameValue,Copy,FillIfPresent,ForCustomInstructions,MandatoryAmazon,MandatoryPartialUpdate,MandatoryTemu,NoAI,ReadablePreviousTemplates,SameValue,Size\n";
    out << "main_image_url,main_product_image_locator#1.media_location,,true,false,true,false,false,true,false,false,true,false,false,false\n";
    out << "other_image_url1,other_product_image_locator_1#1.media_location,,true,false,true,true,false,false,false,false,true,false,false,false\n";
    out << "other_image_url2,other_product_image_locator_2#1.media_location,,true,false,true,true,false,false,false,false,true,false,false,false\n";
    out << ",some_optional_no_copy,,false,false,false,false,false,false,false,false,false,false,false,false\n";
    flagsFile.close();

    // Create mandatoryFieldIds.ini
    QFile mandFile(testDir.filePath("mandatoryFieldIds.ini"));
    QVERIFY(mandFile.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream mandOut(&mandFile);
    mandOut << "[General]\n";
    mandOut << "attrMandatoryAlways=main_product_image_locator#1.media_location\n";
    mandFile.close();

    QString fromPath = testDir.filePath("TEST-001-TOFILL-FR.xlsx");
    {
        QXlsx::Document doc(fromPath);
        doc.addSheet("Template");
        doc.selectSheet("Template");
        doc.write(1, 1, "Settings");
        QStringList headers = {
            "item_sku",
            "product_type#1.value",
            "main_product_image_locator#1.media_location",
            "other_product_image_locator_1#1.media_location",
            "other_product_image_locator_2#1.media_location",
            "some_optional_no_copy"
        };
        for (int i = 0; i < headers.size(); ++i) {
            doc.write(5, i + 1, headers[i]);
        }
        doc.write(6, 1, "ABC123");
        doc.write(7, 1, "PARENT_SKU");
        doc.write(7, 2, "shirt");
        doc.write(8, 1, "CHILD_SKU");
        doc.write(8, 2, "shirt");
        doc.write(8, 3, "https://example.com/main.jpg");
        doc.write(8, 4, "https://example.com/other1.jpg");
        doc.write(8, 5, "https://example.com/other2.jpg");
        doc.addSheet("Data Definitions");
        doc.selectSheet("Data Definitions");
        doc.write(2, 2, "Field Name");
        doc.write(2, 3, "Mandatory");
        doc.write(3, 2, "main_product_image_locator#1.media_location");
        doc.write(3, 3, "Required");
        doc.addSheet("Valid Values");
        doc.selectSheet("Template");
        doc.save();
    }

    QString toPath = testDir.filePath("TEST-001-TOFILL-COM.xlsx");
    {
        QXlsx::Document doc(toPath);
        doc.addSheet("Template");
        doc.selectSheet("Template");
        doc.write(1, 1, "Settings");
        QStringList headers = {
            "item_sku",
            "product_type#1.value",
            "main_product_image_locator#1.media_location",
            "other_product_image_locator_1#1.media_location",
            "other_product_image_locator_2#1.media_location"
        };
        for (int i = 0; i < headers.size(); ++i) {
            doc.write(5, i + 1, headers[i]);
        }
        doc.write(6, 1, "ABC123");
        doc.write(7, 2, "shirt");
        doc.addSheet("Data Definitions");
        doc.addSheet("Valid Values");
        doc.selectSheet("Template");
        doc.save();
    }

    TemplateFiller filler(testDir.path(), fromPath, {toPath}, {}, {});

    // Verify that mandatory IDs do not contain secondary images or optional fields
    const auto mandatoryIds = filler.m_mandatoryAttributesTable->getMandatoryIds();
    QVERIFY(!mandatoryIds.contains("other_product_image_locator_1#1.media_location"));
    QVERIFY(!mandatoryIds.contains("other_product_image_locator_2#1.media_location"));
    QVERIFY(!mandatoryIds.contains("some_optional_no_copy"));

    // Verify that _getFieldIdsToProcess() DOES include optional fields with Copy flag,
    // but excludes optional fields without Copy flag
    const auto processIds = filler._getFieldIdsToProcess();
    QVERIFY(processIds.contains("other_product_image_locator_1#1.media_location"));
    QVERIFY(processIds.contains("other_product_image_locator_2#1.media_location"));
    QVERIFY(!processIds.contains("some_optional_no_copy"));

    // Verify buildAttributes registers Attribute for secondary images
    filler.buildAttributes();
    QVERIFY(filler.m_marketplace_attributeId_attributeInfos["Amazon V02"].contains("other_product_image_locator_1#1.media_location"));

    // Verify _get_sku_fieldId_fromValues reads secondary images from source template
    filler.m_sku_fieldId_fromValues = filler._get_sku_fieldId_fromValues(fromPath);
    QVERIFY(filler.m_sku_fieldId_fromValues["CHILD_SKU"].contains("other_product_image_locator_1#1.media_location"));
    QCOMPARE(filler.m_sku_fieldId_fromValues["CHILD_SKU"]["other_product_image_locator_1#1.media_location"], QString("https://example.com/other1.jpg"));

    // Simulate FillerCopy writing to toValues
    filler.m_countryCode_langCode_sku_fieldId_toValues["COM"]["EN"]["CHILD_SKU"]["other_product_image_locator_1#1.media_location"] = "https://example.com/other1.jpg";

    // Verify _saveTemplates writes optional copied attribute into target FILLED file
    filler._saveTemplates();

    QString filledPath = testDir.filePath("TEST-001-FILLED-COM.xlsx");
    QVERIFY(QFile::exists(filledPath));
    QXlsx::Document docFilled(filledPath);
    docFilled.selectSheet("Template");
    const auto &fieldIdIndexFilled = filler._get_fieldId_index(docFilled);
    int colOther1 = fieldIdIndexFilled.value("other_product_image_locator_1#1.media_location", -1);
    QVERIFY(colOther1 != -1);
    auto cell = docFilled.cellAt(8, colOther1 + 1);
    QVERIFY(cell != nullptr);
    QCOMPARE(cell->value().toString(), QString("https://example.com/other1.jpg"));
}

QTEST_MAIN(TemplateFillerTests)
#include "tst_templatefiller.moc"
