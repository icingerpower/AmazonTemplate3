#include <QtTest>
#include <QCoreApplication>

#include "OpenAi2.h"
#include "ExceptionOpenAiNotInitialized.h"
// add necessary includes here

class TestOpenAi2 : public QObject
{
    Q_OBJECT

public:
    TestOpenAi2();
    ~TestOpenAi2();
    
private slots:
    void initTestCase();
    void test_case1();
    void cleanupTestCase();
    void test_init_defaults();
    void idempotency();
    void reject_before_init();
    void step_retry_logic();
    void step_apply_once();
    void validate_failure_retry();
    void all_failures_max_retries();
    void model_selection_logic();
    void caching_hit();
    void caching_miss();
    void verify_step_callbacks();

private:
    void setFakeTransport(std::function<void(const QString&, const QString&, const QList<QString>&, std::function<void(QString)>, std::function<void(QString)>)> transport);
    void setFakeTransportText(const QString& replyText);
};

TestOpenAi2::TestOpenAi2()
{
}

TestOpenAi2::~TestOpenAi2()
{
}

void TestOpenAi2::initTestCase()
{
#if !defined(OPEN_AI_API_KEY)
    QSKIP("OPEN_AI_API_KEY not defined. Set env OPEN_AI_API_KEY and rebuild tests to enable this integration test.");
#else
    OpenAi2::instance()->init(QString::fromUtf8(OPEN_AI_API_KEY)
                              , 2 // maxQueriesSameTime
                              , 1 //maxQueriesImageSameTime
                              , 30000 //timeoutMsBetweenImageQueries
                           );
#endif
}

void TestOpenAi2::cleanupTestCase()
{
}

void TestOpenAi2::test_case1()
{
#if !defined(OPEN_AI_API_KEY)
    QSKIP("OPEN_AI_API_KEY not defined. Integration test skipped.");
#else
    auto step = QSharedPointer<OpenAi2::Step>::create();
    step->id = "basic_yes";
    step->name = "Basic yes test";
    step->cachingKey = "basic_yes_test_v1"; // optional; remove if you don't want cache involved
    step->maxRetries = 2;

    step->getPrompt = [](int /*nAttempts*/) -> QString {
        return QStringLiteral(
            "Reply with exactly one word: yes\n"
            "No punctuation. No extra text. Lowercase only."
        );
    };

    QString finalReply;
    step->validate = [&](const QString& gptReply, const QString& /*lastWhy*/) -> bool {
        // keep validation strict for a contract test
        const QString s = gptReply.trimmed().toLower();
        if (s == "yes") {
            finalReply = s;
            return true;
        }
        return false;
    };

    step->apply = [&](const QString& gptReply) {
        finalReply = gptReply.trimmed().toLower();
    };

    QList<QSharedPointer<OpenAi2::Step>> steps;
    steps << step;

    bool finished = false;
    bool success = false;
    QString failure;

    // Use your public API; it doesn't expose callbacks, so we gate completion using apply/validate + event loop timeout.
    // If your askGpt emits signals, use QSignalSpy instead (preferred).
    OpenAi2::instance()->askGpt(steps, /*model*/ QStringLiteral("gpt-5-mini"));

    // Wait up to 30s for finalReply to become "yes"
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < 30000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (!finalReply.isEmpty()) {
            finished = true;
            success = (finalReply == "yes");
            break;
        }
        QTest::qWait(25);
    }

    if (!finished) {
        QFAIL("Timeout waiting for OpenAI reply. Network/API may be down or askGpt did not complete.");
    }

    if (!success) {
        failure = QString("Unexpected reply: '%1'").arg(finalReply);
        QFAIL(qPrintable(failure));
    }

    QCOMPARE(finalReply, QStringLiteral("yes"));
#endif
}


void TestOpenAi2::setFakeTransport(std::function<void(const QString&, const QString&, const QList<QString>&, std::function<void(QString)>, std::function<void(QString)>)> transport)
{
    OpenAi2::instance()->m_transport = transport;
}

void TestOpenAi2::setFakeTransportText(const QString& replyText)
{
    setFakeTransport([replyText](const QString&, const QString&, const QList<QString>&, std::function<void(QString)> onOk, std::function<void(QString)>) {
        // Simulate async
        QTimer::singleShot(0, [onOk, replyText]() {
            onOk(replyText);
        });
    });
}

