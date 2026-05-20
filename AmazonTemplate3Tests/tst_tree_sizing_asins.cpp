#include <QtTest>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QEventLoop>
#include <QSignalSpy>
#include <QDate>
#include <QString>
#include <QByteArray>
#include <QStringList>
#include <QMap>
#include <QCoreApplication>

#include "apis/AmazonCatalogApi.h"
#include "apis/TreeSizingAsins.h"

// ---------------------------------------------------------------------------
// Mock JSON helpers
// ---------------------------------------------------------------------------

static QByteArray makeChildJson(const QString& asin, const QString& parentAsin,
                                const QString& color, const QString& size, bool hasSizeChart)
{
    QString sizeChartArr = hasSizeChart
        ? QStringLiteral("[{\"marketplace_id\":\"A13V1IB3VIYZZH\",\"value\":\"12345\"}]")
        : QStringLiteral("[]");

    return QString(
        "{"
        "  \"asin\": \"%1\","
        "  \"relationships\": ["
        "    {"
        "      \"marketplaceId\": \"A13V1IB3VIYZZH\","
        "      \"parentAsins\": [\"%2\"],"
        "      \"relationships\": ["
        "        {\"type\": \"VARIATION\", \"parentAsins\": [\"%2\"]}"
        "      ]"
        "    }"
        "  ],"
        "  \"summaries\": ["
        "    {\"marketplaceId\": \"A13V1IB3VIYZZH\", \"itemName\": \"Product %1\"}"
        "  ],"
        "  \"attributes\": {"
        "    \"color\": [{\"value\": \"%3\", \"marketplace_id\": \"A13V1IB3VIYZZH\"}],"
        "    \"size\":  [{\"value\": \"%4\", \"marketplace_id\": \"A13V1IB3VIYZZH\"}],"
        "    \"size_chart_node_id\": %5"
        "  }"
        "}"
    ).arg(asin, parentAsin, color, size, sizeChartArr).toUtf8();
}

static QByteArray makeParentJson(const QString& parentAsin, const QStringList& childAsins)
{
    QStringList quoted;
    for (const QString& c : childAsins) quoted << ("\"" + c + "\"");
    const QString childList = quoted.join(",");

    return QString(
        "{"
        "  \"asin\": \"%1\","
        "  \"relationships\": ["
        "    {"
        "      \"marketplaceId\": \"A13V1IB3VIYZZH\","
        "      \"childAsins\": [%2],"
        "      \"relationships\": ["
        "        {\"type\": \"VARIATION\", \"childAsins\": [%2]}"
        "      ]"
        "    }"
        "  ],"
        "  \"summaries\": ["
        "    {\"marketplaceId\": \"A13V1IB3VIYZZH\", \"itemName\": \"Parent %1\"}"
        "  ],"
        "  \"attributes\": {}"
        "}"
    ).arg(parentAsin, childList).toUtf8();
}

// ---------------------------------------------------------------------------
// Helper: wait for modelReset (or timeout)
//
// IMPORTANT: create the QSignalSpy BEFORE calling model.load() so that
// synchronous completions (e.g. mock-based tests) are captured.
// Usage:
//   QSignalSpy spy(&model, &QAbstractItemModel::modelReset);
//   model.load(...);
//   QVERIFY(waitForModelReset(spy));
// ---------------------------------------------------------------------------

static bool waitForModelReset(QSignalSpy& spy, int timeoutMs = 5000)
{
    // Signal may have already been emitted synchronously (mock path).
    if (!spy.isEmpty())
        return true;
    return spy.wait(timeoutMs);
}

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

class Test_Tree_Sizing_Asins : public QObject
{
    Q_OBJECT

public:
    Test_Tree_Sizing_Asins() = default;
    ~Test_Tree_Sizing_Asins() override = default;

private slots:
    void initTestCase();
    void cleanupTestCase();

