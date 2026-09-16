#include "apis/AmazonInventoryApi.h"
#include "apis/AmazonPricingApi.h"
#include "apis/ReadRequestControl.h"
#include "gui/panes/PricingProgressDialog.h"
#include "pricing/AmazonPricingRepository.h"
#include <QFile>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <QtTest>

class FakeReply : public QNetworkReply {
  public:
    FakeReply(const QNetworkRequest &request, QByteArray body, bool stall, QObject *parent)
        : QNetworkReply(parent), m_body(std::move(body)) {
        setRequest(request);
        setUrl(request.url());
        open(QIODevice::ReadOnly);
        if (!stall)
            QTimer::singleShot(0, this, [this] {
                setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
                setFinished(true);
                emit readyRead();
                emit finished();
            });
    }
    void abort() override {
        if (isFinished())
            return;
        setError(QNetworkReply::OperationCanceledError, "Cancelled or timed out");
        setFinished(true);
        emit errorOccurred(error());
        emit finished();
    }
    qint64 bytesAvailable() const override {
        return m_body.size() - m_offset + QNetworkReply::bytesAvailable();
    }

  protected:
    qint64 readData(char *data, qint64 size) override {
        const auto n = qMin(size, qint64(m_body.size() - m_offset));
        if (n <= 0)
            return -1;
        memcpy(data, m_body.constData() + m_offset, n);
        m_offset += n;
        return n;
    }

  private:
    QByteArray m_body;
    qint64 m_offset = 0;
};
class FakeNetwork : public QNetworkAccessManager {
  public:
    int tokens = 0, sales = 0, inventory = 0;
    bool stallSales = false;

  protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override {
        const auto path = request.url().path();
        QByteArray body;
        if (path.contains("token")) {
            ++tokens;
            body = R"({"access_token":"offline-token","expires_in":3600})";
        } else if (path.contains("orderMetrics")) {
            ++sales;
            body = R"({"payload":[{"unitCount":3}]})";
        } else if (path.contains("summaries")) {
            ++inventory;
            QJsonArray rows;
            const auto skus = QUrlQuery(request.url()).queryItemValue("sellerSkus").split(',');
            for (const auto &sku : skus)
                rows.append(QJsonObject{{"sellerSku", sku},
                                        {"inventoryDetails", QJsonObject{{"fulfillableQuantity", 7}}}});
            body =
                QJsonDocument(QJsonObject{{"payload", QJsonObject{{"inventorySummaries", rows}}}}).toJson();
        } else
            qFatal("Unexpected request in offline test");
        return new FakeReply(request, body, stallSales && path.contains("orderMetrics"), this);
    }
};
class PricingNetwork : public QNetworkAccessManager {
  public:
    int reads = 0, patches = 0;
    std::function<void()> onPatch;
    QStringList *log = nullptr;
    bool loggedBeforePatch = true;

  protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override {
        const auto path = request.url().path();
        const QString market = QUrlQuery(request.url()).queryItemValue("marketplaceIds");
        QByteArray body;
        if (path.contains("token"))
            body = R"({"access_token":"offline-token","expires_in":3600})";
        else if (request.attribute(QNetworkRequest::CustomVerbAttribute).toByteArray() == "PATCH") {
            ++patches;
            loggedBeforePatch = loggedBeforePatch && log && log->join('\n').contains("SUBMITTING") &&
                                log->join('\n').contains(market);
            body = path.endsWith("FAIL")
                       ? R"({"status":"INVALID","issues":[{"message":"Fixture rejection"}]})"
                       : R"({"status":"ACCEPTED"})";
            if (onPatch)
                onPatch();
        } else if (path.contains("/listings/")) {
            ++reads;
            const double price = path.endsWith("CHANGED") ? 42 : 20;
            const QJsonArray schedule{QJsonObject{{"value_with_tax", price}}};
            const QJsonArray ourPrice{QJsonObject{{"schedule", schedule}}};
            const QJsonArray offers{QJsonObject{{"marketplace_id", market},
                                                {"currency", market == "ATVPDKIKX0DER" ? "USD" : "EUR"},
                                                {"our_price", ourPrice}}};
            body = QJsonDocument(
                       QJsonObject{
                           {"summaries",
                            QJsonArray{QJsonObject{{"marketplaceId", market}, {"productType", "SHOES"}}}},
                           {"offers", QJsonArray{QJsonObject{{"marketplaceId", market},
                                                             {"price", QJsonObject{{"amount", price}}}}}},
                           {"attributes", QJsonObject{{"purchasable_offer",
                                                       path.endsWith("NOOFFER") ? QJsonArray{} : offers}}}})
                       .toJson();
        } else
            qFatal("Unexpected request in offline pricing update test");
        return new FakeReply(request, body, false, this);
    }
};
class PricingRetrievalTests : public QObject {
    Q_OBJECT
  private slots:
    void updateAuditIncludesBeforeAfterAndEveryOutcome();
    void updateCancellation_data();
    void updateCancellation();
    void updateDialogRetainsCancellationLog();
    void reusesAuthenticationAndReportsSalesProgress();
    void cancelAbortsSalesAndKeepsUnknown();
    void readHasHardDeadline();
    void dialogKeepsLogUntilCancellationCompletes();
};
void PricingRetrievalTests::reusesAuthenticationAndReportsSalesProgress() {
    QTemporaryDir dir;
    FakeNetwork network;
    int clients = 0;
    AmazonPricingRepository repository(QDir(dir.path()), {"client", "secret", "eu-token", {}, "seller", {}},
                                       [&](const Pricing::Market &market) {
                                           ++clients;
                                           auto api = std::make_shared<AmazonInventoryApi>(
                                               "client", "secret", "eu-token", "seller", market.id);
                                           api->setNetworkAccessManager(&network);
                                           return api;
                                       });
    const QString fr = "A13V1IB3VIYZZH";
    const auto now = QDateTime::currentSecsSinceEpoch();
    QJsonArray listings;
    for (const QString sku : {QString("A"), QString("B")}) {
        listings.append(QJsonObject{{"sku", sku}});
        const QJsonObject body{
            {"summaries", QJsonArray{QJsonObject{{"marketplaceId", fr}, {"itemName", "Titre"}}}},
            {"offers",
             QJsonArray{QJsonObject{{"marketplaceId", fr}, {"price", QJsonObject{{"amount", 20}}}}}}};
        QVERIFY(repository.cache().write(
            "listing", AmazonDataCache::listingKey(fr, sku),
            {{"sku", sku}, {"marketplace", fr}, {"exists", true}, {"body", body}}, now));
    }
    QVERIFY(repository.cache().write("discovery", fr, {{"listings", listings}}, now));
    const QList<Pricing::Market> markets{{fr, "FR", "EUR", "Europe", 1, true}};
    QStringList phases;
    int maxSales = 0;
    auto cancelled = std::make_shared<bool>(false);
    auto progress = [&](const QString &phase, int done, int total, const QString &) {
        phases << phase;
        if (phase == "90-day sales") {
            QCOMPARE(total, 2);
            maxSales = qMax(maxSales, done);
        }
    };
    QList<Pricing::Row> updates;
    bool done = false;
    auto task = repository
                    .retrieve(
                        markets, {}, cancelled, [](const QString &) {},
                        [&](const auto &row) { updates << row; }, progress)
                    .then([&] { done = true; });
    QTRY_VERIFY_WITH_TIMEOUT(done, 2000);
    QCOMPARE(clients, 1);
    QCOMPARE(network.tokens, 1);
    QCOMPARE(network.sales, 2);
    QCOMPARE(network.inventory, 1);
    QCOMPARE(maxSales, 2);
    QVERIFY(phases.contains("Inventory"));
    QCOMPARE(updates.last().sales90, 3);
    QCOMPARE(updates.last().available, 7);
    // A second retrieval uses saved observations instead of reauthenticating or repeating reads.
    done = false;
    auto second = repository.retrieve(
                                markets, {}, cancelled, [](const QString &) {}, [](const auto &) {})
                      .then([&] { done = true; });
    QTRY_VERIFY_WITH_TIMEOUT(done, 2000);
    QCOMPARE(network.tokens, 1);
    QCOMPARE(network.sales, 2);
    QCOMPARE(network.inventory, 1);
}
void PricingRetrievalTests::cancelAbortsSalesAndKeepsUnknown() {
    FakeNetwork network;
    network.stallSales = true;
    AmazonInventoryApi api("client", "secret", "token", "seller", "FR");
    api.setNetworkAccessManager(&network);
    auto cancelled = std::make_shared<bool>(false);
    api.setReadCancellation(cancelled);
    int units = -1;
    bool done = false;
    const QStringList marketplaces{"FR"};
    auto task = api.fetchSalesUnits("SKU", 90, marketplaces, &units).then([&] { done = true; });
    QTRY_COMPARE_WITH_TIMEOUT(network.sales, 1, 1000);
    *cancelled = true;
    QTRY_VERIFY_WITH_TIMEOUT(done, 1000);
    QCOMPARE(units, -1);
    QCOMPARE(api.lastError(), QString("Cancelled"));
}
void PricingRetrievalTests::readHasHardDeadline() {
    FakeReply reply(QNetworkRequest(QUrl("https://offline.invalid")), {}, true, nullptr);
    bool done = false;
    auto task = AmazonRead::wait(&reply, {}, 25).then([&] { done = true; });
    QTRY_VERIFY_WITH_TIMEOUT(done, 1000);
    QCOMPARE(reply.error(), QNetworkReply::OperationCanceledError);
}
void PricingRetrievalTests::dialogKeepsLogUntilCancellationCompletes() {
    auto cancelled = std::make_shared<bool>(false);
    QPointer<PricingProgressDialog> dialog = new PricingProgressDialog(nullptr, cancelled);
    dialog->show();
    dialog->appendLog("Fetched SKU A");
    dialog->progress("90-day sales", 4, 20, "SKU B / FR");
    QCOMPARE(dialog->findChild<QProgressBar *>("retrievalProgress")->value(), 4);
    auto *button = dialog->findChild<QPushButton *>("retrievalCancelClose");
    button->click();
    QVERIFY(*cancelled);
    QVERIFY(dialog->isVisible());
    QVERIFY(!button->isEnabled());
    QVERIFY(dialog->findChild<QTextEdit *>("retrievalLog")->toPlainText().contains("Fetched SKU A"));
    dialog->finish("Cancelled; cached results kept", false);
    QCOMPARE(button->text(), QString("Close"));
    QVERIFY(button->isEnabled());
    button->click();
    QTRY_VERIFY(dialog.isNull());
}
static std::shared_ptr<AmazonPricingApi> pricingApi(PricingNetwork &network) {
    auto api = std::make_shared<AmazonPricingApi>("client", "secret", "eu", "na", "seller-eu", "seller-na");
    api->setNetworkAccessManager(&network);
    return api;
}
void PricingRetrievalTests::updateAuditIncludesBeforeAfterAndEveryOutcome() {
    QTemporaryDir dir;
    PricingNetwork network;
    AmazonPricingRepository repository(QDir(dir.path()), {}, {}, [&] { return pricingApi(network); });
    QStringList log;
    network.log = &log;
    QList<bool> outcomes;
    const QList<AmazonPricingRepository::Update> updates{
        {"SAME-SKU", "A13V1IB3VIYZZH", "EUR", "SHOES", 20, 25},
        {"SAME-SKU", "A1PA6795UKMFR9", "EUR", "SHOES", 20, 15},
        {"CHANGED", "A13V1IB3VIYZZH", "EUR", "SHOES", 20, 25},
        {"FAIL", "ATVPDKIKX0DER", "USD", "SHOES", 20, 25},
        {"NOOFFER", "A13V1IB3VIYZZH", "EUR", "SHOES", 20, 25}};
    bool done = false;
    auto task = repository
                    .update(
                        updates, std::make_shared<bool>(false), [&](const auto &s) { log << s; },
                        [&](const auto &, bool success) { outcomes << success; })
                    .then([&] { done = true; });
    QTRY_VERIFY_WITH_TIMEOUT(done, 6000);
    QCOMPARE(network.reads, 5);
    QCOMPARE(network.patches, 3);
    QVERIFY(network.loggedBeforePatch);
    QCOMPARE(outcomes, QList<bool>({true, true, false, false, false}));
    const auto text = log.join('\n');
    QVERIFY(text.contains("SKU SAME-SKU | FR [A13V1IB3VIYZZH] | 20.00 → 25.00 EUR | INCREASE"));
    QVERIFY(text.contains("SKU SAME-SKU | DE [A1PA6795UKMFR9] | 20.00 → 15.00 EUR | DECREASE"));
    QVERIFY(text.contains("SKU FAIL | US [ATVPDKIKX0DER] | 20.00 → 25.00 USD"));
    QVERIFY(text.contains("expected 20.00 EUR, live 42.00 EUR"));
    QVERIFY(text.contains("seller offer unavailable"));
    QVERIFY(text.contains("FAILED / UNCONFIRMED"));
    QVERIFY(text.contains("Fixture rejection"));
    QVERIFY(
        text.contains("Planned: 5; submitted: 2; skipped: 2; failed/unconfirmed: 1; cancelled/not sent: 0"));
    QDir logs(dir.path() + "/pricing-update-logs");
    QCOMPARE(logs.entryList({"*.log"}, QDir::Files).size(), 1);
    QFile audit(logs.filePath(logs.entryList({"*.log"}, QDir::Files).first()));
    QVERIFY(audit.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(audit.readAll()), text + '\n');
}
void PricingRetrievalTests::updateCancellation_data() {
    QTest::addColumn<QString>("stage");
    QTest::newRow("before-first-read") << "PLANNED";
    QTest::newRow("after-verification") << "SUBMITTING";
    QTest::newRow("during-rate-limit") << "rate-limit";
    QTest::newRow("in-flight-write") << "patch";
}
void PricingRetrievalTests::updateCancellation() {
    QFETCH(QString, stage);
    QTemporaryDir dir;
    PricingNetwork network;
    auto cancelled = std::make_shared<bool>(false);
    QStringList log;
    network.log = &log;
    if (stage == "patch")
        network.onPatch = [&] { *cancelled = true; };
    AmazonPricingRepository repository(QDir(dir.path()), {}, {}, [&] { return pricingApi(network); });
    const QList<AmazonPricingRepository::Update> updates{
        {"FIRST", "A13V1IB3VIYZZH", "EUR", "SHOES", 20, 25},
        {"SECOND", "A1PA6795UKMFR9", "EUR", "SHOES", 20, 15}};
    bool done = false;
    auto task = repository
                    .update(
                        updates, cancelled,
                        [&](const auto &s) {
                            log << s;
                            if (s.contains(stage))
                                *cancelled = true;
                            if (stage == "rate-limit" && s.contains("SUBMITTING"))
                                QTimer::singleShot(30, [&] { *cancelled = true; });
                        },
                        [](const auto &, bool) {})
                    .then([&] { done = true; });
    QTRY_VERIFY_WITH_TIMEOUT(done, 2500);
    QCOMPARE(network.patches, stage == "patch" ? 1 : 0);
    QCOMPARE(network.reads, stage == "PLANNED" ? 0 : 1);
    const auto text = log.join('\n');
    QVERIFY(text.contains("CANCELLED | SKU SECOND | DE"));
    if (stage == "patch") {
        QVERIFY(text.contains("SUBMITTED [1/2] | SKU FIRST | FR"));
        QVERIFY(text.contains("submitted: 1; skipped: 0; failed/unconfirmed: 0; cancelled/not sent: 1"));
    } else {
        QVERIFY(text.contains("CANCELLED | SKU FIRST | FR"));
        QVERIFY(text.contains("submitted: 0; skipped: 0; failed/unconfirmed: 0; cancelled/not sent: 2"));
    }
}
void PricingRetrievalTests::updateDialogRetainsCancellationLog() {
    auto cancelled = std::make_shared<bool>(false);
    QPointer<PricingProgressDialog> dialog = new PricingProgressDialog(nullptr, cancelled, true);
    dialog->show();
    dialog->appendLog("SKU <A> | FR | 20.00 → 25.00 EUR");
    auto *button = dialog->findChild<QPushButton *>("retrievalCancelClose");
    button->click();
    QVERIFY(*cancelled);
    QVERIFY(dialog->isVisible());
    auto text = dialog->findChild<QTextEdit *>("retrievalLog")->toPlainText();
    QVERIFY(text.contains("SKU <A>"));
    QVERIFY(text.contains("changes already sent cannot be undone"));
    dialog->finish("Cancelled", false);
    button->click();
    QTRY_VERIFY(dialog.isNull());
}
QTEST_MAIN(PricingRetrievalTests)
#include "tst_pricingretrieval.moc"
