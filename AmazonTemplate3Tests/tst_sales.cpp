#pragma GCC optimize("O1")
#include "gui/panes/PaneSales.h"
#include "marketplaces/AbstractTargetMarketplaceFactory.h"
#include "apis/AmazonInventoryApi.h"
#include "apis/TemuInventoryApi.h"
#include <QCoro/QCoroTimer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QListView>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QUrlQuery>
#include <QtTest>

class SalesReply : public QNetworkReply
{
public:
    using QNetworkReply::setRawHeader;
    SalesReply(const QNetworkRequest &request, QByteArray body, int status, QObject *parent)
        : QNetworkReply(parent), m_body(std::move(body))
    {
        setRequest(request);
        setUrl(request.url());
        open(QIODevice::ReadOnly);
        QTimer::singleShot(0, this, [this, status] {
            setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
            if (status != 200) setError(ContentAccessDenied, "Denied");
            setFinished(true);
            emit readyRead();
            emit finished();
        });
    }
    void abort() override {}
    qint64 bytesAvailable() const override { return m_body.size() - m_offset + QNetworkReply::bytesAvailable(); }
protected:
    qint64 readData(char *data, qint64 size) override
    {
        const auto n = std::min(size, m_body.size() - m_offset);
        if (n <= 0) return -1;
        memcpy(data, m_body.constData() + m_offset, n);
        m_offset += n;
        return n;
    }
private:
    QByteArray m_body;
    qint64 m_offset = 0;
};

class SalesNetwork : public QNetworkAccessManager
{
public:
    QByteArray response;
    int status = 200;
    QList<int> statuses;
    QByteArray retryAfter;
    QByteArray requestId;
    QList<qint64> metricTimes;
    std::function<void()> onMetric;
    QList<QUrl> requests;
    QList<QJsonObject> posts;
protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *body) override
    {
        requests.append(request.url());
        if (body) posts.append(QJsonDocument::fromJson(body->readAll()).object());
        const bool token = request.url().path().contains("token");
        if (!token) {
            metricTimes.append(QDateTime::currentMSecsSinceEpoch());
            if (onMetric) onMetric();
        }
        auto *reply = new SalesReply(request, token ? QByteArray(R"({"access_token":"fake","expires_in":3600})") : response,
                              token ? 200 : statuses.isEmpty() ? status : statuses.takeFirst(), this);
        if (!token) {
            reply->setRawHeader("Retry-After", retryAfter);
            reply->setRawHeader("x-amzn-RequestId", requestId);
        }
        return reply;
    }
};

struct FakeCounts
{
    int calls = 0;
    int living = 0;
    bool fail = false;
    QList<QPair<QDateTime, QDateTime>> intervals;
};
class FakeChannel : public AbstractSalesChannel
{
public:
    FakeChannel(QString name, std::shared_ptr<FakeCounts> state) : m_name(name), m_state(state) { ++state->living; }
    ~FakeChannel() override { --m_state->living; }
    QString displayName() const override { return m_name; }
    QString lastError() const override { return m_error; }
    QCoro::Task<void> fetchOrderCount(QDateTime from, QDateTime to, int *out) override
    {
        const int call = ++m_state->calls;
        m_state->intervals.append({from, to});
        QTimer timer;
        timer.setSingleShot(true);
        timer.start(1);
        co_await qCoro(timer).waitForTimeout();
        m_error = m_state->fail && call == 2 ? QStringLiteral("Offline permission error") : QString();
        *out = m_error.isEmpty() ? call - 1 : -1;
    }
private:
    QString m_name, m_error;
    std::shared_ptr<FakeCounts> m_state;
};
class FakeFactory : public AbstractTargetMarketplaceFactory
{
public:
    QString name = "Amazon";
    QStringList rows = {"Amazon.fr", "Amazon.de"};
    std::shared_ptr<FakeCounts> state = std::make_shared<FakeCounts>();
    QString platformId() const override { return name.toLower(); }
    QString platformDisplayName() const override { return name; }
    QList<AbstractTargetMarketplace *> createInstances(QSettings *) const override { return {}; }
    QList<AbstractSalesChannel *> createSalesChannels(QSettings *) const override
    {
        QList<AbstractSalesChannel *> result;
        for (const auto &row : rows) result.append(new FakeChannel(row, state));
        return result;
    }
};