    void test_load_from_asin_two_colors_two_sizes();
    void test_record_size_image_updates_json();
    void test_record_aplus_updates_json();
    void test_parent_row_columns();
    void test_json_persistence_across_instances();
    void test_load_error_on_empty_response();

#ifdef DO_REAL_AMAZON_TESTS
    void test_real_b0dftx45y9_eu();
    void test_real_b0dftx45y9_us();
    void test_real_load_error_bad_credentials();
    void test_real_no_error_good_credentials();
#endif

private:
    QTemporaryDir m_tempDir;
};

void Test_Tree_Sizing_Asins::initTestCase()
{
    QVERIFY(m_tempDir.isValid());
}

void Test_Tree_Sizing_Asins::cleanupTestCase()
{
}

// ---------------------------------------------------------------------------
// test_load_from_asin_two_colors_two_sizes
// ---------------------------------------------------------------------------

void Test_Tree_Sizing_Asins::test_load_from_asin_two_colors_two_sizes()
{
#ifdef AMAZONCATALOGAPI_UNIT_TESTS
    AmazonCatalogApi api("clientId", "clientSecret", "refreshToken",
                         "refreshToken", "refreshToken");

    api.setMockForTests([](const QString& path, const QMap<QString,QString>& /*qp*/) -> QByteArray {
        if (path.endsWith("/B0CHILD0001")) {
            return makeChildJson("B0CHILD0001", "B0PARENT01", "Blue", "S", false);
        }
        if (path.endsWith("/B0CHILD0002")) {
            return makeChildJson("B0CHILD0002", "B0PARENT01", "Red", "M", true);
        }
        if (path.endsWith("/B0PARENT01")) {
            return makeParentJson("B0PARENT01", {"B0CHILD0001", "B0CHILD0002"});
        }
        return {};
    });

    QDir workDir(m_tempDir.path());
    TreeSizingAsins model(workDir);
    model.setApiClient(&api);

    // Spy must be connected before load() so synchronous (mock) completions are captured.
    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    model.load("B0CHILD0001");
    QVERIFY2(waitForModelReset(resetSpy), "TreeSizingAsins did not emit modelReset");

    QCOMPARE(model.rowCount(QModelIndex{}), 1);

    const QModelIndex parentIdx = model.index(0, 0, QModelIndex{});
    QVERIFY(parentIdx.isValid());
    QCOMPARE(model.rowCount(parentIdx), 2);

    // Child 0: Blue / S / no size chart
    const QModelIndex c0Asin = model.index(0, TreeSizingAsins::ASIN, parentIdx);
    QCOMPARE(model.data(c0Asin, Qt::DisplayRole).toString(), QString("B0CHILD0001"));
    QCOMPARE(model.data(model.index(0, TreeSizingAsins::Color, parentIdx), Qt::DisplayRole).toString(),
             QString("Blue"));
    QCOMPARE(model.data(model.index(0, TreeSizingAsins::Size, parentIdx), Qt::DisplayRole).toString(),
             QString("S"));
    QCOMPARE(model.data(model.index(0, TreeSizingAsins::SizeTable, parentIdx), Qt::CheckStateRole).toInt(),
             int(Qt::Unchecked));

    // Child 1: Red / M / with size chart
    QCOMPARE(model.data(model.index(1, TreeSizingAsins::ASIN,  parentIdx), Qt::DisplayRole).toString(),
             QString("B0CHILD0002"));
    QCOMPARE(model.data(model.index(1, TreeSizingAsins::Color, parentIdx), Qt::DisplayRole).toString(),
             QString("Red"));
    QCOMPARE(model.data(model.index(1, TreeSizingAsins::Size,  parentIdx), Qt::DisplayRole).toString(),
             QString("M"));
    QCOMPARE(model.data(model.index(1, TreeSizingAsins::SizeTable, parentIdx), Qt::CheckStateRole).toInt(),
             int(Qt::Checked));
#else
    QSKIP("AMAZONCATALOGAPI_UNIT_TESTS not defined for this build");
#endif
}

