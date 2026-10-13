#include "gui/panes/PaneStore.h"
#include "TableStoreAsin.h"
#include "TreeBrandCategories.h"
#include "TreeDeletedProducts.h"
#include "workingdirectory/WorkingDirectoryManager.h"
#include <QAbstractItemModelTester>
#include <QApplication>
#include <QBrush>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeView>
#include <QUrlQuery>
#include <QtTest>
#include <memory>

class StoreReply : public QNetworkReply
{
public:
    StoreReply(const QNetworkRequest &request, QByteArray body, int status, QObject *parent)
        : QNetworkReply(parent), m_body(std::move(body))
    {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        open(QIODevice::ReadOnly);
        QTimer::singleShot(0, this, [this] { setFinished(true); emit readyRead(); emit finished(); });
    }
    void abort() override { setFinished(true); emit finished(); }
    qint64 bytesAvailable() const override { return m_body.size() - m_offset + QNetworkReply::bytesAvailable(); }
protected:
    qint64 readData(char *data, qint64 size) override
    {
        const qint64 n = qMin(size, m_body.size() - m_offset);
        if (n <= 0) return -1;
        memcpy(data, m_body.constData() + m_offset, n);
        m_offset += n;
        return n;
    }
private:
    QByteArray m_body;
    qint64 m_offset = 0;
};

// Full report + Listings HTTP fixture. Any unexpected request fails offline.
class StoreNetwork : public QNetworkAccessManager
{
public:
    QHash<QString, QList<AmazonCatalogApi::StoreItem>> listings;
    QStringList reports;
    QSet<QString> failedReports, failedAttributes;
    QList<QUrl> attributeRequests;
    QList<QJsonObject> salesReports;
    QHash<QString, QByteArray> salesDocuments;
    std::function<QByteArray(const QJsonObject &)> salesBody;
    std::function<void()> onSalesReport;
    QSet<QString> failedSalesRegions;
protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request, QIODevice *data) override
    {
        const QString path = request.url().path();
        QByteArray body;
        int status = 200;
        if (path == "/auth/o2/token") {
            body = R"({"access_token":"offline-token","expires_in":3600})";
        } else if (operation == PostOperation && path == "/reports/2021-06-30/reports") {
            const auto requestBody = QJsonDocument::fromJson(data->readAll()).object();
            const auto ids = requestBody["marketplaceIds"].toArray();
            const QString mp = ids.first().toString();
            for (const auto &id : ids)
                if (request.url().host() != AmazonMarketplace::forMarketplaceId(id.toString())->endpoint())
                    qFatal("Wrong report region");
            if (requestBody["reportType"].toString() == "GET_FLAT_FILE_ALL_ORDERS_DATA_BY_ORDER_DATE_GENERAL") {
                salesReports.append(requestBody);
                const QString reportId = "sales-" + QString::number(salesReports.size());
                salesDocuments.insert(reportId, salesBody ? salesBody(requestBody) : QByteArray());
                if (onSalesReport) onSalesReport();
                return new StoreReply(request, QJsonDocument(QJsonObject{{"reportId", reportId}}).toJson(),
                    failedSalesRegions.contains(AmazonCatalogApi::lwaRegionForMarketplace(mp)) ? 403 : 202, this);
            }
            if (ids.size() != 1) qFatal("Expected one marketplace per listing report");
            reports.append(mp);
            status = failedReports.contains(mp) ? 500 : 202;
            body = QJsonDocument(QJsonObject{{"reportId", mp}}).toJson();
        } else if (path.startsWith("/reports/2021-06-30/reports/")) {
            body = QJsonDocument(QJsonObject{{"processingStatus", "DONE"},
                {"reportDocumentId", path.section('/', -1)}}).toJson();
        } else if (path.startsWith("/reports/2021-06-30/documents/")) {
            body = QJsonDocument(QJsonObject{{"url", "https://offline.invalid/download/" + path.section('/', -1)}}).toJson();
        } else if (path.startsWith("/download/")) {
            if (salesDocuments.contains(path.section('/', -1)))
                return new StoreReply(request, salesDocuments.value(path.section('/', -1)), 200, this);
            body = "seller-sku\tasin1\tquantity\n";
            for (const auto &item : listings.value(path.section('/', -1)))
                body += item.sku.toUtf8() + '\t' + item.asin.toUtf8() + '\t' + QByteArray::number(item.inventory) + '\n';
        } else if (path.startsWith("/listings/2021-08-01/items/")) {
            attributeRequests.append(request.url());
            const auto ids = QUrlQuery(request.url()).queryItemValue("marketplaceIds").split(',');
            const QString sku = path.section('/', -1);
            QJsonArray summaries;
            AmazonCatalogApi::StoreItem found;
            for (const QString &mp : ids) {
                if (request.url().host() != AmazonMarketplace::forMarketplaceId(mp)->endpoint())
                    qFatal("Mixed regions in attributes request");
                for (const auto &item : listings.value(mp)) {
                    if (item.sku != sku) continue;
                    if (found.asin.isEmpty()) found = item;
                    summaries.append(QJsonObject{{"marketplaceId", mp}, {"itemName", item.title},
                                                 {"productType", item.category}});
                }
            }
            QJsonObject attrs;
            const QMap<QString, QString> values{{"brand", found.brand}, {"color", found.color},
                {"size", found.sizeValue}, {"target_gender", found.gender}, {"age_range_description", found.age},
                {"parentage_level", found.isParent ? "parent" : "child"}};
            for (auto it = values.begin(); it != values.end(); ++it)
                attrs[it.key()] = QJsonArray{QJsonObject{{"value", it.value()}}};
            body = QJsonDocument(QJsonObject{{"summaries", summaries}, {"attributes", attrs}}).toJson();
            if (failedAttributes.contains(sku)) status = 500;
        } else {
            qFatal("Unexpected request in offline store test");
        }
        return new StoreReply(request, body, status, this);
    }
};

// Successful local no-op in place of an AI CLI. The app must append the exact
// selected ASINs itself, regardless of generated category text.
class StoreExportCli : public AbstractCli
{
public:
    mutable QString prompt;
    QString getName() const override { return "Offline export stub"; }
    QString getDescription() const override { return {}; }
    bool canGenSvg() const override { return false; }
    bool canGenImages() const override { return true; }
    bool canGenVideosFromText() const override { return false; }
    bool canGenVideoFromImages() const override { return false; }
    QString getExecutable() const override { return "/usr/bin/true"; }
    QStringList promptArgs() const override { return {}; }
    QString preparePrompt(const QString &value) const override { prompt = value; return value; }
};

class StoreTests : public QObject
{
    Q_OBJECT
    using Item = StorePlacements::Item;
    std::unique_ptr<QTemporaryDir> m_dir;

    static QByteArray salesHeader()
    {
        return "amazon-order-id\torder-item-id\tpurchase-date\torder-status\tsales-channel\tsku\tasin\tquantity\n";
    }
    void seedSales(PaneStore &pane, const QHash<QString, int> &counts, int daysOld = 0)
    {
        QJsonObject byAsin;
        for (auto it = counts.cbegin(); it != counts.cend(); ++it) byAsin[it.key()] = it.value();
        QFile file(m_dir->filePath("stores/sales_v2.json"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(QJsonObject{{"version", 2}, {"scope", pane._salesScope()},
            {"endUtc", QDateTime::currentDateTimeUtc().addDays(-daysOld).toString(Qt::ISODate)},
            {"expected", QJsonArray{"fixture"}}, {"slices", QJsonObject{{"fixture", byAsin}}}}).toJson());
        file.close();
        pane.m_sales.load(QDir(m_dir->path()), pane._salesScope());
    }

    static QList<Item> catalog()
    {
        QList<Item> items;
        for (int size = 38; size <= 40; ++size) {
            Item item;
            item.asin = QStringLiteral("A%1").arg(size);
            item.sku = QStringLiteral("PUMP-BLACK-%1").arg(size);
            item.title = "Black pump";
            item.brand = "Brand";
            item.category = "Pumps";
            item.gender = "female";
            item.age = "adult";
            item.color = "BLACK";
            item.sizeValue = QString::number(size);
            items.append(item);
        }
        auto other = items.first();
        other.asin = "B38";
        other.sku = "OTHER-BLACK-38";
        other.title = "Other shoe";
        items.append(other);
        return items;
    }
    static QSet<QString> pumpAsins() { return {"A38", "A39", "A40"}; }
    static QList<Item> shoeCatalog()
    {
        QList<Item> items;
        for (int size : {37, 44, 45}) {
            auto item = catalog().first();
            item.asin = QStringLiteral("A%1").arg(size);
            item.sku = QStringLiteral("CJNS2269160-BLACK-%1").arg(size);
            item.category = "THIGH BOOTS";
            item.sizeValue = QString::number(size == 37 ? 4 : size == 44 ? 9 : 10);
            item.title = QStringLiteral("Black thigh boots (Black, %1)").arg(size);
            items.append(item);
        }
        return items;
    }
    static QStringList category(const QString &name) { return {"Brand", name}; }

    QModelIndex node(TreeBrandCategories *model, const QStringList &path)
    {
        QModelIndex result;
        for (const auto &part : path) {
            QModelIndex next;
            for (int row = 0; row < model->rowCount(result); ++row) {
                const auto child = model->index(row, 0, result);
                if (model->nodeNameForIndex(child) == part) { next = child; break; }
            }
            if (!next.isValid()) return {};
            result = next;
        }
        return result;
    }
    void setup(PaneStore &pane, const QList<Item> &items = catalog())
    {
        pane.setWorkingDir(QDir(m_dir->path()));
        pane.m_customPaths = {category("Pumps"), category("Low heels"), category("Square heels")};
        pane._saveCustomPaths();
        pane._applyItems(items);
        pane._saveToDisk(pane._marketplaceId(), items);
    }
    void selectNode(PaneStore &pane, const QStringList &path)
    {
        const auto index = node(pane.m_treeModel, path);
        QVERIFY2(index.isValid(), qPrintable(path.join('/')));
        pane.findChild<QTreeView *>("treeViewBrandCategory")->setCurrentIndex(index);
    }
    void selectPump(PaneStore &pane)
    {
        auto *table = pane.findChild<QTableView *>("tableViewAsins");
        for (int i = 0; i < pane.m_storeModel->rowCount(); ++i) {
            if (pane.m_storeModel->rows()[i].asin.startsWith('A')) {
                table->selectRow(i);
                return;
            }
        }
        QFAIL("Pump row missing");
    }
    void transfer(PaneStore &pane, bool duplicate, const QStringList &destination,
                  bool cancel = false, bool inspectDestination = true)
    {
        const auto source = pane._currentNodePath();
        bool interacted = false;
        QTimer timeout;
        timeout.setSingleShot(true);
        connect(&timeout, &QTimer::timeout, this, [] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject();
        });
        timeout.start(2000);
        QTimer::singleShot(0, this, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            if (cancel) { interacted = true; dialog->reject(); return; }
            auto *tree = dialog->findChild<QTreeView *>();
            const auto index = node(pane.m_treeModel, destination);
            if (!tree || !index.isValid()) { dialog->reject(); return; }
            tree->setCurrentIndex(index);
            interacted = true;
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        });
        auto *button = pane.findChild<QPushButton *>(duplicate ? "buttonDuplicateProducts" : "buttonMoveProducts");
        QVERIFY(button->isEnabled());
        button->click();
        QVERIFY(interacted);
        QCOMPARE(pane._currentNodePath(), source);
        // Most existing tests inspect the result in the destination category.
        // Navigation is explicit in the test; the action itself stays at source.
        if (!cancel && inspectDestination) selectNode(pane, destination);
    }
    void confirmButton(PaneStore &pane, const char *buttonName, bool yes = true)
    {
        bool interacted = false;
        QTimer timeout;
        timeout.setSingleShot(true);
        connect(&timeout, &QTimer::timeout, this, [] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject();
        });
        timeout.start(2000);
        QTimer::singleShot(0, this, [&] {
            auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!dialog) return;
            interacted = true;
            dialog->button(yes ? QMessageBox::Yes : QMessageBox::Cancel)->click();
        });
        auto *button = pane.findChild<QPushButton *>(buttonName);
        QVERIFY(button->isEnabled());
        button->click();
        QVERIFY(interacted);
    }
    int pumpRow(const PaneStore &pane) const
    {
        for (int i = 0; i < pane.m_storeModel->rowCount(); ++i)
            if (pane.m_storeModel->rows()[i].asin.startsWith('A')) return i;
        return -1;
    }
    void roundTrip(PaneStore &pane)
    {
        pane._saveToDisk(pane._marketplaceId(), pane.m_items);
        pane._loadFromDisk(pane._marketplaceId());
    }