// A. Init sets defaults and enables requests
// NOTE: initTestCase already called init() with specific params (2, 1, 30000).
// We verify that state here.
void TestOpenAi2::test_init_defaults()
{
    auto ai = OpenAi2::instance();
    QVERIFY(ai->m_initialized);
    // initTestCase sets maxQueriesSameTime to 2
    QCOMPARE(ai->m_maxQueriesSameTime, 2);
    
    // Check key matches global define if available
#ifdef OPEN_AI_API_KEY
    QCOMPARE(ai->m_openAiKey, QString::fromUtf8(OPEN_AI_API_KEY));
#endif
}

// B. Init idempotency
void TestOpenAi2::idempotency()
{
    auto ai = OpenAi2::instance();
    // Use a temp key to test re-init
    ai->init("temp-key-idempotency", 10);
    QCOMPARE(ai->m_maxQueriesSameTime, 10);
    QCOMPARE(ai->m_openAiKey, QString("temp-key-idempotency"));
    
    // Restore original state for other tests (especially contract test)
#ifdef OPEN_AI_API_KEY
    ai->init(QString::fromUtf8(OPEN_AI_API_KEY), 2, 1, 30000);
#endif
}

// C. Reject ask before init
void TestOpenAi2::reject_before_init()
{
    // We must forcibly de-init.
    auto ai = OpenAi2::instance();
    ai->m_initialized = false;
    
    QSharedPointer<OpenAi2::Step> step = QSharedPointer<OpenAi2::Step>::create();
    step->id = "test";
    step->getPrompt = [](int) { return "test_prompt"; };
    step->validate = [](const QString&, const QString&) { return true; };
    step->apply = [](const QString&) {};
    QList<QSharedPointer<OpenAi2::Step>> steps;
    steps << step;

    bool threw = false;
    try {
        ai->askGpt(steps, "gpt-demo");
    } catch (const ExceptionOpenAiNotInitialized&) {
        threw = true;
    }
    QVERIFY(threw);
    
    // Restore logic
    // We can just set m_initialized = true because other members (key etc) weren't cleared by m_initialized=false assignment.
    // However, safest is to re-call init to be consistent.
#ifdef OPEN_AI_API_KEY
    ai->init(QString::fromUtf8(OPEN_AI_API_KEY), 2, 1, 30000);
#else
    ai->m_initialized = true; // Fallback if no key
#endif
}

// D. Step getPrompt called with correct attempt (force retries)
void TestOpenAi2::step_retry_logic()
{
    auto ai = OpenAi2::instance();
    // ai->init("k"); // Don't clobber singleton state; already initialized.

    int attemptCount = 0;
    QList<int> attemptsSeen;

    auto step = QSharedPointer<OpenAi2::Step>::create();
    step->id = "retry_chk";
    step->maxRetries = 3;
    step->getPrompt = [&](int n) {
        attemptsSeen << n;
        attemptCount++;
        return "p";
    };
    step->validate = [](const QString&, const QString&) { return false; }; // Always fail to force retry
    step->apply = [](const QString&) {};

    QList<QSharedPointer<OpenAi2::Step>> steps; 
    steps << step;

    // Mock transport to always succeed network but fail validation
    setFakeTransportText("ok");

    QEventLoop loop;
    ai->_runQueue(steps, [&](){ loop.quit(); }, [&](QString){ loop.quit(); });
    // Don't wait forever
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    loop.exec();

    // maxRetries=3 -> attempts 0, 1, 2. (3 attempts total)
    QCOMPARE(attemptCount, 3);
    QCOMPARE(attemptsSeen, (QList<int>{0, 1, 2}));
}

// E. Step apply called exactly once on success
void TestOpenAi2::step_apply_once()
{
    auto ai = OpenAi2::instance();
    int applyCount = 0;
    auto step = QSharedPointer<OpenAi2::Step>::create();
    step->id = "apply_chk";
    step->getPrompt = [](int) { return "p"; };
    step->validate = [](const QString&, const QString&) { return true; };
    step->apply = [&](QString) { applyCount++; };
    
    setFakeTransportText("resp");
    QList<QSharedPointer<OpenAi2::Step>> steps; steps << step;
    
    QEventLoop loop;
    ai->_runQueue(steps, [&](){ loop.quit(); }, [&](QString){ loop.quit(); });
    loop.exec();
    
    QCOMPARE(applyCount, 1);
}

