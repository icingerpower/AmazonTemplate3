#include <QtTest>
#include <QCoreApplication>
#include <QSet>
#include <QCoro/QCoroTask>
#include <QCoro/QCoroFuture>
#include <QCoroTest>
#include "MandatoryAttributesManager.h"
#include "OpenAi2.h"

class TestMandatoryAttributesManager : public QObject
{
    Q_OBJECT

public:
    TestMandatoryAttributesManager() {}
    ~TestMandatoryAttributesManager() {}

    int m_attr2_calls = 0;

private slots:
    void initTestCase() {
        // Init OpenAi2 with mock transport
        OpenAi2::instance()->init("test-key");
    }

    void cleanupTestCase() {
        OpenAi2::instance()->resetForTests();
        QFile::remove("test_settings.ini");
        QFile::remove("test_settings_table.ini");
        QFile::remove("test_settings_reviewed.ini");
    }

    void testLoadPhases() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QString settingsFile = tempDir.filePath("test_settings.ini");

        // Setup
        MandatoryAttributesManager mgr;
        
        // Prepare mock transport
        m_attr2_calls = 0;
        int callCount = 0;

        OpenAi2::instance()->setTransportForTests(
            [&, this](const QString& model, const QString& prompt, const QList<QString>&, std::function<void(QString)> ok, std::function<void(OpenAi2::TransportError)>) {
                callCount++;
                // Simulate async reply
                QTimer::singleShot(0, [ok, model, prompt, this](){
                    if (model == "gpt-5-mini") {
                        // Phase 1
                        if (prompt.contains("attr1")) ok("yes");
                        else if (prompt.contains("attr2")) {
                             // Return mixed replies to cause "undecided"
                             this->m_attr2_calls++;
                             if (this->m_attr2_calls % 2 != 0) {
                                 ok("yes"); 
                             }
                             else {
                                 ok("no");
                             }
                        }
                    } else if (model == "gpt-5.2") {
                        // Phase 2
                        if (prompt.contains("attr2")) ok("no");
                    } else {
                        ok("unknown_model");
                    }
                });
            }
        );

        // Current template mandatory: none
        QSet<QString> curMandatory;
        // All current: attr1, attr2
        QHash<QString, int> curAll = {{"attr1", 0}, {"attr2", 1}};

        // Use unique product type to avoid cache hits
        QString productType = "TestProduct_" + QString::number(QDateTime::currentMSecsSinceEpoch());
        
        // Execute load as coroutine
        QCoro::waitFor(mgr.load(
            "dummy_template.csv",
            settingsFile, 
            productType, 
            curAll, 
            curMandatory
        ));

        // Validation
        auto mandatory = mgr.getMandatoryIds();
        
        // attr1 should be mandatory (yes)
        QVERIFY(mandatory.contains("attr1"));
        // attr2 should NOT be mandatory (no)
        QVERIFY(!mandatory.contains("attr2"));
        
