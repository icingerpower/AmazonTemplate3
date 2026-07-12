// GCC 13 ICE workaround: coroutines with non-trivially-destructible frame
// locals miscompile at -O2/-O3. Force O1 (same as PanePricing).
#pragma GCC optimize("O1")
#include "PaneDiscount.h"
#include "ui_PaneDiscount.h"
#include "TreeSkuDiscount.h"
#include "TableCurrencyRates.h"
#include "ProgressDialog.h"
#include "AmazonInventoryApi.h"
#include "AmazonPricingApi.h"
#include "AmazonCatalogApi.h"
#include "SettingsTable.h"

#include <QCheckBox>
#include <QDateTime>
#include <QDialog>
#include <QHeaderView>
#include <QListView>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QTextEdit>
#include <QtMath>

namespace {
// EU marketplaces: they share the pan-EU FBA pool and the EU SP-API endpoint,
// so the (EU) inventory-age report and per-country pricing all apply. IDs match
// TableCurrencyRates entries so currency + EUR rate resolve. NA markets are
// deliberately excluded — they don't share the EU inventory pool. Display order.
const QStringList kEuMarketplaceIds = {
    QStringLiteral("A1PA6795UKMFR9"), // DE
    QStringLiteral("A1F83G8C2ARO7P"), // UK
    QStringLiteral("A13V1IB3VIYZZH"), // FR
    QStringLiteral("A1RKKUPIHCS9HS"), // ES
    QStringLiteral("APJ6JRA9NG5V4"),  // IT
    QStringLiteral("A1805IZSGTT6HS"), // NL
    QStringLiteral("AMEN7PMS3EDWL"),  // BE
    QStringLiteral("A1C3SOZRARQ6R3"), // PL
    QStringLiteral("A2NODRKZP88ZB9"), // SE
};
const QString kDeMarketplaceId = QStringLiteral("A1PA6795UKMFR9");
const QString kFrMarketplaceId = QStringLiteral("A13V1IB3VIYZZH"); // preferred title
const QString kUkMarketplaceId = QStringLiteral("A1F83G8C2ARO7P"); // English fallback
} // namespace

PaneDiscount::PaneDiscount(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PaneDiscount)
{
    ui->setupUi(this);

    m_model = new TreeSkuDiscount(this);
    ui->treeViewProducts->setModel(m_model);
    ui->treeViewProducts->header()->setStretchLastSection(false);
    ui->treeViewProducts->header()->setSectionResizeMode(
        TreeSkuDiscount::ColTitle, QHeaderView::Stretch);
    // Double-click opens an editor so cell text (e.g. a SKU) can be selected and
    // copied. Edits are not saved (model has no setData).
    ui->treeViewProducts->setEditTriggers(
        QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);

    _buildCountriesModel();

    // Persist the "100% aged only" filter (default checked).
    ui->checkBox100Aged->setChecked(
        QSettings().value(QStringLiteral("discount/only100Aged"), true).toBool());
    connect(ui->checkBox100Aged, &QCheckBox::toggled, this, [](bool on) {
        QSettings().setValue(QStringLiteral("discount/only100Aged"), on);
    });

    connect(ui->pushButton, &QPushButton::clicked, this,
            [this]() { m_loadTask = _onLoad(); });
    connect(ui->buttonApplyDiscount, &QPushButton::clicked, this,
            [this]() { m_applyTask = _onApply(); });
}

PaneDiscount::~PaneDiscount()
{
    delete ui;
}

