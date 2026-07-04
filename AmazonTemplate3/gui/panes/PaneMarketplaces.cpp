#pragma GCC optimize("O1")
#include "PaneMarketplaces.h"
#include "ui_PaneMarketplaces.h"

#include "AmazonInventoryApi.h"
#include "TemuInventoryApi.h"
#include "TableMarketplaceProducts.h"
#include "TableMarketplaceOrders.h"
#include "SettingsTable.h"

#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QShowEvent>
#include <QSpinBox>
#include <QTableWidget>
#include <QTextEdit>
#include <QVBoxLayout>

#include "TemuStoreModel.h"
#include "../../common/workingdirectory/WorkingDirectoryManager.h"
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

// Amazon France marketplace ID (used for FBA inventory report creation)
static const QString k_marketplaceId = QStringLiteral("A13V1IB3VIYZZH");

// All EU Amazon marketplaces — Sales API only accepts one per call,
// so we query each and sum. Querying an inactive marketplace returns 0.
static const QStringList k_euMarketplaceIds = {
    QStringLiteral("A1PA6795UKMFR9"), // DE
    QStringLiteral("A13V1IB3VIYZZH"), // FR
    QStringLiteral("APJ6JRA9NG5V4"),  // IT
    QStringLiteral("A1RKKUPIHCS9HS"), // ES
    QStringLiteral("A1805IZSGTT6HW"), // NL
    QStringLiteral("A2NODRKZP88ZB9"), // SE
    QStringLiteral("A1C3SOZRARQ6R3"), // PL
    QStringLiteral("ARBP9OOSHTCHU"),  // BE
};

static void saveAmazonCache(const QList<AmazonInventoryApi::InventorySummary> &summaries, const QHash<QString, int> &sales)
{
    auto s = WorkingDirectoryManager::instance()->settings();
    s->setValue(QStringLiteral("AmazonCache/timestamp"), QDateTime::currentDateTimeUtc().toSecsSinceEpoch());

    QJsonArray invArray;
    for (const auto &item : summaries) {
        QJsonObject obj;
        obj.insert(QStringLiteral("sku"), item.sku);
        obj.insert(QStringLiteral("asin"), item.asin);
        obj.insert(QStringLiteral("available"), item.available);
        obj.insert(QStringLiteral("inbound"), item.inbound);
        invArray.append(obj);
    }
    s->setValue(QStringLiteral("AmazonCache/inventory"), QString::fromUtf8(QJsonDocument(invArray).toJson(QJsonDocument::Compact)));

    QJsonObject salesObj;
    for (auto it = sales.begin(); it != sales.end(); ++it) {
        salesObj.insert(it.key(), it.value());
    }
    s->setValue(QStringLiteral("AmazonCache/sales"), QString::fromUtf8(QJsonDocument(salesObj).toJson(QJsonDocument::Compact)));
}

static bool loadAmazonCache(QList<AmazonInventoryApi::InventorySummary> *summariesOut, QHash<QString, int> *salesOut)
{
    auto s = WorkingDirectoryManager::instance()->settings();
    if (!s->contains(QStringLiteral("AmazonCache/timestamp"))) {
        return false;
    }

    qint64 cacheTime = s->value(QStringLiteral("AmazonCache/timestamp")).toLongLong();
    qint64 now = QDateTime::currentDateTimeUtc().toSecsSinceEpoch();
    if (now - cacheTime > 24 * 3600) { // 24 hours
        return false;
    }

    // Load inventory
    QString invStr = s->value(QStringLiteral("AmazonCache/inventory")).toString();
    QJsonParseError err;
    QJsonDocument invDoc = QJsonDocument::fromJson(invStr.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError) return false;

    summariesOut->clear();
    QJsonArray invArray = invDoc.array();
    for (const auto &val : invArray) {
        QJsonObject obj = val.toObject();
        AmazonInventoryApi::InventorySummary item;
        item.sku = obj.value(QStringLiteral("sku")).toString();
        item.asin = obj.value(QStringLiteral("asin")).toString();
        item.available = obj.value(QStringLiteral("available")).toInt();
        item.inbound = obj.value(QStringLiteral("inbound")).toInt();
        summariesOut->append(item);
    }

    // Load sales
    QString salesStr = s->value(QStringLiteral("AmazonCache/sales")).toString();
    QJsonDocument salesDoc = QJsonDocument::fromJson(salesStr.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError) return false;

    salesOut->clear();
    QJsonObject salesObj = salesDoc.object();
    for (const QString &key : salesObj.keys()) {
        salesOut->insert(key, salesObj.value(key).toInt());
    }

    return true;
}

