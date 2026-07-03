// GCC 13 ICE workaround: coroutines with non-trivially-destructible frame locals
// miscompile at -O2/-O3.
#pragma GCC optimize("O1")
#include "PanePricing.h"
#include "ui_PanePricing.h"
#include "TableCurrencyRates.h"
#include "TablePricing.h"
#include "AbstractCli.h"
#include "AmazonPricingApi.h"
#include "AmazonInventoryApi.h"
#include "SettingsTable.h"

#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QClipboard>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QShowEvent>
#include <QTextEdit>
#include <QtMath>
#include <QVBoxLayout>

// Amazon.de marketplace ID — always queried for the EUR base price
static const QString k_deMarketplaceId = QStringLiteral("A1PA6795UKMFR9");

// ---------------------------------------------------------------------------
// Helpers for _onRefreshRate
// ---------------------------------------------------------------------------

static QByteArray extractJson(const QString &output)
{
    const int first = output.indexOf(QLatin1Char('{'));
    const int last  = output.lastIndexOf(QLatin1Char('}'));
    if (first < 0 || last < first)
        return output.trimmed().toUtf8();
    return output.mid(first, last - first + 1).toUtf8();
}

static const QStringList k_fetchCurrencies = {
    QStringLiteral("GBP"), QStringLiteral("SEK"), QStringLiteral("PLN"),
    QStringLiteral("USD"), QStringLiteral("CAD"), QStringLiteral("MXN"),
    QStringLiteral("BRL"),
};

static QString buildBasePrompt()
{
    const QString example =
        QStringLiteral(R"({"rates":{"GBP":0.8650,"SEK":11.450,"PLN":4.250,"USD":1.0800,"CAD":1.4700,"MXN":20.500,"BRL":5.800}})");

    return QStringLiteral(
        "You are a financial data assistant. Report today's foreign exchange rates against the Euro (EUR).\n\n"
        "Reply with ONLY a valid JSON object — no markdown fences, no extra text, no comments:\n"
        "%1\n\n"
        "Requirements:\n"
        "- Include exactly these keys: GBP, SEK, PLN, USD, CAD, MXN, BRL\n"
        "- Each value is a positive decimal number meaning: 1 EUR = VALUE <currency>\n"
        "- No extra keys, no prose, no code fences"
    ).arg(example);
}

static QString validateRatesJson(const QByteArray &json, QHash<QString, double> *out)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError)
        return QStringLiteral("Invalid JSON: %1").arg(err.errorString());
    if (!doc.isObject())
        return QStringLiteral("Response is not a JSON object.");

    const QJsonValue ratesVal = doc.object().value(QStringLiteral("rates"));
    if (!ratesVal.isObject())
        return QStringLiteral("Missing 'rates' key or it is not an object.");

    const QJsonObject ratesObj = ratesVal.toObject();
    for (const QString &cur : k_fetchCurrencies) {
        const QJsonValue v = ratesObj.value(cur);
        if (!v.isDouble())
            return QStringLiteral("Missing or non-numeric value for '%1'.").arg(cur);
        const double rate = v.toDouble();
        if (rate <= 0.0)
            return QStringLiteral("Rate for '%1' must be positive, got %2.").arg(cur).arg(rate);
        out->insert(cur, rate);
    }
    return {};
}

// ---------------------------------------------------------------------------
// Progress dialog factory — reused by Retrieve and Update
// ---------------------------------------------------------------------------

struct ProgressDlgHandles {
    QPointer<QProgressBar> bar;
    QPointer<QTextEdit>    log;
    QPointer<QLabel>       status;
    QPointer<QPushButton>  closeBtn;
};

