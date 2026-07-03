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
#include <QMessageBox>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QShowEvent>
#include <QTextEdit>
#include <QVBoxLayout>

#include "TemuStoreModel.h"
#include "../../common/workingdirectory/WorkingDirectoryManager.h"
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

// Hardcoded SKU list for the first version.
static const QStringList k_skus = {
    "fr-747150648583-1",
    "A5-BOOK-COVER-DESIGN-60-DAULPHIN",
    "086-ICE-SOCKS",
    "A5-BOOK-COVER-DESIGN-33-BLUE",
    "A5-BOOK-COVER-DESIGN-44-UNICORN",
    "A5-BOOK-COVER-DESIGN-58-HEART",
    "CJYD196842511KP",
    "CJYD196842512LO",
    "CJNS2563870-ROSEGOLD-7CM-WIDE-44",
};

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

    connect(ui->buttonClearCache, &QPushButton::clicked, this, [this]() {
        auto s = WorkingDirectoryManager::instance()->settings();
        s->remove(QStringLiteral("AmazonCache/timestamp"));
        s->remove(QStringLiteral("AmazonCache/inventory"));
        s->remove(QStringLiteral("AmazonCache/sales"));
        QMessageBox::information(this, tr("Cache Cleared"), tr("Amazon data cache has been successfully cleared."));
    });
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
    {
        TemuStoreModel temuModel;
        QList<TableMarketplaceProducts::MarketplaceStore> stores;
        for (const TemuStore &ts : temuModel.stores()) {
            TableMarketplaceProducts::MarketplaceStore ms;
            ms.id    = QStringLiteral("temu_%1_%2").arg(ts.country, ts.label);
            ms.label = QStringLiteral("Temu %1 – %2").arg(ts.country, ts.label);
            stores.append(ms);
        }
        delete m_model;
        m_model = new TableMarketplaceProducts(k_skus, stores, this);
        ui->tableViewProducts->setModel(m_model);
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

    // --- Check Amazon cache ---
    QList<AmazonInventoryApi::InventorySummary> summaries;
    QHash<QString, int> cachedSales;
    bool hasCache = loadAmazonCache(&summaries, &cachedSales);

    if (hasCache) {
        appendLog(tr("→ Loading Amazon data from cache (valid for 24h)"));
        m_model->applyInventory(summaries);
        if (!summaries.isEmpty()) {
            appendLog(tr("  ✓ %1 SKU(s) found in cache").arg(summaries.size()));
            for (const auto &s : summaries)
                appendLog(QStringLiteral("    %1 → %2 | avail %3")
                              .arg(s.sku, s.asin).arg(s.available));
        }
        for (auto it = cachedSales.begin(); it != cachedSales.end(); ++it) {
            m_model->applySales(it.key(), it.value());
            if (it.value() < 0)
                appendLog(tr("    %1: failed to retrieve sales data").arg(it.key()));
            else
                appendLog(tr("    %1: %2 units sold in 90 days").arg(it.key()).arg(it.value()));
        }
    } else {
        // --- FBA inventory via Reports API, with user-facing retry on failure ---
        while (true) {
            if (!dlgPtr) { setEnabled(true); co_return; }
            summaries.clear();
            setStatus(tr("Requesting FBA inventory report…"));
            appendLog(tr("→ FBA inventory report: requesting for marketplace %1").arg(k_marketplaceId));
            appendLog(tr("  (report generation may take 1–2 minutes)"));

            co_await _api()->fetchFbaInventoryReport(k_skus, &summaries, appendLog);

            if (!dlgPtr) { setEnabled(true); co_return; }
            if (!summaries.isEmpty())
                break; // success

            const QString err = _api()->lastError();
            appendLog(err.isEmpty()
                      ? tr("  ✗ No inventory data in report")
                      : tr("  ✗ %1").arg(err));

            const int answer = QMessageBox::question(
                this,
                tr("Retry?"),
                tr("Failed to retrieve FBA inventory report.\n\n%1\n\nRetry?").arg(err),
                QMessageBox::Yes | QMessageBox::No);
            if (answer != QMessageBox::Yes)
                break;

            appendLog(tr("─── Retrying… ───"));
        }

        if (!dlgPtr) { setEnabled(true); co_return; }
        m_model->applyInventory(summaries);
        if (!summaries.isEmpty()) {
            appendLog(tr("  ✓ %1 SKU(s) found in report").arg(summaries.size()));
            for (const auto &s : summaries)
                appendLog(QStringLiteral("    %1 → %2 | avail %3")
                              .arg(s.sku, s.asin).arg(s.available));
        }

        // --- Sales 90d (one call per SKU × EU marketplace) ---
        QHash<QString, int> salesToCache;
        if (!summaries.isEmpty()) {
            for (int i = 0; i < k_skus.size(); ++i) {
                if (!dlgPtr) { setEnabled(true); co_return; }
                const QString sku = k_skus[i];
                setStatus(tr("Fetching sales 90d (%1/%2): %3").arg(i + 1).arg(k_skus.size()).arg(sku));
                appendLog(tr("→ Sales 90d: %1").arg(sku));

                int units = -1;
                co_await _api()->fetchSalesUnits(sku, 90, k_euMarketplaceIds, &units);
                if (!dlgPtr) { setEnabled(true); co_return; }
                m_model->applySales(sku, units);
                salesToCache.insert(sku, units);

                if (units < 0)
                    appendLog(tr("  ✗ Failed to retrieve sales data"));
                else
                    appendLog(tr("  ✓ %1 units sold in 90 days").arg(units));
            }

            // Save retrieved data to cache
            saveAmazonCache(summaries, salesToCache);
        } else {
            appendLog(tr("→ Skipping Amazon sales queries (FBA inventory report was empty or failed)"));
        }
    }

    // --- Temu stores ---
    {
        if (!dlgPtr) { setEnabled(true); co_return; }
        auto *st = SettingsTable::instance();
        const QString appKey    = st->value(SettingsTable::KEY_TEMU_APP_KEY);
        const QString appSecret = st->value(SettingsTable::KEY_TEMU_APP_SECRET);

        TemuStoreModel temuModel;
        for (const TemuStore &ts : temuModel.stores()) {
            if (!dlgPtr) { setEnabled(true); co_return; }
            const QString storeId = QStringLiteral("temu_%1_%2").arg(ts.country, ts.label);
            setStatus(tr("Fetching Temu %1 – %2…").arg(ts.country, ts.label));
            appendLog(tr("→ Temu %1 – %2: fetching inventory and sales").arg(ts.country, ts.label));

            TemuInventoryApi temuApi(appKey, appSecret, ts.token,
                                     ts.proxyHost, ts.proxyPort, ts.proxyUser, ts.proxyPassword,
                                     this);

            QHash<QString, int> qtyBySku;
            co_await temuApi.fetchInventory(k_skus, &qtyBySku);
            if (!dlgPtr) { setEnabled(true); co_return; }

            QHash<QString, int> salesBySku;
            co_await temuApi.fetchSales(k_skus, 90, &salesBySku);
            if (!dlgPtr) { setEnabled(true); co_return; }

            if (!temuApi.lastError().isEmpty()) {
                appendLog(tr("  ✗ Failed to retrieve Temu data: %1").arg(temuApi.lastError()));
            } else {
                appendLog(tr("  ✓ Inventory and sales retrieved successfully."));
                m_model->applyStoreInventory(storeId, qtyBySku);
                m_model->applyStoreSales(storeId, salesBySku);
            }
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

    const int pct    = ui->spinBoxPercentageToTargetMkt->value();
    const int maxVal = ui->spinBoxMaxTarget->value();

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
    appendLog(tr("→ Computing target inventory"));
    appendLog(tr("  % of Amazon qty: %1% — Max: %2")
              .arg(pct)
              .arg(maxVal > 0 ? QString::number(maxVal) : tr("∞ (no limit)")));

    QHash<QString, int> targetQtyBySku;
    for (const QString &sku : k_skus) {
        const int amazonQty = m_model->amazonQtyForSku(sku);
        if (amazonQty < 0) {
            appendLog(tr("  ⚠ %1: Amazon qty not loaded, skipping").arg(sku));
            continue;
        }
        int target = static_cast<int>(amazonQty * pct / 100.0);
        if (maxVal > 0 && target > maxVal)
            target = maxVal;
        appendLog(tr("  %1: Amazon %2 × %3% = %4%5")
                  .arg(sku).arg(amazonQty).arg(pct).arg(target)
                  .arg((maxVal > 0 && target == maxVal && static_cast<int>(amazonQty * pct / 100.0) > maxVal)
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

    // 2. Fetch Amazon outbound fulfillment orders
    QJsonArray amazonOrders;
    if (!allTemuOrders.isEmpty()) {
        if (!dlgPtr) { setEnabled(true); co_return; }
        setStatus(tr("Fetching Amazon outbound orders…"));
        appendLog(tr("→ Requesting Amazon MCF orders from last 30 days…"));

        // Query starting from 30 days ago
        QDateTime queryStart = QDateTime::currentDateTimeUtc().addDays(-30);
        amazonOrders = co_await _api()->fetchFulfillmentOrders(queryStart);
        if (!dlgPtr) { setEnabled(true); co_return; }

        if (!_api()->lastError().isEmpty()) {
            appendLog(tr("  ✗ Failed to retrieve Amazon outbound orders: %1").arg(_api()->lastError()));
        } else {
            appendLog(tr("  ✓ Retrieved %1 Amazon outbound order(s)").arg(amazonOrders.size()));
        }
    }

    // 3. Match orders and fetch tracking
    setStatus(tr("Matching orders and fetching tracking details…"));
    for (const auto &tOrder : allTemuOrders) {
        if (!dlgPtr) { setEnabled(true); co_return; }

        TableMarketplaceOrders::OrderRow row;
        row.targetStore = orderToStoreMap.value(tOrder.parentOrderSn).label;
        if (row.targetStore.isEmpty()) row.targetStore = orderToStoreMap.value(tOrder.orderSn).label;
        row.parentOrderSn = tOrder.parentOrderSn;
        row.orderSn = tOrder.orderSn;
        row.targetOrderId = tOrder.orderSn; // Target Order ID column
        row.goodsId = tOrder.goodsId;
        row.skuId = tOrder.skuId;
        row.quantity = tOrder.quantity;
        
        TemuStore ts = orderToStoreMap.value(tOrder.parentOrderSn);
        if (ts.label.isEmpty()) ts = orderToStoreMap.value(tOrder.orderSn);
        row.temuStoreToken = ts.token;
        row.temuProxyHost = ts.proxyHost;
        row.temuProxyPort = ts.proxyPort;
        row.temuProxyUser = ts.proxyUser;
        row.temuProxyPass = ts.proxyPassword;

        // Try to find matching Amazon order
        QJsonObject matchedAmazonOrder;
        QString matchedFulfillmentId;
        for (const QJsonValue &amVal : amazonOrders) {
            QJsonObject amObj = amVal.toObject();
            QString sellerOrderId = amObj.value(QStringLiteral("sellerFulfillmentOrderId")).toString();
            if (sellerOrderId.contains(tOrder.parentOrderSn, Qt::CaseInsensitive) ||
                sellerOrderId.contains(tOrder.orderSn, Qt::CaseInsensitive) ||
                tOrder.parentOrderSn.contains(sellerOrderId, Qt::CaseInsensitive) ||
                tOrder.orderSn.contains(sellerOrderId, Qt::CaseInsensitive)) {
                matchedAmazonOrder = amObj;
                matchedFulfillmentId = sellerOrderId;
                break;
            }
        }

        if (!matchedFulfillmentId.isEmpty()) {
            appendLog(tr("→ Matched Temu order %1 with Amazon outbound order %2").arg(tOrder.orderSn, matchedFulfillmentId));
            
            // Get detailed order to extract tracking number
            QJsonObject detailedOrder = co_await _api()->getFulfillmentOrder(matchedFulfillmentId);
            if (!dlgPtr) { setEnabled(true); co_return; }

            QString trackingNumber;
            QString carrierCode;
            QJsonArray shipments = detailedOrder.value(QStringLiteral("fulfillmentShipments")).toArray();
            for (const QJsonValue &shipVal : shipments) {
                QJsonObject shipObj = shipVal.toObject();
                QJsonArray packages = shipObj.value(QStringLiteral("fulfillmentShipmentPackage")).toArray();
                if (packages.isEmpty()) packages = shipObj.value(QStringLiteral("fulfillmentShipmentPackages")).toArray();
                
                for (const QJsonValue &pkgVal : packages) {
                    QJsonObject pkgObj = pkgVal.toObject();
                    trackingNumber = pkgObj.value(QStringLiteral("trackingNumber")).toString();
                    carrierCode = pkgObj.value(QStringLiteral("carrierCode")).toString();
                    if (!trackingNumber.isEmpty()) break;
                }
                if (!trackingNumber.isEmpty()) break;
            }

            row.source = QStringLiteral("Amazon (%1)").arg(carrierCode.isEmpty() ? QStringLiteral("FBA") : carrierCode);
            row.sourceOrderId = matchedFulfillmentId; // Source Order ID column
            row.trackingNumber = trackingNumber;

            if (trackingNumber.isEmpty()) {
                appendLog(tr("  ⚠ No tracking number generated yet."));
            } else {
                appendLog(tr("  ✓ Found tracking number: %1").arg(trackingNumber));
            }
        } else {
            appendLog(tr("→ No matching Amazon outbound order found for Temu order %1").arg(tOrder.orderSn));
            row.source = tr("None");
            row.sourceOrderId = QString();
            row.trackingNumber = QString();
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
                                             order.trackingNumber, carrier);
        if (!dlgPtr) { setEnabled(true); co_return; }

        if (ok) {
            appendLog(tr("  ✓ Successfully shipped order %1 on Temu."));
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