PaneMarketplaces::PaneMarketplaces(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PaneMarketplaces)
{
    ui->setupUi(this);

    ui->tableViewProducts->horizontalHeader()->setStretchLastSection(true);
    ui->tableViewProducts->verticalHeader()->hide();

    m_ordersModel = new TableMarketplaceOrders(this);
    ui->tableViewOrders->setModel(m_ordersModel);
    ui->tableViewOrders->horizontalHeader()->setStretchLastSection(true);
    ui->tableViewOrders->verticalHeader()->hide();

    // Restore sync parameters from the working directory settings.
    {
        auto s = WorkingDirectoryManager::instance()->settings();
        ui->spinBoxMaxTarget->setValue(
            s->value(QStringLiteral("MarketplacesSync/maxTarget"), 0).toInt());
        ui->spinBoxPercentageToTargetMkt->setValue(
            s->value(QStringLiteral("MarketplacesSync/pctInventory"), 90).toInt());
        ui->spinBoxMinDays->setValue(
            s->value(QStringLiteral("MarketplacesSync/minDays"), 0).toInt());
    }
    auto onSyncParamChanged = [this]() {
        auto s = WorkingDirectoryManager::instance()->settings();
        s->setValue(QStringLiteral("MarketplacesSync/maxTarget"),
                    ui->spinBoxMaxTarget->value());
        s->setValue(QStringLiteral("MarketplacesSync/pctInventory"),
                    ui->spinBoxPercentageToTargetMkt->value());
        s->setValue(QStringLiteral("MarketplacesSync/minDays"),
                    ui->spinBoxMinDays->value());
        if (m_model)
            m_model->setSyncParams(ui->spinBoxPercentageToTargetMkt->value(),
                                   ui->spinBoxMaxTarget->value(),
                                   ui->spinBoxMinDays->value());
    };
    connect(ui->spinBoxMaxTarget, &QSpinBox::valueChanged, this, onSyncParamChanged);
    connect(ui->spinBoxPercentageToTargetMkt, &QSpinBox::valueChanged, this, onSyncParamChanged);
    connect(ui->spinBoxMinDays, &QSpinBox::valueChanged, this, onSyncParamChanged);

    connect(ui->buttonLoad, &QPushButton::clicked, this, [this]() {
        m_loadTask = _onLoad();
    });

    connect(ui->buttonSyncInventory, &QPushButton::clicked, this, [this]() {
        m_syncInventoryTask = _onSyncInventory();
    });

    connect(ui->buttonLoadOrders, &QPushButton::clicked, this, [this]() {
        m_loadOrdersTask = _onLoadOrders();
    });

    connect(ui->buttonSyncOrders, &QPushButton::clicked, this, [this]() {
        m_syncOrdersTask = _onSyncOrders();
    });

    ui->tableViewOrders->setSelectionBehavior(QAbstractItemView::SelectRows);
    connect(ui->buttonShipByAmz, &QPushButton::clicked, this, [this]() {
        m_shipByAmzTask = _onShipByAmazon();
    });

    connect(ui->buttonClearCache, &QPushButton::clicked, this, [this]() {
        auto s = WorkingDirectoryManager::instance()->settings();
        s->remove(QStringLiteral("AmazonCache/timestamp"));
        s->remove(QStringLiteral("AmazonCache/inventory"));
        s->remove(QStringLiteral("AmazonCache/sales"));
        QMessageBox::information(this, tr("Cache Cleared"), tr("Amazon data cache has been successfully cleared."));
    });

    // Right-click a product row: invalidate the Amazon cache for that SKU only,
    // so the next Load re-fetches it while the rest stays cached.
    ui->tableViewProducts->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->tableViewProducts, &QTableView::customContextMenuRequested, this,
            [this](const QPoint &pos) {
        if (!m_model) return;
        const QModelIndex idx = ui->tableViewProducts->indexAt(pos);
        if (!idx.isValid()) return;
        const QString sku = m_model->index(idx.row(), TableMarketplaceProducts::ColSku)
                                .data().toString();
        if (sku.isEmpty()) return;
        QMenu menu(this);
        QAction *invalidate = menu.addAction(tr("Invalidate Amazon cache for \"%1\"").arg(sku));
        if (menu.exec(ui->tableViewProducts->viewport()->mapToGlobal(pos)) == invalidate)
            _invalidateAmazonCacheForSku(sku);
    });
}

void PaneMarketplaces::_invalidateAmazonCacheForSku(const QString &sku)
{
    auto s = WorkingDirectoryManager::instance()->settings();

    QJsonArray inv = QJsonDocument::fromJson(
        s->value(QStringLiteral("AmazonCache/inventory")).toString().toUtf8()).array();
    QJsonArray newInv;
    for (const QJsonValue &v : inv)
        if (v.toObject().value(QStringLiteral("sku")).toString()
                .compare(sku, Qt::CaseInsensitive) != 0)
            newInv.append(v);
    s->setValue(QStringLiteral("AmazonCache/inventory"),
                QString::fromUtf8(QJsonDocument(newInv).toJson(QJsonDocument::Compact)));

    QJsonObject sales = QJsonDocument::fromJson(
        s->value(QStringLiteral("AmazonCache/sales")).toString().toUtf8()).object();
    const QStringList keys = sales.keys();
    for (const QString &key : keys)
        if (key.compare(sku, Qt::CaseInsensitive) == 0)
            sales.remove(key);
    s->setValue(QStringLiteral("AmazonCache/sales"),
                QString::fromUtf8(QJsonDocument(sales).toJson(QJsonDocument::Compact)));

    QMessageBox::information(this, tr("Cache invalidated"),
        tr("Amazon cache invalidated for \"%1\".\nClick Load to re-fetch it (other SKUs stay cached).")
            .arg(sku));
}

PaneMarketplaces::~PaneMarketplaces()
{
    delete ui;
}

void PaneMarketplaces::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_progressDlg)
        m_progressDlg->show();
}

void PaneMarketplaces::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    if (m_progressDlg)
        m_progressDlg->hide();
}

AmazonInventoryApi *PaneMarketplaces::_api()
{
    if (!m_api) {
        auto *st = SettingsTable::instance();
        m_api = new AmazonInventoryApi(
            st->value(SettingsTable::KEY_LWA_CLIENT_ID),
            st->value(SettingsTable::KEY_LWA_CLIENT_SECRET),
            st->value(SettingsTable::KEY_EU_LWA_REFRESH_TOKEN),
            st->value(SettingsTable::KEY_EU_SELLER_ID),
            k_marketplaceId,
            this);
    }
    return m_api;
}