// F. Validate failure triggers retry until success
void TestOpenAi2::validate_failure_retry()
{
    auto ai = OpenAi2::instance();
    int calls = 0;
    auto step = QSharedPointer<OpenAi2::Step>::create();
    step->id = "val_retry";
    step->maxRetries = 5;
    step->validate = [&](const QString&, const QString&) {
        calls++;
        return (calls >= 3); // Fail 2 times, succeed on 3rd
    };
    step->getPrompt = [](int) { return "p"; };
    step->apply = [](const QString&) {};
    
    setFakeTransportText("ok");
    QList<QSharedPointer<OpenAi2::Step>> steps; steps << step;
    
    QEventLoop loop;
    bool success = false;
    ai->_runQueue(steps, [&](){ success=true; loop.quit(); }, [&](QString){ loop.quit(); });
    loop.exec();
    
    QVERIFY(success);
    QCOMPARE(calls, 3);
}

// G. Stops after maxRetries (validate always false)
void TestOpenAi2::all_failures_max_retries()
{
    auto ai = OpenAi2::instance();
    auto step = QSharedPointer<OpenAi2::Step>::create();
    step->id = "fail_test";
    step->maxRetries = 2;
    step->validate = [](const QString&, const QString&) { return false; };
    step->getPrompt = [](int) { return "p"; };
    step->apply = [](const QString&) {};
    
    setFakeTransportText("ok");
    QList<QSharedPointer<OpenAi2::Step>> steps; steps << step;

    QEventLoop loop;
    bool reachedSuccess = false;
    // When retries exhausted, it calls apply() then stores cache then SUCCESS callback (per code logic: even if validate failed, if max retries reached, it finishes step as 'success' effectively, or at least proceeds).
    // Wait, let's check code: "if ((*attempt) >= maxRetries) { if (step->apply)... this->_storeCache... if (onStepSuccess)... return; }"
    // So yes, it calls success even if validation failed on last attempt.
    
    ai->_runQueue(steps, [&](){ reachedSuccess=true; loop.quit(); }, [&](QString){ loop.quit(); });
    loop.exec();
    
    QVERIFY(reachedSuccess); 
}

// H. Model selection before/after half failures
void TestOpenAi2::model_selection_logic()
{
    auto ai = OpenAi2::instance();
    QList<QString> modelsUsed;
    
    auto step = QSharedPointer<OpenAi2::Step>::create();
    step->id = "model_sel";
    step->gptModel = "A";
    step->gptModelAfterHalfFailure = "B";
    step->maxRetries = 4; // half = 2. attempts 0,1,2,3.
    // 0 <= 2 -> A
    // 1 <= 2 -> A
    // 2 <= 2 -> A 
    // Wait, code says: "if (attempt > half) return afterHalf;"
    // 4/2 = 2.
    // 0: A
    // 1: A
    // 2: A
    // 3: B
    
    // 3: B
    
    step->validate = [](const QString&, const QString&) { return false; };
    step->getPrompt = [](int) { return "p"; };
    step->apply = [](const QString&) {};
    setFakeTransport([&](const QString& model, const QString&, const QList<QString>&, std::function<void(QString)> onOk, std::function<void(QString)>) {
        modelsUsed << model;
        QTimer::singleShot(0, [onOk](){ onOk("ok"); });
    });

    QList<QSharedPointer<OpenAi2::Step>> steps; steps << step;
    
    QEventLoop loop;
    ai->_runQueue(steps, [&](){ loop.quit(); }, [&](QString){ loop.quit(); });
    loop.exec();
    
    QCOMPARE(modelsUsed.size(), 4);
    QCOMPARE(modelsUsed[0], QString("A"));
    QCOMPARE(modelsUsed[1], QString("A"));
    QCOMPARE(modelsUsed[2], QString("A"));
    QCOMPARE(modelsUsed[3], QString("B"));
}