// ---------------------------------------------------------------------------
// test_record_size_image_updates_json
// ---------------------------------------------------------------------------

void Test_Tree_Sizing_Asins::test_record_size_image_updates_json()
{
#ifdef AMAZONCATALOGAPI_UNIT_TESTS
    QTemporaryDir local;
    QVERIFY(local.isValid());
    QDir workDir(local.path());

    AmazonCatalogApi api("c", "c", "r", "r", "r", "a", "s");
    api.setMockForTests([](const QString& path, const QMap<QString,QString>&) -> QByteArray {
        if (path.endsWith("/B0CHILD0001"))
            return makeChildJson("B0CHILD0001", "B0PARENT01", "Blue", "S", false);
        if (path.endsWith("/B0PARENT01"))
            return makeParentJson("B0PARENT01", {"B0CHILD0001"});
        return {};
    });

    TreeSizingAsins model(workDir);
    model.setApiClient(&api);
    QSignalSpy resetSpy1(&model, &QAbstractItemModel::modelReset);
    model.load("B0CHILD0001");
    QVERIFY(waitForModelReset(resetSpy1));

    const QModelIndex parentIdx = model.index(0, 0, QModelIndex{});
    QCOMPARE(model.rowCount(parentIdx), 1);

    model.recordSizeImageUploaded("B0CHILD0001", QDate(2024,1,15), "A13V1IB3VIYZZH");
    const QModelIndex idx = model.index(0, TreeSizingAsins::SizeImage, parentIdx);
    QCOMPARE(model.data(idx, Qt::DisplayRole).toString(), QString("2024-01-15"));

    // Verify JSON file
    const QString jsonPath = workDir.filePath("sizing_upload.json");
    QVERIFY(QFile::exists(jsonPath));
    QFile f(jsonPath);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    QVERIFY(doc.isObject());
    QCOMPARE(doc.object().value("sizeImages").toObject().value("B0CHILD0001").toString(),
             QString("2024-01-15"));
#else
    QSKIP("AMAZONCATALOGAPI_UNIT_TESTS not defined");
#endif
}

// ---------------------------------------------------------------------------
// test_record_aplus_updates_json
// ---------------------------------------------------------------------------

void Test_Tree_Sizing_Asins::test_record_aplus_updates_json()
{
#ifdef AMAZONCATALOGAPI_UNIT_TESTS
    QTemporaryDir local;
    QVERIFY(local.isValid());
    QDir workDir(local.path());

    AmazonCatalogApi api("c", "c", "r", "r", "r", "a", "s");
    api.setMockForTests([](const QString& path, const QMap<QString,QString>&) -> QByteArray {
        if (path.endsWith("/B0CHILD0001"))
            return makeChildJson("B0CHILD0001", "B0PARENT01", "Blue", "S", false);
        if (path.endsWith("/B0PARENT01"))
            return makeParentJson("B0PARENT01", {"B0CHILD0001"});
        return {};
    });

    TreeSizingAsins model(workDir);
    model.setApiClient(&api);
    QSignalSpy resetSpy2(&model, &QAbstractItemModel::modelReset);
    model.load("B0CHILD0001");
    QVERIFY(waitForModelReset(resetSpy2));

    const QModelIndex parentIdx = model.index(0, 0, QModelIndex{});
    model.recordAPlusUploaded("B0CHILD0001", QDate(2024,1,16), "A13V1IB3VIYZZH");
    const QModelIndex idx = model.index(0, TreeSizingAsins::APlusContent, parentIdx);
    QCOMPARE(model.data(idx, Qt::DisplayRole).toString(), QString("2024-01-16"));

    const QString jsonPath = workDir.filePath("sizing_upload.json");
    QVERIFY(QFile::exists(jsonPath));
    QFile f(jsonPath);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    QCOMPARE(doc.object().value("aPlus").toObject().value("B0CHILD0001").toString(),
             QString("2024-01-16"));
#else
    QSKIP("AMAZONCATALOGAPI_UNIT_TESTS not defined");
#endif
}