void PaneDiscount::_buildCountriesModel()
{
    m_countriesModel = new QStandardItemModel(this);
    QSettings s;
    // Country label (code) comes from the shared currency table, keyed by id.
    TableCurrencyRates rates;
    for (const QString &id : kEuMarketplaceIds) {
        QString cc;
        for (const TableCurrencyRates::Entry &e : rates.entries())
            if (e.marketplaceId == id) { cc = e.country; break; }
        if (cc.isEmpty()) continue;   // no rate entry → can't convert, skip
        auto *item = new QStandardItem(cc);
        item->setCheckable(true);
        item->setData(id, Qt::UserRole + 1);
        // Default: all checked; persisted per marketplace.
        const QString key = QStringLiteral("discount/country/%1/checked").arg(id);
        const bool checked = s.value(key, true).toBool();
        item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
        m_countriesModel->appendRow(item);
    }
    ui->listViewCountries->setModel(m_countriesModel);

    // Horizontal, wrapping strip (like PaneSizing's country list) rather than a
    // tall vertical list.
    ui->listViewCountries->setFlow(QListView::LeftToRight);
    ui->listViewCountries->setWrapping(true);
    ui->listViewCountries->setResizeMode(QListView::Adjust);
    ui->listViewCountries->setMaximumHeight(30);

    connect(m_countriesModel, &QStandardItemModel::itemChanged, this,
            [](QStandardItem *item) {
                QSettings().setValue(
                    QStringLiteral("discount/country/%1/checked")
                        .arg(item->data(Qt::UserRole + 1).toString()),
                    item->checkState() == Qt::Checked);
            });
}

// ---------------------------------------------------------------------------
// Load — fetch aged inventory + per-country prices/titles, build the tree.
// ---------------------------------------------------------------------------