QCoro::Task<void> PaneMarketplaces::_onLoad()
{
    // Rebuild model from current Temu store configuration.
    TemuStoreModel temuModel;
    QList<TableMarketplaceProducts::MarketplaceStore> stores;
    for (const TemuStore &ts : temuModel.stores()) {
        TableMarketplaceProducts::MarketplaceStore ms;
        ms.id    = QStringLiteral("temu_%1_%2").arg(ts.country, ts.label);
        ms.label = QStringLiteral("Temu %1 – %2").arg(ts.country, ts.label);
        stores.append(ms);
    }

    // --- Progress dialog ---
    auto *dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(tr("Loading marketplace data…"));
    dlg->resize(560, 400);

    auto *vLayout = new QVBoxLayout(dlg);

    auto *statusLabel = new QLabel(tr("Starting…"), dlg);
    QFont boldFont = statusLabel->font();
    boldFont.setBold(true);
    statusLabel->setFont(boldFont);
    vLayout->addWidget(statusLabel);

    auto *progressBar = new QProgressBar(dlg);
    progressBar->setRange(0, 0); // indeterminate spinner
    vLayout->addWidget(progressBar);

    auto *logEdit = new QTextEdit(dlg);
    logEdit->setReadOnly(true);
    logEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    vLayout->addWidget(logEdit);

    auto *hLayout  = new QHBoxLayout();
    auto *copyBtn  = new QPushButton(tr("Copy log"), dlg);
    hLayout->addWidget(copyBtn);
    hLayout->addStretch();
    auto *closeBtns = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
    QPushButton *closeBtn = closeBtns->button(QDialogButtonBox::Close);
    if (closeBtn) closeBtn->setEnabled(false);
    hLayout->addWidget(closeBtns);
    vLayout->addLayout(hLayout);

    QPointer<QLabel>       statusPtr(statusLabel);
    QPointer<QProgressBar> barPtr(progressBar);
    QPointer<QTextEdit>    logPtr(logEdit);
    QPointer<QPushButton>  closeBtnPtr(closeBtn);

    auto appendLog = [logPtr](const QString &msg) {
        if (!logPtr) return;
        const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
        logPtr->append(QStringLiteral("[%1] %2").arg(ts, msg));
    };
    auto setStatus = [statusPtr](const QString &msg) {
        if (statusPtr) statusPtr->setText(msg);
    };

    connect(copyBtn, &QPushButton::clicked, dlg, [logPtr]() {
        if (logPtr) QGuiApplication::clipboard()->setText(logPtr->toPlainText());
    });
    connect(closeBtns, &QDialogButtonBox::rejected, dlg, &QDialog::close);

    QPointer<QDialog> dlgPtr(dlg);
    m_progressDlg = dlg;
    dlg->show();
    setEnabled(false);

    // --- Discover SKUs from the Temu stores (rows = union of store SKUs;
    //     a SKU existing only on Amazon is not displayed) ---
    auto *st = SettingsTable::instance();
    const QString appKey    = st->value(SettingsTable::KEY_TEMU_APP_KEY);
    const QString appSecret = st->value(SettingsTable::KEY_TEMU_APP_SECRET);

    QHash<QString, QHash<QString,int>> storeQtyByStoreId; // storeId → (sku → qty)
    QMap<QString, QString> skuByLower;                     // lower → original casing (sorted)
    for (const TemuStore &ts : temuModel.stores()) {
        if (!dlgPtr) { setEnabled(true); co_return; }
        const QString storeId = QStringLiteral("temu_%1_%2").arg(ts.country, ts.label);
        setStatus(tr("Listing SKUs of Temu %1 – %2…").arg(ts.country, ts.label));
        appendLog(tr("→ Temu %1 – %2: listing store SKUs").arg(ts.country, ts.label));

        TemuInventoryApi temuApi(appKey, appSecret, ts.token,
                                 ts.proxyHost, ts.proxyPort, ts.proxyUser, ts.proxyPassword,
                                 this);
        QHash<QString, int> qtyBySku;
        co_await temuApi.fetchInventory({}, &qtyBySku); // empty filter = all SKUs
        if (!dlgPtr) { setEnabled(true); co_return; }

        if (!temuApi.lastError().isEmpty()) {
            appendLog(tr("  ✗ Failed to list SKUs: %1").arg(temuApi.lastError()));
            continue;
        }
        appendLog(tr("  ✓ %1 SKU(s) in store").arg(qtyBySku.size()));
        storeQtyByStoreId.insert(storeId, qtyBySku);
        for (auto it = qtyBySku.begin(); it != qtyBySku.end(); ++it)
            if (!skuByLower.contains(it.key().toLower()))
                skuByLower.insert(it.key().toLower(), it.key());
    }

    const QStringList allSkus = skuByLower.values();
    appendLog(tr("→ %1 unique SKU(s) across all target marketplaces").arg(allSkus.size()));

    delete m_model;
    m_model = new TableMarketplaceProducts(allSkus, stores, this);
    m_model->setSyncParams(ui->spinBoxPercentageToTargetMkt->value(),
                           ui->spinBoxMaxTarget->value(),
                           ui->spinBoxMinDays->value());
    ui->tableViewProducts->setModel(m_model);

    for (auto it = storeQtyByStoreId.begin(); it != storeQtyByStoreId.end(); ++it)
        m_model->applyStoreInventory(it.key(), it.value());

    if (allSkus.isEmpty()) {
        appendLog(tr("→ No SKU found in any target marketplace — nothing to load."));
        if (barPtr) { barPtr->setRange(0, 1); barPtr->setValue(1); }
        setStatus(tr("Done."));
        if (closeBtnPtr) closeBtnPtr->setEnabled(true);
        setEnabled(true);
        co_return;
    }

    // --- Amazon data: cache is PARTIAL — any SKU missing from the cache or
    //     whose sales retrieval failed (-1) is re-fetched. ---
    QList<AmazonInventoryApi::InventorySummary> summaries;
    QHash<QString, int> cachedSales;
    const bool hasCache = loadAmazonCache(&summaries, &cachedSales);

    // Which table SKUs have no cached inventory data?
    QSet<QString> cachedInvLower;
    for (const auto &s : summaries)
        cachedInvLower.insert(s.sku.toLower());
    QStringList missingInv;
    for (const QString &sku : allSkus)
        if (!cachedInvLower.contains(sku.toLower()))
            missingInv.append(sku);

    if (hasCache) {
        appendLog(tr("→ Loading Amazon data from cache (valid for 24h)"));
        m_model->applyInventory(summaries);
        appendLog(tr("  ✓ %1 SKU(s) found in cache").arg(summaries.size()));
        for (auto it = cachedSales.begin(); it != cachedSales.end(); ++it) {
            if (it.value() < 0)
                continue; // failed last time — will be re-fetched below
            m_model->applySales(it.key(), it.value());
        }
        if (!missingInv.isEmpty())
            appendLog(tr("  ⚠ %1 SKU(s) missing from cache — fetching from Amazon: %2")
                      .arg(missingInv.size()).arg(missingInv.join(QStringLiteral(", "))));
    }

    if (hasCache && !missingInv.isEmpty()) {
        // Partial refresh: fetch only the missing SKUs via the live FBA
        // Inventory API — fresher than the MYI report and not subject to the
        // report-generation quota (repeated reports return FATAL).
        setStatus(tr("Fetching %1 missing SKU(s) from the live FBA Inventory API…")
                  .arg(missingInv.size()));
        appendLog(tr("→ Live FBA Inventory API: fetching %1 missing SKU(s)").arg(missingInv.size()));

        QList<AmazonInventoryApi::InventorySummary> liveMissing;
        _api()->clearLastError();
        co_await _api()->fetchFbaInventory(missingInv, &liveMissing, appendLog);
        if (!dlgPtr) { setEnabled(true); co_return; }

        if (!_api()->lastError().isEmpty())
            appendLog(tr("  ✗ %1").arg(_api()->lastError()));
        appendLog(tr("  ✓ %1 SKU(s) retrieved").arg(liveMissing.size()));
        for (const auto &s : liveMissing) {
            appendLog(QStringLiteral("    %1 → %2 | avail %3")
                          .arg(s.sku, s.asin).arg(s.available));
            summaries.append(s);
        }
        m_model->applyInventory(liveMissing);
    } else if (!hasCache) {
        // Cold start: full MYI report (covers all SKUs in one shot).
        while (true) {
            if (!dlgPtr) { setEnabled(true); co_return; }
            summaries.clear();
            setStatus(tr("Requesting FBA inventory report…"));
            appendLog(tr("→ FBA inventory report: requesting for marketplace %1").arg(k_marketplaceId));
            appendLog(tr("  (report generation may take 1–2 minutes)"));

            co_await _api()->fetchFbaInventoryReport(allSkus, &summaries, appendLog);

            if (!dlgPtr) { setEnabled(true); co_return; }
            if (!summaries.isEmpty())
                break; // success

            const QString err = _api()->lastError();
            appendLog(err.isEmpty()
                      ? tr("  ✗ No inventory data in report")
                      : tr("  ✗ %1").arg(err));

            // Report failed (often the generation quota) — the live FBA
            // Inventory API returns the same numbers without that quota.
            appendLog(tr("→ Report failed — falling back to the live FBA Inventory API"));
            _api()->clearLastError();
            co_await _api()->fetchFbaInventory(allSkus, &summaries, appendLog);
            if (!dlgPtr) { setEnabled(true); co_return; }
            if (!summaries.isEmpty())
                break;

            const int answer = QMessageBox::question(
                this,
                tr("Retry?"),
                tr("Failed to retrieve FBA inventory (report and live API).\n\n%1\n\nRetry?")
                    .arg(_api()->lastError().isEmpty() ? err : _api()->lastError()),
                QMessageBox::Yes | QMessageBox::No);
            if (answer != QMessageBox::Yes)
                break;

            appendLog(tr("─── Retrying… ───"));
        }

        if (!dlgPtr) { setEnabled(true); co_return; }
        m_model->applyInventory(summaries);
        if (!summaries.isEmpty()) {
            appendLog(tr("  ✓ %1 SKU(s) retrieved").arg(summaries.size()));
            for (const auto &s : summaries)
                appendLog(QStringLiteral("    %1 → %2 | avail %3")
                              .arg(s.sku, s.asin).arg(s.available));
        }
    }

    // --- Sales 90d (one call per SKU × EU marketplace) — only for SKUs Amazon
    //     knows, and only those without a valid cached value. ---
    if (!summaries.isEmpty()) {
        QHash<QString, int> cachedSalesLower;
        for (auto it = cachedSales.begin(); it != cachedSales.end(); ++it)
            cachedSalesLower.insert(it.key().toLower(), it.value());

        QStringList salesToFetch;
        for (const auto &s : summaries)
            if (cachedSalesLower.value(s.sku.toLower(), -1) < 0)
                salesToFetch.append(s.sku);

        QHash<QString, int> salesToCache = cachedSales;
        for (int i = 0; i < salesToFetch.size(); ++i) {
            if (!dlgPtr) { setEnabled(true); co_return; }
            const QString sku = salesToFetch[i];
            setStatus(tr("Fetching sales 90d (%1/%2): %3").arg(i + 1).arg(salesToFetch.size()).arg(sku));
            appendLog(tr("→ Sales 90d: %1").arg(sku));

            int units = -1;
            co_await _api()->fetchSalesUnits(sku, 90, k_euMarketplaceIds, &units);
            if (!dlgPtr) { setEnabled(true); co_return; }
            m_model->applySales(sku, units);
            salesToCache.insert(sku, units); // -1 on failure → re-fetched next Load

            if (units < 0)
                appendLog(tr("  ✗ Failed to retrieve sales data"));
            else
                appendLog(tr("  ✓ %1 units sold in 90 days").arg(units));
        }

        if (!hasCache || !missingInv.isEmpty() || !salesToFetch.isEmpty())
            saveAmazonCache(summaries, salesToCache);
    } else {
        appendLog(tr("→ Skipping Amazon sales queries (no Amazon inventory data)"));
    }

    // --- Temu sales 90d per store (inventory already applied at discovery) ---
    for (const TemuStore &ts : temuModel.stores()) {
        if (!dlgPtr) { setEnabled(true); co_return; }
        const QString storeId = QStringLiteral("temu_%1_%2").arg(ts.country, ts.label);
        setStatus(tr("Fetching Temu sales %1 – %2…").arg(ts.country, ts.label));
        appendLog(tr("→ Temu %1 – %2: fetching sales").arg(ts.country, ts.label));

        TemuInventoryApi temuApi(appKey, appSecret, ts.token,
                                 ts.proxyHost, ts.proxyPort, ts.proxyUser, ts.proxyPassword,
                                 this);

        QHash<QString, int> salesBySku;
        co_await temuApi.fetchSales(allSkus, 90, &salesBySku);
        if (!dlgPtr) { setEnabled(true); co_return; }

        if (!temuApi.lastError().isEmpty()) {
            appendLog(tr("  ✗ Failed to retrieve Temu sales: %1").arg(temuApi.lastError()));
        } else {
            appendLog(tr("  ✓ Sales retrieved successfully."));
            m_model->applyStoreSales(storeId, salesBySku);
        }
    }

    // --- Done ---
    if (barPtr) { barPtr->setRange(0, 1); barPtr->setValue(1); }
    setStatus(tr("Done."));
    appendLog(tr("─── Load complete ───"));
    if (closeBtnPtr) closeBtnPtr->setEnabled(true);

    setEnabled(true);
}