static QDialog *makeProgressDlg(QWidget *parent, const QString &title,
                                 ProgressDlgHandles *h)
{
    auto *dlg = new QDialog(parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(title);
    dlg->resize(600, 440);

    auto *vLayout = new QVBoxLayout(dlg);

    auto *statusLabel = new QLabel(QObject::tr("Starting…"), dlg);
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
    auto *copyBtn  = new QPushButton(QObject::tr("Copy log"), dlg);
    hLayout->addWidget(copyBtn);
    hLayout->addStretch();
    auto *closeBtns = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
    QPushButton *closeBtn = closeBtns->button(QDialogButtonBox::Close);
    if (closeBtn) closeBtn->setEnabled(false);
    hLayout->addWidget(closeBtns);
    vLayout->addLayout(hLayout);

    QObject::connect(copyBtn, &QPushButton::clicked, dlg, [logEdit]() {
        QGuiApplication::clipboard()->setText(logEdit->toPlainText());
    });
    QObject::connect(closeBtns, &QDialogButtonBox::rejected, dlg, &QDialog::close);

    h->bar      = progressBar;
    h->log      = logEdit;
    h->status   = statusLabel;
    h->closeBtn = closeBtn;

    return dlg;
}

// ---------------------------------------------------------------------------
// PanePricing
// ---------------------------------------------------------------------------

PanePricing::PanePricing(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PanePricing)
{
    ui->setupUi(this);

    m_ratesModel = new TableCurrencyRates(this);
    ui->tableViewCurrenceRates->setModel(m_ratesModel);
    ui->tableViewCurrenceRates->verticalHeader()->hide();
    ui->tableViewCurrenceRates->horizontalHeader()->setStretchLastSection(true);
    ui->tableViewCurrenceRates->resizeColumnsToContents();

    connect(ui->buttonRefreshRate, &QPushButton::clicked, this, [this]() {
        m_refreshTask = _onRefreshRate();
    });

    connect(ui->buttonRetrieve, &QPushButton::clicked, this, [this]() {
        m_retrieveTask = _onRetrieve();
    });

    connect(ui->buttonUpdate, &QPushButton::clicked, this, [this]() {
        m_updateTask = _onUpdate();
    });

    // Source country combo — populated from the rates model, default DE
    const QString savedSource = QSettings().value(
        QStringLiteral("pricing/sourceMarketplaceId"),
        k_deMarketplaceId).toString();
    for (const TableCurrencyRates::Entry &e : m_ratesModel->entries())
        ui->comboBoxSourceCountry->addItem(e.country, e.marketplaceId);
    {
        const int idx = ui->comboBoxSourceCountry->findData(savedSource);
        ui->comboBoxSourceCountry->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    connect(ui->comboBoxSourceCountry, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        QSettings().setValue(QStringLiteral("pricing/sourceMarketplaceId"),
                             ui->comboBoxSourceCountry->itemData(index).toString());
    });
}

PanePricing::~PanePricing()
{
    delete ui;
}

void PanePricing::setAvailableClis(const QList<AbstractCli *> &clis)
{
    m_availableClis = clis;

    ui->comboBoxCli->blockSignals(true);
    ui->comboBoxCli->clear();
    for (AbstractCli *cli : clis)
        ui->comboBoxCli->addItem(cli->getName(), QVariant::fromValue(cli));

    const QString saved = QSettings().value(QStringLiteral("pricing/selectedCli")).toString();
    int restoredIndex = -1;
    for (int i = 0; i < clis.size(); ++i) {
        if (clis[i]->getName() == saved) { restoredIndex = i; break; }
    }
    ui->comboBoxCli->setCurrentIndex(restoredIndex >= 0 ? restoredIndex : 0);
    ui->comboBoxCli->blockSignals(false);

    connect(ui->comboBoxCli, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        if (index < 0 || index >= m_availableClis.size()) return;
        QSettings().setValue(QStringLiteral("pricing/selectedCli"),
                             m_availableClis[index]->getName());
    });
}

void PanePricing::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_progressDlg)
        m_progressDlg->show();
}

void PanePricing::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    if (m_progressDlg)
        m_progressDlg->hide();
}

// ---------------------------------------------------------------------------
// _onRefreshRate — ask CLI for currency rates, validate, retry on bad format
// ---------------------------------------------------------------------------

