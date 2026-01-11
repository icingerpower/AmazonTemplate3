#include <QtTest>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QSet>
#include "xlsxdocument.h"
#include "TemplateFiller.h"

class TemplateFillerTests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void test_getAllFieldIds();

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

QTEST_MAIN(TemplateFillerTests)
#include "tst_templatefiller.moc"