// ---------------------------------------------------------------------------
// test_parent_row_columns
// ---------------------------------------------------------------------------

void Test_Tree_Sizing_Asins::test_parent_row_columns()
{
#ifdef AMAZONCATALOGAPI_UNIT_TESTS
    QTemporaryDir local;
    QVERIFY(local.isValid());
    QDir workDir(local.path());

    AmazonCatalogApi api("c", "c", "r", "r", "r", "a", "s");
    api.setMockForTests([](const QString& path, const QMap<QString,QString>&) -> QByteArray {
        if (path.endsWith("/B0CHILD0001"))
            return makeChildJson("B0CHILD0001", "B0PARENT01", "Blue", "S", false);
        if (path.endsWith("/B0PARENT01"))
            return makeParentJson("B0PARENT01", {"B0CHILD0001"});
        return {};
    });

    TreeSizingAsins model(workDir);
    model.setApiClient(&api);
    QSignalSpy resetSpy3(&model, &QAbstractItemModel::modelReset);
    model.load("B0CHILD0001");
    QVERIFY(waitForModelReset(resetSpy3));

    const QModelIndex parentIdx = model.index(0, 0, QModelIndex{});
    QVERIFY(parentIdx.isValid());

    // Parent row: only SKU and ASIN populated; all other columns empty
    QCOMPARE(model.data(model.index(0, TreeSizingAsins::ASIN, QModelIndex{}), Qt::DisplayRole).toString(),
             QString("B0PARENT01"));

    QVERIFY(model.data(model.index(0, TreeSizingAsins::Title, QModelIndex{}), Qt::DisplayRole).toString().isEmpty());
    QVERIFY(model.data(model.index(0, TreeSizingAsins::Size,  QModelIndex{}), Qt::DisplayRole).toString().isEmpty());
    QVERIFY(model.data(model.index(0, TreeSizingAsins::Color, QModelIndex{}), Qt::DisplayRole).toString().isEmpty());
    QVERIFY(model.data(model.index(0, TreeSizingAsins::SizeImage, QModelIndex{}), Qt::DisplayRole).toString().isEmpty());
    QVERIFY(model.data(model.index(0, TreeSizingAsins::APlusContent, QModelIndex{}), Qt::DisplayRole).toString().isEmpty());

    // SizeTable on parent row: no CheckStateRole, not user-checkable
    QVariant checkVar = model.data(model.index(0, TreeSizingAsins::SizeTable, QModelIndex{}), Qt::CheckStateRole);
    QVERIFY(!checkVar.isValid());

    const Qt::ItemFlags pFlags = model.flags(model.index(0, TreeSizingAsins::SizeTable, QModelIndex{}));
    QVERIFY((pFlags & Qt::ItemIsUserCheckable) == 0);
#else
    QSKIP("AMAZONCATALOGAPI_UNIT_TESTS not defined");
#endif
}

// ---------------------------------------------------------------------------
// test_json_persistence_across_instances
// ---------------------------------------------------------------------------

