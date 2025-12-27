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
    }

    void testLoadPhases() {
        // Clear settings file
        QFile::remove("test_settings.ini");

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
            "test_settings.ini", 
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
};

QTEST_MAIN(TestMandatoryAttributesManager)
#include "tst_mandatoryattributesmanager.moc"