class SalesTests : public QObject
{
    Q_OBJECT
private slots:
    void calendarAndRegistry()
    {
        const auto months = PaneSales::monthsEndingAt(QDate(2024, 2, 29));
        QCOMPARE(months.size(), 12);
        QCOMPARE(months.first(), QDate(2023, 3, 1));
        QCOMPARE(months.last(), QDate(2024, 2, 1));
        const auto factories = AbstractTargetMarketplaceFactory::ALL_MARKETPLACE_FACTORIES();
        QCOMPARE(factories[0]->platformId(), "amazon");
        QCOMPARE(factories[1]->platformId(), "temu");
    }
    void amazonCountsOrdersAndRejectsMalformed()
    {
        QCOMPARE(AmazonInventoryApi::parseOrderCount(R"({"payload":[{"orderCount":7,"unitCount":19}]})"), 7);
        QCOMPARE(AmazonInventoryApi::parseOrderCount(R"({"payload":[{"orderCount":0}]})"), 0);
        for (const auto &body : {"{}", "garbage", "{\"payload\":[]}",
             "{\"payload\":[{\"unitCount\":5}]}", "{\"payload\":[{\"orderCount\":-1}]}",
             "{\"payload\":[{\"orderCount\":1.5}]}", "{\"payload\":[{\"orderCount\":\"8\"}]}"})
            QCOMPARE(AmazonInventoryApi::parseOrderCount(body), -1);
        SalesNetwork network;
        network.response = R"({"payload":[{"orderCount":7,"unitCount":19}]})";
        AmazonInventoryApi api("fake", "fake", "fake", "fake", "ATVPDKIKX0DER");
        api.setNetworkAccessManager(&network);
        const auto from = QDate(2024, 2, 1).startOfDay(QTimeZone::UTC);
        const auto to = from.addMonths(1);
        bool done = false;
        int count = -1;
        auto run = [&]() -> QCoro::Task<void> { co_await api.fetchOrderCount(from, to, &count); done = true; };
        auto task = run();
        QTRY_VERIFY_WITH_TIMEOUT(done, 3000);
        QCOMPARE(count, 7);
        QCOMPARE(network.requests.last().host(), "sellingpartnerapi-na.amazon.com");
        const QUrlQuery query(network.requests.last());
        QCOMPARE(query.queryItemValue("interval"), "2024-02-01T00:00:00Z--2024-03-01T00:00:00Z");
        QCOMPARE(query.queryItemValue("granularity"), "Total");
        QVERIFY(!query.hasQueryItem("sku"));
        QVERIFY(!query.hasQueryItem("fulfillmentNetwork"));
        network.status = 403;
        network.response = R"({"errors":[{"code":"AccessDenied","message":"Missing role"}]})";
        network.requestId = "offline-request-id";
        done = false;
        auto task2 = run();
        QTRY_VERIFY_WITH_TIMEOUT(done, 3000);
        QCOMPARE(count, -1);
        QVERIFY(api.lastError().contains("403"));
        QVERIFY(api.lastError().contains("AccessDenied"));
        QVERIFY(api.lastError().contains("offline-request-id"));
    }
    void monthlyRequestMappingRetriesAndSharedPacing()
    {
        const auto months = PaneSales::monthsEndingAt(QDate(2026, 10, 10));
        const auto until = QDateTime(QDate(2026, 10, 10), QTime(12, 0), QTimeZone::UTC);
        QJsonArray metrics;
        for (int i = 11; i >= 0; --i) { // The response need not be in column order.
            const auto from = months[i].startOfDay(QTimeZone::UTC);
            const auto to = qMin(from.addMonths(1), until);
            metrics.append(QJsonObject{{"interval", from.toString(Qt::ISODate) + "--" + to.toString(Qt::ISODate)},
                                       {"orderCount", i}, {"unitCount", 900}});
        }
        const auto body = QJsonDocument(QJsonObject{{"payload", metrics}}).toJson();
        SalesNetwork network;
        network.response = body;
        network.statuses = {429, 200};
        network.retryAfter = "3";
        AmazonInventoryApi api("fake", "fake", "fake", "fake", "A13V1IB3VIYZZH");
        api.setNetworkAccessManager(&network);
        QStringList progress;
        api.setReadProgress([&](const QString &message) { progress.append(message); });
        bool done = false;
        QList<int> counts;
        auto run = [&]() -> QCoro::Task<void> {
            co_await api.fetchMonthlyOrderCounts(months, until, &counts);
            done = true;
        };
        auto task = run();
        QTRY_VERIFY_WITH_TIMEOUT(done, 8000);
        QCOMPARE(counts.size(), 12);
        for (int i = 0; i < 12; ++i) QCOMPARE(counts[i], i);
        QCOMPARE(network.metricTimes.size(), 2); // one batch, plus its 429 retry
        QVERIFY(network.metricTimes[1] - network.metricTimes[0] >= 3000);
        QVERIFY(progress.join(' ').contains("429"));
        QVERIFY(api.lastError().isEmpty());
        const QUrlQuery query(network.requests.last());
        QCOMPARE(query.queryItemValue("granularity"), "Month");
        QCOMPARE(query.queryItemValue("granularityTimeZone"), "UTC");
        QCOMPARE(query.queryItemValue("interval"), "2025-11-01T00:00:00Z--2026-10-10T12:00:00Z");
        QVERIFY(!query.hasQueryItem("sku"));

        QCOMPARE(AmazonInventoryApi::parseMonthlyOrderCounts(body, {months[0], months[5], months[11]}, until),
                 (QList<int>{0, 5, 11}));

        // A new marketplace instance must share the same pacing gate.
        AmazonInventoryApi second("fake", "fake", "fake", "fake", "A1PA6795UKMFR9");
        second.setNetworkAccessManager(&network);
        done = false;
        auto next = [&]() -> QCoro::Task<void> {
            co_await second.fetchMonthlyOrderCounts(months, until, &counts);
            done = true;
        };
        auto task2 = next();
        QTRY_VERIFY_WITH_TIMEOUT(done, 4000);
        QCOMPARE(network.metricTimes.size(), 3);
        QVERIFY(network.metricTimes[2] - network.metricTimes[1] >= 2050);

        metrics.removeLast(); // Missing November must not turn into zero.
        auto parsed = AmazonInventoryApi::parseMonthlyOrderCounts(
            QJsonDocument(QJsonObject{{"payload", metrics}}).toJson(), months, until);
        QCOMPARE(parsed[0], -1);
        QCOMPARE(parsed[1], 1);
        metrics.append(metrics.first()); // Duplicate interval is ambiguous.
        parsed = AmazonInventoryApi::parseMonthlyOrderCounts(
            QJsonDocument(QJsonObject{{"payload", metrics}}).toJson(), months, until);
        QCOMPARE(parsed[0], -1); // Missing November.
        QCOMPARE(parsed[11], -1); // Duplicate October.
        for (int i = 1; i < 11; ++i) QCOMPARE(parsed[i], i);
    }
    void monthlyCalendarLabelsWithPartialCurrentMonth()
    {
        // Observed from a live HTTP 200 response: minute precision and a full
        // October bucket label even though the query ended on October 10.
        const auto months = PaneSales::monthsEndingAt(QDate(2026, 10, 10));
        const auto until = QDateTime(QDate(2026, 10, 10), QTime(8, 23, 45), QTimeZone::UTC);
        QJsonArray metrics;
        for (int i = 0; i < months.size(); ++i) {
            const auto from = months[i].startOfDay(QTimeZone::UTC);
            metrics.append(QJsonObject{
                {"interval", from.toString("yyyy-MM-dd'T'HH:mm'Z'") + "--"
                    + from.addMonths(1).toString("yyyy-MM-dd'T'HH:mm'Z'")},
                {"orderCount", i}});
        }
        const auto parse = [&] {
            return AmazonInventoryApi::parseMonthlyOrderCounts(
                QJsonDocument(QJsonObject{{"payload", metrics}}).toJson(), months, until);
        };
        const auto counts = parse();
        for (int i = 0; i < 12; ++i) QCOMPARE(counts[i], i);

        // Accepting the calendar label must not allow arbitrary ranges, nor
        // discard eleven sound months because one month's label is invalid.
        auto last = metrics.last().toObject();
        last["interval"] = "2026-10-01T00:00Z--2026-12-01T00:00Z";
        metrics[11] = last;
        const auto invalid = parse();
        QCOMPARE(invalid[11], -1);
        for (int i = 0; i < 11; ++i) QCOMPARE(invalid[i], i);
    }
    void cancelDuringAmazonBackoff()
    {
        SalesNetwork network;
        network.status = 429;
        network.retryAfter = "120";
        AmazonInventoryApi api("fake", "fake", "fake", "fake", "A13V1IB3VIYZZH");
        api.setNetworkAccessManager(&network);
        auto cancelled = std::make_shared<bool>(false);
        api.setReadCancellation(cancelled);
        // Cancel after the retry wait starts, rather than waiting two minutes.
        api.setReadProgress([cancelled](const QString &) {
            QTimer::singleShot(20, [cancelled] { *cancelled = true; });
        });
        bool done = false;
        QList<int> counts;
        auto run = [&]() -> QCoro::Task<void> {
            co_await api.fetchMonthlyOrderCounts({QDate(2026, 9, 1)},
                QDate(2026, 10, 1).startOfDay(QTimeZone::UTC), &counts);
            done = true;
        };
        auto task = run();
        QTRY_VERIFY_WITH_TIMEOUT(done, 4000);
        QCOMPARE(network.metricTimes.size(), 1);
        QCOMPARE(counts, QList<int>{-1});
        QCOMPARE(api.lastError(), "Cancelled");
    }
    void temuUsesParentTotalAndExclusiveBoundary()
    {
        SalesNetwork network;
        network.response = R"({"success":true,"result":{"totalItemNum":203,"pageItems":[{"orderList":[{},{}]}]}})";
        TemuInventoryApi api("fake", "fake", "fake");
        api.setNetworkAccessManager(&network);
        const auto from = QDate(2024, 2, 1).startOfDay(QTimeZone::UTC);
        const auto to = from.addMonths(1);
        bool done = false;
        int count = -1;
        auto run = [&]() -> QCoro::Task<void> { co_await api.fetchOrderCount(from, to, &count); done = true; };
        auto task = run();
        QTRY_VERIFY_WITH_TIMEOUT(done, 3000);
        QCOMPARE(count, 203);
        QCOMPARE(network.requests.size(), 1); // no need to fetch 203 orders
        QCOMPARE(network.posts.last().value("createBefore").toInteger(), to.toSecsSinceEpoch() - 1);
        QCOMPARE(network.posts.last().value("createAfter").toInteger(), from.toSecsSinceEpoch());
        QCOMPARE(network.posts.last().value("pageNumber").toInt(), 1);
        QCOMPARE(network.posts.last().value("pageSize").toInt(), 1);
        network.response = R"({"success":true,"result":{"pageItems":[]}})";
        done = false;
        auto task2 = run();
        QTRY_VERIFY_WITH_TIMEOUT(done, 3000);
        QCOMPARE(count, -1);
        QVERIFY(!api.lastError().isEmpty());
        network.response = R"({"success":true,"result":{"totalItemNum":0}})";
        done = false;
        auto task3 = run();
        QTRY_VERIFY_WITH_TIMEOUT(done, 3000);
        QCOMPARE(count, 0);
        QVERIFY(api.lastError().isEmpty());
    }
    void paneSelectionCountsAndChart()
    {
        QTemporaryDir dir;
        auto settings = QSharedPointer<QSettings>::create(dir.filePath("settings.ini"), QSettings::IniFormat);
        FakeFactory amazon, temu;
        amazon.state->fail = true;
        temu.name = "Temu";
        temu.rows.clear();
        for (const auto &store : {"Store A", "Store B"})
            for (const auto &country : {"FR", "DE", "IT", "ES"})
                temu.rows.append(QString("Temu %1 – %2").arg(country, store));
        PaneSales pane(settings, {&amazon, &temu});
        pane.resize(1100, 700);
        pane.show();
        auto *list = pane.findChild<QListView *>();
        auto *table = pane.findChild<QTableView *>();
        auto *count = pane.findChild<QPushButton *>("buttonCountOrder");
        auto *chart = pane.findChild<QWidget *>("salesChart");
        QCOMPARE(list->currentIndex().row(), 0);
        QCOMPARE(table->model()->columnCount(), 12);
        QCOMPARE(pane.findChild<QSplitter *>()->orientation(), Qt::Vertical);
        QTest::mouseClick(count, Qt::LeftButton);
        QVERIFY(!list->isEnabled());
        QTRY_VERIFY_WITH_TIMEOUT(count->isEnabled(), 4000);
        QCOMPARE(amazon.state->calls, 24);
        QCOMPARE(table->model()->rowCount(), 2);
        QCOMPARE(table->model()->index(0, 0).data().toInt(), 0);
        QCOMPARE(table->model()->index(0, 1).data().toString(), "N/A");
        QVERIFY(table->model()->index(0, 1).data(Qt::ToolTipRole).toString().contains("permission"));
        QCOMPARE(amazon.state->intervals[0].second, amazon.state->intervals[1].first);
        table->selectRow(1);
        QCOMPARE(chart->accessibleDescription(), "Amazon.de");
        QVERIFY(!chart->grab().isNull()); // exercise painting, including zero/missing values
        auto *retry = pane.findChild<QPushButton *>("buttonRetrySales");
        QVERIFY(retry->isEnabled());
        retry->click();
        QTRY_VERIFY_WITH_TIMEOUT(count->isEnabled(), 3000);
        QCOMPARE(amazon.state->calls, 25); // only the missing month is retried
        QCOMPARE(table->model()->index(0, 1).data().toInt(), 24);
        QCOMPARE(table->model()->index(0, 0).data().toInt(), 0); // successful count preserved
        QCOMPARE(table->model()->index(1, 11).data().toInt(), 23); // successful row not fetched
        QVERIFY(!retry->isEnabled());
        list->setCurrentIndex(list->model()->index(1, 0));
        QCOMPARE(table->model()->rowCount(), 0);
        QTest::mouseClick(count, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(count->isEnabled(), 5000);
        QCOMPARE(table->model()->rowCount(), 8);
        QCOMPARE(temu.state->calls, 96);
        QCOMPARE(table->model()->headerData(7, Qt::Vertical).toString(), "Temu ES – Store B");
        QCOMPARE(amazon.state->living, 0);
        QCOMPARE(temu.state->living, 0);
    }
    void cancelAndDestroyWhileCounting()
    {
        QTemporaryDir dir;
        auto settings = QSharedPointer<QSettings>::create(dir.filePath("settings.ini"), QSettings::IniFormat);
        FakeFactory factory;
        auto *pane = new PaneSales(settings, {&factory});
        auto *count = pane->findChild<QPushButton *>("buttonCountOrder");
        count->click();
        pane->findChild<QPushButton *>("buttonCancelCount")->click();
        QTRY_VERIFY_WITH_TIMEOUT(count->isEnabled(), 3000);
        QCOMPARE(factory.state->calls, 1);
        QVERIFY(pane->findChild<QLabel *>("labelSalesStatus")->text().contains("Canceled"));
        count->click();
        delete pane;
        QTRY_COMPARE_WITH_TIMEOUT(factory.state->living, 0, 3000);
        QCOMPARE(factory.state->calls, 2);
    }
    void noConfiguredChannels()
    {
        QTemporaryDir dir;
        auto settings = QSharedPointer<QSettings>::create(dir.filePath("settings.ini"), QSettings::IniFormat);
        FakeFactory factory;
        factory.rows.clear();
        PaneSales pane(settings, {&factory});
        pane.findChild<QPushButton *>("buttonCountOrder")->click();
        QVERIFY(pane.findChild<QLabel *>("labelSalesStatus")->text().contains("Settings"));
        QVERIFY(pane.findChild<QPushButton *>("buttonCountOrder")->isEnabled());
    }
};

QTEST_MAIN(SalesTests)
#include "tst_sales.moc"