        QVERIFY(callCount >= 11);
    }

    void test_table_model() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QString settingsFile = tempDir.filePath("test_settings_table.ini");

        MandatoryAttributesManager mgr;
        
        QHash<QString, int> curAll = {
            {"c_attr_man", 0}, 
            {"b_attr_opt", 1},
            {"a_id", 2}
        }; 
        QSet<QString> curMandatory = {"c_attr_man", "a_id"};
        
        // Mock transport for b_attr_opt (not mandatory, so load will ask AI)
        OpenAi2::instance()->setTransportForTests(
            [&](const QString&, const QString&, const QList<QString>&, std::function<void(QString)> ok, std::function<void(OpenAi2::TransportError)>) {
                QTimer::singleShot(0, [ok](){ ok("no"); });
            }
        );

        // Mock load
        QCoro::waitFor(mgr.load(
            "dummy.csv", settingsFile, "SortTest", curAll, curMandatory
        ));
        
        // --- Test Rows & Sorting ---
        // Expected sort: Mandatory first, then Alpha.
        // c_attr_man (Mandatory) -> should be first? "c_..."
        // a_id (Mandatory) -> "a_..."
        // b_attr_opt (Not Mandatory) -> last
        // Between c and a (both mandatory): a_id comes before c_attr_man alphabetically.
        // So order: a_id, c_attr_man, b_attr_opt.

        // Actually, logic is: Mandatory > Not Mandatory.
        // effectivelyMandatory = (Initial && !Removed) || Added.
        // Initially: c=Man, a=Man, b=Not.
        // All effectively Mandatory except b.
        // Sort: (a, c) group first, then b.
        // (a, c) sorted by name -> a_id, c_attr_man.
        
        QCOMPARE(mgr.rowCount(), 3);
        QCOMPARE(mgr.columnCount(), 3);
        
        QCOMPARE(mgr.data(mgr.index(0, 0)).toString(), "a_id");
        QCOMPARE(mgr.data(mgr.index(1, 0)).toString(), "c_attr_man");
        QCOMPARE(mgr.data(mgr.index(2, 0)).toString(), "b_attr_opt");
        
        // --- Test Data Roles ---
        // Col 1: Initially Mandatory
        QCOMPARE(mgr.data(mgr.index(0, 1), Qt::CheckStateRole).toInt(), Qt::Checked); // a_id
        QCOMPARE(mgr.data(mgr.index(2, 1), Qt::CheckStateRole).toInt(), Qt::Unchecked); // b_attr_opt
        
        // Col 2: Editable
        QCOMPARE(mgr.data(mgr.index(0, 2), Qt::CheckStateRole).toInt(), Qt::Checked); // a_id
        
        // --- Test Editing ---
        // Uncheck a_id (Col 2)
        QAbstractTableModel* model = &mgr;
        model->setData(mgr.index(0, 2), Qt::Unchecked, Qt::CheckStateRole);
        
        // Verify Effective state
        QCOMPARE(mgr.data(mgr.index(0, 2), Qt::CheckStateRole).toInt(), Qt::Unchecked);
        // Verify Persistence Logic
        // Was Initial, now Removed -> should be in removed set?
        // Wait, m_idsRemovedManually is private. We can check via getMandatoryIds().
        QVERIFY(!mgr.getMandatoryIds().contains("a_id"));
        
        // Check b_attr_opt (Col 2)
        model->setData(mgr.index(2, 2), Qt::Checked, Qt::CheckStateRole);
        QCOMPARE(mgr.data(mgr.index(2, 2), Qt::CheckStateRole).toInt(), Qt::Checked);
        QVERIFY(mgr.getMandatoryIds().contains("b_attr_opt"));
        
        // Re-sorting does not happen automatically unless we reload?
        // Changing data emits dataChanged but doesn't resort internal list immediately usuallly.
        // The implementation computes order in `load`. `setData` just updates sets.
        // So order remains: a_id, c_attr_man, b_attr_opt.
        QCOMPARE(mgr.data(mgr.index(2, 0)).toString(), "b_attr_opt");
    }

    void test_persistence_reviewed() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QString settingsFile = tempDir.filePath("test_settings_reviewed.ini");
        
        int calls = 0;
        OpenAi2::instance()->setTransportForTests(
            [&](const QString&, const QString&, const QList<QString>&, std::function<void(QString)> ok, std::function<void(OpenAi2::TransportError)>) {
                calls++;
                QTimer::singleShot(0, [ok](){ ok("yes"); });
            }
        );

        QHash<QString, int> curAll = {{"new_attr", 0}};
        QSet<QString> curMandatory; // empty

        // 1. First run, AI called
        {
            MandatoryAttributesManager mgr;
            QCoro::waitFor(mgr.load("t1", settingsFile, "TypeA", curAll, curMandatory));
            QVERIFY(calls > 0);
        }

        calls = 0;
        // 2. Second run, same product type -> should load reviewed attributes -> NO AI calls
        {
            MandatoryAttributesManager mgr;
            QCoro::waitFor(mgr.load("t2", settingsFile, "TypeA", curAll, curMandatory));
            QCOMPARE(calls, 0); 
        }

        calls = 0;
        // 3. Different product type -> AI called
        {
            MandatoryAttributesManager mgr;
            QCoro::waitFor(mgr.load("t3", settingsFile, "TypeB", curAll, curMandatory));
            QVERIFY(calls > 0);
        }
    }
};

QTEST_MAIN(TestMandatoryAttributesManager)
#include "tst_mandatoryattributesmanager.moc"