void Test_Tree_Sizing_Asins::test_json_persistence_across_instances()
{
#ifdef AMAZONCATALOGAPI_UNIT_TESTS
    QTemporaryDir local;
    QVERIFY(local.isValid());
    QDir workDir(local.path());

    auto mockFn = [](const QString& path, const QMap<QString,QString>&) -> QByteArray {
        if (path.endsWith("/B0CHILD0001"))
            return makeChildJson("B0CHILD0001", "B0PARENT01", "Blue", "S", false);
        if (path.endsWith("/B0PARENT01"))
            return makeParentJson("B0PARENT01", {"B0CHILD0001"});
        return {};
    };

    // First instance: record upload date
    {
        AmazonCatalogApi api("c", "c", "r", "r", "r", "a", "s");
        api.setMockForTests(mockFn);
        TreeSizingAsins model(workDir);
        model.setApiClient(&api);
        QSignalSpy spy4a(&model, &QAbstractItemModel::modelReset);
        model.load("B0CHILD0001");
        QVERIFY(waitForModelReset(spy4a));
        model.recordSizeImageUploaded("B0CHILD0001", QDate(2024, 3, 1), "A13V1IB3VIYZZH");
    }

    // Second instance: should pick up the saved date
    {
        AmazonCatalogApi api("c", "c", "r", "r", "r", "a", "s");
        api.setMockForTests(mockFn);
        TreeSizingAsins model(workDir);
        model.setApiClient(&api);
        QSignalSpy spy4b(&model, &QAbstractItemModel::modelReset);
        model.load("B0CHILD0001");
        QVERIFY(waitForModelReset(spy4b));

        const QModelIndex parentIdx = model.index(0, 0, QModelIndex{});
        const QModelIndex siIdx = model.index(0, TreeSizingAsins::SizeImage, parentIdx);
        QCOMPARE(model.data(siIdx, Qt::DisplayRole).toString(), QString("2024-03-01"));
    }
#else
    QSKIP("AMAZONCATALOGAPI_UNIT_TESTS not defined");
#endif
}

// ---------------------------------------------------------------------------
// test_load_error_on_empty_response
// Mock returns empty body -> fetchVariationFamily -> empty family -> loadError emitted
// ---------------------------------------------------------------------------

void Test_Tree_Sizing_Asins::test_load_error_on_empty_response()
{
#ifdef AMAZONCATALOGAPI_UNIT_TESTS
    AmazonCatalogApi api("c","c","r","r","r","a","s");
    api.setMockForTests([](const QString&, const QMap<QString,QString>&) -> QByteArray {
        return {};  // Empty = simulated network/auth failure
    });

    QDir workDir(m_tempDir.path());
    TreeSizingAsins model(workDir);
    model.setApiClient(&api);

    QSignalSpy errorSpy(&model, &TreeSizingAsins::loadError);
    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    model.load("BADASIN00001", "A13V1IB3VIYZZH");

    // loadError must fire; model must stay empty (modelReset may also fire for the clear)
    QVERIFY(errorSpy.count() > 0 || errorSpy.wait(5000));
    QCOMPARE(model.rowCount(QModelIndex{}), 0);
#else
    QSKIP("AMAZONCATALOGAPI_UNIT_TESTS not defined");
#endif
}

// ---------------------------------------------------------------------------
// Real API tests (opt-in)
// ---------------------------------------------------------------------------

#ifdef DO_REAL_AMAZON_TESTS
static QString envOrEmpty(const char* name)
{
    return QString::fromLocal8Bit(qgetenv(name));
}

static AmazonCatalogApi::VariationFamily waitForFamily(AmazonCatalogApi& api,
                                                       const QString& asin,
                                                       const QString& mkt,
                                                       int timeoutMs = 30000)
{
    AmazonCatalogApi::VariationFamily result;
    bool done = false;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

    auto task = [&]() -> QCoro::Task<void> {
        co_await api.fetchVariationFamily(asin, mkt, &result);
        done = true;
        loop.quit();
    }();
    Q_UNUSED(task);

    timeout.start(timeoutMs);
    loop.exec();
    Q_UNUSED(done);
    return result;
}

void Test_Tree_Sizing_Asins::test_real_b0dftx45y9_eu()
{
    const QString refreshEu = envOrEmpty("AMAZON_LWA_REFRESH_TOKEN_EU");
    if (refreshEu.isEmpty())
        QSKIP("AMAZON_LWA_REFRESH_TOKEN_EU not set");
    const QString clientId  = envOrEmpty("AMAZON_LWA_CLIENT_ID");
    const QString clientSec = envOrEmpty("AMAZON_LWA_CLIENT_SECRET");

    AmazonCatalogApi api(clientId, clientSec, refreshEu, {}, {});
    auto fam = waitForFamily(api, "B0DFTX45Y9", "A13V1IB3VIYZZH");
    QVERIFY(fam.children.size() >= 1);

    QSet<QString> colors;
    for (const auto& c : fam.children)
        if (!c.color.isEmpty()) colors.insert(c.color);
    QVERIFY2(colors.size() >= 2, "Expected at least 2 distinct colors in EU family");
}