QCoro::Task<void> PaneDiscount::_onLoad()
{
    // --- Collect checked countries, resolving currency + EUR rate from the
    //     shared (PanePricing) rate store, which loads from QSettings. ---
    struct Country { QString mp; QString country; QString currency; double rate; };
    QList<Country> countries;
    {
        TableCurrencyRates rates;   // fresh: picks up whatever Pricing last saved
        for (int r = 0; r < m_countriesModel->rowCount(); ++r) {
            QStandardItem *it = m_countriesModel->item(r);
            if (it->checkState() != Qt::Checked) continue;
            const QString mp = it->data(Qt::UserRole + 1).toString();
            for (const TableCurrencyRates::Entry &e : rates.entries()) {
                if (e.marketplaceId == mp) {
                    countries.append({mp, e.country, e.currency, e.rate});
                    break;
                }
            }
        }
    }
    if (countries.isEmpty()) {
        QMessageBox::information(this, tr("No Countries Selected"),
            tr("Check at least one country before loading."));
        co_return;
    }

    const int  pct        = ui->spinBox->value();                    // discount %
    const int  minMonths  = ui->spinBoxDays_3->value();              // min months stored
    const int  minDayInv  = ui->spinBoxMinDayInventory->value();     // min days of inventory
    const int  exclSales  = ui->spinBoxDaysExcludeIfSales->value();  // exclude if 90d sales >=
    const bool only100Aged = ui->checkBox100Aged->isChecked();       // only fully-aged stock

    ProgressDlgHandles h;
    auto *dlg = makeProgressDlg(this, tr("Loading discount candidates…"), &h);
    m_progressDlg = dlg;
    dlg->show();
    auto appendLog = [h](const QString &msg) {
        if (!h.log) return;
        const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
        h.log->append(QStringLiteral("[%1] %2").arg(ts, msg));
    };
    auto setStatus = [h](const QString &msg) { if (h.status) h.status->setText(msg); };

    ui->pushButton->setEnabled(false);
    ui->buttonApplyDiscount->setEnabled(false);

    // --- Build APIs from settings ---
    const auto *st = SettingsTable::instance();
    AmazonInventoryApi inventoryApi(
        st->value(SettingsTable::KEY_LWA_CLIENT_ID),
        st->value(SettingsTable::KEY_LWA_CLIENT_SECRET),
        st->value(SettingsTable::KEY_EU_LWA_REFRESH_TOKEN),
        st->value(SettingsTable::KEY_EU_SELLER_ID),
        kDeMarketplaceId,
        this);
    AmazonPricingApi pricingApi(
        st->value(SettingsTable::KEY_LWA_CLIENT_ID),
        st->value(SettingsTable::KEY_LWA_CLIENT_SECRET),
        st->value(SettingsTable::KEY_EU_LWA_REFRESH_TOKEN),
        st->value(SettingsTable::KEY_NA_LWA_REFRESH_TOKEN),
        st->value(SettingsTable::KEY_EU_SELLER_ID),
        st->value(SettingsTable::KEY_NA_SELLER_ID),
        this);
    AmazonCatalogApi catalogApi(
        st->value(SettingsTable::KEY_LWA_CLIENT_ID),
        st->value(SettingsTable::KEY_LWA_CLIENT_SECRET),
        st->value(SettingsTable::KEY_EU_LWA_REFRESH_TOKEN),
        st->value(SettingsTable::KEY_NA_LWA_REFRESH_TOKEN),
        st->value(SettingsTable::KEY_JP_LWA_REFRESH_TOKEN),
        st->value(SettingsTable::KEY_EU_SELLER_ID),
        st->value(SettingsTable::KEY_NA_SELLER_ID),
        st->value(SettingsTable::KEY_JP_SELLER_ID),
        QString(),
        this);

    // --- Fetch aged inventory (defines the SKU universe) ---
    setStatus(tr("Fetching FBA inventory health report…"));
    appendLog(tr("→ Requesting inventory planning report (may take 1–2 min)…"));
    QList<AmazonInventoryApi::InventoryAge> ages;
    co_await inventoryApi.fetchInventoryAgeReport(QStringList(), &ages, appendLog);
    appendLog(tr("  ✓ %1 SKU(s) in inventory health report").arg(ages.size()));

    // Marketplace IDs of the checked countries — used to sum 90-day sales
    // "across all Amazon" (order metrics summed over these marketplaces).
    QStringList checkedMps;
    for (const Country &c : countries) checkedMps << c.mp;

    // --- Filter to discount candidates, then enrich with titles + prices ---
    QList<TreeSkuDiscount::Product> products;
    const int total = ages.size();
    for (int i = 0; i < total; ++i) {
        const AmazonInventoryApi::InventoryAge &a = ages[i];

        const int aged   = a.unitsStoredAtLeastDays(minMonths * 30);
        const int onHand = a.totalBucketUnits();   // sum of all age buckets
        // Unknown (-1) days-of-supply doesn't disqualify.
        const bool passDos = a.daysOfSupply < 0 || a.daysOfSupply > minDayInv;
        if (!(a.available > 0 && aged > 0 && passDos))
            continue;

        // "100% aged": every on-hand unit is older than the threshold.
        if (only100Aged && !(onHand > 0 && aged >= onHand)) {
            appendLog(tr("  – %1: not 100%% aged (%2/%3 on-hand aged ≥%4mo), skipped")
                          .arg(a.sku).arg(aged).arg(onHand).arg(minMonths));
            continue;
        }

        // 90-day sales summed across the selected marketplaces (true "all Amazon"
        // figure, unlike the report's single-marketplace column). Then apply the
        // exclude-if-recent-sales filter on this accurate number.
        setStatus(tr("SKU %1/%2: %3 — checking 90-day sales…")
                      .arg(i + 1).arg(total).arg(a.sku));
        int sold90 = -1;
        co_await inventoryApi.fetchSalesUnits(a.sku, 90, checkedMps, &sold90);
        if (exclSales > 0 && sold90 >= 0 && sold90 >= exclSales) {
            appendLog(tr("  – %1: %2 sale(s) in 90d ≥ %3, excluded")
                          .arg(a.sku).arg(sold90).arg(exclSales));
            continue;
        }

        setStatus(tr("SKU %1/%2: %3 — fetching titles & prices…")
                      .arg(i + 1).arg(total).arg(a.sku));

        // Preferred-language title: FR, then English (UK), then report name.
        QString title; QStringList bullets;
        co_await catalogApi.fetchListingText(kFrMarketplaceId, a.asin, &title, &bullets);
        if (title.isEmpty()) {
            bullets.clear();
            co_await catalogApi.fetchListingText(kUkMarketplaceId, a.asin, &title, &bullets);
        }
        if (title.isEmpty()) title = a.productName;

        // Product type (needed for the discount PATCH) from the DE listing.
        double  dePrice = -1.0; bool deExists = false; QString productType;
        co_await pricingApi.fetchListingPrice(kDeMarketplaceId, a.sku,
                                              &dePrice, &deExists, &productType);

        TreeSkuDiscount::Product p;
        p.sku         = a.sku;
        p.asin        = a.asin;
        p.title       = title;
        p.productType = productType;
        p.available   = a.available;
        p.agedUnits   = aged;
        p.unitsSold90 = sold90;
        p.minMonths   = minMonths;

        for (const Country &c : countries) {
            double price = -1.0; bool exists = false;
            double minPrice = -1.0, maxPrice = -1.0;
            QString cProductType;
            // Product type can differ per marketplace (e.g. BE rejects DE's), so
            // capture each marketplace's own type for the PATCH.
            co_await pricingApi.fetchListingPrice(c.mp, a.sku, &price, &exists,
                                                  &cProductType, &minPrice, &maxPrice);

            QString cTitle; QStringList cBullets;
            co_await catalogApi.fetchListingText(c.mp, a.asin, &cTitle, &cBullets);

            // price>0 only when the seller has an explicit price on this
            // marketplace; 404 (not listed) or Amazon-managed (BIL) prices leave
            // it <=0, which we surface as blank + tooltip rather than a number.
            const bool hasPrice = exists && price > 0.0;

            TreeSkuDiscount::CountryRow row;
            row.marketplaceId = c.mp;
            row.countryCode   = c.country;
            row.productType   = cProductType.isEmpty() ? productType : cProductType;
            row.title         = cTitle.isEmpty() ? title : cTitle;
            row.units         = a.available;              // shared pan-EU pool
            row.origCurrency  = c.currency;
            row.origAmount    = hasPrice ? price : 0.0;
            row.unitPriceEur  = (hasPrice && c.rate > 0.0) ? price / c.rate : 0.0;
            row.minPrice      = minPrice > 0.0 ? minPrice : 0.0;
            row.maxPrice      = maxPrice > 0.0 ? maxPrice : 0.0;
            row.listed        = exists;
            // Included SKUs are eligible; a country row can only be discounted
            // when it actually has a usable price.
            row.eligible      = hasPrice;
            row.newPrice      = hasPrice
                                    ? qRound(price * (100 - pct)) / 100.0   // in origCurrency
                                    : 0.0;
            p.countries.append(row);
        }

        products.append(p);
        appendLog(tr("→ %1 (%2) — %3 available, %4/%5 on-hand aged ≥%6mo, %7 sold 90d")
                      .arg(a.sku, a.asin).arg(a.available).arg(aged).arg(onHand)
                      .arg(minMonths)
                      .arg(sold90 >= 0 ? QString::number(sold90) : QStringLiteral("?")));
        // Aged is on-hand-based; available is fulfillable only. When aged exceeds
        // available the extra units are reserved/unsellable (not an error).
        if (aged > a.available)
            appendLog(tr("    ℹ %1: %2 aged unit(s) > %3 fulfillable — extra are "
                         "reserved / unsellable / inbound")
                          .arg(a.sku).arg(aged).arg(a.available));
    }

    m_model->setProducts(products);
    ui->treeViewProducts->expandAll();
    ui->treeViewProducts->resizeColumnToContents(TreeSkuDiscount::ColSku);

    setStatus(tr("Done — %1 candidate SKU(s).").arg(products.size()));
    appendLog(tr("─── Load complete: %1 discount candidate(s) ───").arg(products.size()));
    if (h.bar) h.bar->setRange(0, 1);
    if (h.closeBtn) h.closeBtn->setEnabled(true);

    ui->pushButton->setEnabled(true);
    ui->buttonApplyDiscount->setEnabled(true);
    m_progressDlg = nullptr;
}

