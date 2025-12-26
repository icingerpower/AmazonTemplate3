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
        MandatoryAttributesManager *mgr = MandatoryAttributesManager::instance();
        
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
        QSet<QString> curAll = {"attr1", "attr2"};

        // Use unique product type to avoid cache hits
        QString productType = "TestProduct_" + QString::number(QDateTime::currentMSecsSinceEpoch());
        
        // Execute load as coroutine
        QCoro::waitFor(mgr->load(
            "test_settings.ini", 
            productType, 
            curAll, 
            curMandatory
        ));

        // Validation
        auto mandatory = mgr->getMandatoryIds();
        
        // attr1 should be mandatory (yes)
        QVERIFY(mandatory.contains("attr1"));
        // attr2 should NOT be mandatory (no)
        QVERIFY(!mandatory.contains("attr2"));
        
        // Verify call count?
        // Phase 1: 2 items * 3 calls each = 6 calls.
        // Phase 2: 1 item (attr2) * 5 calls = 5 calls.
        // Total 11 calls.
        // However, retries logic might affect this if validation fails.
        // Phase 1: "invalid_response" -> validation fails. Retries up to maxRetries(3).
        // Total attempts for attr2 phase 1: 3.
        // Total attempts for attr1 phase 1: 1 (if strict unanimous? wait. neededReplies=3. So 3 calls even if success?)
        // OpenAi2::_runStepCollectN runs N times.
        // So attr1: 3 calls. attr2: 3 calls.
        // Phase 2: attr2: 5 calls.
        // Total should be roughly 11.
        QVERIFY(callCount >= 11);
    }
};

QTEST_MAIN(TestMandatoryAttributesManager)
#include "tst_mandatoryattributesmanager.moc"