QCoro::Task<void> PanePricing::_onRefreshRate()
{
    AbstractCli *cli = ui->comboBoxCli->currentData().value<AbstractCli *>();
    if (!cli) {
        QMessageBox::warning(this, tr("No CLI selected"),
                             tr("Please select a CLI before refreshing rates."));
        co_return;
    }

    ui->buttonRefreshRate->setEnabled(false);

    static constexpr int k_maxAttempts = 3;
    const QString basePrompt = buildBasePrompt();
    QString prompt = basePrompt;

    for (int attempt = 0; attempt < k_maxAttempts; ++attempt) {
        const CliRunResult result = co_await cli->runPrompt(prompt);

        if (!result.processStarted) {
            QMessageBox::critical(this, tr("CLI Error"),
                                  tr("Failed to start '%1'.").arg(cli->getName()));
            break;
        }

        QHash<QString, double> rates;
        const QString parseError = validateRatesJson(extractJson(result.output), &rates);

        if (parseError.isEmpty()) {
            for (int row = 0; row < m_ratesModel->rowCount(); ++row) {
                const QString &cur = m_ratesModel->entries()[row].currency;
                if (rates.contains(cur)) {
                    m_ratesModel->setData(
                        m_ratesModel->index(row, TableCurrencyRates::ColRate),
                        rates[cur]);
                }
            }
            break;
        }

        if (attempt + 1 < k_maxAttempts) {
            prompt = QStringLiteral(
                "Your previous reply was rejected.\n"
                "Error: %1\n\n"
                "Your reply was:\n%2\n\n"
                "%3"
            ).arg(parseError, result.output.trimmed(), basePrompt);
        } else {
            QMessageBox::critical(this, tr("Rate Refresh Failed"),
                tr("Could not obtain valid rates after %1 attempts.\nLast error: %2")
                    .arg(k_maxAttempts).arg(parseError));
        }
    }

    ui->buttonRefreshRate->setEnabled(true);
}

// ---------------------------------------------------------------------------
// _onRetrieve — fetch prices from Amazon for all SKUs, build TablePricing
// ---------------------------------------------------------------------------

