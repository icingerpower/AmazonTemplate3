#pragma GCC optimize("O1")
#include "PaneMarketplaces.h"
#include "ui_PaneMarketplaces.h"

#include "AbstractInventorySource.h"
#include "AbstractInventorySourceFactory.h"
#include "AbstractTargetMarketplace.h"
#include "AbstractTargetMarketplaceFactory.h"
#include "MarketplaceTypes.h"
#include "TableMarketplaceProducts.h"
#include "TableMarketplaceOrders.h"

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

#include "../../common/workingdirectory/WorkingDirectoryManager.h"
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

// ---------------------------------------------------------------------------
// Fulfillment source inventory/sales cache (24 h, partial — failed entries
// are re-fetched on the next Load).
// ---------------------------------------------------------------------------

static void saveAmazonCache(const QList<StockRecord> &records, const QHash<QString, int> &sales)
{
    auto s = WorkingDirectoryManager::instance()->settings();
    s->setValue(QStringLiteral("AmazonCache/timestamp"), QDateTime::currentDateTimeUtc().toSecsSinceEpoch());

    QJsonArray invArray;
    for (const auto &item : records) {
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

static bool loadAmazonCache(QList<StockRecord> *recordsOut, QHash<QString, int> *salesOut)
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

    recordsOut->clear();
    QJsonArray invArray = invDoc.array();
    for (const auto &val : invArray) {
        QJsonObject obj = val.toObject();
        StockRecord item;
        item.sku = obj.value(QStringLiteral("sku")).toString();
        item.asin = obj.value(QStringLiteral("asin")).toString();
        item.available = obj.value(QStringLiteral("available")).toInt();
        item.inbound = obj.value(QStringLiteral("inbound")).toInt();
        recordsOut->append(item);
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
    qDeleteAll(m_marketplaces);
    qDeleteAll(m_sources);
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

// Rebuild the configured platforms from the working directory settings via
// the registered factories (Recorder pattern) — picks up new stores/sources
// added in Settings without restarting the app.
void PaneMarketplaces::_rebuildPlatforms()
{
    qDeleteAll(m_marketplaces);
    m_marketplaces.clear();
    qDeleteAll(m_sources);
    m_sources.clear();

    auto s = WorkingDirectoryManager::instance()->settings();
    m_marketplaces = AbstractTargetMarketplaceFactory::buildAllInstances(s.data());
    m_sources      = AbstractInventorySourceFactory::buildAllInstances(s.data());

    QStringList mktNames, srcNames;
    for (const auto *m : m_marketplaces) mktNames << m->displayName();
    for (const auto *src : m_sources)    srcNames << src->displayName();
    qDebug() << "PaneMarketplaces::_rebuildPlatforms: marketplaces =" << mktNames
             << "sources =" << srcNames;
}

AbstractInventorySource *PaneMarketplaces::_source() const
{
    return m_sources.isEmpty() ? nullptr : m_sources.first();
}

AbstractTargetMarketplace *PaneMarketplaces::_marketplaceById(const QString &id) const
{
    for (auto *m : m_marketplaces)
        if (m->id() == id)
            return m;
    return nullptr;
}

QCoro::Task<void> PaneMarketplaces::_onLoad()
{
    _rebuildPlatforms();

    QList<TableMarketplaceProducts::MarketplaceStore> stores;
    for (const auto *mkt : m_marketplaces) {
        TableMarketplaceProducts::MarketplaceStore ms;
        ms.id    = mkt->id();
        ms.label = mkt->displayName();
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

    // Configuration summary — which platforms are active for this run.
    {
        QStringList mktNames;
        for (const auto *m : m_marketplaces) mktNames << m->displayName();
        appendLog(tr("→ %1 target marketplace(s): %2")
                  .arg(m_marketplaces.size())
                  .arg(mktNames.isEmpty() ? tr("none — add stores in Settings")
                                          : mktNames.join(QStringLiteral(", "))));
        appendLog(tr("→ Fulfillment source: %1")
                  .arg(_source() ? _source()->displayName()
                                 : tr("none configured — check API credentials in Settings")));
    }

    // --- Discover SKUs from the target marketplaces (rows = union of store
    //     SKUs; a SKU existing only at the fulfillment source is not shown) ---
    QHash<QString, QHash<QString,int>> storeQtyByStoreId; // marketplace id → (sku → qty)
    QMap<QString, QString> skuByLower;                     // lower → original casing (sorted)
    for (auto *mkt : m_marketplaces) {
        if (!dlgPtr) { setEnabled(true); co_return; }
        setStatus(tr("Listing SKUs of %1…").arg(mkt->displayName()));
        appendLog(tr("→ %1: listing store SKUs").arg(mkt->displayName()));

        QHash<QString, int> qtyBySku;
        co_await mkt->fetchInventory({}, &qtyBySku); // empty filter = all SKUs
        if (!dlgPtr) { setEnabled(true); co_return; }

        if (!mkt->lastError().isEmpty()) {
            appendLog(tr("  ✗ Failed to list SKUs: %1").arg(mkt->lastError()));
            continue;
        }
        appendLog(tr("  ✓ %1 SKU(s) in store").arg(qtyBySku.size()));
        storeQtyByStoreId.insert(mkt->id(), qtyBySku);
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

    // --- Fulfillment source data: cache is PARTIAL — any SKU missing from
    //     the cache or whose sales retrieval failed (-1) is re-fetched. ---
    QList<StockRecord> records;
    QHash<QString, int> cachedSales;
    const bool hasCache = loadAmazonCache(&records, &cachedSales);

    // Which table SKUs have no cached inventory data?
    QSet<QString> cachedInvLower;
    for (const auto &s : records)
        cachedInvLower.insert(s.sku.toLower());
    QStringList missingInv;
    for (const QString &sku : allSkus)
        if (!cachedInvLower.contains(sku.toLower()))
            missingInv.append(sku);

    if (hasCache) {
        appendLog(tr("→ Loading %1 data from cache (valid for 24h)")
                  .arg(_source() ? _source()->displayName() : QStringLiteral("Amazon")));
        m_model->applyInventory(records);
        appendLog(tr("  ✓ %1 SKU(s) found in cache").arg(records.size()));
        for (auto it = cachedSales.begin(); it != cachedSales.end(); ++it) {
            if (it.value() < 0)
                continue; // failed last time — will be re-fetched below
            m_model->applySales(it.key(), it.value());
        }
        if (!missingInv.isEmpty())
            appendLog(tr("  ⚠ %1 SKU(s) missing from cache — fetching from the source: %2")
                      .arg(missingInv.size()).arg(missingInv.join(QStringLiteral(", "))));
    }

    if (!_source()) {
        appendLog(tr("→ No fulfillment source configured — skipping source inventory/sales."));
    } else if (hasCache && !missingInv.isEmpty()) {
        // Partial refresh: fetch only the missing SKUs via the source's live
        // API — fresher than the bulk report and not subject to the
        // report-generation quota (repeated Amazon reports return FATAL).
        setStatus(tr("Fetching %1 missing SKU(s) from %2…")
                  .arg(missingInv.size()).arg(_source()->displayName()));
        appendLog(tr("→ %1 live API: fetching %2 missing SKU(s)")
                  .arg(_source()->displayName()).arg(missingInv.size()));

        QList<StockRecord> liveMissing;
        _source()->clearLastError();
        co_await _source()->fetchInventory(missingInv, &liveMissing, appendLog);
        if (!dlgPtr) { setEnabled(true); co_return; }

        if (!_source()->lastError().isEmpty())
            appendLog(tr("  ✗ %1").arg(_source()->lastError()));
        appendLog(tr("  ✓ %1 SKU(s) retrieved").arg(liveMissing.size()));
        for (const auto &s : liveMissing) {
            appendLog(QStringLiteral("    %1 → %2 | avail %3")
                          .arg(s.sku, s.asin).arg(s.available));
            records.append(s);
        }
        m_model->applyInventory(liveMissing);
    } else if (!hasCache) {
        // Cold start: bulk fetch (Amazon: MYI report — covers all SKUs in one shot).
        while (true) {
            if (!dlgPtr) { setEnabled(true); co_return; }
            records.clear();
            setStatus(tr("Requesting %1 bulk inventory…").arg(_source()->displayName()));
            appendLog(tr("→ %1: requesting bulk inventory").arg(_source()->displayName()));
            appendLog(tr("  (may take 1–2 minutes)"));

            co_await _source()->fetchAllInventory(allSkus, &records, appendLog);

            if (!dlgPtr) { setEnabled(true); co_return; }
            if (!records.isEmpty())
                break; // success

            const QString err = _source()->lastError();
            appendLog(err.isEmpty()
                      ? tr("  ✗ No inventory data returned")
                      : tr("  ✗ %1").arg(err));

            // Bulk fetch failed (often the report generation quota) — the
            // live API returns the same numbers without that quota.
            appendLog(tr("→ Bulk fetch failed — falling back to the live API"));
            _source()->clearLastError();
            co_await _source()->fetchInventory(allSkus, &records, appendLog);
            if (!dlgPtr) { setEnabled(true); co_return; }
            if (!records.isEmpty())
                break;

            const int answer = QMessageBox::question(
                this,
                tr("Retry?"),
                tr("Failed to retrieve %1 inventory (bulk and live API).\n\n%2\n\nRetry?")
                    .arg(_source()->displayName(),
                         _source()->lastError().isEmpty() ? err : _source()->lastError()),
                QMessageBox::Yes | QMessageBox::No);
            if (answer != QMessageBox::Yes)
                break;

            appendLog(tr("─── Retrying… ───"));
        }

        if (!dlgPtr) { setEnabled(true); co_return; }
        m_model->applyInventory(records);
        if (!records.isEmpty()) {
            appendLog(tr("  ✓ %1 SKU(s) retrieved").arg(records.size()));
            for (const auto &s : records)
                appendLog(QStringLiteral("    %1 → %2 | avail %3")
                              .arg(s.sku, s.asin).arg(s.available));
        }
    }

    // --- Source sales (Amazon: one call per SKU × EU marketplace) — only for
    //     SKUs the source knows, and only those without a valid cached value. ---
    if (_source() && !records.isEmpty()) {
        QHash<QString, int> cachedSalesLower;
        for (auto it = cachedSales.begin(); it != cachedSales.end(); ++it)
            cachedSalesLower.insert(it.key().toLower(), it.value());

        QStringList salesToFetch;
        for (const auto &s : records)
            if (cachedSalesLower.value(s.sku.toLower(), -1) < 0)
                salesToFetch.append(s.sku);

        QHash<QString, int> salesToCache = cachedSales;
        for (int i = 0; i < salesToFetch.size(); ++i) {
            if (!dlgPtr) { setEnabled(true); co_return; }
            const QString sku = salesToFetch[i];
            setStatus(tr("Fetching sales 90d (%1/%2): %3").arg(i + 1).arg(salesToFetch.size()).arg(sku));
            appendLog(tr("→ Sales 90d: %1").arg(sku));

            int units = -1;
            co_await _source()->fetchSalesUnits(sku, 90, &units);
            if (!dlgPtr) { setEnabled(true); co_return; }
            m_model->applySales(sku, units);
            salesToCache.insert(sku, units); // -1 on failure → re-fetched next Load

            if (units < 0)
                appendLog(tr("  ✗ Failed to retrieve sales data"));
            else
                appendLog(tr("  ✓ %1 units sold in 90 days").arg(units));
        }

        if (!hasCache || !missingInv.isEmpty() || !salesToFetch.isEmpty())
            saveAmazonCache(records, salesToCache);
    } else {
        appendLog(tr("→ Skipping source sales queries (no source inventory data)"));
    }

    // --- Marketplace sales 90d (inventory already applied at discovery) ---
    for (auto *mkt : m_marketplaces) {
        if (!dlgPtr) { setEnabled(true); co_return; }
        setStatus(tr("Fetching %1 sales…").arg(mkt->displayName()));
        appendLog(tr("→ %1: fetching sales").arg(mkt->displayName()));

        QHash<QString, int> salesBySku;
        co_await mkt->fetchSales(allSkus, 90, &salesBySku);
        if (!dlgPtr) { setEnabled(true); co_return; }

        if (!mkt->lastError().isEmpty()) {
            appendLog(tr("  ✗ Failed to retrieve sales: %1").arg(mkt->lastError()));
        } else {
            appendLog(tr("  ✓ Sales retrieved successfully."));
            m_model->applyStoreSales(mkt->id(), salesBySku);
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
    if (m_marketplaces.isEmpty())
        _rebuildPlatforms();

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

    // --- Compute target qty per SKU from the fulfillment source data ---
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

    // --- Push to each configured target marketplace ---
    if (m_marketplaces.isEmpty())
        appendLog(tr("→ No target marketplace configured — add stores in Settings"));

    for (auto *mkt : m_marketplaces) {
        if (!dlgPtr) { setEnabled(true); co_return; }
        setStatus(tr("Syncing %1…").arg(mkt->displayName()));
        appendLog(tr("─── %1 ───").arg(mkt->displayName()));

        co_await mkt->updateInventory(targetQtyBySku, appendLog);
        if (!dlgPtr) { setEnabled(true); co_return; }

        if (!mkt->lastError().isEmpty())
            appendLog(tr("  ✗ Store-level error: %1").arg(mkt->lastError()));
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
    _rebuildPlatforms();

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

    // 1. Fetch unshipped orders from every target marketplace
    QList<MarketOrder> allOrders;
    for (auto *mkt : m_marketplaces) {
        if (!dlgPtr) { setEnabled(true); co_return; }
        setStatus(tr("Fetching %1 orders…").arg(mkt->displayName()));
        appendLog(tr("→ %1: requesting unshipped orders").arg(mkt->displayName()));

        const QList<MarketOrder> storeOrders = co_await mkt->fetchUnshippedOrders();
        if (!dlgPtr) { setEnabled(true); co_return; }

        if (!mkt->lastError().isEmpty()) {
            appendLog(tr("  ✗ Failed to retrieve orders: %1").arg(mkt->lastError()));
        } else {
            appendLog(tr("  ✓ Retrieved %1 unshipped order(s)").arg(storeOrders.size()));
            allOrders.append(storeOrders);
        }
    }

    // 2. For each marketplace order, fetch the fulfillment order tracking
    // directly by id: orderIdPrefix() + "-" + orderId (e.g. "temu-PO-…").
    setStatus(tr("Fetching fulfillment tracking details…"));
    bool authErrorLogged = false;
    for (const MarketOrder &order : allOrders) {
        if (!dlgPtr) { setEnabled(true); co_return; }

        AbstractTargetMarketplace *mkt = _marketplaceById(order.marketplaceId);

        TableMarketplaceOrders::OrderRow row;
        row.marketplaceId = order.marketplaceId;
        row.targetStore = mkt ? mkt->displayName() : order.marketplaceId;
        row.parentOrderSn = order.orderId;
        row.orderSn = order.itemId;
        row.targetOrderId = order.itemId;
        row.sku = order.sku;
        row.goodsId = order.goodsId;
        row.skuId = order.skuId;
        row.quantity = order.quantity;

        if (!_source()) {
            appendLog(tr("→ %1: no fulfillment source configured — cannot fetch tracking")
                      .arg(order.itemId));
            row.source = tr("None");
            orderRows.append(row);
            continue;
        }

        const QString fulfillmentId =
            (mkt ? mkt->orderIdPrefix() : QStringLiteral("temu"))
            + QStringLiteral("-") + order.orderId;
        appendLog(tr("→ Order %1 — fetching %2 order %3")
                  .arg(order.itemId, _source()->displayName(), fulfillmentId));

        _source()->clearLastError();
        TrackingInfo tracking;
        co_await _source()->fetchTracking(fulfillmentId, &tracking);
        if (!dlgPtr) { setEnabled(true); co_return; }

        if (!_source()->lastError().isEmpty()) {
            const QString err = _source()->lastError();
            const bool isAuth = err.contains(QLatin1String("403"));
            if (!isAuth) {
                appendLog(tr("  ✗ %1").arg(err));
            } else if (!authErrorLogged) {
                appendLog(tr("  ✗ %1").arg(err));
                authErrorLogged = true;
            } else {
                appendLog(tr("  ✗ 403 — see message above"));
            }
            row.source = tr("None");
            row.sourceOrderId = QString();
            row.trackingNumber = QString();
            orderRows.append(row);
            continue;
        }

        row.source = QStringLiteral("%1 (%2)")
            .arg(_source()->displayName(),
                 tracking.carrierName.isEmpty() ? QStringLiteral("?") : tracking.carrierName);
        row.sourceOrderId = fulfillmentId;
        row.trackingNumber = tracking.trackingNumber;

        if (!tracking.hasTracking()) {
            appendLog(tr("  ⚠ No tracking number yet for %1.").arg(fulfillmentId));
        } else {
            appendLog(tr("  ✓ Tracking number: %1 (carrier %2)")
                      .arg(tracking.trackingNumber, tracking.carrierName));
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
    if (m_marketplaces.isEmpty())
        _rebuildPlatforms();

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

    auto orders = m_ordersModel->orders();
    int successCount = 0;

    for (int i = 0; i < orders.size(); ++i) {
        if (!dlgPtr) { setEnabled(true); co_return; }
        const auto &order = orders[i];

        if (order.trackingNumber.isEmpty()) {
            appendLog(tr("Skipping order %1: No tracking number found.").arg(order.orderSn));
            continue;
        }

        AbstractTargetMarketplace *mkt = _marketplaceById(order.marketplaceId);
        if (!mkt) {
            appendLog(tr("Skipping order %1: marketplace \"%2\" is not configured anymore.")
                      .arg(order.orderSn, order.marketplaceId));
            continue;
        }

        statusLabel->setText(tr("Syncing order %1 (%2/%3)…").arg(order.orderSn).arg(i + 1).arg(orders.size()));
        appendLog(tr("→ Shipping order %1 on %2 with tracking %3…")
                  .arg(order.orderSn, mkt->displayName(), order.trackingNumber));

        // Extract carrier name from ColSource (e.g. "Amazon FBA EU (Amazon Logistics)")
        QString carrier = order.source;
        const int open  = carrier.lastIndexOf(QLatin1Char('('));
        if (open >= 0 && carrier.endsWith(QLatin1Char(')')))
            carrier = carrier.mid(open + 1, carrier.length() - open - 2);

        MarketOrder mo;
        mo.marketplaceId = order.marketplaceId;
        mo.orderId  = order.parentOrderSn;
        mo.itemId   = order.orderSn;
        mo.sku      = order.sku;
        mo.goodsId  = order.goodsId;
        mo.skuId    = order.skuId;
        mo.quantity = order.quantity;

        TrackingInfo tracking;
        tracking.trackingNumber = order.trackingNumber;
        tracking.carrierName    = carrier;

        const bool ok = co_await mkt->confirmShipment(mo, tracking,
            [appendLog](const QString &msg) { appendLog(QStringLiteral("  ") + msg); });
        if (!dlgPtr) { setEnabled(true); co_return; }

        if (ok) {
            appendLog(tr("  ✓ Successfully shipped order %1 on %2.")
                      .arg(order.orderSn, mkt->displayName()));
            successCount++;
        } else {
            appendLog(tr("  ✗ Failed to ship order %1: %2").arg(order.orderSn, mkt->lastError()));
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
    if (m_marketplaces.isEmpty())
        _rebuildPlatforms();
    if (!_source()) {
        QMessageBox::warning(this, tr("No fulfillment source"),
            tr("No fulfillment source is configured — check API credentials in Settings."));
        co_return;
    }

    QModelIndexList selected;
    if (ui->tableViewOrders->selectionModel())
        selected = ui->tableViewOrders->selectionModel()->selectedRows();
    if (selected.isEmpty()) {
        QMessageBox::information(this, tr("No selection"),
            tr("Select the order(s) to ship in the orders table first (Load orders, then click a row)."));
        co_return;
    }

    const auto orders = m_ordersModel->orders();

    // --- Phase 1: build one candidate per selected order (address + payload) ---
    struct Candidate {
        TableMarketplaceOrders::OrderRow order;
        FulfillmentRequest request;
        QJsonObject payload; // exact JSON the source would send (preview)
    };
    QList<Candidate> candidates;

    for (const QModelIndex &idx : selected) {
        if (idx.row() < 0 || idx.row() >= orders.size())
            continue;
        const auto &order = orders.at(idx.row());

        AbstractTargetMarketplace *mkt = _marketplaceById(order.marketplaceId);
        if (!mkt) {
            QMessageBox::warning(this, tr("Unknown marketplace"),
                tr("Order %1 belongs to marketplace \"%2\" which is not configured anymore.")
                    .arg(order.orderSn, order.marketplaceId));
            continue;
        }
        if (order.sku.isEmpty()) {
            QMessageBox::warning(this, tr("Missing SKU"),
                tr("Order %1 has no SKU — reload orders first.").arg(order.orderSn));
            continue;
        }

        ShippingAddress address;
        co_await mkt->fetchOrderAddress(order.parentOrderSn, &address);
        if (!mkt->lastError().isEmpty() || !address.isValid()) {
            QMessageBox::warning(this, tr("Address error"),
                tr("Could not fetch the shipping address of order %1:\n%2")
                    .arg(order.orderSn, mkt->lastError()));
            continue;
        }

        FulfillmentRequest request;
        request.fulfillmentOrderId = mkt->orderIdPrefix() + QStringLiteral("-") + order.parentOrderSn;
        request.comment = QStringLiteral("%1 order %2").arg(mkt->displayName(), order.parentOrderSn);
        request.address = address;
        FulfillmentItem item;
        item.sku      = order.sku;
        item.itemId   = order.orderSn;
        item.quantity = order.quantity;
        request.items.append(item);

        Candidate c;
        c.order   = order;
        c.request = request;
        c.payload = _source()->previewFulfillmentOrder(request);
        candidates.append(c);
    }

    if (candidates.isEmpty())
        co_return;

    // --- Phase 2: one confirmation dialog, one checkable line per order ---
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Ship by %1 — confirmation").arg(_source()->displayName()));
    dlg.resize(940, 520);
    auto *vLayout = new QVBoxLayout(&dlg);
    vLayout->addWidget(new QLabel(
        tr("These %1 orders will be created. Uncheck a line to skip it.")
            .arg(_source()->displayName()), &dlg));

    auto *table = new QTableWidget(candidates.size(), 6, &dlg);
    table->setHorizontalHeaderLabels({tr("New order ID"), tr("SKU"), tr("Qty"),
                                      tr("Recipient"), tr("Address"), tr("Note")});
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->hide();
    for (int i = 0; i < candidates.size(); ++i) {
        const Candidate &c = candidates.at(i);
        const bool hasTracking = !c.order.trackingNumber.isEmpty();

        auto *idItem = new QTableWidgetItem(c.request.fulfillmentOrderId);
        idItem->setFlags(idItem->flags() | Qt::ItemIsUserCheckable);
        // Orders that already have a tracking number probably already exist at
        // the source — leave them unchecked by default.
        idItem->setCheckState(hasTracking ? Qt::Unchecked : Qt::Checked);
        table->setItem(i, 0, idItem);
        table->setItem(i, 1, new QTableWidgetItem(c.order.sku));
        table->setItem(i, 2, new QTableWidgetItem(QString::number(c.order.quantity)));
        table->setItem(i, 3, new QTableWidgetItem(c.request.address.name));
        table->setItem(i, 4, new QTableWidgetItem(QStringLiteral("%1, %2 %3 (%4)")
            .arg(c.request.address.addressLine1,
                 c.request.address.postalCode,
                 c.request.address.city,
                 c.request.address.countryCode)));
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
    payloadView->setPlaceholderText(tr("Click a line to preview the exact payload sent to the source."));
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
        _source()->clearLastError();
        const bool ok = co_await _source()->createFulfillmentOrder(c.request);
        results.append(ok ? tr("✓ %1 created").arg(c.request.fulfillmentOrderId)
                          : tr("✗ %1 failed: %2").arg(c.request.fulfillmentOrderId,
                                                      _source()->lastError()));
    }

    if (results.isEmpty()) {
        QMessageBox::information(this, tr("Nothing sent"),
            tr("All lines were unchecked — no order was created."));
        co_return;
    }
    QMessageBox::information(this, tr("Ship by %1 — results").arg(_source()->displayName()),
        results.join(QStringLiteral("\n"))
        + tr("\n\nUse \"Load\" then \"Sync orders\" once the source generates the tracking numbers."));
    co_return;
}