// ---------------------------------------------------------------------------
// Apply — PATCH a scheduled discounted price for each eligible country row.
// ---------------------------------------------------------------------------

QCoro::Task<void> PaneDiscount::_onApply()
{
    const QList<TreeSkuDiscount::Product> products = m_model->products();
    if (products.isEmpty()) {
        QMessageBox::information(this, tr("No Data"),
            tr("Click Load first to build the discount candidate list."));
        co_return;
    }

    const int days = ui->spinBoxDays->value();
    if (QMessageBox::question(this, tr("Apply discounts"),
            tr("Apply scheduled discounts to Amazon for %1 product(s), lasting %2 day(s)?")
                .arg(products.size()).arg(days))
        != QMessageBox::Yes) {
        co_return;
    }

    ProgressDlgHandles h;
    auto *dlg = makeProgressDlg(this, tr("Applying discounts…"), &h);
    m_progressDlg = dlg;
    dlg->show();
    auto appendLog = [h](const QString &msg) {
        if (!h.log) return;
        const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
        h.log->append(QStringLiteral("[%1] %2").arg(ts, msg));
    };
    auto setStatus = [h](const QString &msg) { if (h.status) h.status->setText(msg); };

    ui->pushButton->setEnabled(false);
    ui->buttonApplyDiscount->setEnabled(false);

    const auto *st = SettingsTable::instance();
    AmazonPricingApi pricingApi(
        st->value(SettingsTable::KEY_LWA_CLIENT_ID),
        st->value(SettingsTable::KEY_LWA_CLIENT_SECRET),
        st->value(SettingsTable::KEY_EU_LWA_REFRESH_TOKEN),
        st->value(SettingsTable::KEY_NA_LWA_REFRESH_TOKEN),
        st->value(SettingsTable::KEY_EU_SELLER_ID),
        st->value(SettingsTable::KEY_NA_SELLER_ID),
        this);

    const QDateTime startAt = QDateTime::currentDateTimeUtc();
    const QDateTime endAt   = startAt.addDays(days);

    int applied = 0, failed = 0, skipped = 0;
    for (const TreeSkuDiscount::Product &p : products) {
        for (const TreeSkuDiscount::CountryRow &c : p.countries) {
            if (!c.eligible || c.newPrice <= 0.0 || c.origAmount <= 0.0) {
                ++skipped;
                continue;
            }
            // Product type is per-marketplace (BE rejects DE's type, etc.).
            if (c.productType.isEmpty()) {
                appendLog(tr("  – %1 / %2: product type unknown, skipped")
                              .arg(p.sku, c.countryCode));
                ++skipped;
                continue;
            }
            setStatus(tr("%1 / %2…").arg(p.sku, c.countryCode));
            bool success = false;
            co_await pricingApi.patchListingDiscount(
                c.marketplaceId, p.sku, c.productType, c.origCurrency,
                c.origAmount, c.newPrice, startAt, endAt, &success);
            if (success) {
                appendLog(tr("  ✓ %1 / %2: %3 → %4 %5")
                              .arg(p.sku, c.countryCode)
                              .arg(c.origAmount, 0, 'f', 2)
                              .arg(c.newPrice, 0, 'f', 2)
                              .arg(c.origCurrency));
                ++applied;
            } else {
                appendLog(tr("  ✗ %1 / %2: %3")
                              .arg(p.sku, c.countryCode, pricingApi.lastError()));
                ++failed;
            }
        }
    }

    setStatus(tr("Done — %1 applied, %2 failed.").arg(applied).arg(failed));
    appendLog(tr("─── Apply complete: %1 applied, %2 failed, %3 skipped ───")
                  .arg(applied).arg(failed).arg(skipped));
    if (h.bar) h.bar->setRange(0, 1);
    if (h.closeBtn) h.closeBtn->setEnabled(true);

    ui->pushButton->setEnabled(true);
    ui->buttonApplyDiscount->setEnabled(true);
    m_progressDlg = nullptr;
}