QCoro::Task<void> PaneMarketplaces::_onSyncInventory()
{
    if (!m_model) {
        QMessageBox::information(this, tr("No data"),
            tr("Please load marketplace data first (click Load)."));
        co_return;
    }

    const int pct     = ui->spinBoxPercentageToTargetMkt->value();
    const int maxVal  = ui->spinBoxMaxTarget->value();
    const int minDays = ui->spinBoxMinDays->value();
    m_model->setSyncParams(pct, maxVal, minDays);

    // --- Progress dialog ---
    auto *dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(tr("Syncing inventory to target marketplaces…"));
    dlg->resize(560, 420);

    auto *vLayout = new QVBoxLayout(dlg);

    auto *statusLabel = new QLabel(tr("Computing targets…"), dlg);
    QFont boldFont = statusLabel->font();
    boldFont.setBold(true);
    statusLabel->setFont(boldFont);
    vLayout->addWidget(statusLabel);

    auto *progressBar = new QProgressBar(dlg);
    progressBar->setRange(0, 0);
    vLayout->addWidget(progressBar);

    auto *logEdit = new QTextEdit(dlg);
    logEdit->setReadOnly(true);
    logEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    vLayout->addWidget(logEdit);

    auto *hLayout  = new QHBoxLayout();
    auto *copyBtn  = new QPushButton(tr("Copy log"), dlg);
    hLayout->addWidget(copyBtn);
    hLayout->addStretch();
    auto *closeBtns = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
    QPushButton *closeBtn = closeBtns->button(QDialogButtonBox::Close);
    if (closeBtn) closeBtn->setEnabled(false);
    hLayout->addWidget(closeBtns);
    vLayout->addLayout(hLayout);

    QPointer<QLabel>       statusPtr(statusLabel);
    QPointer<QProgressBar> barPtr(progressBar);
    QPointer<QTextEdit>    logPtr(logEdit);
    QPointer<QPushButton>  closeBtnPtr(closeBtn);

    auto appendLog = [logPtr](const QString &msg) {
        if (!logPtr) return;
        const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
        logPtr->append(QStringLiteral("[%1] %2").arg(ts, msg));
    };
    auto setStatus = [statusPtr](const QString &msg) {
        if (statusPtr) statusPtr->setText(msg);
    };

    connect(copyBtn, &QPushButton::clicked, dlg, [logPtr]() {
        if (logPtr) QGuiApplication::clipboard()->setText(logPtr->toPlainText());
    });
    connect(closeBtns, &QDialogButtonBox::rejected, dlg, &QDialog::close);

    QPointer<QDialog> dlgPtr(dlg);
    dlg->show();
    setEnabled(false);

    // --- Compute target qty per SKU from Amazon FBA data ---
    // The computation lives in TableMarketplaceProducts::targetQtyForSku so the
    // "Sync Qty" table columns always show exactly what would be uploaded.
    appendLog(tr("→ Computing target inventory"));
    appendLog(tr("  % of Amazon qty: %1% — Max: %2 — Min days: %3")
              .arg(pct)
              .arg(maxVal > 0 ? QString::number(maxVal) : tr("∞ (no limit)"))
              .arg(minDays > 0 ? QString::number(minDays) : tr("off")));

    QHash<QString, int> targetQtyBySku;
    const QStringList modelSkus = m_model->skus();
    for (const QString &sku : modelSkus) {
        const int amazonQty = m_model->amazonQtyForSku(sku);
        if (amazonQty < 0) {
            appendLog(tr("  ⚠ %1: Amazon qty not loaded, skipping").arg(sku));
            continue;
        }
        const int estDays = m_model->estDaysForSku(sku);
        const int target  = m_model->targetQtyForSku(sku);

        QString detail;
        if (minDays > 0 && estDays >= 0)
            detail = tr(" (est. %1 days, counting beyond %2)").arg(estDays).arg(minDays);
        appendLog(tr("  %1: Amazon %2%3 × %4% → %5%6")
                  .arg(sku).arg(amazonQty).arg(detail).arg(pct).arg(target)
                  .arg(maxVal > 0 && target == maxVal
                       ? QStringLiteral(" (capped at max %1)").arg(maxVal) : QString()));
        targetQtyBySku.insert(sku, target);
    }
    appendLog(tr("  → %1 SKU(s) with known Amazon qty").arg(targetQtyBySku.size()));

    if (targetQtyBySku.isEmpty()) {
        appendLog(tr("  Nothing to sync — load inventory first."));
        if (barPtr) { barPtr->setRange(0, 1); barPtr->setValue(1); }
        setStatus(tr("Nothing to sync."));
        if (closeBtnPtr) closeBtnPtr->setEnabled(true);
        setEnabled(true);
        co_return;
    }

    // --- Push to each configured Temu store ---
    auto *st = SettingsTable::instance();
    const QString appKey    = st->value(SettingsTable::KEY_TEMU_APP_KEY);
    const QString appSecret = st->value(SettingsTable::KEY_TEMU_APP_SECRET);

    TemuStoreModel temuModel;
    if (temuModel.stores().isEmpty())
        appendLog(tr("→ No Temu stores configured — add stores in Settings"));

    for (const TemuStore &ts : temuModel.stores()) {
        if (!dlgPtr) { setEnabled(true); co_return; }
        setStatus(tr("Syncing Temu %1 – %2…").arg(ts.country, ts.label));
        appendLog(tr("─── Temu %1 – %2 ───").arg(ts.country, ts.label));

        TemuInventoryApi temuApi(appKey, appSecret, ts.token,
                                 ts.proxyHost, ts.proxyPort, ts.proxyUser, ts.proxyPassword,
                                 this);

        co_await temuApi.updateInventory(targetQtyBySku, appendLog);
        if (!dlgPtr) { setEnabled(true); co_return; }

        if (!temuApi.lastError().isEmpty())
            appendLog(tr("  ✗ Store-level error: %1").arg(temuApi.lastError()));
    }

    // --- Done ---
    if (barPtr) { barPtr->setRange(0, 1); barPtr->setValue(1); }
    setStatus(tr("Done."));
    appendLog(tr("─── Sync complete ───"));
    if (closeBtnPtr) closeBtnPtr->setEnabled(true);
    setEnabled(true);
}