void Test_Tree_Sizing_Asins::test_real_b0dftx45y9_us()
{
    const QString refreshEu = envOrEmpty("AMAZON_LWA_REFRESH_TOKEN_EU");
    const QString refreshNa = envOrEmpty("AMAZON_LWA_REFRESH_TOKEN_NA");
    if (refreshEu.isEmpty() || refreshNa.isEmpty())
        QSKIP("AMAZON_LWA_REFRESH_TOKEN_EU and AMAZON_LWA_REFRESH_TOKEN_NA must both be set");
    const QString clientId  = envOrEmpty("AMAZON_LWA_CLIENT_ID");
    const QString clientSec = envOrEmpty("AMAZON_LWA_CLIENT_SECRET");

    AmazonCatalogApi apiEu(clientId, clientSec, refreshEu, {}, {});
    auto famEu = waitForFamily(apiEu, "B0DFTX45Y9", "A13V1IB3VIYZZH");

    AmazonCatalogApi apiUs(clientId, clientSec, {}, refreshNa, {});
    auto famUs = waitForFamily(apiUs, "B0DFTX45Y9", "ATVPDKIKX0DER");

    QSet<QString> sizesEu, sizesUs;
    for (const auto& c : famEu.children)
        if (!c.size.isEmpty()) sizesEu.insert(c.size);
    for (const auto& c : famUs.children)
        if (!c.size.isEmpty()) sizesUs.insert(c.size);
    QVERIFY2(sizesEu != sizesUs, "Expected EU and US sizes to differ");
}

void Test_Tree_Sizing_Asins::test_real_load_error_bad_credentials()
{
    // All credentials intentionally wrong
    AmazonCatalogApi api("bad_client_id", "bad_client_secret",
                         "bad_refresh_token", "bad_refresh_token", "bad_refresh_token");

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    TreeSizingAsins model(QDir(tmp.path()));
    model.setApiClient(&api);

    QSignalSpy errorSpy(&model, &TreeSizingAsins::loadError);
    model.load("B0DFTX45Y9", "A13V1IB3VIYZZH");

    QVERIFY2(errorSpy.count() > 0 || errorSpy.wait(30000),
             "Expected loadError signal with bad credentials");
    QCOMPARE(model.rowCount(QModelIndex{}), 0);
}

void Test_Tree_Sizing_Asins::test_real_no_error_good_credentials()
{
    const QString refreshEu = envOrEmpty("AMAZON_LWA_REFRESH_TOKEN_EU");
    if (refreshEu.isEmpty()) QSKIP("AMAZON_LWA_REFRESH_TOKEN_EU not set");

    AmazonCatalogApi api(envOrEmpty("AMAZON_LWA_CLIENT_ID"),
                         envOrEmpty("AMAZON_LWA_CLIENT_SECRET"),
                         refreshEu, {}, {});

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    TreeSizingAsins model(QDir(tmp.path()));
    model.setApiClient(&api);

    QSignalSpy errorSpy(&model, &TreeSizingAsins::loadError);
    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    model.load("B0DFTX45Y9", "A13V1IB3VIYZZH");

    // Wait for modelReset (success) and verify no error
    QVERIFY2(resetSpy.count() > 0 || resetSpy.wait(30000),
             "Expected modelReset with valid credentials");
    QCOMPARE(errorSpy.count(), 0);
    QVERIFY(model.rowCount(QModelIndex{}) >= 1);
}
#endif // DO_REAL_AMAZON_TESTS

QTEST_GUILESS_MAIN(Test_Tree_Sizing_Asins)
#include "tst_tree_sizing_asins.moc"
