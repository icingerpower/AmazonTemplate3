#include <QtTest>
#include <QCoreApplication>

#include "OpenAi2.h"
// add necessary includes here

class TestOpenAi2 : public QObject
{
    Q_OBJECT

public:
    TestOpenAi2();
    ~TestOpenAi2();

private slots:
    void initTestCase();
    void cleanupTestCase();
    void test_case1();

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
    OpenAi2::instance()->init(QString::fromUtf8(OPEN_AI_API_KEY),
                              /*maxQueriesSameTime*/ 2,
                              /*maxQueriesImageSameTime*/ 1,
                              /*timeoutMsBetweenImageQueries*/ 30000);
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

QTEST_MAIN(TestOpenAi2)

#include "tst_testopenai2.moc"