QCoro::Task<void> PaneMarketplaces::_onLoadOrders()
{
    // Disable UI
    setEnabled(false);

    // --- Progress dialog ---
    auto *dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(tr("Loading marketplace orders…"));
    dlg->resize(560, 400);

    auto *vLayout = new QVBoxLayout(dlg);

    auto *statusLabel = new QLabel(tr("Starting…"), dlg);
    QFont boldFont = statusLabel->font();
    boldFont.setBold(true);
    statusLabel->setFont(boldFont);
    vLayout->addWidget(statusLabel);

    auto *progressBar = new QProgressBar(dlg);
    progressBar->setRange(0, 0); // indeterminate spinner
    vLayout->addWidget(progressBar);

    auto *logEdit = new QTextEdit(dlg);
    logEdit->setReadOnly(true);
    logEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    vLayout->addWidget(logEdit);

    auto *hLayout  = new QHBoxLayout();
    auto *copyBtn  = new QPushButton(tr("Copy log"), dlg);
    hLayout->addWidget(copyBtn);
    hLayout->addStretch();
    auto *closeBtns = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
    QPushButton *closeBtn = closeBtns->button(QDialogButtonBox::Close);
    if (closeBtn) closeBtn->setEnabled(false);
    hLayout->addWidget(closeBtns);
    vLayout->addLayout(hLayout);

    QPointer<QLabel>       statusPtr(statusLabel);
    QPointer<QProgressBar> barPtr(progressBar);
    QPointer<QTextEdit>    logPtr(logEdit);
    QPointer<QPushButton>  closeBtnPtr(closeBtn);

    auto appendLog = [logPtr](const QString &msg) {
        if (!logPtr) return;
        const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
        logPtr->append(QStringLiteral("[%1] %2").arg(ts, msg));
    };
    auto setStatus = [statusPtr](const QString &msg) {
        if (statusPtr) statusPtr->setText(msg);
    };

    connect(copyBtn, &QPushButton::clicked, dlg, [logPtr]() {
        if (logPtr) QGuiApplication::clipboard()->setText(logPtr->toPlainText());
    });
    connect(closeBtns, &QDialogButtonBox::rejected, dlg, &QDialog::close);

    QPointer<QDialog> dlgPtr(dlg);
    dlg->show();

    QList<TableMarketplaceOrders::OrderRow> orderRows;

    // 1. Fetch unshipped orders from Temu stores
    auto *st = SettingsTable::instance();
    const QString appKey    = st->value(SettingsTable::KEY_TEMU_APP_KEY);
    const QString appSecret = st->value(SettingsTable::KEY_TEMU_APP_SECRET);

    TemuStoreModel temuModel;
    QList<TemuInventoryApi::TemuOrder> allTemuOrders;
    QHash<QString, TemuStore> orderToStoreMap; // parentOrderSn -> TemuStore

    for (const TemuStore &ts : temuModel.stores()) {
        if (!dlgPtr) { setEnabled(true); co_return; }
        setStatus(tr("Fetching Temu %1 – %2 orders…").arg(ts.country, ts.label));
        appendLog(tr("→ Temu %1 – %2: requesting unshipped orders").arg(ts.country, ts.label));

        TemuInventoryApi temuApi(appKey, appSecret, ts.token,
                                 ts.proxyHost, ts.proxyPort, ts.proxyUser, ts.proxyPassword,
                                 this);

        QList<TemuInventoryApi::TemuOrder> storeOrders = co_await temuApi.fetchUnshippedOrders();
        if (!dlgPtr) { setEnabled(true); co_return; }

        if (!temuApi.lastError().isEmpty()) {
            appendLog(tr("  ✗ Failed to retrieve Temu orders: %1").arg(temuApi.lastError()));
        } else {
            appendLog(tr("  ✓ Retrieved %1 unshipped order(s)").arg(storeOrders.size()));
            for (const auto &o : storeOrders) {
                allTemuOrders.append(o);
                orderToStoreMap.insert(o.parentOrderSn, ts);
                orderToStoreMap.insert(o.orderSn, ts);
            }
        }
    }

    // 2. For each Temu order, fetch the corresponding Amazon MCF order directly.
    // Amazon sellerFulfillmentOrderId convention: "temu-" + parentOrderSn.
    // We bypass fetchFulfillmentOrders (the list endpoint requires the
    // "Multi-Channel Fulfillment" SP-API role which is often missing from EU tokens).
    setStatus(tr("Fetching Amazon MCF tracking details…"));
    bool mcfRoleErrorLogged = false;
    for (const auto &tOrder : allTemuOrders) {
        if (!dlgPtr) { setEnabled(true); co_return; }

        TableMarketplaceOrders::OrderRow row;
        row.targetStore = orderToStoreMap.value(tOrder.parentOrderSn).label;
        if (row.targetStore.isEmpty()) row.targetStore = orderToStoreMap.value(tOrder.orderSn).label;
        row.parentOrderSn = tOrder.parentOrderSn;
        row.orderSn = tOrder.orderSn;
        row.targetOrderId = tOrder.orderSn;
        row.sku = tOrder.sku;
        row.goodsId = tOrder.goodsId;
        row.skuId = tOrder.skuId;
        row.quantity = tOrder.quantity;

        TemuStore ts = orderToStoreMap.value(tOrder.parentOrderSn);
        if (ts.label.isEmpty()) ts = orderToStoreMap.value(tOrder.orderSn);
        row.temuStoreToken = ts.token;
        row.temuStoreCountry = ts.country;
        row.temuProxyHost = ts.proxyHost;
        row.temuProxyPort = ts.proxyPort;
        row.temuProxyUser = ts.proxyUser;
        row.temuProxyPass = ts.proxyPassword;

        const QString fulfillmentId = QStringLiteral("temu-") + tOrder.parentOrderSn;
        appendLog(tr("→ Temu order %1 — fetching Amazon MCF order %2").arg(tOrder.orderSn, fulfillmentId));

        _api()->clearLastError();
        QJsonObject detailedOrder = co_await _api()->getFulfillmentOrder(fulfillmentId);
        if (!dlgPtr) { setEnabled(true); co_return; }

        if (!_api()->lastError().isEmpty()) {
            const QString err = _api()->lastError();
            const bool is403 = err.contains(QLatin1String("403"));
            if (!is403) {
                appendLog(tr("  ✗ %1").arg(err));
            } else if (!mcfRoleErrorLogged) {
                appendLog(tr("  ✗ %1").arg(err));
                mcfRoleErrorLogged = true;
            } else {
                appendLog(tr("  ✗ MCF 403 — see message above"));
            }
            row.source = tr("None");
            row.sourceOrderId = QString();
            row.trackingNumber = QString();
            orderRows.append(row);
            continue;
        }

        QString trackingNumber;
        QString carrierCode;
        QJsonArray shipments = detailedOrder.value(QStringLiteral("fulfillmentShipments")).toArray();
        for (const QJsonValue &shipVal : shipments) {
            QJsonObject shipObj = shipVal.toObject();
            QJsonArray packages = shipObj.value(QStringLiteral("fulfillmentShipmentPackage")).toArray();
            if (packages.isEmpty())
                packages = shipObj.value(QStringLiteral("fulfillmentShipmentPackages")).toArray();
            for (const QJsonValue &pkgVal : packages) {
                QJsonObject pkgObj = pkgVal.toObject();
                trackingNumber = pkgObj.value(QStringLiteral("trackingNumber")).toString();
                carrierCode = pkgObj.value(QStringLiteral("carrierCode")).toString();
                if (!trackingNumber.isEmpty()) break;
            }
            if (!trackingNumber.isEmpty()) break;
        }

        row.source = QStringLiteral("Amazon (%1)").arg(carrierCode.isEmpty() ? QStringLiteral("FBA") : carrierCode);
        row.sourceOrderId = fulfillmentId;
        row.trackingNumber = trackingNumber;

        if (trackingNumber.isEmpty()) {
            appendLog(tr("  ⚠ No tracking number yet for %1.").arg(fulfillmentId));
        } else {
            appendLog(tr("  ✓ Tracking number: %1").arg(trackingNumber));
        }

        orderRows.append(row);
    }

    m_ordersModel->setOrders(orderRows);

    if (barPtr) { barPtr->setRange(0, 1); barPtr->setValue(1); }
    setStatus(tr("Done."));
    appendLog(tr("─── Order load complete ───"));
    if (closeBtnPtr) closeBtnPtr->setEnabled(true);

    setEnabled(true);
}