private slots:
    void salesReportParserCountsCountriesAndSkusOnce()
    {
        const auto from = QDateTime::fromString("2026-09-01T00:00:00Z", Qt::ISODate);
        const auto to = from.addDays(30);
        QByteArray tsv = salesHeader();
        const QByteArray ca = "order-ca\ti1\t2026-09-01T00:00:00Z\tShipped\tAmazon.ca\tSKU-CA\tB0DCZLPXTY\t5\n";
        tsv += ca + ca; // duplicate report line
        tsv += "order-ca\ti2\t2026-09-02T00:00:00Z\tShipped\tAmazon.ca\tOTHER-SKU\tB0DCZLPXTY\t3\n";
        tsv += "order-us\ti1\t2026-09-02T00:00:00Z\tPending\tAmazon.com\tSKU-US\tB0DCZLPXTY\t2\n";
        tsv += "cancel\ti1\t2026-09-02T00:00:00Z\tCancelled\tAmazon.ca\tSKU-CA\tB0DCZLPXTY\t99\n";
        tsv += "boundary\ti1\t2026-10-01T00:00:00Z\tShipped\tAmazon.ca\tSKU-CA\tB0DCZLPXTY\t99\n";
        tsv += "mcf\ti1\t2026-09-02T00:00:00Z\tShipped\tNon-Amazon\tSKU-CA\tB0DCZLPXTY\t99\n";
        QHash<QString, QHash<QString, int>> out;
        QString error;
        QVERIFY(AmazonCatalogApi::parseSalesReport(tsv, {"A2EUQ1WTGCTBG2", "ATVPDKIKX0DER", "A1AM78C64UM0Y8"}, from, to, &out, &error));
        QCOMPARE(out["A2EUQ1WTGCTBG2"].value("B0DCZLPXTY"), 8);
        QCOMPARE(out["ATVPDKIKX0DER"].value("B0DCZLPXTY"), 2);
        QVERIFY(out.contains("A1AM78C64UM0Y8"));
        QVERIFY(out["A1AM78C64UM0Y8"].isEmpty());
        QVERIFY(!AmazonCatalogApi::parseSalesReport(tsv + "broken\trow\n", {"A2EUQ1WTGCTBG2"}, from, to, &out, &error));
        QVERIFY(out.isEmpty());
        QVERIFY(!error.isEmpty());
        QVERIFY(!AmazonCatalogApi::parseSalesReport({}, {"A2EUQ1WTGCTBG2"}, from, to, &out, &error));
    }

    void salesReportIgnoresUnrequestedChannels()
    {
        const auto from = QDateTime::fromString("2026-09-01T00:00:00Z", Qt::ISODate);
        const auto to = from.addDays(30);
        QByteArray tsv = salesHeader();
        tsv += "de\ti1\t2026-09-02T00:00:00Z\tShipped\tAmazon.de\tSKU-DE\tBOOT\t7\n";
        tsv += "tr\ti1\t2026-09-02T00:00:00Z\tShipped\tAmazon.com.tr\tSKU-TR\tBOOT\t99\n";
        tsv += "ca\ti1\t2026-09-02T00:00:00Z\tShipped\tAmazon.ca\tSKU-CA\tBOOT\t50\n";
        QHash<QString, QHash<QString, int>> out;
        QString error;
        QVERIFY2(AmazonCatalogApi::parseSalesReport(tsv, {"A1PA6795UKMFR9"}, from, to, &out, &error), qPrintable(error));
        QCOMPARE(out.size(), 1);
        QCOMPARE(out["A1PA6795UKMFR9"].value("BOOT"), 7);
        QVERIFY(error.isEmpty());
        QVERIFY(!AmazonCatalogApi::parseSalesReport(tsv, {"unsupported-marketplace"}, from, to, &out, &error));
        QVERIFY(out.isEmpty());
        QVERIFY(!error.isEmpty());
    }

    void globalSalesRetryCacheAndRanking()
    {
        PaneStore pane;
        auto tennis = catalog().first(); tennis.asin = "B0DCZLPXTY";
        auto secondSize = tennis; secondSize.asin = "B0DCZP4B8X"; secondSize.sku = "PUMP-BLACK-39";
        setup(pane, {tennis, secondSize, catalog().last()});
        delete pane.m_catalogApi;
        pane.m_catalogApi = new AmazonCatalogApi("offline", "offline", "eu", "na", "jp", "eu-seller", "na-seller", "jp-seller", {}, &pane);
        auto *network = new StoreNetwork;
        pane.m_catalogApi->setNetworkAccessManagerForTests(network);
        network->failedSalesRegions.insert("JP");
        network->salesBody = [](const QJsonObject &request) {
            QByteArray body = salesHeader();
            const QString date = request["dataStartTime"].toString();
            for (const auto &id : request["marketplaceIds"].toArray()) {
                const auto *market = AmazonMarketplace::forMarketplaceId(id.toString());
                const int qty = market->countryCode() == "CA" ? 10 : 1;
                body += ("order-" + market->countryCode() + "\ti1\t" + date + "\tShipped\t" + market->salesChannelName()
                         + "\tREGIONAL-SKU\tB0DCZLPXTY\t" + QString::number(qty) + '\n').toUtf8();
                if (market->countryCode() == "CA")
                    body += ("size-2\ti2\t" + date + "\tShipped\tAmazon.ca\tDIFFERENT-SKU\tB0DCZP4B8X\t2\n").toUtf8();
            }
            return body;
        };
        selectNode(pane, category("Pumps"));
        pane._onSortBySales();
        QTRY_VERIFY_WITH_TIMEOUT(pane.isEnabled(), 10000);
        QCOMPARE(network->salesReports.size(), 27); // 13 EU + 13 NA + failed JP
        QVERIFY(!pane.m_sales.total({tennis.asin}).complete);
        const int expectedPartial = 13 * (10 + 2 + 10 + 2); // EU10 + NA(US1,MX1,CA10), plus second size2
        QCOMPARE(pane.m_storeModel->rows().first().salesYear, expectedPartial);
        QVERIFY(!pane.m_storeModel->rows().first().salesComplete);
        QVERIFY(pane.m_storeModel->data(pane.m_storeModel->index(0, TableStoreAsin::ColSalesYear)).toString().endsWith('*'));
        network->failedSalesRegions.clear();
        pane._onSortBySales();
        QTRY_VERIFY_WITH_TIMEOUT(pane.isEnabled(), 10000);
        QCOMPARE(network->salesReports.size(), 40); // Only the 13 missing JP slices retried
        QCOMPARE(pane.m_storeModel->rows().first().salesYear, expectedPartial + 13);
        QVERIFY(pane.m_storeModel->rows().first().salesComplete);
        QCOMPARE(pane.m_sales.total({tennis.asin, tennis.asin}).units, 13 * 23);
        pane._onSortBySales();
        QCOMPARE(network->salesReports.size(), 40); // fresh cache, no requests
        roundTrip(pane);
        selectNode(pane, category("Pumps"));
        QCOMPARE(pane.m_storeModel->rows().first().salesYear, expectedPartial + 13);
        StoreSales reloaded;
        reloaded.load(QDir(m_dir->path()), pane._salesScope());
        QVERIFY(reloaded.total({tennis.asin}).complete);
        QCOMPARE(reloaded.total({tennis.asin}).units, 13 * 23);
        reloaded.load(QDir(m_dir->path()), "different-account");
        QCOMPARE(reloaded.total({tennis.asin}).units, -1);
        for (const auto &request : network->salesReports) {
            const auto from = QDateTime::fromString(request["dataStartTime"].toString(), Qt::ISODate);
            const auto to = QDateTime::fromString(request["dataEndTime"].toString(), Qt::ISODate);
            QVERIFY(from < to);
            QVERIFY(from.secsTo(to) <= 30 * 86400);
        }
        for (auto *widget : QApplication::topLevelWidgets())
            if (auto *dialog = qobject_cast<QDialog *>(widget)) dialog->close();
    }

    void salesCancellationAndMissingDataAreNotZero()
    {
        PaneStore pane;
        setup(pane);
        delete pane.m_catalogApi;
        pane.m_catalogApi = new AmazonCatalogApi("offline", "offline", {}, "na", {}, {}, "na-seller", {}, {}, &pane);
        auto *network = new StoreNetwork;
        pane.m_catalogApi->setNetworkAccessManagerForTests(network);
        network->salesBody = [](const QJsonObject &) { return salesHeader(); };
        network->onSalesReport = [&pane] { *pane.m_salesCancelled = true; };
        selectNode(pane, category("Pumps"));
        pane._onSortBySales();
        QTRY_VERIFY_WITH_TIMEOUT(pane.isEnabled(), 10000);
        QVERIFY(!pane.m_sales.total({"A38"}).complete);
        QCOMPARE(pane.m_sales.total({"A38"}).units, -1);
        QCOMPARE(network->salesReports.size(), 1);
        network->onSalesReport = {};
        pane._onSortBySales();
        QTRY_VERIFY_WITH_TIMEOUT(pane.isEnabled(), 10000);
        QVERIFY(pane.m_sales.total({"A38"}).complete);
        QCOMPARE(pane.m_sales.total({"A38"}).units, 0); // successful empty reports are real zeros
        for (auto *widget : QApplication::topLevelWidgets())
            if (auto *dialog = qobject_cast<QDialog *>(widget)) dialog->close();
    }

    void salesProgressResumesFromSuccessfulPeriods()
    {
        AmazonCatalogApi api("offline", "offline", "eu", "na", "jp", "eu-seller", "na-seller", "jp-seller");
        auto *network = new StoreNetwork;
        api.setNetworkAccessManagerForTests(network);
        network->salesBody = [](const QJsonObject &request) {
            const QString date = request["dataStartTime"].toString();
            QByteArray body = salesHeader();
            body += ("tr\ti1\t" + date + "\tShipped\tAmazon.com.tr\tSKU-TR\tBOOT\t99\n").toUtf8();
            for (const auto &id : request["marketplaceIds"].toArray()) {
                const auto *mp = AmazonMarketplace::forMarketplaceId(id.toString());
                body += ("order\ti1\t" + date + "\tShipped\t" + mp->salesChannelName()
                         + "\tSKU\tBOOT\t2\n").toUtf8();
            }
            return body;
        };
        network->failedSalesRegions.insert("EU");
        StoreSales sales;
        sales.load(QDir(m_dir->path()), "progress-fixture");
        QList<QPair<int, int>> counts;
        QStringList messages;
        const QList<QStringList> regions{{"A1PA6795UKMFR9"}, {"A2EUQ1WTGCTBG2"}, {"A1VC38T7YXB528"}};
        const auto cancelled = std::make_shared<bool>(false);
        bool done = false;
        auto run = [&]() -> QCoro::Task<void> {
            co_await sales.refresh(&api, regions, cancelled,
                [&](const QString &message) { messages.append(message); },
                [&](int completed, int total) { counts.append({completed, total}); });
            done = true;
        };
        auto task = run();
        QTRY_VERIFY_WITH_TIMEOUT(done, 3000);
        QCOMPARE(counts.first(), qMakePair(0, 39));
        QCOMPARE(counts.last(), qMakePair(26, 39)); // failed EU does not advance the bar
        QCOMPARE(sales.total({"BOOT"}).units, 52);
        QVERIFY(!sales.total({"BOOT"}).complete);
        QVERIFY(messages.join('\n').contains("CA —"));
        QVERIFY(messages.join('\n').contains("Waiting for Amazon"));
        QVERIFY(messages.join('\n').contains("Downloading sales report"));
        sales.load(QDir(m_dir->path()), "progress-fixture");
        network->failedSalesRegions.clear();
        counts.clear();
        done = false;
        task = run();
        QTRY_VERIFY_WITH_TIMEOUT(done, 3000);
        QCOMPARE(counts.first(), qMakePair(26, 39));
        QCOMPARE(counts.last(), qMakePair(39, 39));
        QCOMPARE(network->salesReports.size(), 40); // failed EU, 26 CA/JP, then only 13 EU
        QCOMPARE(sales.total({"BOOT"}).units, 78); // Turkey excluded, saved sales counted once
        QVERIFY(sales.total({"BOOT"}).complete);
    }

    void salesCacheAgePolicy_data()
    {
        QTest::addColumn<int>("daysOld");
        QTest::addColumn<bool>("acceptRefresh");
        QTest::addColumn<bool>("expectPrompt");
        QTest::addColumn<bool>("expectRefresh");
        QTest::newRow("under-two-weeks") << 13 << false << false << false;
        QTest::newRow("two-weeks-decline") << 15 << false << true << false;
        QTest::newRow("two-weeks-accept") << 15 << true << true << true;
        QTest::newRow("under-thirty-days-decline") << 29 << false << true << false;
        QTest::newRow("over-thirty-days") << 31 << false << false << true;
    }

    void salesCacheAgePolicy()
    {
        QFETCH(int, daysOld);
        QFETCH(bool, acceptRefresh);
        QFETCH(bool, expectPrompt);
        QFETCH(bool, expectRefresh);
        PaneStore pane;
        setup(pane);
        delete pane.m_catalogApi;
        pane.m_catalogApi = new AmazonCatalogApi("offline", "offline", {}, "na", {}, {}, "na-seller", {}, {}, &pane);
        auto *network = new StoreNetwork;
        pane.m_catalogApi->setNetworkAccessManagerForTests(network);
        network->salesBody = [](const QJsonObject &) { return salesHeader(); };
        seedSales(pane, {{"A38", 123}}, daysOld);
        const auto oldEnd = pane.m_sales.endTime();
        QCOMPARE(pane.m_sales.fresh(), daysOld <= 30);
        QCOMPARE(pane.m_sales.refreshSuggested(), expectPrompt);
        selectNode(pane, category("Pumps"));
        int prompts = 0;
        QTimer answer;
        connect(&answer, &QTimer::timeout, this, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                ++prompts;
                box->button(acceptRefresh ? QMessageBox::Yes : QMessageBox::No)->click();
            }
        });
        answer.start(10);
        pane._onSortBySales();
        QTRY_VERIFY_WITH_TIMEOUT(pane.isEnabled(), 10000);
        answer.stop();
        QCOMPARE(prompts, expectPrompt ? 1 : 0);
        QCOMPARE(network->salesReports.size(), expectRefresh ? 13 : 0);
        QVERIFY(pane.m_sales.total({"A38"}).complete);
        QCOMPARE(pane.m_sales.total({"A38"}).units, expectRefresh ? 0 : 123);
        if (expectRefresh) QVERIFY(pane.m_sales.endTime() > oldEnd);
        else QCOMPARE(pane.m_sales.endTime(), oldEnd);
    }

    void salesJobSurvivesPaneDestruction()
    {
        auto *pane = new PaneStore;
        setup(*pane);
        delete pane->m_catalogApi;
        pane->m_catalogApi = new AmazonCatalogApi("offline", "offline", {}, "na", {}, {}, "na-seller", {}, {}, pane);
        auto *network = new StoreNetwork;
        pane->m_catalogApi->setNetworkAccessManagerForTests(network);
        QPointer<StoreNetwork> networkGuard(network);
        QPointer<PaneStore> paneGuard(pane);
        network->salesBody = [](const QJsonObject &) { return salesHeader(); };
        network->onSalesReport = [pane] { QTimer::singleShot(0, [pane] { delete pane; }); };
        selectNode(*pane, category("Pumps"));
        pane->_onSortBySales();
        QTRY_VERIFY_WITH_TIMEOUT(paneGuard.isNull(), 2000);
        QTRY_VERIFY_WITH_TIMEOUT(networkGuard.isNull(), 2000);
    }
    void initTestCase()
    {
        QCoreApplication::setOrganizationName("AmazonTemplate3OfflineTests");
        QCoreApplication::setApplicationName("StoreTests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
    }
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_dir->isValid());
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_dir->path());
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, m_dir->path());
        WorkingDirectoryManager::instance()->setWorkingDir(QDir(m_dir->path()));
    }
    void retrieveMergesCountriesAndPreservesSavedWork()
    {
        PaneStore pane;
        auto shared = catalog().first();
        shared.category = "Low heels";
        shared.manuallyMoved = true;
        setup(pane, {shared});
        pane.m_placements.transfer(pane.m_items, {shared.asin}, category("Low heels"), category("Square heels"), true);
        const auto placements = pane.m_placements.toJson(shared);
        const QString groupKey = TreeBrandCategories::colorGroupKey(shared);
        pane.m_nodeOrders.insert("Brand\x1fLow heels", {groupKey});
        pane._saveOrder();
        auto deleted = catalog().last();
        pane.m_deletedProducts->record({deleted});
        const QString de = "A1PA6795UKMFR9", us = "ATVPDKIKX0DER", ca = "A2EUQ1WTGCTBG2", jp = "A1VC38T7YXB528";
        delete pane.m_catalogApi;
        pane.m_catalogApi = new AmazonCatalogApi("offline", "offline", "eu", "na", "jp", "eu-seller", "na-seller", "jp-seller", {}, &pane);
        auto *network = new StoreNetwork;
        pane.m_catalogApi->setNetworkAccessManagerForTests(network);
        shared.category = "Pumps";
        shared.inventory = 7;
        auto alternative = shared;
        alternative.sku = "US-OTHER-SKU";
        alternative.inventory = 20;
        auto onlyUs = shared; onlyUs.asin = "US1"; onlyUs.sku = "US-BLACK-38";
        auto onlyCa = shared; onlyCa.asin = "B0DCZLPXTY"; onlyCa.sku = "TENNIS-BLACK-38";
        auto onlyJp = shared; onlyJp.asin = "JP1"; onlyJp.sku = "JP-BLACK-38";
        auto parent = shared; parent.asin = "PARENT"; parent.sku = "PARENT"; parent.isParent = true;
        network->listings[de] = {shared, parent};
        // FR summary follows DE: it must not be lost by an early return.
        network->listings["A13V1IB3VIYZZH"] = {shared};
        network->listings[us] = {alternative, onlyUs};
        network->listings[ca] = {shared, onlyCa, deleted};
        network->listings[jp] = {shared, onlyJp};
        for (int pass = 0; pass < 2; ++pass) {
            pane.m_retrieveTask = pane._onRetrieve();
            QTRY_VERIFY_WITH_TIMEOUT(pane.isEnabled(), 10000);
            QCOMPARE(pane.m_items.size(), 4);
            QCOMPARE(pane.m_asinToItem.size(), 4);
            QVERIFY(pane.m_asinToItem.contains(onlyCa.asin));
            QVERIFY(pane.m_asinToItem.contains(onlyUs.asin));
            QVERIFY(pane.m_asinToItem.contains(onlyJp.asin));
            QVERIFY(!pane.m_asinToItem.contains(parent.asin));
            QVERIFY(!pane.m_asinToItem.contains(deleted.asin));
            const auto merged = pane.m_asinToItem.value(shared.asin);
            QCOMPARE(merged.sku, shared.sku);
            QCOMPARE(merged.inventory, 7); // shared country stock is not added
            QCOMPARE(merged.category, "Low heels");
            QCOMPARE(merged.existsInMarketplaces, (QSet<QString>{de, us, ca, jp, "A13V1IB3VIYZZH"}));
            QCOMPARE(pane.m_placements.toJson(merged), placements);
            roundTrip(pane);
            QCOMPARE(pane.m_items.size(), 4);
            QCOMPARE(pane.m_nodeOrders.value("Brand\x1fLow heels"), QStringList{groupKey});
            QVERIFY(pane.m_deletedProducts->contains(deleted.asin));
        }
        QCOMPARE(network->reports, (QStringList{de, us, ca, jp, de, us, ca, jp}));
    }
    void retrieveFallsBackToCanadaAndDeduplicatesPrimary_data()
    {
        QTest::addColumn<bool>("knownCanadianSku");
        QTest::newRow("new-product-falls-back") << false;
        QTest::newRow("existing-canadian-sku-remains-canonical") << true;
    }
    void retrieveFallsBackToCanadaAndDeduplicatesPrimary()
    {
        QFETCH(bool, knownCanadianSku);
        QSettings().setValue("store/marketplaceId", "ATVPDKIKX0DER");
        PaneStore pane;
        setup(pane, {});
        delete pane.m_catalogApi;
        pane.m_catalogApi = new AmazonCatalogApi("offline", "offline", "eu", "na", "jp", "eu-seller", "na-seller", "jp-seller", {}, &pane);
        auto *network = new StoreNetwork;
        pane.m_catalogApi->setNetworkAccessManagerForTests(network);
        auto usItem = catalog().first();
        auto caItem = usItem; caItem.sku = "CANADA-SKU"; caItem.title = "Canadian title";
        if (knownCanadianSku) pane._applyItems({caItem});
        network->listings["ATVPDKIKX0DER"] = {usItem};
        network->listings["A2EUQ1WTGCTBG2"] = {caItem};
        network->failedAttributes.insert(usItem.sku);
        pane.m_retrieveTask = pane._onRetrieve();
        QTRY_VERIFY_WITH_TIMEOUT(pane.isEnabled(), 10000);
        QCOMPARE(network->reports, (QStringList{"ATVPDKIKX0DER", "A2EUQ1WTGCTBG2", "A1VC38T7YXB528"}));
        QCOMPARE(pane.m_items.size(), 1);
        QCOMPARE(pane.m_items.first().sku, knownCanadianSku ? caItem.sku : usItem.sku);
        QCOMPARE(pane.m_items.first().title, caItem.title);
        QCOMPARE(pane.m_items.first().existsInMarketplaces, (QSet<QString>{"ATVPDKIKX0DER", "A2EUQ1WTGCTBG2"}));
        QCOMPARE(network->attributeRequests.size(), knownCanadianSku ? 1 : 2);
    }
    void retrieveKeepsCacheWhenRegionsAreUnavailable()
    {
        PaneStore pane;
        auto cached = catalog().last();
        cached.existsInMarketplaces = {"A2EUQ1WTGCTBG2"};
        setup(pane, {cached});
        delete pane.m_catalogApi;
        // Seller ID alone must not trigger reports without a refresh token.
        pane.m_catalogApi = new AmazonCatalogApi("offline", "offline", "eu", {}, "jp", "eu-seller", "na-seller", "jp-seller", {}, &pane);
        auto *network = new StoreNetwork;
        pane.m_catalogApi->setNetworkAccessManagerForTests(network);
        network->listings["A1PA6795UKMFR9"] = {catalog().first()};
        network->failedReports.insert("A1VC38T7YXB528");
        pane.m_retrieveTask = pane._onRetrieve();
        QTRY_VERIFY_WITH_TIMEOUT(pane.isEnabled(), 10000);
        QCOMPARE(network->reports, (QStringList{"A1PA6795UKMFR9", "A1VC38T7YXB528"}));
        QCOMPARE(pane.m_items.size(), 2);
        QCOMPARE(pane.m_asinToItem.value(cached.asin).existsInMarketplaces, cached.existsInMarketplaces);
        roundTrip(pane);
        QCOMPARE(pane.m_items.size(), 2);
    }
    void legacyGroupingMoveRemoveAndCopy()
    {
        PaneStore pane;
        setup(pane);
        QAbstractItemModelTester treeTester(pane.m_treeModel, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QAbstractItemModelTester tableTester(pane.m_storeModel, QAbstractItemModelTester::FailureReportingMode::QtTest);
        selectNode(pane, category("Pumps"));
        QCOMPARE(pane.m_storeModel->rowCount(), 2); // two families sharing BLACK
        selectPump(pane);
        const auto representative = pane.m_storeModel->rows()[pumpRow(pane)].asin;
        pane.findChild<QPushButton *>("buttonCopyAsins")->click();
        QCOMPARE(QApplication::clipboard()->text(), representative);
        transfer(pane, false, category("Low heels"));
        QCOMPARE(pane.m_treeModel->asinsForIndex(node(pane.m_treeModel, category("Low heels"))).size(), 3);
        QCOMPARE(pane.m_treeModel->asinsForIndex(node(pane.m_treeModel, category("Pumps"))), QStringList{"B38"});
        for (const auto &item : pane.m_items)
            if (item.asin.startsWith('A')) { QCOMPARE(item.category, "Low heels"); QVERIFY(item.manuallyMoved); }
        selectPump(pane);
        confirmButton(pane, "buttonRemoveProducts");
        QCOMPARE(pane.m_items.size(), 1);
        roundTrip(pane);
        QCOMPARE(pane.m_items.first().asin, "B38");
    }
    void duplicateMoveDeleteAndColor()
    {
        PaneStore pane;
        setup(pane);
        QVERIFY(!pane.findChild<QPushButton *>("buttonDuplicateProducts")->isEnabled());
        selectNode(pane, category("Pumps"));
        selectPump(pane);
        transfer(pane, true, category("Low heels"));
        QCOMPARE(pane.m_items.size(), 4);
        QCOMPARE(pane.m_storeModel->rowCount(), 1);
        QVERIFY(pane.m_storeModel->rows().first().duplicate);
        QCOMPARE(pane.m_storeModel->data(pane.m_storeModel->index(0, TableStoreAsin::ColAsin), Qt::ForegroundRole)
                     .value<QBrush>().color(), QColor(135, 206, 250));
        QCOMPARE(pane.m_treeModel->asinsForIndex(node(pane.m_treeModel, category("Low heels"))).size(), 3);
        selectPump(pane);
        transfer(pane, true, category("Low heels")); // same destination is idempotent
        QCOMPARE(pane.m_storeModel->rowCount(), 1);
        for (const auto &item : pane.m_items)
            if (item.asin.startsWith('A')) QCOMPARE(pane.m_placements.placements(item).size(), 2);
        selectPump(pane);
        transfer(pane, false, category("Square heels"));
        QVERIFY(pane.m_treeModel->asinsForIndex(node(pane.m_treeModel, category("Low heels"))).isEmpty());
        QCOMPARE(pane.m_storeModel->rowCount(), 1);
        selectPump(pane);
        confirmButton(pane, "buttonRemoveProducts");
        selectNode(pane, category("Pumps"));
        QCOMPARE(pane.m_storeModel->rowCount(), 2);
        QVERIFY(!pane.m_storeModel->rows()[pumpRow(pane)].duplicate);
        QCOMPARE(pane.m_items.size(), 4);
    }
    void destinationsStayWithinProductBrand_data()
    {
        QTest::addColumn<bool>("duplicate");
        QTest::addColumn<bool>("unknownBrand");
        QTest::newRow("duplicate") << true << false;
        QTest::newRow("move") << false << false;
        QTest::newRow("duplicate-unknown-brand") << true << true;
        QTest::newRow("move-unknown-brand") << false << true;
    }
    void destinationsStayWithinProductBrand()
    {
        QFETCH(bool, duplicate);
        QFETCH(bool, unknownBrand);
        auto items = catalog();
        if (unknownBrand)
            for (auto &item : items) item.brand.clear();
        auto other = items.last();
        other.asin = "C38";
        other.brand = "Other brand";
        other.category = "Low heels";
        items.append(other);
        PaneStore pane;
        setup(pane, items);
        const QString sourceBrand = unknownBrand ? "(unknown brand)" : "Brand";
        const QStringList source{sourceBrand, "Pumps"};
        const QStringList target{sourceBrand, "Low heels"};
        pane.m_customPaths.append(target);
        pane._applyItems(items);
        selectNode(pane, source);
        selectPump(pane);

        bool inspected = false;
        QTimer timeout;
        timeout.setSingleShot(true);
        connect(&timeout, &QTimer::timeout, this, [] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject();
        });
        timeout.start(2000);
        QTimer::singleShot(0, this, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            auto *tree = dialog->findChild<QTreeView *>();
            QVERIFY(tree);
            QCOMPARE(tree->rootIndex(), node(pane.m_treeModel, {sourceBrand}));
            const auto allowed = node(pane.m_treeModel, target);
            QCOMPARE(allowed.parent(), tree->rootIndex());
            const auto otherBrand = node(pane.m_treeModel, {"Other brand", "Low heels"});
            QVERIFY(otherBrand.isValid());
            QVERIFY(otherBrand.parent() != tree->rootIndex());
            auto *ok = dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);

            // Even a programmatically selected index outside the visible subtree
            // must not bypass the brand restriction.
            tree->setCurrentIndex(otherBrand);
            ok->click();
            QVERIFY(dialog->isVisible());
            QCOMPARE(pane.m_asinToItem.value("A38").brand, unknownBrand ? QString() : QString("Brand"));
            tree->setCurrentIndex(allowed);
            inspected = true;
            ok->click();
        });
        pane.findChild<QPushButton *>(duplicate ? "buttonDuplicateProducts" : "buttonMoveProducts")->click();
        QVERIFY(inspected);
        QCOMPARE(pane._currentNodePath(), source);
        QCOMPARE(pane.m_treeModel->asinsForIndex(node(pane.m_treeModel, target)).size(), 3);
        QCOMPARE(pane.m_treeModel->asinsForIndex(node(pane.m_treeModel, {"Other brand", "Low heels"})), QStringList{"C38"});
        QCOMPARE(pane.m_treeModel->asinsForIndex(node(pane.m_treeModel, source)).size(), duplicate ? 4 : 1);
        QCOMPARE(pane.m_asinToItem.value("A38").brand, unknownBrand ? QString() : QString("Brand"));
    }
    void productActionsPreserveTree_data()
    {
        QTest::addColumn<QString>("action");
        QTest::addColumn<bool>("lastProduct");
        for (const auto &action : {"duplicate", "move", "remove"}) {
            QTest::newRow(action) << QString(action) << false;
            QTest::newRow(qPrintable(QString(action) + "-last-product")) << QString(action) << true;
        }
    }
    void productActionsPreserveTree()
    {
        QFETCH(QString, action);
        QFETCH(bool, lastProduct);
        auto items = catalog();
        if (lastProduct) items.removeLast();
        auto other = catalog().last();
        other.asin = "C38";
        other.brand = "Other brand";
        items.prepend(other);
        PaneStore pane;
        setup(pane, items);
        for (int i = 0; i < 30; ++i)
            pane.m_customPaths.append({"Other brand", QStringLiteral("Category %1").arg(i), "female"});
        pane._applyItems(items);
        QAbstractItemModelTester treeTester(pane.m_treeModel, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QSignalSpy resetSpy(pane.m_treeModel, &QAbstractItemModel::modelReset);
        pane.resize(1500, 650);
        pane.show();
        auto *tree = pane.findChild<QTreeView *>("treeViewBrandCategory");
        tree->expandAll();
        tree->collapse(node(pane.m_treeModel, {"Other brand", "Category 0"}));
        const QStringList source{"Brand", "Pumps", "female"};
        selectNode(pane, source);
        selectPump(pane);
        tree->selectionModel()->select(node(pane.m_treeModel, category("Square heels")),
                                       QItemSelectionModel::Select | QItemSelectionModel::Rows);
        tree->doItemsLayout();
        tree->scrollTo(tree->currentIndex(), QAbstractItemView::PositionAtBottom);
        QCoreApplication::processEvents();
        const QPersistentModelIndex current = tree->currentIndex();
        const int scrollPosition = tree->verticalScrollBar()->value();
        QVERIFY(scrollPosition > 0);
        QList<QPersistentModelIndex> oldNodes;
        QList<bool> oldExpansion;
        std::function<void(QModelIndex)> remember = [&](QModelIndex parent) {
            for (int row = 0; row < pane.m_treeModel->rowCount(parent); ++row) {
                const auto child = pane.m_treeModel->index(row, 0, parent);
                oldNodes.append(child);
                oldExpansion.append(tree->isExpanded(child));
                remember(child);
            }
        };
        remember({});
        const auto siblingNames = [&]() {
            QStringList names;
            const auto brand = node(pane.m_treeModel, {"Brand"});
            for (int row = 0; row < pane.m_treeModel->rowCount(brand); ++row)
                names.append(pane.m_treeModel->nodeNameForIndex(pane.m_treeModel->index(row, 0, brand)));
            return names;
        };
        const auto order = siblingNames();
        if (action == "remove") confirmButton(pane, "buttonRemoveProducts");
        else transfer(pane, action == "duplicate", category("Low heels"), false, false);
        QCoreApplication::processEvents();
        QCOMPARE(resetSpy.count(), 0);
        QVERIFY(current.isValid());
        QCOMPARE(tree->currentIndex(), QModelIndex(current));
        QCOMPARE(pane._currentNodePath(), source);
        QCOMPARE(siblingNames(), order);
        QCOMPARE(tree->verticalScrollBar()->value(), scrollPosition);
        QCOMPARE(tree->selectionModel()->selectedRows().size(), 2);
        for (int i = 0; i < oldNodes.size(); ++i) {
            QVERIFY(oldNodes[i].isValid());
            QCOMPARE(tree->isExpanded(oldNodes[i]), oldExpansion[i]);
        }
        const int expected = action == "duplicate" ? (lastProduct ? 1 : 2) : (lastProduct ? 0 : 1);
        QCOMPARE(pane.m_storeModel->rowCount(), expected);
        QCOMPARE(pane.m_treeModel->colorCountForIndex(current), expected);
        if (expected == 1 && action != "duplicate") QCOMPARE(pane.m_storeModel->rows().first().asin, "B38");
    }
    void aggregateCountsStockAndWarnings()
    {
        PaneStore pane;
        setup(pane);
        pane.m_placements.transfer(pane.m_items, pumpAsins(), category("Pumps"), category("Low heels"), true);
        pane.m_placements.transfer(pane.m_items, pumpAsins(), category("Pumps"), category("Square heels"), true);
        const auto items = pane.m_items;
        pane._applyItems(items);
        for (const auto &item : items) {
            pane.m_stockAvailableBySkuLower[item.sku.toLower()] = 10;
            pane.m_stockSales90BySkuLower[item.sku.toLower()] = 90;
        }
        QHash<QString, int> counts;
        for (const auto &item : items) counts.insert(item.asin, 365);
        seedSales(pane, counts);
        selectNode(pane, {"Brand"});
        QCOMPARE(pane.m_treeModel->asinsForIndex(node(pane.m_treeModel, {"Brand"})).size(), 4);
        QCOMPARE(pane.m_treeModel->colorCountForIndex(node(pane.m_treeModel, {"Brand"})), 2);
        QCOMPARE(pane.m_storeModel->rowCount(), 2);
        auto row = pane.m_storeModel->rows()[pumpRow(pane)];
        QCOMPARE(row.salesYear, 1095);
        QCOMPARE(row.stockDays, 10);
        QVERIFY(!row.duplicate);
        selectNode(pane, category("Low heels"));
        row = pane.m_storeModel->rows().first();
        QVERIFY(row.duplicate);
        QCOMPARE(row.salesYear, 1095);
        QCOMPARE(row.stockDays, 10);
        QCOMPARE(pane.m_storeModel->data(pane.m_storeModel->index(0, TableStoreAsin::ColStockDays), Qt::ForegroundRole)
                     .value<QBrush>().color(), QColor(0xd9, 0x3c, 0x3c));
        selectPump(pane);
        pane.findChild<QPushButton *>("buttonCopyAsins")->click();
        QCOMPARE(QApplication::clipboard()->text(), row.asin);
    }
    void persistenceRefreshAndOriginalDeletion()
    {
        PaneStore pane;
        setup(pane);
        selectNode(pane, category("Pumps"));
        selectPump(pane);
        transfer(pane, true, category("Low heels"));
        roundTrip(pane);
        selectNode(pane, category("Low heels"));
        QVERIFY(pane.m_storeModel->rows().first().duplicate);
        // This is the same apply/save boundary used by Retrieve, with fake fresh attributes.
        auto fresh = catalog();
        for (auto &item : fresh) item.title = "Refreshed title";
        pane._applyItems(fresh);
        roundTrip(pane);
        selectNode(pane, category("Low heels"));
        QCOMPARE(pane.m_storeModel->rows().first().title, "Refreshed title");
        selectNode(pane, category("Pumps"));
        selectPump(pane);
        confirmButton(pane, "buttonRemoveProducts");
        QCOMPARE(pane.m_items.size(), 4); // shared data retained for the remaining placement
        roundTrip(pane);
        pane._applyItems(fresh);
        selectNode(pane, category("Pumps"));
        QCOMPARE(pane.m_storeModel->rowCount(), 1);
        QCOMPARE(pane.m_storeModel->rows().first().asin, "B38");
        selectNode(pane, category("Low heels"));
        QCOMPARE(pane.m_storeModel->rowCount(), 1);
        QVERIFY(pane.m_storeModel->rows().first().duplicate);
        selectPump(pane);
        confirmButton(pane, "buttonRemoveProducts");
        QCOMPARE(pane.m_items.size(), 1);
        roundTrip(pane);
        pane._applyItems(fresh);
        QCOMPARE(pane.m_items.size(), 1); // last duplicate deletion cannot resurrect the original
        QCOMPARE(pane.m_deletedProducts->items().size(), 3);
    }
    void deletedProductsPersistAndRestore()
    {
        PaneStore pane;
        setup(pane);
        QAbstractItemModelTester tester(pane.m_deletedProducts,
            QAbstractItemModelTester::FailureReportingMode::QtTest);
        selectNode(pane, category("Pumps"));
        selectPump(pane);
        confirmButton(pane, "buttonRemoveProducts", false);
        QVERIFY(pane.m_deletedProducts->items().isEmpty());
        confirmButton(pane, "buttonRemoveProducts");
        QCOMPARE(pane.m_deletedProducts->items().size(), 3);
        roundTrip(pane);
        pane._applyItems(catalog()); // same filtering boundary as Retrieve
        QCOMPARE(pane.m_items.size(), 1);
        roundTrip(pane);
        QCOMPARE(pane.m_deletedProducts->items().size(), 3);

        bool restored = false;
        QTimer::singleShot(0, this, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            auto *tree = dialog->findChild<QTreeView *>();
            tree->setCurrentIndex(pane.m_deletedProducts->index(0, 0));
            auto *button = dialog->findChild<QPushButton *>("buttonRestoreDeleted");
            restored = button && button->isEnabled();
            if (restored) button->click();
            dialog->reject();
        });
        pane.findChild<QPushButton *>("buttonViewDeleted")->click();
        QVERIFY(restored);
        QCOMPARE(pane.m_items.size(), 4);
        QVERIFY(pane.m_deletedProducts->items().isEmpty());
        roundTrip(pane);
        pane._applyItems(catalog());
        QCOMPARE(pane.m_items.size(), 4);
        selectNode(pane, category("Pumps"));
        QCOMPARE(pane.m_storeModel->rowCount(), 2);

        selectPump(pane);
        confirmButton(pane, "buttonRemoveProducts");
        pane._loadFromDisk("other-marketplace");
        QVERIFY(pane.m_deletedProducts->items().isEmpty());
        pane._loadFromDisk(pane._marketplaceId());
        QCOMPARE(pane.m_deletedProducts->items().size(), 3);
        QTemporaryDir other;
        pane.setWorkingDir(QDir(other.path()));
        QVERIFY(pane.m_deletedProducts->items().isEmpty());
    }

    void duplicateDeletionNeverArchived()
    {
        PaneStore pane;
        setup(pane);
        selectNode(pane, category("Pumps"));
        selectPump(pane);
        transfer(pane, true, category("Low heels"));
        selectPump(pane);
        confirmButton(pane, "buttonRemoveProducts");
        QVERIFY(pane.m_deletedProducts->items().isEmpty());
        roundTrip(pane);
        pane._applyItems(catalog());
        selectNode(pane, category("Low heels"));
        QCOMPARE(pane.m_storeModel->rowCount(), 0);

        selectNode(pane, category("Pumps"));
        selectPump(pane);
        transfer(pane, true, category("Low heels"));
        selectNode(pane, category("Pumps"));
        selectPump(pane);
        confirmButton(pane, "buttonRemoveProducts");
        QCOMPARE(pane.m_deletedProducts->items().size(), 3);
        roundTrip(pane);
        pane._restoreDeleted(pumpAsins());
        selectNode(pane, category("Pumps"));
        QCOMPARE(pane.m_storeModel->rowCount(), 2);
        selectNode(pane, category("Low heels"));
        QCOMPARE(pane.m_storeModel->rowCount(), 1);
        QVERIFY(pane.m_storeModel->rows().first().duplicate);
    }

    void categoryRemovalKeepsExpansion()
    {
        PaneStore pane;
        setup(pane);
        auto *tree = pane.findChild<QTreeView *>("treeViewBrandCategory");
        QAbstractItemModelTester tester(pane.m_treeModel,
            QAbstractItemModelTester::FailureReportingMode::QtTest);
        tree->expandAll();
        const QPersistentModelIndex brand(node(pane.m_treeModel, {"Brand"}));
        const QPersistentModelIndex pumps(node(pane.m_treeModel, category("Pumps")));
        const QPersistentModelIndex gender(node(pane.m_treeModel, {"Brand", "Pumps", "female"}));
        tree->collapse(gender);
        QSignalSpy resets(pane.m_treeModel, &QAbstractItemModel::modelReset);
        selectNode(pane, category("Low heels"));
        confirmButton(pane, "buttonRemoveCategory");
        QCOMPARE(resets.size(), 0);
        QVERIFY(brand.isValid());
        QVERIFY(pumps.isValid());
        QVERIFY(tree->isExpanded(brand));
        QVERIFY(tree->isExpanded(pumps));
        QVERIFY(!tree->isExpanded(gender));
        QVERIFY(!node(pane.m_treeModel, category("Low heels")).isValid());
    }

    void originalMoveAndDestinationCollision()
    {
        PaneStore pane;
        setup(pane);
        selectNode(pane, category("Pumps"));
        selectPump(pane);
        transfer(pane, true, category("Low heels"));
        selectNode(pane, category("Pumps"));
        selectPump(pane);
        transfer(pane, false, category("Square heels"));
        selectNode(pane, category("Low heels"));
        QCOMPARE(pane.m_storeModel->rowCount(), 1);
        QVERIFY(pane.m_storeModel->rows().first().duplicate);
        selectPump(pane);
        transfer(pane, false, category("Square heels"));
        QVERIFY(!pane.m_storeModel->rows().first().duplicate);
        for (const auto &item : pane.m_items)
            if (item.asin.startsWith('A')) QCOMPARE(pane.m_placements.placements(item).size(), 1);
    }
    void representativeShoeSize_data()
    {
        QTest::addColumn<QString>("sizeSource");
        QTest::addColumn<int>("stockedSize");
        QTest::addColumn<QString>("expectedAsin");
        QTest::newRow("regional-catalog-size") << "sku" << 0 << "A45";
        QTest::newRow("opaque-sku-title-size") << "title" << 0 << "A45";
        QTest::newRow("eu-catalog-size") << "catalog" << 0 << "A45";
        QTest::newRow("only-44-in-stock") << "sku" << 44 << "A44";
        QTest::newRow("only-37-in-stock") << "sku" << 37 << "A37";
        QTest::newRow("45-in-stock") << "sku" << 45 << "A45";
        QTest::newRow("several-sizes-in-stock") << "sku" << -1 << "A45";
        QTest::newRow("44-fallback") << "without45" << 0 << "A44";
        QTest::newRow("half-size-is-not-45") << "half" << 0 << "A45";
    }
    void representativeShoeSize()
    {
        QFETCH(QString, sizeSource);
        QFETCH(int, stockedSize);
        QFETCH(QString, expectedAsin);
        auto items = shoeCatalog();
        if (sizeSource == "without45") items.removeLast();
        if (sizeSource == "half") {
            auto half = items.last();
            half.asin = "A455";
            half.sku = "CJNS2269160-BLACK-45.5";
            half.sizeValue = "45.5";
            half.title = "Black thigh boots (Black, 45.5)";
            items.prepend(half);
        }
        for (int i = 0; i < items.size(); ++i) {
            auto &item = items[i];
            if (sizeSource == "title" || sizeSource == "catalog") {
                item.sku = QStringLiteral("CJNS2269160%1AB").arg(i, 2, 10, QLatin1Char('0'));
                item.sizeValue = sizeSource == "catalog" ? item.asin.mid(1) : QString{};
                item.title = sizeSource == "title"
                    ? QStringLiteral("Boots with 14 cm heels (Black, EU-%1)").arg(item.asin.mid(1))
                    : QStringLiteral("Boots with 14 cm heels");
            }
        }
        PaneStore pane;
        setup(pane, items);
        for (const auto &item : items)
            pane.m_stockAvailableBySkuLower[item.sku.toLower()] =
                stockedSize == -1 || item.asin == QStringLiteral("A%1").arg(stockedSize) ? 20 : 0;
        selectNode(pane, category("THIGH BOOTS"));
        QCOMPARE(pane.m_storeModel->rowCount(), 1);
        QCOMPARE(pane.m_storeModel->rows().first().asin, expectedAsin);
        QCOMPARE(pane.m_treeModel->colorCountForIndex(node(pane.m_treeModel, category("THIGH BOOTS"))), 1);
        QCOMPARE(pane._buildAsinGroups(pane.m_treeModel->asinsForIndex(
                     node(pane.m_treeModel, category("THIGH BOOTS")))).value(expectedAsin).size(), items.size());
    }
    void representativeChangePreservesSavedWork_data()
    {
        QTest::addColumn<bool>("legacyOrder");
        QTest::newRow("legacy-asin-order") << true;
        QTest::newRow("per-category-order") << false;
    }
    void representativeChangePreservesSavedWork()
    {
        QFETCH(bool, legacyOrder);
        auto items = shoeCatalog();
        for (const QString &family : {QStringLiteral("B"), QStringLiteral("C")}) {
            for (auto item : shoeCatalog()) {
                item.asin.replace(0, 1, family);
                item.sku.prepend(family);
                items.append(item);
            }
        }
        PaneStore pane;
        setup(pane, items);
        const QSet<QString> familyA{"A37", "A44", "A45"};
        const QSet<QString> familyC{"C37", "C44", "C45"};
        pane.m_placements.transfer(pane.m_items, familyA, category("THIGH BOOTS"), category("Low heels"), true);
        QList<Item> deleted;
        for (const auto &item : items)
            if (familyC.contains(item.asin)) deleted.append(item);
        pane.m_deletedProducts->record(deleted);
        pane.m_placements.remove(pane.m_items, familyC, category("THIGH BOOTS"));
        pane._saveToDisk(pane._marketplaceId(), pane.m_items);

        const QString nodeKey = category("THIGH BOOTS").join('\x1f');
        const QJsonArray oldAsins{"B37", "A37", "C37"};
        const QJsonArray groupKeys{TreeBrandCategories::colorGroupKey(items[3]),
                                   TreeBrandCategories::colorGroupKey(items[0]),
                                   TreeBrandCategories::colorGroupKey(items[6])};
        const QJsonDocument order = legacyOrder ? QJsonDocument(oldAsins)
            : QJsonDocument(QJsonObject{{"legacy", oldAsins}, {"nodes", QJsonObject{{nodeKey, groupKeys}}}});
        QFile orderFile(m_dir->filePath("stores/A1PA6795UKMFR9_order.json"));
        QVERIFY(orderFile.open(QIODevice::WriteOnly));
        const auto orderBytes = order.toJson();
        orderFile.write(orderBytes);
        orderFile.close();
        QFile catalogFile(m_dir->filePath("stores/A1PA6795UKMFR9.json"));
        QVERIFY(catalogFile.open(QIODevice::ReadOnly));
        const auto catalogBytes = catalogFile.readAll();
        catalogFile.close();

        PaneStore reloaded;
        reloaded.setWorkingDir(QDir(m_dir->path()));
        selectNode(reloaded, category("THIGH BOOTS"));
        QCOMPARE(reloaded.m_storeModel->rowCount(), 2);
        QCOMPARE(reloaded.m_storeModel->rows()[0].asin, "B45");
        QCOMPARE(reloaded.m_storeModel->rows()[1].asin, "A45");
        for (const auto &asin : familyC) QVERIFY(reloaded.m_deletedProducts->contains(asin));
        QCOMPARE(reloaded.m_deletedProducts->items().size(), 3);
        selectNode(reloaded, category("Low heels"));
        QCOMPARE(reloaded.m_storeModel->rowCount(), 1);
        QVERIFY(reloaded.m_storeModel->rows().first().duplicate);
        QCOMPARE(reloaded.m_storeModel->rows().first().asin, "A45");

        // Stock changes the representative, never the product's saved position.
        for (const auto &item : reloaded.m_items)
            reloaded.m_stockAvailableBySkuLower[item.sku.toLower()] = item.asin.endsWith("44") ? 20 : 0;
        reloaded._saveStockCache();
        reloaded.setWorkingDir(QDir(m_dir->path()));
        selectNode(reloaded, category("THIGH BOOTS"));
        QCOMPARE(reloaded.m_storeModel->rows()[0].asin, "B44");
        QCOMPARE(reloaded.m_storeModel->rows()[1].asin, "A44");
        QCOMPARE(reloaded.m_deletedProducts->items().size(), 3);
        QVERIFY(orderFile.open(QIODevice::ReadOnly));
        QCOMPARE(orderFile.readAll(), orderBytes);
        QVERIFY(catalogFile.open(QIODevice::ReadOnly));
        QCOMPARE(catalogFile.readAll(), catalogBytes);
    }
    void moveSelectionToEdge_data()
    {
        QTest::addColumn<bool>("toTop");
        QTest::addColumn<QList<int>>("selectedRows");
        for (bool toTop : {true, false}) {
            const QByteArray prefix = toTop ? "top-" : "bottom-";
            QTest::newRow((prefix + "disjoint").constData()) << toTop << QList<int>{1, 3};
            QTest::newRow((prefix + "contiguous").constData()) << toTop << QList<int>{1, 2};
            QTest::newRow((prefix + "both-edges").constData()) << toTop << QList<int>{0, 4};
            QTest::newRow((prefix + "already-at-edge").constData()) << toTop
                << (toTop ? QList<int>{0, 1} : QList<int>{3, 4});
            QTest::newRow((prefix + "all").constData()) << toTop << QList<int>{0, 1, 2, 3, 4};
            QTest::newRow((prefix + "single").constData()) << toTop << QList<int>{2};
            QTest::newRow((prefix + "empty").constData()) << toTop << QList<int>{};
        }
    }
    void moveSelectionToEdge()
    {
        QFETCH(bool, toTop);
        QFETCH(QList<int>, selectedRows);
        QList<Item> items;
        for (int i = 0; i < 5; ++i) {
            auto item = catalog().first();
            item.asin = QStringLiteral("PRODUCT%1").arg(i);
            item.sku = QStringLiteral("SHOE%1-BLACK-38").arg(i);
            items.append(item);
        }
        PaneStore pane;
        setup(pane, items);
        selectNode(pane, category("Pumps"));
        QCOMPARE(pane.m_storeModel->rowCount(), 5);
        QAbstractItemModelTester tester(pane.m_storeModel,
                                       QAbstractItemModelTester::FailureReportingMode::QtTest);
        auto *table = pane.findChild<QTableView *>("tableViewAsins");
        table->clearSelection();
        QStringList selectedAsins, remainingAsins;
        for (int row = 0; row < 5; ++row) {
            const auto asin = pane.m_storeModel->rows()[row].asin;
            if (selectedRows.contains(row)) selectedAsins.append(asin);
            else remainingAsins.append(asin);
        }
        // Select in reverse click order; movement must preserve table order.
        for (auto it = selectedRows.crbegin(); it != selectedRows.crend(); ++it)
            table->selectionModel()->select(pane.m_storeModel->index(*it, 0),
                                           QItemSelectionModel::Select | QItemSelectionModel::Rows);
        pane.findChild<QPushButton *>(toTop ? "buttonMoveToTop" : "buttonMoveToBottom")->click();
        const QStringList expected = toTop ? selectedAsins + remainingAsins
                                           : remainingAsins + selectedAsins;
        QStringList actual;
        for (const auto &row : pane.m_storeModel->rows()) actual.append(row.asin);
        QCOMPARE(actual, expected);
        QStringList stillSelected;
        for (int row : pane._selectedTableRows())
            stillSelected.append(pane.m_storeModel->rows()[row].asin);
        QCOMPARE(stillSelected, selectedAsins);

        pane._loadOrder();
        roundTrip(pane);
        selectNode(pane, category("Pumps"));
        actual.clear();
        for (const auto &row : pane.m_storeModel->rows()) actual.append(row.asin);
        QCOMPARE(actual, expected);
    }
    void independentOrderingAndLegacyMigration()
    {
        PaneStore pane;
        setup(pane);
        // Legacy order files contained representative ASINs; preserve that initial order.
        QFile legacy(m_dir->filePath("stores/A1PA6795UKMFR9_order.json"));
        QVERIFY(legacy.open(QIODevice::WriteOnly));
        legacy.write("[\"B38\",\"A38\"]");
        legacy.close();
        pane._loadOrder();
        selectNode(pane, category("Pumps"));
        QCOMPARE(pane.m_storeModel->rows().first().asin, "B38");
        pane.findChild<QTableView *>("tableViewAsins")->selectAll();
        transfer(pane, true, category("Low heels"));
        QCOMPARE(pane.m_storeModel->rows().first().asin, "B38");
        selectPump(pane);
        pane.findChild<QPushButton *>("buttonMoveToTop")->click();
        QVERIFY(pane.m_storeModel->rows().first().asin.startsWith('A'));
        selectNode(pane, category("Pumps"));
        QCOMPARE(pane.m_storeModel->rows().first().asin, "B38");
        roundTrip(pane);
        selectNode(pane, category("Low heels"));
        QVERIFY(pane.m_storeModel->rows().first().asin.startsWith('A'));
        selectNode(pane, category("Pumps"));
        QCOMPARE(pane.m_storeModel->rows().first().asin, "B38");
    }
    void removeCategoryPreservesOtherPlacements()
    {
        PaneStore pane;
        setup(pane);
        selectNode(pane, category("Pumps"));
        selectPump(pane);
        transfer(pane, true, category("Low heels"));
        confirmButton(pane, "buttonRemoveCategory");
        QVERIFY(!node(pane.m_treeModel, category("Low heels")).isValid());
        selectNode(pane, category("Pumps"));
        QCOMPARE(pane.m_storeModel->rowCount(), 2);
        selectPump(pane);
        transfer(pane, true, category("Square heels"));
        selectNode(pane, category("Pumps"));
        confirmButton(pane, "buttonRemoveCategory");
        QVERIFY(!node(pane.m_treeModel, category("Pumps")).isValid());
        selectNode(pane, category("Square heels"));
        QCOMPARE(pane.m_storeModel->rowCount(), 1);
        selectNode(pane, category("(unknown category)"));
        QCOMPARE(pane.m_storeModel->rowCount(), 2);
        for (const auto &item : pane.m_items) QVERIFY(item.manuallyMoved);
        roundTrip(pane);
        QVERIFY(!node(pane.m_treeModel, category("Pumps")).isValid());
    }
    void mergeIsScopedAndDeduplicates()
    {
        PaneStore pane;
        auto items = catalog();
        auto otherBrand = items.first();
        otherBrand.asin = "C38";
        otherBrand.brand = "Other brand";
        otherBrand.category = "Low heels";
        items.append(otherBrand);
        setup(pane, items);
        selectNode(pane, category("Pumps"));
        selectPump(pane);
        transfer(pane, true, category("Low heels"));
        auto names = pane.m_treeModel->englishNames();
        names[QStringList{"Brand", "Low heels", "female"}.join('\x1f')] = "Women's heels";
        pane.m_treeModel->setEnglishNames(names);
        auto *tree = pane.findChild<QTreeView *>("treeViewBrandCategory");
        tree->selectionModel()->clearSelection();
        for (const auto &path : {category("Pumps"), category("Low heels")})
            tree->selectionModel()->select(node(pane.m_treeModel, path), QItemSelectionModel::Select | QItemSelectionModel::Rows);
        bool interacted = false;
        QTimer timeout;
        timeout.setSingleShot(true);
        connect(&timeout, &QTimer::timeout, this, [] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject();
        });
        timeout.start(2000);
        QTimer::singleShot(0, this, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            for (auto *radio : dialog->findChildren<QRadioButton *>())
                if (radio->text().startsWith("Pumps")) radio->setChecked(true);
            interacted = true;
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        });
        auto *button = pane.findChild<QPushButton *>("buttonMerge");
        QVERIFY(button->isEnabled());
        button->click();
        QVERIFY(interacted);
        QVERIFY(!node(pane.m_treeModel, category("Low heels")).isValid());
        QVERIFY(node(pane.m_treeModel, {"Other brand", "Low heels"}).isValid());
        QCOMPARE(pane.m_storeModel->rowCount(), 2);
        QCOMPARE(pane.m_treeModel->asinsForIndex(node(pane.m_treeModel, {"Brand"})).size(), 4);
        QCOMPARE(pane.m_treeModel->englishNameForIndex(node(pane.m_treeModel, {"Brand", "Pumps", "female"})), "Women's heels");
        roundTrip(pane);
        QVERIFY(!node(pane.m_treeModel, category("Low heels")).isValid());
    }
    void cancelAndWorkingDirectoryIsolation()
    {
        PaneStore pane;
        setup(pane);
        selectNode(pane, category("Pumps"));
        selectPump(pane);
        transfer(pane, true, category("Low heels"), true);
        QCOMPARE(pane.m_placements.placements(pane.m_items.first()).size(), 1);
        confirmButton(pane, "buttonRemoveProducts", false);
        QCOMPARE(pane.m_items.size(), 4);
        transfer(pane, true, category("Low heels"));
        QTemporaryDir empty;
        QVERIFY(empty.isValid());
        WorkingDirectoryManager::instance()->setWorkingDir(QDir(empty.path()));
        pane.setWorkingDir(QDir(empty.path()));
        QVERIFY(pane.m_items.isEmpty());
        QCOMPARE(pane.m_treeModel->rowCount({}), 0);
        QVERIFY(!pane.findChild<QPushButton *>("buttonDuplicateProducts")->isEnabled());
    }
    void mixedSelectionMovesOnlyCurrentPlacements()
    {
        PaneStore pane;
        setup(pane);
        pane.m_placements.transfer(pane.m_items, pumpAsins(), category("Pumps"), category("Low heels"), true);
        pane.m_placements.transfer(pane.m_items, {"B38"}, category("Pumps"), category("Low heels"), false);
        const auto items = pane.m_items;
        pane._applyItems(items);
        selectNode(pane, category("Low heels"));
        QCOMPARE(pane.m_storeModel->rowCount(), 2);
        QVERIFY(pane.m_storeModel->rows()[pumpRow(pane)].duplicate);
        pane.findChild<QTableView *>("tableViewAsins")->selectAll();
        transfer(pane, false, category("Square heels"));
        QCOMPARE(pane.m_storeModel->rowCount(), 2);
        QCOMPARE(pane.m_asinToItem.value("B38").category, "Square heels");
        selectNode(pane, category("Pumps"));
        QCOMPARE(pane.m_storeModel->rowCount(), 1);
        QVERIFY(!pane.m_storeModel->rows().first().duplicate);
        selectNode(pane, category("Low heels"));
        QCOMPARE(pane.m_storeModel->rowCount(), 0);
    }
    void aggregateRemovalPreservesOutsideBrand()
    {
        auto items = catalog();
        StorePlacements placements;
        placements.transfer(items, pumpAsins(), category("Pumps"), category("Low heels"), true);
        placements.transfer(items, pumpAsins(), category("Pumps"), {"Other brand", "Pumps"}, true);
        placements.remove(items, pumpAsins(), {"Brand"});
        QCOMPARE(items.size(), 4);
        for (const auto &item : items) {
            if (!item.asin.startsWith('A')) continue;
            const auto remaining = placements.placements(item);
            QCOMPARE(remaining.size(), 1);
            QCOMPARE(remaining.first().path[0], "Other brand");
            QVERIFY(remaining.first().duplicate);
        }
        placements.removeCategory(items, {"Other brand"});
        QCOMPARE(items.size(), 1);
        QCOMPARE(items.first().asin, "B38");
    }
    void duplicateExportUsesCategoryAndUniqueAsins()
    {
        StoreExportCli cli;
        PaneStore pane;
        setup(pane);
        pane.setAvailableClis({&cli});
        selectNode(pane, category("Pumps"));
        selectPump(pane);
        transfer(pane, true, category("Low heels"));
        auto names = pane.m_treeModel->englishNames();
        names[category("Low heels").join('\x1f')] = "Low heels";
        names[QStringLiteral("Brand")] = "All shoes";
        pane.m_treeModel->setEnglishNames(names);
        // Rebuild to display the English names, just as loading persisted names does.
        const auto items = pane.m_items;
        pane._applyItems(items);
        QDir dir(m_dir->path());
        QVERIFY(dir.mkpath("stores/storefront"));
        QPixmap image(4, 4);
        image.fill(Qt::white);
        QVERIFY(image.save(dir.filePath("stores/storefront/fixture.png")));
        QFile versions(dir.filePath("stores/storefront/versions.json"));
        QVERIFY(versions.open(QIODevice::WriteOnly));
        versions.write(QJsonDocument(QJsonArray{QJsonObject{{"ts", 1}, {"desktop", "fixture.png"}}}).toJson());
        versions.close();
        pane.findChild<QLineEdit *>("lineEditExportFolder")->setText(dir.filePath("exports"));

        for (const auto &path : {category("Low heels"), QStringList{"Brand"}}) {
            selectNode(pane, path);
            pane.findChild<QTableView *>("tableViewAsins")->selectAll();
            pane.findChild<QListWidget *>("listVersionStrip")->setCurrentRow(0);
            QStringList expected;
            for (const auto &row : pane.m_storeModel->rows()) expected.append(row.asin);
            QCOMPARE(expected.size(), path.size() == 1 ? 2 : 1);
            bool completed = false;
            bool success = false;
            QTimer closeDialog;
            connect(&closeDialog, &QTimer::timeout, this, [&] {
                if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                    success = box->icon() == QMessageBox::Information;
                    completed = true;
                    box->accept();
                }
            });
            closeDialog.start(10);
            pane.findChild<QPushButton *>("buttonExportProducts")->click();
            QTRY_VERIFY_WITH_TIMEOUT(completed, 3000);
            QVERIFY(success);
            const auto outputDir = dir.filePath("exports/" + path.join('/'));
            QFile asins(outputDir + "/ASINS.txt");
            QVERIFY(asins.open(QIODevice::ReadOnly));
            QCOMPARE(QString::fromUtf8(asins.readAll()).trimmed(), expected.join(','));
            QVERIFY(QFile::exists(outputDir + "/fixture.png"));
            QVERIFY(cli.prompt.contains(path.size() == 1 ? "All shoes" : "Low heels"));
        }
    }

    void placementValidationAndRefreshPaths()
    {
        StorePlacements placements;
        auto items = catalog();
        placements.load("A38", QJsonArray{QJsonObject{{"path", QJsonArray{"bad"}}}});
        QCOMPARE(placements.placements(items.first()).size(), 1);
        placements.transfer(items, pumpAsins(), category("Pumps"), category("Low heels"), true);
        items.first().category = "Updated Amazon category";
        const auto memberships = placements.placements(items.first());
        QCOMPARE(memberships.size(), 2);
        QCOMPARE(memberships.first().path[1], "Updated Amazon category");
        QCOMPARE(memberships.last().path[1], "Low heels");
        StorePlacements loaded;
        loaded.load(items.first().asin, placements.toJson(items.first()));
        QCOMPARE(loaded.placements(items.first()).size(), 2);
        QVERIFY(loaded.isDuplicate(items.first(), category("Low heels")));
    }

    void exportImagesAcrossSizesAndOver60Warning()
    {
        StoreExportCli cli;
        QList<Item> items;
        for (int i = 0; i < 65; ++i) {
            Item it;
            it.asin = QStringLiteral("Z%1").arg(i);
            it.sku = QStringLiteral("SKU-%1").arg(i);
            it.title = QStringLiteral("Product %1").arg(i);
            it.brand = "Brand";
            it.category = "Sneakers";
            it.gender = "female";
            it.age = "adult";
            it.color = QStringLiteral("COLOR_%1").arg(i);
            it.sizeValue = "40";
            items.append(it);
        }

        PaneStore pane;
        setup(pane, items);
        pane.setAvailableClis({&cli});

        auto names = pane.m_treeModel->englishNames();
        names[category("Sneakers").join('\x1f')] = "Sneakers";
        pane.m_treeModel->setEnglishNames(names);
        pane._applyItems(items);

        QDir dir(m_dir->path());
        QVERIFY(dir.mkpath("stores/storefront"));
        QPixmap imgH(4, 4); imgH.fill(Qt::red);
        QVERIFY(imgH.save(dir.filePath("stores/storefront/horizontal.jpg")));
        QPixmap imgS(4, 4); imgS.fill(Qt::blue);
        QVERIFY(imgS.save(dir.filePath("stores/storefront/square.jpg")));

        QFile versions(dir.filePath("stores/storefront/versions.json"));
        QVERIFY(versions.open(QIODevice::WriteOnly));
        versions.write(QJsonDocument(QJsonArray{QJsonObject{
            {"ts", 2},
            {"horizontal", "horizontal.jpg"},
            {"square", "square.jpg"},
            {"nodePath", QJsonArray{"Brand", "Sneakers"}}
        }}).toJson());
        versions.close();

        pane.findChild<QLineEdit *>("lineEditExportFolder")->setText(dir.filePath("exports"));
        selectNode(pane, category("Sneakers"));
        pane.findChild<QTableView *>("tableViewAsins")->selectAll();
        QCOMPARE(pane._selectedTableRows().size(), 65);

        // Case A: Prompted with > 60 warning. User responds No -> export cancelled.
        {
            bool promptHandled = false;
            QTimer handlePrompt;
            handlePrompt.setSingleShot(true);
            connect(&handlePrompt, &QTimer::timeout, this, [&] {
                if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                    QVERIFY(box->text().contains("60"));
                    promptHandled = true;
                    if (auto *noBtn = box->button(QMessageBox::No)) {
                        noBtn->click();
                    } else {
                        box->reject();
                    }
                }
            });
            handlePrompt.start(10);
            pane.findChild<QPushButton *>("buttonExportProducts")->click();
            QTRY_VERIFY_WITH_TIMEOUT(promptHandled, 1000);
            const auto outDir = dir.filePath("exports/Brand/Sneakers");
            QVERIFY(!QFile::exists(outDir + "/ASINS.txt"));
            QVERIFY(!QFile::exists(outDir + "/horizontal.jpg"));
            QVERIFY(!QFile::exists(outDir + "/square.jpg"));
        }

        // Case B: Prompted with > 60 warning. User responds Yes -> export proceeds, both images copied.
        {
            bool promptHandled = false;
            bool success = false;
            bool infoHandled = false;
            QTimer handleDialogs;
            connect(&handleDialogs, &QTimer::timeout, this, [&] {
                if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                    if (!promptHandled && box->icon() == QMessageBox::Question) {
                        promptHandled = true;
                        if (auto *yesBtn = box->button(QMessageBox::Yes)) {
                            yesBtn->click();
                        } else {
                            box->accept();
                        }
                    } else if (box->icon() == QMessageBox::Information) {
                        infoHandled = true;
                        success = true;
                        box->accept();
                    }
                }
            });
            handleDialogs.start(10);
            pane.findChild<QPushButton *>("buttonExportProducts")->click();
            QTRY_VERIFY_WITH_TIMEOUT(infoHandled, 3000);
            handleDialogs.stop();
            QVERIFY(promptHandled);
            QVERIFY(success);

            const auto outDir = dir.filePath("exports/Brand/Sneakers");
            QVERIFY(QFile::exists(outDir + "/ASINS.txt"));
            QVERIFY(QFile::exists(outDir + "/horizontal.jpg"));
            QVERIFY(QFile::exists(outDir + "/square.jpg"));
        }
    }

    void oldInBottomCompletesMissingSales_data()
    {
        QTest::addColumn<bool>("failRetry");
        QTest::addColumn<bool>("cancelRetry");
        QTest::newRow("complete-then-reorder") << false << false;
        QTest::newRow("failure-explains-unchanged-order") << true << false;
        QTest::newRow("cancel-keeps-order") << false << true;
    }

    void oldInBottomCompletesMissingSales()
    {
        QFETCH(bool, failRetry);
        QFETCH(bool, cancelRetry);
        auto old = catalog().first();
        old.asin = "OLD"; old.sku = "OLD-39";
        old.createdDate = QDate::currentDate().addYears(-6);
        auto recent = old;
        recent.asin = "NEW"; recent.sku = "NEW-39";
        recent.createdDate = QDate::currentDate().addMonths(-5);
        PaneStore pane;
        setup(pane, {old, recent});
        delete pane.m_catalogApi;
        pane.m_catalogApi = new AmazonCatalogApi("offline", "offline", "eu", "na", "jp", "eu-seller", "na-seller", "jp-seller", {}, &pane);
        auto *network = new StoreNetwork;
        pane.m_catalogApi->setNetworkAccessManagerForTests(network);
        network->salesBody = [](const QJsonObject &) { return salesHeader(); };
        network->failedSalesRegions.insert("EU");
        pane.m_sales.load(QDir(m_dir->path()), pane._salesScope());
        bool seeded = false;
        auto prime = [&]() -> QCoro::Task<void> {
            co_await pane.m_sales.refresh(pane.m_catalogApi, pane._salesRegions(), {}, {});
            seeded = true;
        };
        auto task = prime();
        QTRY_VERIFY_WITH_TIMEOUT(seeded, 5000);
        QCOMPARE(network->salesReports.size(), 27); // failed EU, 13 NA + 13 JP saved
        pane.m_stockAvailableBySkuLower[old.sku.toLower()] = 0;
        pane.m_stockAvailableBySkuLower[recent.sku.toLower()] = 10;
        selectNode(pane, category("Pumps"));
        QCOMPARE(pane.m_storeModel->rows().first().asin, QString("OLD"));
        QCOMPARE(pane.m_storeModel->rows().first().salesYear, 0);
        QVERIFY(!pane.m_storeModel->rows().first().salesComplete);
        if (!failRetry) network->failedSalesRegions.clear();
        if (cancelRetry) network->onSalesReport = [&] { *pane.m_salesCancelled = true; };
        QString explanation;
        QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, this, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                explanation = box->text();
                box->accept();
            }
        });
        dismiss.start(10);
        pane.findChild<QPushButton *>("buttonOldInBottom")->click();
        QTRY_VERIFY_WITH_TIMEOUT(pane.isEnabled(), 5000);
        dismiss.stop();
        QCOMPARE(network->salesReports.size(), failRetry || cancelRetry ? 28 : 40);
        QCOMPARE(pane.m_storeModel->rows().first().asin,
                 failRetry || cancelRetry ? QString("OLD") : QString("NEW"));
        QCOMPARE(pane.m_sales.total({}).complete, !failRetry && !cancelRetry);
        if (failRetry) QVERIFY(explanation.contains("still incomplete"));
        else QVERIFY(explanation.isEmpty());
        if (!failRetry && !cancelRetry) {
            pane.findChild<QPushButton *>("buttonOldInBottom")->click();
            QCOMPARE(network->salesReports.size(), 40); // complete cache is reused
            roundTrip(pane);
            selectNode(pane, category("Pumps"));
            QCOMPARE(pane.m_storeModel->rows().first().asin, QString("NEW"));
        }
        for (auto *widget : QApplication::topLevelWidgets())
            if (auto *dialog = qobject_cast<QDialog *>(widget)) dialog->close();
    }

    void oldInBottomMovesOldDeadProductsToBottom()
    {
        const QDate now = QDate::currentDate();
        struct Spec {
            QString asin;
            QString sku;
            QDate created;
            int sales365;
            int available;
        };
        const QList<Spec> specs = {
            {"A01", "SKU-A01", now.addYears(-3), 0, 0},
            {"B01", "SKU-B01", now.addYears(-3), 10, 0},
            {"C01", "SKU-C01", now.addYears(-3), 0, 5},
            {"D01", "SKU-D01", now.addMonths(-6), 0, 0},
            {"E01", "SKU-E01", now.addYears(-4), 0, 0},
            {"F01", "SKU-F01", now.addYears(-1), 50, 20},
        };

        QList<Item> items;
        for (const auto &sp : specs) {
            Item it;
            it.asin = sp.asin;
            it.sku = sp.sku;
            it.title = QString("Item %1").arg(sp.asin);
            it.brand = "Brand";
            it.category = "Boots";
            it.gender = "female";
            it.age = "adult";
            it.color = sp.asin; // distinct color for distinct row
            it.sizeValue = "39";
            it.createdDate = sp.created;
            items.append(it);
        }

        // Set up stock cache in settings
        auto s = WorkingDirectoryManager::instance()->settings();
        QJsonObject availObj, sales365Obj;
        for (const auto &sp : specs) {
            availObj.insert(sp.sku.toLower(), sp.available);
            sales365Obj.insert(sp.sku.toLower(), sp.sales365);
        }
        s->setValue(QStringLiteral("PaneStoreStockCache/timestamp"),
                    static_cast<qlonglong>(QDateTime::currentDateTimeUtc().toSecsSinceEpoch()));
        s->setValue(QStringLiteral("PaneStoreStockCache/available"),
                    QString::fromUtf8(QJsonDocument(availObj).toJson(QJsonDocument::Compact)));
        s->setValue(QStringLiteral("PaneStoreStockCache/sales365"),
                    QString::fromUtf8(QJsonDocument(sales365Obj).toJson(QJsonDocument::Compact)));

        PaneStore pane;
        setup(pane, items);
        QHash<QString, int> counts;
        for (const auto &sp : specs) counts.insert(sp.asin, sp.sales365);
        seedSales(pane, counts);
        selectNode(pane, category("Boots"));

        // Check initial order in table
        QStringList initialAsins;
        for (const auto &r : pane.m_storeModel->rows()) initialAsins.append(r.asin);
        QCOMPARE(initialAsins, (QStringList{"A01", "B01", "C01", "D01", "E01", "F01"}));

        // Click "Old in bottom" button
        auto *btn = pane.findChild<QPushButton *>("buttonOldInBottom");
        QVERIFY(btn);
        QVERIFY(btn->toolTip().contains("2 years"));
        btn->click();

        // Expected order:
        // Remaining active products in exact relative order: B01, C01, D01, F01
        // Old dead products moved to bottom in relative order: A01, E01
        QStringList resultAsins;
        for (const auto &r : pane.m_storeModel->rows()) resultAsins.append(r.asin);
        QCOMPARE(resultAsins, (QStringList{"B01", "C01", "D01", "F01", "A01", "E01"}));

        // Verify saved order persistence across reload
        roundTrip(pane);
        selectNode(pane, category("Boots"));
        QStringList reloadedAsins;
        for (const auto &r : pane.m_storeModel->rows()) reloadedAsins.append(r.asin);
        QCOMPARE(reloadedAsins, (QStringList{"B01", "C01", "D01", "F01", "A01", "E01"}));
    }
};

QTEST_MAIN(StoreTests)
#include "tst_store.moc"