QCoro::Task<void> PanePricing::_onRetrieve()
{
    // --- Collect checked countries from the rates table ---
    QList<PricingCountry> countries;
    for (const TableCurrencyRates::Entry &e : m_ratesModel->entries()) {
        if (!e.checked) continue;
        PricingCountry pc;
        pc.marketplaceId = e.marketplaceId;
        pc.country       = e.country;
        pc.currency      = e.currency;
        pc.rate          = e.rate;
        countries.append(pc);
    }

    if (countries.isEmpty()) {
        QMessageBox::information(this, tr("No Countries Selected"),
            tr("Check at least one country in the currency rates table before retrieving prices."));
        co_return;
    }

    // --- Build progress dialog ---
    ProgressDlgHandles h;
    auto *dlg = makeProgressDlg(this, tr("Retrieving prices…"), &h);
    m_progressDlg = dlg;
    dlg->show();

    auto appendLog = [h](const QString &msg) {
        if (!h.log) return;
        const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
        h.log->append(QStringLiteral("[%1] %2").arg(ts, msg));
    };
    auto setStatus = [h](const QString &msg) {
        if (h.status) h.status->setText(msg);
    };

    ui->buttonRetrieve->setEnabled(false);
    ui->buttonUpdate->setEnabled(false);

    // --- Rebuild pricing model with selected countries ---
    delete m_pricingModel;
    m_pricingModel = new TablePricing(countries, this);
    ui->tableViewPricing->setModel(m_pricingModel);
    ui->tableViewPricing->verticalHeader()->hide();
    ui->tableViewPricing->horizontalHeader()->setStretchLastSection(true);

    // --- Build APIs from settings ---
    const auto *st = SettingsTable::instance();
    AmazonPricingApi pricingApi(
        st->value(SettingsTable::KEY_LWA_CLIENT_ID),
        st->value(SettingsTable::KEY_LWA_CLIENT_SECRET),
        st->value(SettingsTable::KEY_EU_LWA_REFRESH_TOKEN),
        st->value(SettingsTable::KEY_NA_LWA_REFRESH_TOKEN),
        st->value(SettingsTable::KEY_EU_SELLER_ID),
        st->value(SettingsTable::KEY_NA_SELLER_ID),
        this);

    AmazonInventoryApi inventoryApi(
        st->value(SettingsTable::KEY_LWA_CLIENT_ID),
        st->value(SettingsTable::KEY_LWA_CLIENT_SECRET),
        st->value(SettingsTable::KEY_EU_LWA_REFRESH_TOKEN),
        st->value(SettingsTable::KEY_EU_SELLER_ID),
        k_deMarketplaceId,
        this);

    // --- Fetch all EU FBA inventory via Reports API ---
    setStatus(tr("Fetching EU FBA inventory report…"));
    appendLog(tr("→ Requesting FBA inventory report (may take 1–2 min)…"));

    QList<AmazonInventoryApi::InventorySummary> inventorySummaries;
    co_await inventoryApi.fetchFbaInventoryReport(QStringList(), &inventorySummaries, appendLog);
    appendLog(tr("  ✓ %1 SKU(s) in FBA inventory").arg(inventorySummaries.size()));

    // --- Per-SKU: DE price + per-country current price ---
    const int totalSkus = inventorySummaries.size();
    for (int skuIdx = 0; skuIdx < totalSkus; ++skuIdx) {
        const auto &summary = inventorySummaries[skuIdx];
        const QString &sku  = summary.sku;
        const int      euQty = summary.available;

        setStatus(tr("SKU %1/%2: %3 — fetching DE price…")
                      .arg(skuIdx + 1).arg(totalSkus).arg(sku));

        // Step 1: fetch EUR base price + product type from amazon.de
        double  eurPrice    = -1.0;
        bool    existsDE    = false;
        QString productType;
        co_await pricingApi.fetchListingPrice(k_deMarketplaceId, sku,
                                              &eurPrice, &existsDE, &productType);

        if (!existsDE) {
            appendLog(tr("  ✗ %1 — not listed on amazon.de, skipped").arg(sku));
            continue;
        }

        m_pricingModel->setBaseData(sku, euQty, eurPrice, productType);
        appendLog(tr("→ %1 | DE price: %2 EUR | EU qty: %3 | type: %4")
                      .arg(sku)
                      .arg(eurPrice, 0, 'f', 2)
                      .arg(euQty)
                      .arg(productType.isEmpty() ? QStringLiteral("(unknown)") : productType));

        // Step 2: fetch current price on each checked country
        for (int i = 0; i < countries.size(); ++i) {
            const PricingCountry &pc = countries[i];

            // Reuse DE data when the checked country is also amazon.de
            if (pc.marketplaceId == k_deMarketplaceId) {
                m_pricingModel->setCountryData(sku, i, eurPrice, existsDE);
                appendLog(QStringLiteral("    %1: %2 %3 (DE, reused)")
                              .arg(pc.country)
                              .arg(eurPrice, 0, 'f', 2)
                              .arg(pc.currency));
                continue;
            }

            setStatus(tr("SKU %1/%2: %3 — fetching %4 price…")
                          .arg(skuIdx + 1).arg(totalSkus).arg(sku).arg(pc.country));

            double currentPrice = -1.0;
            bool   exists       = false;
            co_await pricingApi.fetchListingPrice(pc.marketplaceId, sku,
                                                  &currentPrice, &exists);
            m_pricingModel->setCountryData(sku, i, currentPrice, exists);

            if (!exists) {
                appendLog(QStringLiteral("    %1: not listed").arg(pc.country));
            } else {
                appendLog(QStringLiteral("    %1: %2 %3")
                              .arg(pc.country)
                              .arg(currentPrice, 0, 'f', 2)
                              .arg(pc.currency));
            }
        }
    }

    ui->tableViewPricing->resizeColumnsToContents();

    setStatus(tr("Done."));
    if (h.bar) h.bar->setRange(0, 1);
    if (h.closeBtn) h.closeBtn->setEnabled(true);
    appendLog(tr("─── Retrieve complete ───"));

    ui->buttonRetrieve->setEnabled(true);
    ui->buttonUpdate->setEnabled(true);
    m_progressDlg = nullptr;
}

// ---------------------------------------------------------------------------
// _onUpdate — PATCH new prices to Amazon for all listed SKUs where price changed
// ---------------------------------------------------------------------------