QCoro::Task<void> PaneMarketplaces::_onSyncOrders()
{
    setEnabled(false);

    auto *dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(tr("Syncing tracking numbers…"));
    dlg->resize(560, 400);

    auto *vLayout = new QVBoxLayout(dlg);
    auto *statusLabel = new QLabel(tr("Starting…"), dlg);
    vLayout->addWidget(statusLabel);

    auto *progressBar = new QProgressBar(dlg);
    progressBar->setRange(0, 0);
    vLayout->addWidget(progressBar);

    auto *logEdit = new QTextEdit(dlg);
    logEdit->setReadOnly(true);
    logEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    vLayout->addWidget(logEdit);

    auto *hLayout  = new QHBoxLayout();
    auto *copyBtn  = new QPushButton(tr("Copy log"), dlg);
    hLayout->addWidget(copyBtn);
    hLayout->addStretch();
    auto *closeBtns = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
    QPushButton *closeBtn = closeBtns->button(QDialogButtonBox::Close);
    if (closeBtn) closeBtn->setEnabled(false);
    hLayout->addWidget(closeBtns);
    vLayout->addLayout(hLayout);

    QPointer<QLabel>       statusPtr(statusLabel);
    QPointer<QProgressBar> barPtr(progressBar);
    QPointer<QTextEdit>    logPtr(logEdit);
    QPointer<QPushButton>  closeBtnPtr(closeBtn);

    auto appendLog = [logPtr](const QString &msg) {
        if (!logPtr) return;
        const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
        logPtr->append(QStringLiteral("[%1] %2").arg(ts, msg));
    };

    connect(copyBtn, &QPushButton::clicked, dlg, [logPtr]() {
        if (logPtr) QGuiApplication::clipboard()->setText(logPtr->toPlainText());
    });
    connect(closeBtns, &QDialogButtonBox::rejected, dlg, &QDialog::close);

    QPointer<QDialog> dlgPtr(dlg);
    dlg->show();

    auto *st = SettingsTable::instance();
    const QString appKey    = st->value(SettingsTable::KEY_TEMU_APP_KEY);
    const QString appSecret = st->value(SettingsTable::KEY_TEMU_APP_SECRET);

    auto orders = m_ordersModel->orders();
    int successCount = 0;

    for (int i = 0; i < orders.size(); ++i) {
        if (!dlgPtr) { setEnabled(true); co_return; }
        const auto &order = orders[i];

        if (order.trackingNumber.isEmpty()) {
            appendLog(tr("Skipping order %1: No tracking number found.").arg(order.orderSn));
            continue;
        }

        if (order.temuStoreToken.isEmpty()) {
            appendLog(tr("Skipping order %1: No store credentials.").arg(order.orderSn));
            continue;
        }

        statusLabel->setText(tr("Syncing order %1 (%2/%3)…").arg(order.orderSn).arg(i + 1).arg(orders.size()));
        appendLog(tr("→ Shipping order %1 on Temu with tracking %2…").arg(order.orderSn, order.trackingNumber));

        TemuInventoryApi temuApi(appKey, appSecret, order.temuStoreToken,
                                 order.temuProxyHost, order.temuProxyPort, order.temuProxyUser, order.temuProxyPass,
                                 this);

        // Extract carrier Name from ColSource (e.g. "Amazon (CarrierCode)")
        QString carrier = order.source;
        if (carrier.startsWith(QStringLiteral("Amazon (")) && carrier.endsWith(QStringLiteral(")"))) {
            carrier = carrier.mid(8, carrier.length() - 9);
        } else {
            carrier = QStringLiteral("Amazon");
        }

        bool ok = co_await temuApi.shipOrder(order.parentOrderSn, order.orderSn,
                                             order.goodsId, order.skuId, order.quantity,
                                             order.trackingNumber, carrier, order.temuStoreCountry,
                                             [appendLog](const QString &msg) { appendLog(QStringLiteral("  ") + msg); });
        if (!dlgPtr) { setEnabled(true); co_return; }

        if (ok) {
            appendLog(tr("  ✓ Successfully shipped order %1 on Temu.").arg(order.orderSn));
            successCount++;
        } else {
            appendLog(tr("  ✗ Failed to ship order %1 on Temu: %2").arg(order.orderSn, temuApi.lastError()));
        }
    }

    if (barPtr) { barPtr->setRange(0, 1); barPtr->setValue(1); }
    statusLabel->setText(tr("Done."));
    appendLog(tr("─── Sync complete. %1 orders successfully synced. ───").arg(successCount));
    if (closeBtnPtr) closeBtnPtr->setEnabled(true);

    setEnabled(true);
}