// I. Caching hit bypasses network
void TestOpenAi2::caching_hit()
{
    auto ai = OpenAi2::instance();
    auto step = QSharedPointer<OpenAi2::Step>::create();
    step->id = "cache_hit";
    step->cachingKey = "tests_cache_hit_k";
    
    // Store manually
    ai->_storeCache(*step, "cached_val");
    
    bool netCalled = false;
    setFakeTransport([&](const QString&, const QString&, const QList<QString>&, std::function<void(QString)>, std::function<void(QString)>) {
        netCalled = true;
    });

    QString val;
    step->getPrompt = [](int) { return "p"; };
    step->validate = [](const QString&, const QString&) { return true; };
    step->apply = [&](QString s) { val = s; };
    
    QList<QSharedPointer<OpenAi2::Step>> steps; steps << step;
    QEventLoop loop;
    ai->_runQueue(steps, [&](){ loop.quit(); }, [&](QString){ loop.quit(); });
    loop.exec();
    
    QVERIFY(!netCalled);
    QCOMPARE(val, QString("cached_val"));
}

// J. Caching miss writes cache and next run is hit
void TestOpenAi2::caching_miss()
{
    auto ai = OpenAi2::instance();
    // Clear cache first (brute force remove file or use unique key)
    QString uniqueKey = "cache_miss_" + QString::number(QDateTime::currentMSecsSinceEpoch());
    
    auto step = QSharedPointer<OpenAi2::Step>::create();
    step->id = "cache_miss";
    step->cachingKey = uniqueKey;
    step->getPrompt = [](int) { return "p"; };
    step->validate = [](const QString&, const QString&) { return true; };
    step->apply = [](const QString&) {};
    
    int netCalls = 0;
    setFakeTransport([&](const QString&, const QString&, const QList<QString>&, std::function<void(QString)> onOk, std::function<void(QString)>) {
        netCalls++;
        QTimer::singleShot(0, [onOk](){ onOk("fresh_val"); });
    });

    QList<QSharedPointer<OpenAi2::Step>> steps; steps << step;
    
    // Run 1: Miss
    QEventLoop loop;
    ai->_runQueue(steps, [&](){ loop.quit(); }, [&](QString){ loop.quit(); });
    loop.exec();
    QCOMPARE(netCalls, 1);
    
    QCOMPARE(netCalls, 1);
    
    // Run 2: Hit
    QEventLoop loop2;
    ai->_runQueue(steps, [&](){ loop2.quit(); }, [&](QString){ loop2.quit(); });
    loop2.exec();
    QCOMPARE(netCalls, 1); // No increment
}

// Ensure getPrompt, validate, and apply are correctly used in the flow
void TestOpenAi2::verify_step_callbacks()
{
    auto ai = OpenAi2::instance();
    auto step = QSharedPointer<OpenAi2::Step>::create();
    step->id = "callback_flow";
    
    // 1. getPrompt returns specific text
    step->getPrompt = [](int) { return "PROMPT_PAYLOAD"; };
    
    // 2. Transport should receive exactly "PROMPT_PAYLOAD"
    bool promptMatch = false;
    setFakeTransport([&](const QString& model, const QString& prompt, const QList<QString>&, std::function<void(QString)> onOk, std::function<void(QString)>) {
        if (prompt == "PROMPT_PAYLOAD") promptMatch = true;
        // Reply with specific text
        QTimer::singleShot(0, [onOk](){ onOk("GPT_REPLY"); });
    });
    
    // 3. validate receives "GPT_REPLY"
    bool validateCalled = false;
    step->validate = [&](const QString& reply, const QString&) {
        validateCalled = (reply == "GPT_REPLY");
        return true;
    };
    
    // 4. apply receives "GPT_REPLY"
    bool applyCalled = false;
    step->apply = [&](const QString& reply) {
        applyCalled = (reply == "GPT_REPLY");
    };

    QList<QSharedPointer<OpenAi2::Step>> steps; steps << step;
    
    QEventLoop loop;
    ai->_runQueue(steps, [&](){ loop.quit(); }, [&](QString){ loop.quit(); });
    loop.exec();
    
    QVERIFY(promptMatch);
    QVERIFY(validateCalled);
    QVERIFY(applyCalled);
}

QTEST_MAIN(TestOpenAi2)

#include "tst_testopenai2.moc"