QCoro::Task<void> PanePricing::_onUpdate()
{
    if (!m_pricingModel || m_pricingModel->rowCount() == 0) {
        QMessageBox::information(this, tr("No Data"),
            tr("Run 'Retrieve' first to load pricing data before updating."));
        co_return;
    }

    // --- Build progress dialog ---
    ProgressDlgHandles h;
    auto *dlg = makeProgressDlg(this, tr("Updating prices…"), &h);
    m_progressDlg = dlg;
    dlg->show();

    auto appendLog = [h](const QString &msg) {
        if (!h.log) return;
        const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
        h.log->append(QStringLiteral("[%1] %2").arg(ts, msg));
    };
    auto setStatus = [h](const QString &msg) {
        if (h.status) h.status->setText(msg);
    };

    ui->buttonRetrieve->setEnabled(false);
    ui->buttonUpdate->setEnabled(false);

    // --- Build pricing API ---
    const auto *st = SettingsTable::instance();
    AmazonPricingApi pricingApi(
        st->value(SettingsTable::KEY_LWA_CLIENT_ID),
        st->value(SettingsTable::KEY_LWA_CLIENT_SECRET),
        st->value(SettingsTable::KEY_EU_LWA_REFRESH_TOKEN),
        st->value(SettingsTable::KEY_NA_LWA_REFRESH_TOKEN),
        st->value(SettingsTable::KEY_EU_SELLER_ID),
        st->value(SettingsTable::KEY_NA_SELLER_ID),
        this);

    const QString sourceMarketplaceId =
        ui->comboBoxSourceCountry->currentData().toString();

    const QList<TablePricing::Row>            &rows      = m_pricingModel->rows();
    const QList<PricingCountry>               &countries = m_pricingModel->countries();

    int updatedCount = 0;
    int failedCount  = 0;
    int skippedCount = 0;

    const int totalRows = rows.size();
    for (int ri = 0; ri < totalRows; ++ri) {
        const TablePricing::Row &row = rows[ri];

        for (int ci = 0; ci < countries.size(); ++ci) {
            const PricingCountry              &pc   = countries[ci];
            const TablePricing::CountryData   &cd   = row.countries[ci];

            if (pc.marketplaceId == sourceMarketplaceId) {
                appendLog(QStringLiteral("  ⊘ %1 / %2: source country, never updated")
                              .arg(row.sku, pc.country));
                continue;
            }

            if (!cd.exists) {
                appendLog(QStringLiteral("  – %1 / %2: not listed, skipped")
                              .arg(row.sku, pc.country));
                continue;
            }

            if (row.eurPrice < 0.0) {
                appendLog(QStringLiteral("  – %1 / %2: no EUR base price, skipped")
                              .arg(row.sku, pc.country));
                continue;
            }

            if (cd.currentPrice < 0.0) {
                appendLog(QStringLiteral("  – %1 / %2: current price unknown, skipped")
                              .arg(row.sku, pc.country));
                continue;
            }

            const double newPrice = qRound(row.eurPrice * pc.rate * 100.0) / 100.0;
            const double diff     = qAbs(newPrice - cd.currentPrice);

            if (diff < 0.005) {
                appendLog(QStringLiteral("  ≈ %1 / %2: no change (%3 %4)")
                              .arg(row.sku, pc.country)
                              .arg(cd.currentPrice, 0, 'f', 2)
                              .arg(pc.currency));
                ++skippedCount;
                continue;
            }

            setStatus(tr("Updating %1 / %2…").arg(row.sku, pc.country));
            appendLog(QStringLiteral("  → %1 / %2: %3 → %4 %5")
                          .arg(row.sku, pc.country)
                          .arg(cd.currentPrice, 0, 'f', 2)
                          .arg(newPrice, 0, 'f', 2)
                          .arg(pc.currency));

            bool success = false;
            co_await pricingApi.patchListingPrice(pc.marketplaceId, row.sku,
                                                  row.productType, pc.currency,
                                                  newPrice, &success);

            if (success) {
                appendLog(QStringLiteral("    ✓ Updated"));
                ++updatedCount;
            } else {
                appendLog(QStringLiteral("    ✗ Failed: %1").arg(pricingApi.lastError()));
                ++failedCount;
            }
        }
    }

    const QString summary = tr("─── Update complete: %1 updated, %2 failed, %3 no change ───")
                                .arg(updatedCount).arg(failedCount).arg(skippedCount);
    appendLog(summary);
    setStatus(tr("Done — %1 updated, %2 failed").arg(updatedCount).arg(failedCount));

    if (h.bar) h.bar->setRange(0, 1);
    if (h.closeBtn) h.closeBtn->setEnabled(true);

    ui->buttonRetrieve->setEnabled(true);
    ui->buttonUpdate->setEnabled(true);
    m_progressDlg = nullptr;
}