QCoro::Task<void> PaneMarketplaces::_onShipByAmazon()
{
    QModelIndexList selected;
    if (ui->tableViewOrders->selectionModel())
        selected = ui->tableViewOrders->selectionModel()->selectedRows();
    if (selected.isEmpty()) {
        QMessageBox::information(this, tr("No selection"),
            tr("Select the order(s) to ship in the orders table first (Load orders, then click a row)."));
        co_return;
    }

    auto *st = SettingsTable::instance();
    const QString appKey    = st->value(SettingsTable::KEY_TEMU_APP_KEY);
    const QString appSecret = st->value(SettingsTable::KEY_TEMU_APP_SECRET);

    const auto orders = m_ordersModel->orders();

    // --- Phase 1: build one candidate per selected order (address + payload) ---
    struct Candidate {
        TableMarketplaceOrders::OrderRow order;
        QString fulfillmentId;
        QJsonObject dest;
        QJsonObject payload;
    };
    QList<Candidate> candidates;

    for (const QModelIndex &idx : selected) {
        if (idx.row() < 0 || idx.row() >= orders.size())
            continue;
        const auto &order = orders.at(idx.row());
        const QString fulfillmentId = QStringLiteral("temu-") + order.parentOrderSn;

        if (order.sku.isEmpty()) {
            QMessageBox::warning(this, tr("Missing SKU"),
                tr("Order %1 has no SKU — reload orders first.").arg(order.orderSn));
            continue;
        }

        TemuInventoryApi temuApi(appKey, appSecret, order.temuStoreToken,
                                 order.temuProxyHost, order.temuProxyPort,
                                 order.temuProxyUser, order.temuProxyPass,
                                 this);
        QJsonObject addr;
        co_await temuApi.fetchOrderAddress(order.parentOrderSn, &addr);
        if (!temuApi.lastError().isEmpty() || addr.isEmpty()) {
            QMessageBox::warning(this, tr("Address error"),
                tr("Could not fetch the shipping address of order %1:\n%2")
                    .arg(order.orderSn, temuApi.lastError()));
            continue;
        }

        QJsonObject dest;
        dest.insert(QStringLiteral("name"), addr.value(QStringLiteral("receiptName")).toString());
        dest.insert(QStringLiteral("addressLine1"), addr.value(QStringLiteral("addressLine1")).toString());
        const QString line2 = addr.value(QStringLiteral("addressLine2")).toString();
        if (!line2.isEmpty())
            dest.insert(QStringLiteral("addressLine2"), line2);
        dest.insert(QStringLiteral("city"), addr.value(QStringLiteral("regionName3")).toString());
        dest.insert(QStringLiteral("stateOrRegion"), addr.value(QStringLiteral("regionName2")).toString());
        dest.insert(QStringLiteral("postalCode"), addr.value(QStringLiteral("postCode")).toString());
        dest.insert(QStringLiteral("countryCode"),
                    order.temuStoreCountry.isEmpty() ? QStringLiteral("FR") : order.temuStoreCountry);
        const QString phone = addr.value(QStringLiteral("mobile")).toString();
        if (!phone.isEmpty())
            dest.insert(QStringLiteral("phone"), phone);

        QJsonObject item;
        item.insert(QStringLiteral("sellerSku"), order.sku);
        item.insert(QStringLiteral("sellerFulfillmentOrderItemId"), order.orderSn);
        item.insert(QStringLiteral("quantity"), order.quantity);
        QJsonArray items;
        items.append(item);

        QJsonObject payload;
        payload.insert(QStringLiteral("sellerFulfillmentOrderId"), fulfillmentId);
        payload.insert(QStringLiteral("displayableOrderId"), fulfillmentId);
        payload.insert(QStringLiteral("displayableOrderDate"),
                       QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        payload.insert(QStringLiteral("displayableOrderComment"),
                       QStringLiteral("Temu order %1").arg(order.parentOrderSn));
        payload.insert(QStringLiteral("shippingSpeedCategory"), QStringLiteral("Standard"));
        payload.insert(QStringLiteral("marketplaceId"), k_marketplaceId);
        payload.insert(QStringLiteral("destinationAddress"), dest);
        payload.insert(QStringLiteral("items"), items);

        candidates.append(Candidate{order, fulfillmentId, dest, payload});
    }

    if (candidates.isEmpty())
        co_return;

    // --- Phase 2: one confirmation dialog, one checkable line per order ---
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Ship by Amazon — confirmation"));
    dlg.resize(940, 520);
    auto *vLayout = new QVBoxLayout(&dlg);
    vLayout->addWidget(new QLabel(
        tr("These Amazon MCF orders will be created. Uncheck a line to skip it."), &dlg));

    auto *table = new QTableWidget(candidates.size(), 6, &dlg);
    table->setHorizontalHeaderLabels({tr("New order ID"), tr("SKU"), tr("Qty"),
                                      tr("Recipient"), tr("Address"), tr("Note")});
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->hide();
    for (int i = 0; i < candidates.size(); ++i) {
        const Candidate &c = candidates.at(i);
        const bool hasTracking = !c.order.trackingNumber.isEmpty();

        auto *idItem = new QTableWidgetItem(c.fulfillmentId);
        idItem->setFlags(idItem->flags() | Qt::ItemIsUserCheckable);
        // Orders that already have a tracking number probably already exist on
        // Amazon — leave them unchecked by default.
        idItem->setCheckState(hasTracking ? Qt::Unchecked : Qt::Checked);
        table->setItem(i, 0, idItem);
        table->setItem(i, 1, new QTableWidgetItem(c.order.sku));
        table->setItem(i, 2, new QTableWidgetItem(QString::number(c.order.quantity)));
        table->setItem(i, 3, new QTableWidgetItem(c.dest.value(QStringLiteral("name")).toString()));
        table->setItem(i, 4, new QTableWidgetItem(QStringLiteral("%1, %2 %3 (%4)")
            .arg(c.dest.value(QStringLiteral("addressLine1")).toString(),
                 c.dest.value(QStringLiteral("postalCode")).toString(),
                 c.dest.value(QStringLiteral("city")).toString(),
                 c.dest.value(QStringLiteral("countryCode")).toString())));
        table->setItem(i, 5, new QTableWidgetItem(hasTracking
            ? tr("⚠ already has tracking %1").arg(c.order.trackingNumber)
            : QString()));
    }
    table->resizeColumnsToContents();
    table->horizontalHeader()->setStretchLastSection(true);
    vLayout->addWidget(table, 1);

    auto *payloadView = new QTextEdit(&dlg);
    payloadView->setReadOnly(true);
    payloadView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    payloadView->setPlaceholderText(tr("Click a line to preview the exact payload sent to Amazon."));
    payloadView->setMaximumHeight(150);
    vLayout->addWidget(payloadView);
    connect(table, &QTableWidget::currentCellChanged, &dlg,
            [table, payloadView, &candidates](int row, int, int, int) {
        if (row >= 0 && row < candidates.size())
            payloadView->setPlainText(QString::fromUtf8(
                QJsonDocument(candidates.at(row).payload).toJson(QJsonDocument::Indented)));
    });

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Yes | QDialogButtonBox::No, &dlg);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept); // Yes
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject); // No
    vLayout->addWidget(btns);

    if (dlg.exec() != QDialog::Accepted)
        co_return;

    // --- Phase 3: create the checked orders ---
    QStringList results;
    for (int i = 0; i < candidates.size(); ++i) {
        if (table->item(i, 0)->checkState() != Qt::Checked)
            continue;
        const Candidate &c = candidates.at(i);
        const bool ok = co_await _api()->createFulfillmentOrder(c.payload);
        results.append(ok ? tr("✓ %1 created").arg(c.fulfillmentId)
                          : tr("✗ %1 failed: %2").arg(c.fulfillmentId, _api()->lastError()));
    }

    if (results.isEmpty()) {
        QMessageBox::information(this, tr("Nothing sent"),
            tr("All lines were unchecked — no order was created."));
        co_return;
    }
    QMessageBox::information(this, tr("Ship by Amazon — results"),
        results.join(QStringLiteral("\n"))
        + tr("\n\nUse \"Load\" then \"Sync orders\" once Amazon generates the tracking numbers."));
    co_return;
}
