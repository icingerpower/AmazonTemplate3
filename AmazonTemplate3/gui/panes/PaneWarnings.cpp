#pragma GCC optimize("O1")
#include "PaneWarnings.h"
#include "ui_PaneWarnings.h"
#include "AmazonMarketplace.h"
#include "Attribute.h"
#include "AttributeFlagsTable.h"
#include "SettingsTable.h"
#include "apis/AmazonCatalogApi.h"
#include "apis/AmazonWarningsApi.h"
#include "TreeProductWarnings.h"
#include "gui/DialogClassificationTypes.h"

#include <algorithm>

#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHash>
#include <QSet>
#include <QTextStream>
#include <QFontDatabase>
#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QComboBox>
#include <QLabel>
#include <QStyledItemDelegate>
#include <QLineEdit>
#include <QListWidgetItem>
#include <QTableView>
#include <QTableWidgetItem>
#include <QTreeView>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QInputDialog>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QTextEdit>
#include <QUrl>
#include <QVBoxLayout>

#include <QCoro/QCoroNetworkReply>
#include <QCoro/QCoroSignal>

#include <xlsxdocument.h>

// Attribute IDs / issue codes that cannot be fixed by AI and belong in the
// excluded table: image content flags and GSPR regulatory compliance notices.
static bool isBlacklisted(const QString &attributeId)
{
    static const QSet<QString> kBlacklist = {
        QStringLiteral("100232"), // Sexual/child content detected in images
        QStringLiteral("100527"), // GSPR — manufacturer information required
        QStringLiteral("100528"), // GSPR — responsible person information required
    };
    if (kBlacklist.contains(attributeId))
        return true;

    // Any attribute ID consisting purely of digits is a numeric issue code (GSPR,
    // image-moderation, etc.) — these cannot be fixed by AI, so blacklist them.
    static const QRegularExpression kNumericRe(QStringLiteral("^\\d+$"));
    return kNumericRe.match(attributeId).hasMatch();
}

// Parse warnings pasted from Amazon Seller Central's Account Health page.
// Anchors on "ASIN: <10-char>" lines; maps the violation type to an attributeId.
static QList<WarningRow> parsePastedWarnings(const QString &text)
{
    static const QHash<QString, QString> kViolationAttr = {
        {QStringLiteral("Bullet Point Removed"),        QStringLiteral("bullet_point")},
        {QStringLiteral("Product Description Removed"), QStringLiteral("product_description")},
    };
    static const QRegularExpression kAsinRe(QStringLiteral("^ASIN:\\s*([A-Z0-9]{10})$"));
    static const QRegularExpression kDateRe(
        QStringLiteral("^(?:Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec)\\s+\\d{1,2},\\s+\\d{4}$"));

    QStringList lines;
    for (const QString &l : text.split(QLatin1Char('\n'))) {
        const QString t = l.trimmed();
        if (!t.isEmpty()) lines.append(t);
    }

    // Skip optional header block (up to and including "Next steps")
    int start = 0;
    for (int i = 0; i < lines.size(); ++i) {
        if (lines.at(i) == QStringLiteral("Next steps")) {
            start = i + 1;
            break;
        }
    }

    QList<WarningRow> result;

    for (int i = start; i < lines.size(); ++i) {
        const auto m = kAsinRe.match(lines.at(i));
        if (!m.hasMatch()) continue;

        const QString asin = m.captured(1);

        // Title: line immediately before ASIN, skip if it looks like a date/header
        QString title;
        if (i >= 1) {
            const QString candidate = lines.at(i - 1);
            if (!kDateRe.match(candidate).hasMatch()
                && candidate != QStringLiteral("What was affected?")
                && !candidate.startsWith(QStringLiteral("Product Attribute")))
                title = candidate;
        }

        // Violation: i+2 (skip at-risk sales at i+1); fallback to i+1
        QString attributeId;
        QString issueMessage;
        for (int offset : {2, 1}) {
            const int idx = i + offset;
            if (idx >= lines.size()) continue;
            const QString candidate = lines.at(idx);
            if (kViolationAttr.contains(candidate)) {
                attributeId  = kViolationAttr.value(candidate);
                issueMessage = candidate;
                break;
            }
        }

        if (attributeId.isEmpty()) continue; // unknown violation, skip

        WarningRow row;
        row.asin         = asin;
        row.title        = title;
        row.attributeId  = attributeId;
        row.issueMessage = issueMessage;
        result.append(row);
    }

    return result;
}

// ---------------------------------------------------------------------------
// WarningsValueDelegate — shows a QComboBox for AI-value cells when a list of
// valid values is available; falls back to the default QLineEdit otherwise.
// ---------------------------------------------------------------------------

class WarningsValueDelegate : public QStyledItemDelegate
{
public:
    explicit WarningsValueDelegate(QObject *parent = nullptr)
        : QStyledItemDelegate(parent) {}

    void setValidValues(const QHash<QString, QStringList> &values)
    { m_validValues = values; }

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override
    {
        if (!index.internalPointer())
            return QStyledItemDelegate::createEditor(parent, option, index); // parent rows unchanged

        auto *vn = static_cast<TreeProductWarnings::ViolationNode *>(index.internalPointer());
        if (!vn || index.row() < 0 || index.row() >= vn->children.size())
            return QStyledItemDelegate::createEditor(parent, option, index);

        const TreeProductWarnings::ChildNode &child = vn->children[index.row()];
        if (child.isCurrentValue) return nullptr; // "Current value" rows: never editable

        // Both the label column (ColAttribute) and the value column (ColError)
        // open an editor — clicking either one edits the value stored in ColError.
        const int col = index.column();
        if (col != TreeProductWarnings::ColAttribute && col != TreeProductWarnings::ColError)
            return nullptr;

        const QStringList values = m_validValues.value(vn->row.attributeId.toLower());
        if (!values.isEmpty()) {
            auto *combo = new QComboBox(parent);
            combo->setEditable(true);
            combo->setInsertPolicy(QComboBox::NoInsert);
            combo->addItems(values);
            return combo;
        }

        // Free-text: plain line editor, but only on the value column.
        if (col == TreeProductWarnings::ColError)
            return QStyledItemDelegate::createEditor(parent, option, index);
        return nullptr;
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        // Value lives in ColError — redirect reads from ColAttribute.
        const QModelIndex src = (index.column() == TreeProductWarnings::ColAttribute)
            ? index.siblingAtColumn(TreeProductWarnings::ColError) : index;
        const QString cur = src.data(Qt::EditRole).toString();
        if (auto *combo = qobject_cast<QComboBox *>(editor)) {
            const int idx = combo->findText(cur);
            combo->setCurrentIndex(idx >= 0 ? idx : 0);
            if (idx < 0) combo->setCurrentText(cur);
        } else {
            QStyledItemDelegate::setEditorData(editor, src);
        }
    }

    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override
    {
        // Always write to ColError.
        const QModelIndex target = (index.column() == TreeProductWarnings::ColAttribute)
            ? index.siblingAtColumn(TreeProductWarnings::ColError) : index;
        if (auto *combo = qobject_cast<QComboBox *>(editor))
            model->setData(target, combo->currentText(), Qt::EditRole);
        else
            QStyledItemDelegate::setModelData(editor, model, target);
    }

private:
    QHash<QString, QStringList> m_validValues;
};

PaneWarnings::PaneWarnings(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PaneWarnings)
{
    ui->setupUi(this);

    m_model = new TreeProductWarnings(this);
    ui->tableViewWarnings->setModel(m_model);
    ui->tableViewWarnings->header()->setStretchLastSection(true);
    ui->tableViewWarnings->setRootIsDecorated(true);
    ui->tableViewWarnings->setUniformRowHeights(false);
    ui->tableViewWarnings->setAlternatingRowColors(true);

    m_valueDelegate = new WarningsValueDelegate(this);
    ui->tableViewWarnings->setItemDelegate(m_valueDelegate);

    ui->tableWidgetExcluded->horizontalHeader()->setStretchLastSection(true);
    ui->tableWidgetExcluded->verticalHeader()->hide();
    ui->tableWidgetExcluded->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->tableWidgetExcluded->setSelectionBehavior(QAbstractItemView::SelectRows);

    // Top (actionable) gets 2/3, bottom (excluded) gets 1/3.
    ui->splitterVertical->setStretchFactor(0, 2);
    ui->splitterVertical->setStretchFactor(1, 1);

    _populateMarketplaces();
    _connectSlots();
    _loadSettings();
}

PaneWarnings::~PaneWarnings()
{
    delete ui;
}

void PaneWarnings::setWorkingDir(const QDir &workingDir)
{
    m_workingDir = workingDir;
    delete m_flagsTable;
    m_flagsTable = new AttributeFlagsTable(m_workingDir.absolutePath(), this);
    m_model->setWorkingDir(m_workingDir.absolutePath());
    m_classificationMap.load(m_workingDir.absolutePath());
    _loadSettings();
}

void PaneWarnings::setAvailableClis(const QList<AbstractCli *> &clis)
{
    m_availableClis = clis;

    ui->comboBoxCli->blockSignals(true);
    ui->comboBoxCli->clear();
    for (AbstractCli *cli : clis)
        ui->comboBoxCli->addItem(cli->getName(), QVariant::fromValue(cli));

    int defaultIndex = 0;
    for (int i = 0; i < clis.size(); ++i) {
        if (clis[i]->canGenImages()) { defaultIndex = i; break; }
    }

    const QString saved = QSettings().value(QStringLiteral("warnings/selectedCli")).toString();
    int restoredIndex = -1;
    for (int i = 0; i < clis.size(); ++i) {
        if (clis[i]->getName() == saved) { restoredIndex = i; break; }
    }
    ui->comboBoxCli->setCurrentIndex(restoredIndex >= 0 ? restoredIndex : defaultIndex);
    ui->comboBoxCli->blockSignals(false);

    connect(ui->comboBoxCli, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        if (index < 0 || index >= m_availableClis.size()) return;
        QSettings().setValue(QStringLiteral("warnings/selectedCli"),
                             m_availableClis[index]->getName());
    });
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void PaneWarnings::_populateMarketplaces()
{
    struct RegionInfo { AmazonMarketplace::Region region; QString label; };
    static const QList<RegionInfo> REGIONS = {
        { AmazonMarketplace::Region::Europe,       "Europe"        },
        { AmazonMarketplace::Region::NorthAmerica, "North America" },
        { AmazonMarketplace::Region::Japan,        "Japan"         },
    };

    for (const RegionInfo &ri : REGIONS) {
        auto *sep = new QListWidgetItem(ri.label);
        sep->setFlags(Qt::NoItemFlags);
        QFont f = sep->font();
        f.setBold(true);
        sep->setFont(f);
        ui->listWidgetAmazon->addItem(sep);

        for (const AmazonMarketplace *mp : AmazonMarketplace::forRegion(ri.region)) {
            auto *item = new QListWidgetItem(
                QString("%1 (%2)").arg(mp->countryName(), mp->countryCode()));
            item->setData(Qt::UserRole, mp->countryCode());
            ui->listWidgetAmazon->addItem(item);
        }
    }

    for (int i = 0; i < ui->listWidgetAmazon->count(); ++i) {
        QListWidgetItem *item = ui->listWidgetAmazon->item(i);
        if (item->flags() & Qt::ItemIsSelectable) {
            ui->listWidgetAmazon->setCurrentItem(item);
            break;
        }
    }
}

void PaneWarnings::_loadSettings()
{
    const QString saved = QSettings().value(QStringLiteral("warnings/templateFolder")).toString();
    ui->lineEditTemplateFolder->setText(saved);
}

AmazonWarningsApi *PaneWarnings::_api()
{
    if (!m_api) {
        auto *st = SettingsTable::instance();
        m_api = new AmazonWarningsApi(
            st->value(SettingsTable::KEY_LWA_CLIENT_ID),
            st->value(SettingsTable::KEY_LWA_CLIENT_SECRET),
            st->value(SettingsTable::KEY_EU_LWA_REFRESH_TOKEN),
            st->value(SettingsTable::KEY_NA_LWA_REFRESH_TOKEN),
            st->value(SettingsTable::KEY_JP_LWA_REFRESH_TOKEN),
            st->value(SettingsTable::KEY_EU_SELLER_ID),
            st->value(SettingsTable::KEY_NA_SELLER_ID),
            st->value(SettingsTable::KEY_JP_SELLER_ID),
            this);
    }
    return m_api;
}

AmazonCatalogApi *PaneWarnings::_catalogApi()
{
    if (!m_catalogApi) {
        auto *st = SettingsTable::instance();
        m_catalogApi = new AmazonCatalogApi(
            st->value(SettingsTable::KEY_LWA_CLIENT_ID),
            st->value(SettingsTable::KEY_LWA_CLIENT_SECRET),
            st->value(SettingsTable::KEY_EU_LWA_REFRESH_TOKEN),
            st->value(SettingsTable::KEY_NA_LWA_REFRESH_TOKEN),
            st->value(SettingsTable::KEY_JP_LWA_REFRESH_TOKEN),
            st->value(SettingsTable::KEY_EU_SELLER_ID),
            st->value(SettingsTable::KEY_NA_SELLER_ID),
            st->value(SettingsTable::KEY_JP_SELLER_ID),
            st->value(SettingsTable::KEY_IMGBB_API_KEY),
            this);
    }
    return m_catalogApi;
}

QNetworkAccessManager *PaneWarnings::_imageNam()
{
    if (!m_imageNam) {
        m_imageNam = new QNetworkAccessManager(this);
        m_imageNam->setTransferTimeout(30'000);
    }
    return m_imageNam;
}

QString PaneWarnings::_selectedMarketplaceId() const
{
    QListWidgetItem *item = ui->listWidgetAmazon->currentItem();
    if (!item || !(item->flags() & Qt::ItemIsSelectable))
        return {};
    const QString countryCode = item->data(Qt::UserRole).toString();
    const AmazonMarketplace *mp = AmazonMarketplace::forCountryCode(countryCode);
    return mp ? mp->marketplaceId() : QString{};
}

void PaneWarnings::_downloadMainImage(const QString &url, const QString &asin, const QString &/*mktSubdir*/)
{
    if (url.isEmpty() || asin.isEmpty()) return;

    // Shared path — not per-marketplace, so the same ASIN is only downloaded once
    // even when it appears in violations for multiple marketplaces.
    m_workingDir.mkpath(QStringLiteral("warnings/%1").arg(asin));
    const QString path = m_workingDir.filePath(
        QStringLiteral("warnings/%1/%1_main.jpg").arg(asin));

    if (QFileInfo::exists(path)) return;

    QNetworkReply *reply = _imageNam()->get(QNetworkRequest{QUrl(url)});
    connect(reply, &QNetworkReply::finished, this, [reply, path]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;
        QFile f(path);
        if (f.open(QIODevice::WriteOnly))
            f.write(reply->readAll());
    });
}

QCoro::Task<void> PaneWarnings::_onLoadWarnings()
{
    QListWidgetItem *selItem = ui->listWidgetAmazon->currentItem();
    if (!selItem || !(selItem->flags() & Qt::ItemIsSelectable)) co_return;
    const QString countryCodeUpper = selItem->data(Qt::UserRole).toString();
    const AmazonMarketplace *mp = AmazonMarketplace::forCountryCode(countryCodeUpper);
    if (!mp) co_return;
    const QString marketplaceId = mp->marketplaceId();
    const QString mktSubdir     = QStringLiteral("warnings/%1").arg(countryCodeUpper.toLower());

    // -------------------------------------------------------------------
    // Build progress dialog (same pattern as PaneSizing)
    // -------------------------------------------------------------------
    auto *progressDlg = new QDialog(parentWidget());
    progressDlg->setAttribute(Qt::WA_DeleteOnClose);
    // Non-modal: PaneWarnings is disabled via setEnabled(false); other panes stay active.
    progressDlg->setWindowTitle(tr("Loading warnings… [%1]").arg(countryCodeUpper));
    progressDlg->resize(560, 400);
    auto *pLayout = new QVBoxLayout(progressDlg);

    auto *statusLabel = new QLabel(tr("Starting…"), progressDlg);
    QFont boldFont = statusLabel->font();
    boldFont.setBold(true);
    statusLabel->setFont(boldFont);
    pLayout->addWidget(statusLabel);

    auto *progressBar = new QProgressBar(progressDlg);
    progressBar->setRange(0, 0); // indeterminate until step 2
    pLayout->addWidget(progressBar);

    auto *logEdit = new QTextEdit(progressDlg);
    logEdit->setReadOnly(true);
    logEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    pLayout->addWidget(logEdit);

    auto *btnLayout = new QHBoxLayout();
    auto *copyBtn = new QPushButton(tr("Copy log"), progressDlg);
    btnLayout->addWidget(copyBtn);
    btnLayout->addStretch();
    auto *closeBtns = new QDialogButtonBox(QDialogButtonBox::Close, progressDlg);
    QPushButton *closeBtn = closeBtns->button(QDialogButtonBox::Close);
    if (closeBtn) closeBtn->setEnabled(false);
    btnLayout->addWidget(closeBtns);
    pLayout->addLayout(btnLayout);

    QPointer<QLabel>       statusLabelPtr(statusLabel);
    QPointer<QProgressBar> progressBarPtr(progressBar);
    QPointer<QTextEdit>    logEditPtr(logEdit);
    QPointer<QPushButton>  closeBtnPtr(closeBtn);

    auto appendLog = [logEditPtr](const QString &line) {
        if (!logEditPtr) return;
        const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
        logEditPtr->append(QStringLiteral("[%1] %2").arg(ts, line));
    };

    connect(copyBtn, &QPushButton::clicked, progressDlg, [logEditPtr]() {
        if (logEditPtr)
            QGuiApplication::clipboard()->setText(logEditPtr->toPlainText());
    });
    connect(closeBtns, &QDialogButtonBox::rejected, progressDlg, &QDialog::close);

    AmazonWarningsApi *api = _api();

    connect(api, &AmazonWarningsApi::logMessage, progressDlg,
            [statusLabelPtr, appendLog](const QString &msg) {
        if (statusLabelPtr) statusLabelPtr->setText(msg);
        appendLog(msg);
    });
    connect(api, &AmazonWarningsApi::progressChanged, progressDlg,
            [progressBarPtr](int current, int total) {
        if (!progressBarPtr) return;
        progressBarPtr->setRange(0, total);
        progressBarPtr->setValue(current);
    });

    m_progressDlg = progressDlg;
    progressDlg->show();
    setEnabled(false);

    ui->buttonLoadWarnings->setEnabled(false);
    m_model->clear();
    ui->tableWidgetExcluded->setRowCount(0);

    // -------------------------------------------------------------------
    // Load upload records: ASIN,fieldId,dateSent (CSV, one per line).
    // Entries older than 4 days are ignored and pruned from the file.
    // -------------------------------------------------------------------
    m_workingDir.mkpath(mktSubdir);
    const QString cursorPath = m_workingDir.filePath(mktSubdir + QStringLiteral("/processed_asins.txt"));

    // key = "ASIN\tfield_id" for uploads sent < 4 days ago
    QSet<QString> recentlyUploaded;
    {
        const QDate cutoff = QDate::currentDate().addDays(-4);
        QFile f(cursorPath);
        QStringList validLines; // lines to keep (within 4-day window)
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&f);
            while (!in.atEnd()) {
                const QString line = in.readLine().trimmed();
                const QStringList parts = line.split(QLatin1Char(','));
                if (parts.size() < 3) continue;
                const QDate dateSent = QDate::fromString(parts[2].trimmed(), Qt::ISODate);
                if (!dateSent.isValid()) continue;
                if (dateSent >= cutoff) {
                    recentlyUploaded.insert(parts[0].trimmed() + QLatin1Char('\t')
                                            + parts[1].trimmed());
                    validLines.append(line);
                }
                // entries older than 4 days are silently dropped (pruning)
            }
        }
        // Rewrite the file with only the still-valid entries (compact on every load).
        if (f.isOpen()) f.close();
        if (!validLines.isEmpty() || f.exists()) {
            QSaveFile sf(cursorPath);
            if (sf.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream out(&sf);
                for (const QString &l : std::as_const(validLines))
                    out << l << '\n';
                sf.commit();
            }
        }
    }

    // -------------------------------------------------------------------
    // Run the API call — dialog stays live and responsive during co_await
    // -------------------------------------------------------------------
    QList<WarningRow> violations;
    co_await api->fetchViolations(marketplaceId, &violations,
                                  ui->spinBoxNumberWarnings->value());

    // Auto-disconnected when progressDlg is destroyed; disconnect now so a
    // subsequent load doesn't double-connect if the dialog is still open.
    disconnect(api, &AmazonWarningsApi::logMessage,       progressDlg, nullptr);
    disconnect(api, &AmazonWarningsApi::progressChanged,  progressDlg, nullptr);

    // -------------------------------------------------------------------
    // Filter out violations for ASIN+fieldId combos uploaded within 4 days
    // -------------------------------------------------------------------
    if (!recentlyUploaded.isEmpty()) {
        violations.erase(
            std::remove_if(violations.begin(), violations.end(),
                [&recentlyUploaded](const WarningRow &r) {
                    return recentlyUploaded.contains(
                        r.asin + QLatin1Char('\t') + r.attributeId);
                }),
            violations.end());
    }

    // -------------------------------------------------------------------
    // Populate tables: actionable rows → main model;
    //   blacklisted or NoAI-flagged → excluded (bottom) table
    // -------------------------------------------------------------------
    /*
    auto isNoAi = [this](const QString &attrId) {
        if (!m_flagsTable) return false;
        return m_flagsTable->hasFlag(QStringLiteral("Amazon V01"), attrId, Attribute::NoAI)
            || m_flagsTable->hasFlag(QStringLiteral("Amazon V02"), attrId, Attribute::NoAI);
    };
    //*/

    for (const WarningRow &row : violations) {
        if (isBlacklisted(row.attributeId) /* || isNoAi(row.attributeId) //*/) {
            const int r = ui->tableWidgetExcluded->rowCount();
            ui->tableWidgetExcluded->insertRow(r);
            ui->tableWidgetExcluded->setItem(r, 0, new QTableWidgetItem(row.asin));
            ui->tableWidgetExcluded->setItem(r, 1, new QTableWidgetItem(row.sku));
            ui->tableWidgetExcluded->setItem(r, 2, new QTableWidgetItem(row.attributeId));
            ui->tableWidgetExcluded->setItem(r, 3, new QTableWidgetItem(row.value));
        } else {
            m_workingDir.mkpath(QStringLiteral("%1/%2").arg(mktSubdir, row.asin));
            _downloadMainImage(row.mainImageUrl, row.asin, mktSubdir);
            m_model->addRow(row);
        }
    }

    const int loadedCount     = m_model->violationCount();
    const int suppressedCount = static_cast<int>(recentlyUploaded.size());
    appendLog(tr("Loaded %1 violation row(s) into table.").arg(violations.size()));
    ui->labelWarningsInfo->setText(
        tr("%1 warning(s) loaded  •  %2 suppressed (uploaded < 4 days)")
        .arg(loadedCount).arg(suppressedCount));

    // -------------------------------------------------------------------
    // Pre-fetch valid enum values so the combo box delegate works immediately
    // (independent of whether Ask AI is run).
    // -------------------------------------------------------------------
    if (m_model->violationCount() > 0) {
        appendLog(tr("Fetching valid values for combo-box editing…"));
        if (statusLabelPtr) statusLabelPtr->setText(tr("Fetching valid values…"));

        // Collect unique scalar attribute IDs from the loaded violations.
        QStringList attrIds;
        QSet<QString> seen;
        QString firstSku;
        for (int vi = 0; vi < m_model->violationCount(); ++vi) {
            const auto *vn = m_model->violationAt(vi);
            if (!vn) continue;
            if (firstSku.isEmpty()) firstSku = vn->row.sku;
            const QString id = vn->row.attributeId;
            // Skip bullet_point and product_description (no enum, free-text).
            if (id.compare(QStringLiteral("bullet_point"), Qt::CaseInsensitive) == 0) continue;
            if (id.contains(QStringLiteral("product_description"), Qt::CaseInsensitive)) continue;
            if (!seen.contains(id.toLower())) { seen.insert(id.toLower()); attrIds.append(id); }
        }

        QString productType;
        if (!firstSku.isEmpty())
            co_await api->fetchListingProductType(marketplaceId, firstSku, &productType);

        if (!productType.isEmpty()) {
            appendLog(tr("  productType = %1 — fetching %2 attribute schema(s)…")
                      .arg(productType).arg(attrIds.size()));

            // Re-connect API log pump for this step.
            auto logConn2 = connect(api, &AmazonWarningsApi::logMessage, progressDlg,
                [statusLabelPtr, appendLog](const QString &msg) {
                    if (statusLabelPtr) statusLabelPtr->setText(msg);
                    appendLog(msg);
                });

            QHash<QString, QStringList> apiEnums;
            for (const QString &id : std::as_const(attrIds)) {
                QStringList vals;
                co_await api->fetchAttributeEnumValues(marketplaceId, productType, id, &vals);
                if (!vals.isEmpty()) apiEnums.insert(id.toLower(), vals);
            }

            disconnect(logConn2);

            // Merge with any template values already in m_validValues (from a
            // prior Ask AI run), API wins on conflict.
            for (auto it = apiEnums.constBegin(); it != apiEnums.constEnd(); ++it)
                m_validValues.insert(it.key(), it.value());
            if (m_valueDelegate) m_valueDelegate->setValidValues(m_validValues);

            appendLog(tr("  Combo-box values ready for %1 attribute(s).").arg(m_validValues.size()));
        } else {
            appendLog(tr("  ⚠ Could not determine product type — combo box will use text input."));
        }
    }

    if (statusLabelPtr) statusLabelPtr->setText(tr("Done."));
    if (closeBtnPtr)    closeBtnPtr->setEnabled(true);

    ui->tableViewWarnings->expandAll();
    for (int col = 0; col < TreeProductWarnings::ColCount; ++col)
        ui->tableViewWarnings->resizeColumnToContents(col);
    ui->tableViewWarnings->header()->setStretchLastSection(true);
    ui->buttonLoadWarnings->setEnabled(true);
    setEnabled(true);
}

// ---------------------------------------------------------------------------
// _onPasteWarnings — user pastes warnings, parse them, enrich via API.
// ---------------------------------------------------------------------------

QCoro::Task<void> PaneWarnings::_onPasteWarnings()
{
    // 1. Input dialog: user pastes the warnings text
    QDialog inputDlg(this);
    inputDlg.setWindowTitle(tr("Paste warnings from Amazon Seller Central"));
    inputDlg.resize(720, 500);
    auto *dlgLayout = new QVBoxLayout(&inputDlg);
    auto *label = new QLabel(
        tr("Paste warnings copied from Account Health → Product Compliance:"), &inputDlg);
    dlgLayout->addWidget(label);
    auto *textEdit = new QTextEdit(&inputDlg);
    textEdit->setPlaceholderText(tr("Paste here…"));
    dlgLayout->addWidget(textEdit);
    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &inputDlg);
    dlgLayout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &inputDlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &inputDlg, &QDialog::reject);

    if (inputDlg.exec() != QDialog::Accepted) co_return;

    const QString pasteText = textEdit->toPlainText();
    if (pasteText.trimmed().isEmpty()) co_return;

    // 2. Parse
    QList<WarningRow> rows = parsePastedWarnings(pasteText);
    if (rows.isEmpty()) {
        QMessageBox::information(this, tr("No warnings found"),
            tr("Could not recognize any warnings in the pasted text.\n\n"
               "Expected format: paste from Amazon Account Health → Product Compliance."));
        co_return;
    }

    // 3. Validate marketplace selection
    QListWidgetItem *selItem = ui->listWidgetAmazon->currentItem();
    if (!selItem || !(selItem->flags() & Qt::ItemIsSelectable)) {
        QMessageBox::warning(this, tr("No marketplace"),
            tr("Please select a marketplace first."));
        co_return;
    }
    const QString countryCode = selItem->data(Qt::UserRole).toString();
    const AmazonMarketplace *mp = AmazonMarketplace::forCountryCode(countryCode);
    if (!mp) co_return;
    const QString marketplaceId = mp->marketplaceId();
    const QString mktSubdir     = QStringLiteral("warnings/%1").arg(countryCode.toLower());

    // 4. Progress dialog (same pattern as _onLoadWarnings)
    auto *progressDlg = new QDialog(parentWidget());
    progressDlg->setAttribute(Qt::WA_DeleteOnClose);
    progressDlg->setWindowTitle(tr("Fetching listing data… [%1]").arg(countryCode));
    progressDlg->resize(560, 400);
    auto *pLayout = new QVBoxLayout(progressDlg);

    auto *statusLabel = new QLabel(tr("Starting…"), progressDlg);
    QFont boldFont = statusLabel->font();
    boldFont.setBold(true);
    statusLabel->setFont(boldFont);
    pLayout->addWidget(statusLabel);

    auto *progressBar = new QProgressBar(progressDlg);
    progressBar->setRange(0, 0);
    pLayout->addWidget(progressBar);

    auto *logEdit = new QTextEdit(progressDlg);
    logEdit->setReadOnly(true);
    logEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    pLayout->addWidget(logEdit);

    auto *btnLayout2 = new QHBoxLayout();
    auto *copyBtn = new QPushButton(tr("Copy log"), progressDlg);
    btnLayout2->addWidget(copyBtn);
    btnLayout2->addStretch();
    auto *closeBtns = new QDialogButtonBox(QDialogButtonBox::Close, progressDlg);
    QPushButton *closeBtn = closeBtns->button(QDialogButtonBox::Close);
    if (closeBtn) closeBtn->setEnabled(false);
    btnLayout2->addWidget(closeBtns);
    pLayout->addLayout(btnLayout2);

    QPointer<QLabel>       statusLabelPtr(statusLabel);
    QPointer<QProgressBar> progressBarPtr(progressBar);
    QPointer<QTextEdit>    logEditPtr(logEdit);
    QPointer<QPushButton>  closeBtnPtr(closeBtn);

    auto appendLog = [logEditPtr](const QString &line) {
        if (!logEditPtr) return;
        const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
        logEditPtr->append(QStringLiteral("[%1] %2").arg(ts, line));
    };

    connect(copyBtn, &QPushButton::clicked, progressDlg, [logEditPtr]() {
        if (logEditPtr) QGuiApplication::clipboard()->setText(logEditPtr->toPlainText());
    });
    connect(closeBtns, &QDialogButtonBox::rejected, progressDlg, &QDialog::close);

    AmazonWarningsApi *api = _api();

    connect(api, &AmazonWarningsApi::logMessage, progressDlg,
            [statusLabelPtr, appendLog](const QString &msg) {
        if (statusLabelPtr) statusLabelPtr->setText(msg);
        appendLog(msg);
    });
    connect(api, &AmazonWarningsApi::progressChanged, progressDlg,
            [progressBarPtr](int current, int total) {
        if (!progressBarPtr) return;
        progressBarPtr->setRange(0, total);
        progressBarPtr->setValue(current);
    });

    m_progressDlg = progressDlg;
    progressDlg->show();
    setEnabled(false);

    appendLog(tr("Parsed %1 warning(s) from paste.").arg(rows.size()));

    // 5. Enrich rows with SKU + listing data via API
    co_await api->enrichPastedRows(marketplaceId, &rows);

    disconnect(api, &AmazonWarningsApi::logMessage,      progressDlg, nullptr);
    disconnect(api, &AmazonWarningsApi::progressChanged, progressDlg, nullptr);

    // Detect enrichment failure: if every row still has an empty SKU the FBA
    // report step failed (FATAL, rate-limit, etc.).  Rows are still added so
    // Ask AI can run, but warn the user that Upload will not work.
    const bool enrichmentFailed = std::none_of(rows.constBegin(), rows.constEnd(),
                                               [](const WarningRow &r){ return !r.sku.isEmpty(); });
    if (enrichmentFailed) {
        appendLog(tr("⚠ SKU enrichment failed (FBA report error — see log above). "
                     "Rows will be added with no SKU: Ask AI works, Upload will not. "
                     "Wait ~15 min before retrying to avoid the report rate limit."));
        if (statusLabelPtr)
            statusLabelPtr->setText(tr("⚠ Enrichment failed — rows added without SKU"));
    }

    // 6. Add to model (append — do not clear existing rows)
    int added = 0;
    for (const WarningRow &row : rows) {
        if (isBlacklisted(row.attributeId)) {
            const int r = ui->tableWidgetExcluded->rowCount();
            ui->tableWidgetExcluded->insertRow(r);
            ui->tableWidgetExcluded->setItem(r, 0, new QTableWidgetItem(row.asin));
            ui->tableWidgetExcluded->setItem(r, 1, new QTableWidgetItem(row.sku));
            ui->tableWidgetExcluded->setItem(r, 2, new QTableWidgetItem(row.attributeId));
            ui->tableWidgetExcluded->setItem(r, 3, new QTableWidgetItem(row.value));
        } else {
            m_workingDir.mkpath(QStringLiteral("%1/%2").arg(mktSubdir, row.asin));
            _downloadMainImage(row.mainImageUrl, row.asin, mktSubdir);
            m_model->addRow(row);
            ++added;
        }
    }

    appendLog(tr("Added %1 row(s) to the warnings table.").arg(added));
    if (statusLabelPtr) statusLabelPtr->setText(tr("Done."));
    if (closeBtnPtr)    closeBtnPtr->setEnabled(true);

    ui->tableViewWarnings->expandAll();
    for (int col = 0; col < TreeProductWarnings::ColCount; ++col)
        ui->tableViewWarnings->resizeColumnToContents(col);
    ui->tableViewWarnings->header()->setStretchLastSection(true);

    setEnabled(true);
}

// ---------------------------------------------------------------------------
// _onAskAi — iterate non-AI-filled rows and run them through the selected CLI
// ---------------------------------------------------------------------------

namespace {

static bool isBulletPointAttr(const QString &attrId) {
    return attrId.compare(QStringLiteral("bullet_point"), Qt::CaseInsensitive) == 0;
}

static bool isDescriptionAttr(const QString &attrId) {
    return attrId.contains(QStringLiteral("product_description"), Qt::CaseInsensitive);
}

// Shared bullet extractor — strips markdown fences, handles object or array
// JSON wrapper, falls back to line splitting. Returns up to 5 bullet strings.
static QStringList extractBullets(const QString &raw)
{
    QString jsonText = raw.trimmed();
    static const QRegularExpression kFence(
        QStringLiteral("```(?:json)?\\s*\\n?([\\s\\S]*?)\\n?```"),
        QRegularExpression::MultilineOption);
    const auto fm = kFence.match(jsonText);
    if (fm.hasMatch()) jsonText = fm.captured(1).trimmed();

    QStringList bullets;
    auto fromObj = [&](const QJsonObject &obj) {
        const QJsonArray arr = obj.value(QStringLiteral("bullet_points")).toArray();
        for (const QJsonValue &v : arr) {
            if (bullets.size() >= 5) break;
            const QString s = v.toString().trimmed();
            if (!s.isEmpty()) bullets.append(s);
        }
    };
    const QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8());
    if (doc.isObject())
        fromObj(doc.object());
    else if (doc.isArray() && !doc.array().isEmpty())
        fromObj(doc.array().first().toObject());

    if (bullets.size() < 5) {
        // Line-split fallback — skip JSON syntax / fence lines
        bullets.clear();
        for (const QString &line : raw.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            if (bullets.size() >= 5) break;
            const QString t = line.trimmed();
            if (t.isEmpty() || t.startsWith(QLatin1String("```"))
                || t == QLatin1String("{") || t == QLatin1String("}")
                || t == QLatin1String("[") || t == QLatin1String("]"))
                continue;
            bullets.append(t);
        }
    }
    return bullets;
}

// Scan all .xlsx files in folderPath, collect unique non-empty values per attributeId.
// Returns a map: attributeId (lower-cased) → sorted unique values (max 30 per attr).
// Uses QXlsx::Document, same as the rest of the codebase.
static QHash<QString, QStringList> scanTemplatesForValidValues(const QString &folderPath)
{
    QHash<QString, QStringList> result;
    if (folderPath.isEmpty()) return result;

    const QDir dir(folderPath);
    if (!dir.exists()) return result;

    for (const QString &fileName : dir.entryList({QStringLiteral("*.xlsx")}, QDir::Files)) {
        QXlsx::Document doc(dir.filePath(fileName));
        if (!doc.load()) continue;

        // Find the header row: scan rows 1..10 for the row with the most non-empty cells.
        int headerRow = -1;
        int maxCells = 0;
        for (int r = 1; r <= 10; ++r) {
            int count = 0;
            for (int c = 1; c <= 50; ++c) {
                const QString v = doc.read(r, c).toString().trimmed();
                if (!v.isEmpty()) ++count;
            }
            if (count > maxCells) { maxCells = count; headerRow = r; }
        }
        if (headerRow < 0 || maxCells < 3) continue;

        // Map column index → attribute ID (lower-cased).
        QHash<int, QString> colToAttr;
        for (int c = 1; c <= 200; ++c) {
            const QString header = doc.read(headerRow, c).toString().trimmed().toLower();
            if (!header.isEmpty()) colToAttr[c] = header;
        }
        if (colToAttr.isEmpty()) continue;

        // Collect values from data rows (skip header row + up to 3 metadata rows).
        const int firstDataRow = headerRow + 3;
        const int lastRow = doc.dimension().lastRow();
        for (int r = firstDataRow; r <= lastRow; ++r) {
            for (auto it = colToAttr.constBegin(); it != colToAttr.constEnd(); ++it) {
                const QString val = doc.read(r, it.key()).toString().trimmed();
                if (val.isEmpty()) continue;
                QStringList &list = result[it.value()];
                if (!list.contains(val) && list.size() < 30)
                    list.append(val);
            }
        }
    }
    return result;
}

struct AiRow {
    int     violIdx;
    QString asin;
    QString attributeId;
    QString prompt;
    QString workDir;
    // Optional: if set, called with the raw output. If it returns a non-empty
    // string that is the reformat prompt, doAskAiRows makes one additional call
    // before advancing — onDone then sees the reformatted output.
    std::function<QString(const QString &rawOutput)> reformatPromptFn;
};

using AiRowDoneFn = std::function<void(int step, int total,
                                       const AiRow &row, CliRunResult)>;

// Recursive free function — same pattern as PaneSizing's doRunSequentially.
// Supports an optional reformat call: if row.reformatPromptFn returns a
// non-empty string, a second CLI call is made and onDone sees that result.
static void doAskAiRows(AbstractCli *cli,
                         QPointer<PaneWarnings> self,
                         QList<AiRow> rows,
                         int step, int total,
                         AiRowDoneFn onDone)
{
    if (!self || rows.isEmpty()) {
        if (onDone) onDone(total + 1, total, {}, {});
        return;
    }
    AiRow row = rows.takeFirst();
    cli->runPromptAsync(row.prompt, row.workDir, self,
        [self, cli, rows, row, step, total, onDone](CliRunResult result) mutable {
            if (!self) return;

            // Check if a reformat pass is needed.
            if (row.reformatPromptFn) {
                const QString reformatPrompt = row.reformatPromptFn(result.output);
                if (!reformatPrompt.isEmpty()) {
                    cli->runPromptAsync(reformatPrompt, row.workDir, self,
                        [self, cli, rows, row, step, total, onDone](CliRunResult r2) mutable {
                            if (!self) return;
                            if (onDone) onDone(step, total, row, r2);
                            doAskAiRows(cli, self, std::move(rows), step + 1, total, onDone);
                        });
                    return; // wait for the reformat call before advancing
                }
            }

            if (onDone) onDone(step, total, row, result);
            doAskAiRows(cli, self, std::move(rows), step + 1, total, onDone);
        });
}

// Returns false for known error / rate-limit messages that must never be stored
// or displayed as AI-generated content.
static bool isValidAiValue(const QString &value)
{
    const QString lower = value.toLower();
    return !lower.contains(QStringLiteral("session limit"))
        && !lower.contains(QStringLiteral("rate limit"))
        && !lower.contains(QStringLiteral("you've hit your"));
}

} // namespace

void PaneWarnings::_loadAiCache(const QString &cc)
{
    if (m_aiCacheCc == cc) return;
    m_aiCacheCc = cc;
    m_aiValueCache.clear();
    if (cc.isEmpty()) return;
    const QString path = m_workingDir.filePath(
        QStringLiteral("warnings/%1/ai_value_cache.json").arg(cc.toLower()));
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
        m_aiValueCache.insert(it.key(), it.value().toString());
}

void PaneWarnings::_saveAiCache() const
{
    if (m_aiCacheCc.isEmpty()) return;
    const QString dirRel = QStringLiteral("warnings/%1").arg(m_aiCacheCc.toLower());
    m_workingDir.mkpath(dirRel);
    const QString path = m_workingDir.filePath(dirRel + QStringLiteral("/ai_value_cache.json"));
    QJsonObject obj;
    for (auto it = m_aiValueCache.constBegin(); it != m_aiValueCache.constEnd(); ++it)
        obj.insert(it.key(), it.value());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(obj).toJson());
}

// ---------------------------------------------------------------------------
QCoro::Task<void> PaneWarnings::_onAskAi()
{
    AbstractCli *cli = ui->comboBoxCli->currentData().value<AbstractCli *>();
    if (!cli) {
        QMessageBox::warning(this, tr("No CLI"),
                             tr("Please select a CLI in the combobox first."));
        QTimer::singleShot(0, this, [this]() { emit askAiFinished(); });
        co_return;
    }

    QListWidgetItem *selItem = ui->listWidgetAmazon->currentItem();
    if (!selItem || !(selItem->flags() & Qt::ItemIsSelectable)) {
        QMessageBox::warning(this, tr("No marketplace"),
                             tr("Please select a marketplace first."));
        QTimer::singleShot(0, this, [this]() { emit askAiFinished(); });
        co_return;
    }
    const QString countryCode = selItem->data(Qt::UserRole).toString();
    const QString marketplaceId = _selectedMarketplaceId();

    // -------------------------------------------------------------------
    // Pass 1 — collect the indices of violations that need AI processing.
    // We also gather the unique attribute IDs so we can pre-fetch schemas.
    // -------------------------------------------------------------------
    struct CandidateRow {
        int     violIdx;
        QString asin;
        QString sku;
        QString attributeId;
        QString title;
        QString value;
        QString issueMessage;
        QStringList bulletPoints;
    };
    QList<CandidateRow> candidates;
    QStringList uniqueAttrIds;     // preserves first-seen order
    QSet<QString>  seenAttrIds;
    QString firstSku;              // used to fetch the productType

    _loadAiCache(countryCode);

    for (int vi = 0; vi < m_model->violationCount(); ++vi) {
        const TreeProductWarnings::ViolationNode *vn = m_model->violationAt(vi);
        if (!vn) continue;
        const WarningRow &wr = vn->row;

        // Skip attributes flagged NoAI in either Amazon marketplace variant.
        if (m_flagsTable) {
            const bool noAi =
                m_flagsTable->hasFlag(QStringLiteral("Amazon V01"), wr.attributeId, Attribute::NoAI) ||
                m_flagsTable->hasFlag(QStringLiteral("Amazon V02"), wr.attributeId, Attribute::NoAI);
            if (noAi) continue;
        }

        // Skip rows the user has unchecked in the "Ask AI" column.
        if (!m_model->isAskAi(vi)) continue;

        // Skip if every AI child already has a value.
        bool anyEmpty = false;
        for (const auto &child : vn->children) {
            if (!child.isCurrentValue && child.aiValue.isEmpty()) {
                anyEmpty = true;
                break;
            }
        }
        if (!anyEmpty) continue;

        // Restore from AI value cache — avoids re-calling AI for inactive listings
        const QString cacheKey = wr.asin + QLatin1Char(':') + wr.attributeId;
        if (m_aiValueCache.contains(cacheKey)) {
            const QString cached = m_aiValueCache.value(cacheKey);
            if (isBulletPointAttr(wr.attributeId)) {
                const QStringList bullets = cached.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
                for (int i = 0; i < bullets.size() && i < 5; ++i)
                    m_model->setAiValue(vi, i, bullets[i]);
            } else {
                m_model->setAiValue(vi, 0, cached);
            }
            continue; // skip AI call for this row
        }

        candidates.append({vi, wr.asin, wr.sku, wr.attributeId, wr.title,
                           wr.value, wr.issueMessage, wr.bulletPoints});

        if (firstSku.isEmpty()) firstSku = wr.sku;

        // Only collect schema enums for "scalar" attributes (not bullets/description).
        if (!isBulletPointAttr(wr.attributeId) && !isDescriptionAttr(wr.attributeId)) {
            if (!seenAttrIds.contains(wr.attributeId)) {
                seenAttrIds.insert(wr.attributeId);
                uniqueAttrIds.append(wr.attributeId);
            }
        }
    }

    if (candidates.isEmpty()) {
        QMessageBox::information(this, tr("Ask AI"),
                                 tr("All rows already have AI suggestions."));
        QTimer::singleShot(0, this, [this]() { emit askAiFinished(); });
        co_return;
    }

    // Truncate by spinBoxNumberAskingAi value (0 == unlimited).
    {
        const int maxRows = ui->spinBoxNumberAskingAi->value();
        if (maxRows > 0 && candidates.size() > maxRows)
            candidates = candidates.mid(0, maxRows);
    }

    // -------------------------------------------------------------------
    // Build progress dialog (same layout as _onLoadWarnings).
    // Shown BEFORE the pre-fetch step so the user sees per-attribute logs.
    // -------------------------------------------------------------------
    auto *progressDlg = new QDialog(parentWidget());
    progressDlg->setAttribute(Qt::WA_DeleteOnClose);
    // Non-modal: PaneWarnings is disabled via setEnabled(false); other panes stay active.
    progressDlg->setWindowTitle(tr("Asking AI… [%1]").arg(countryCode));
    progressDlg->resize(560, 400);
    auto *pLayout = new QVBoxLayout(progressDlg);

    auto *statusLabel = new QLabel(tr("Preparing…"), progressDlg);
    QFont boldFont = statusLabel->font();
    boldFont.setBold(true);
    statusLabel->setFont(boldFont);
    pLayout->addWidget(statusLabel);

    auto *progressBar = new QProgressBar(progressDlg);
    progressBar->setRange(0, 0); // indeterminate during pre-fetch
    pLayout->addWidget(progressBar);

    auto *logEdit = new QTextEdit(progressDlg);
    logEdit->setReadOnly(true);
    logEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    pLayout->addWidget(logEdit);

    auto *btnLayout = new QHBoxLayout();
    auto *copyBtn = new QPushButton(tr("Copy log"), progressDlg);
    btnLayout->addWidget(copyBtn);
    btnLayout->addStretch();
    auto *closeBtns = new QDialogButtonBox(QDialogButtonBox::Close, progressDlg);
    QPushButton *closeBtn = closeBtns->button(QDialogButtonBox::Close);
    if (closeBtn) closeBtn->setEnabled(false);
    btnLayout->addWidget(closeBtns);
    pLayout->addLayout(btnLayout);

    QPointer<QLabel>       statusLabelPtr(statusLabel);
    QPointer<QProgressBar> progressBarPtr(progressBar);
    QPointer<QTextEdit>    logEditPtr(logEdit);
    QPointer<QPushButton>  closeBtnPtr(closeBtn);

    auto appendLog = [logEditPtr](const QString &line) {
        if (!logEditPtr) return;
        const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
        logEditPtr->append(QStringLiteral("[%1] %2").arg(ts, line));
    };

    connect(copyBtn, &QPushButton::clicked, progressDlg, [logEditPtr]() {
        if (logEditPtr)
            QGuiApplication::clipboard()->setText(logEditPtr->toPlainText());
    });
    connect(closeBtns, &QDialogButtonBox::rejected, progressDlg, &QDialog::close);

    m_progressDlg = progressDlg;
    progressDlg->show();
    setEnabled(false);
    ui->buttonAskAi->setEnabled(false);

    // Pipe API log messages into the progress dialog for the duration of this
    // call. The connection is removed before we return.
    AmazonWarningsApi *api = _api();
    auto logConn = connect(api, &AmazonWarningsApi::logMessage, progressDlg,
        [statusLabelPtr, appendLog](const QString &msg) {
            if (statusLabelPtr) statusLabelPtr->setText(msg);
            appendLog(msg);
        });

    // -------------------------------------------------------------------
    // Step A — resolve productType (needed by Product Type Definitions API).
    // Prefer the value already stored in the row (captured at load time); only
    // make an API call when it is absent (e.g. rows imported via paste without
    // a successful enrichment).
    // -------------------------------------------------------------------
    QString productType;
    {
        const auto *firstVn = m_model->violationAt(0);
        if (firstVn) productType = firstVn->row.productType;
    }
    if (productType.isEmpty() && !marketplaceId.isEmpty() && !firstSku.isEmpty()) {
        appendLog(tr("Fetching product type for SKU %1…").arg(firstSku));
        co_await api->fetchListingProductType(marketplaceId, firstSku, &productType);
    }
    if (productType.isEmpty()) {
        appendLog(tr("  ⚠ Could not determine product type — continuing without API enums."));
    } else {
        appendLog(tr("  productType = %1").arg(productType));
    }

    // -------------------------------------------------------------------
    // Step B — pre-fetch enum values from the schema for each unique attrId.
    // -------------------------------------------------------------------
    QHash<QString, QStringList> apiValidValues;
    if (!productType.isEmpty() && !marketplaceId.isEmpty()) {
        for (const QString &attrId : std::as_const(uniqueAttrIds)) {
            appendLog(tr("Fetching valid values for: %1").arg(attrId));
            QStringList apiEnums;
            co_await api->fetchAttributeEnumValues(marketplaceId, productType, attrId, &apiEnums);
            apiValidValues.insert(attrId.toLower(), apiEnums);
        }
    }

    // -------------------------------------------------------------------
    // Step C — scan template folder for additional valid values.
    // -------------------------------------------------------------------
    appendLog(tr("Scanning template folder…"));
    const QString templateFolder = ui->lineEditTemplateFolder->text().trimmed();
    QHash<QString, QStringList> templateValues = scanTemplatesForValidValues(templateFolder);
    appendLog(tr("  Found %1 attribute(s) with values in templates").arg(templateValues.size()));

    // Merge API enums + template values into m_validValues (API wins on conflict).
    // Update the delegate so manual edits on unchecked rows get the combo.
    m_validValues = templateValues;
    for (auto it = apiValidValues.constBegin(); it != apiValidValues.constEnd(); ++it)
        m_validValues.insert(it.key(), it.value()); // API overrides template for same key
    if (m_valueDelegate)
        m_valueDelegate->setValidValues(m_validValues);

    // Disconnect the API log pump — subsequent log messages come from the
    // CLI loop and we don't want stray API logs interleaved.
    disconnect(logConn);

    // -------------------------------------------------------------------
    // Step D — build the prompts now that we have the valid-value lookups.
    // -------------------------------------------------------------------
    QList<AiRow> rows;
    rows.reserve(candidates.size());

    for (const CandidateRow &c : std::as_const(candidates)) {
        const QString workDir = m_workingDir.filePath(
            QStringLiteral("warnings/%1/%2").arg(countryCode.toLower(), c.asin));
        QString prompt;

        if (isBulletPointAttr(c.attributeId)) {
            QString currentBullets;
            if (!c.bulletPoints.isEmpty()) {
                for (int b = 0; b < c.bulletPoints.size(); ++b)
                    currentBullets += QStringLiteral("%1. %2\n").arg(b + 1).arg(c.bulletPoints[b]);
            } else if (!c.value.isEmpty()) {
                currentBullets = c.value;
            }
            prompt = QStringLiteral(
                "You are an expert in writing Amazon product pages. The following product "
                "has bullet point violations.\n\n"
                "Product title: %1\nASIN: %2\n\n"
                "Amazon violation message: \"%3\"\n\n"
                "Current bullet points:\n%4\n"
                "Write 5 improved, Amazon-compliant bullet points that increase perceived value "
                "while respecting Amazon policies. Always add exactly one emoji at the beginning "
                "of each bullet point. Do not invent information; only state verifiable facts.\n\n"
                "Output a valid JSON object with a single key \"bullet_points\" containing an "
                "array of exactly 5 strings.\n"
                "Example: {\"bullet_points\": [\"\xe2\x9c\xa8 Point 1\", \"\xf0\x9f\x8e\xaf Point 2\", \"\xf0\x9f\x92\xaa Point 3\", "
                "\"\xf0\x9f\x8c\x9f Point 4\", \"\xe2\x9c\x85 Point 5\"]}")
                .arg(c.title, c.asin, c.issueMessage, currentBullets);
        } else if (isDescriptionAttr(c.attributeId)) {
            prompt = QStringLiteral(
                "You are fixing an Amazon product listing's description.\n"
                "\n"
                "Product title: %1\n"
                "ASIN: %2\n"
                "\n"
                "Amazon violation message: \"%3\"\n"
                "\n"
                "Current description: \"%4\"\n"
                "\n"
                "The description has a violation. Please provide an improved, "
                "Amazon-compliant product description (plain text, 1\xe2\x80\x932 paragraphs, "
                "no bullet points, no HTML).\n"
                "Respond with ONLY the new description text, nothing else.")
                .arg(c.title, c.asin, c.issueMessage, c.value);
        } else {
            // Combine API enums + template values, prefer API enums (authoritative).
            QStringList validValues = apiValidValues.value(c.attributeId.toLower());
            if (validValues.isEmpty())
                validValues = templateValues.value(c.attributeId.toLower());

            if (!validValues.isEmpty()) {
                const QString joined = validValues.join(QStringLiteral("\n- "))
                    .prepend(QStringLiteral("- "));
                prompt = QStringLiteral(
                    "You are fixing an Amazon listing attribute violation.\n\n"
                    "Product title: %1\nASIN: %2\n\n"
                    "Violated attribute ID: %3\n"
                    "Current value: \"%4\"\n"
                    "Amazon violation: \"%5\"\n\n"
                    "Valid values for this attribute (choose the most appropriate one):\n%6\n\n"
                    "Respond with ONLY one value from the list above, exactly as written. "
                    "If none is appropriate, respond: UNKNOWN")
                    .arg(c.title, c.asin, c.attributeId, c.value, c.issueMessage, joined);
            } else {
                prompt = QStringLiteral(
                    "You are fixing an Amazon listing attribute violation.\n\n"
                    "Product title: %1\nASIN: %2\n\n"
                    "Violated attribute ID: %3\n"
                    "Current value: \"%4\"\n"
                    "Amazon violation: \"%5\"\n\n"
                    "Respond with ONLY the corrected value \xe2\x80\x94 no explanation, no formatting, no quotes. "
                    "If you cannot determine the correct value, respond: UNKNOWN")
                    .arg(c.title, c.asin, c.attributeId, c.value, c.issueMessage);
            }
        }

        std::function<QString(const QString &)> reformatFn;
        if (isBulletPointAttr(c.attributeId)) {
            reformatFn = [](const QString &raw) -> QString {
                if (extractBullets(raw).size() >= 5) return {};
                return QStringLiteral(
                    "The following text should contain 5 bullet points for an Amazon product "
                    "listing, each starting with an emoji. Extract them and return ONLY a valid "
                    "JSON object with this exact format:\n"
                    "{\"bullet_points\": [\"bullet1\", \"bullet2\", \"bullet3\", "
                    "\"bullet4\", \"bullet5\"]}\n\n"
                    "Text to process:\n%1").arg(raw);
            };
        }
        rows.append({c.violIdx, c.asin, c.attributeId, prompt, workDir, reformatFn});
    }

    // -------------------------------------------------------------------
    // Step E — switch progress bar to determinate mode and kick off the CLI.
    // -------------------------------------------------------------------
    const int total = rows.size();
    if (progressBarPtr) {
        progressBarPtr->setRange(0, total);
        progressBarPtr->setValue(0);
    }
    appendLog(tr("Starting AI on %1 row(s)…").arg(total));

    // onDone callback: called for each completed row, and once with sentinel
    // step == total+1 when all rows are done.
    auto onDone = [this, statusLabelPtr, progressBarPtr, logEditPtr, closeBtnPtr, total, appendLog]
        (int step, int /*total2*/, const AiRow &row, CliRunResult r) mutable
    {
        // Sentinel: all done.
        if (step == total + 1) {
            if (statusLabelPtr) statusLabelPtr->setText(tr("Done."));
            if (closeBtnPtr)    closeBtnPtr->setEnabled(true);
            ui->buttonAskAi->setEnabled(true);
            setEnabled(true);
            ui->tableViewWarnings->expandAll();

            // Count how many AI value cells are now filled and update the status label.
            int filled = 0;
            for (int vi = 0; vi < m_model->violationCount(); ++vi) {
                const auto *vn = m_model->violationAt(vi);
                if (!vn) continue;
                for (const auto &child : vn->children)
                    if (!child.isCurrentValue && !child.aiValue.isEmpty())
                        ++filled;
            }
            const QString existing = ui->labelWarningsInfo->text();
            if (existing.isEmpty())
                ui->labelWarningsInfo->setText(tr("%1 AI value(s) filled").arg(filled));
            else
                ui->labelWarningsInfo->setText(
                    existing + tr("  •  %1 AI value(s) filled").arg(filled));
            emit askAiFinished();
            return;
        }

        const QString answer = r.output.trimmed();
        if (!answer.isEmpty() && answer != QStringLiteral("UNKNOWN") && isValidAiValue(answer)) {
            if (isBulletPointAttr(row.attributeId)) {
                const QStringList bullets = extractBullets(answer);
                for (int i = 0; i < bullets.size() && i < 5; ++i)
                    m_model->setAiValue(row.violIdx, i, bullets[i]);
                appendLog(QStringLiteral("[%1/%2] %3 / %4 \xe2\x86\x92 %5 bullet(s) stored")
                          .arg(step).arg(total).arg(row.asin, row.attributeId)
                          .arg(bullets.size()));
            } else {
                m_model->setAiValue(row.violIdx, 0, answer);
                appendLog(QStringLiteral("[%1/%2] %3 / %4 \xe2\x86\x92 %5")
                          .arg(step).arg(total).arg(row.asin, row.attributeId, answer));
            }

            ui->tableViewWarnings->expandAll();
        } else {
            if (!answer.isEmpty() && answer != QStringLiteral("UNKNOWN"))
                appendLog(QStringLiteral("[%1/%2] %3 / %4 \xe2\x86\x92 IGNORED (session/rate-limit message)")
                          .arg(step).arg(total).arg(row.asin, row.attributeId));
            else
                appendLog(QStringLiteral("[%1/%2] %3 / %4 \xe2\x86\x92 UNKNOWN")
                          .arg(step).arg(total).arg(row.asin, row.attributeId));
        }
        if (progressBarPtr) progressBarPtr->setValue(step);
        if (statusLabelPtr)
            statusLabelPtr->setText(tr("[%1/%2] %3 / %4")
                                    .arg(step).arg(total).arg(row.asin, row.attributeId));
    };

    doAskAiRows(cli, this, std::move(rows), 1, total, std::move(onDone));
    co_return;
}

// ---------------------------------------------------------------------------
// _onUpload — push aiValue back to Amazon via Listings Items PATCH
// ---------------------------------------------------------------------------

QCoro::Task<void> PaneWarnings::_onUpload()
{
    const QString marketplaceId = _selectedMarketplaceId();
    if (marketplaceId.isEmpty()) {
        QMessageBox::warning(this, tr("No marketplace"),
                             tr("Please select a marketplace first."));
        co_return;
    }

    // Collect (violIdx, attributeId, value) tuples to upload. Each non-empty
    // AI child produces one upload item. For bullet_point violations, all 5
    // bullets are collected and joined with CELL_SEP / newline before sending.
    struct UploadItem {
        int     violIdx;
        QString sku;
        QString asin;
        QString attributeId;
        QString value;
        QString marketplaceId; // defaults to the user-selected marketplace
        QString productType;   // captured at load time; avoids re-fetching for inactive listings
        QString classificationId;          // browseClassification.classificationId (when productType unavailable)
        QString classificationDisplayName; // browseClassification.displayName
        QString title;                     // product title (for the unknown-classification dialog)
    };
    QList<UploadItem> toUpload;

    for (int vi = 0; vi < m_model->violationCount(); ++vi) {
        const TreeProductWarnings::ViolationNode *vn = m_model->violationAt(vi);
        if (!vn) continue;
        const WarningRow &wr = vn->row;

        if (isBulletPointAttr(wr.attributeId)) {
            // Collect non-empty bullet AI values, in order. Join with \n.
            QStringList bullets;
            for (const auto &child : vn->children) {
                if (child.isCurrentValue) continue;
                if (!child.aiValue.isEmpty()) bullets.append(child.aiValue);
            }
            if (!bullets.isEmpty()) {
                toUpload.append({vi, wr.sku, wr.asin, wr.attributeId,
                                 bullets.join(QLatin1Char('\n')), marketplaceId,
                                 wr.productType, wr.classificationId,
                                 wr.classificationDisplayName, wr.title});
            }
        } else {
            // Single AI child expected; upload first non-empty.
            for (const auto &child : vn->children) {
                if (child.isCurrentValue) continue;
                if (!child.aiValue.isEmpty()) {
                    toUpload.append({vi, wr.sku, wr.asin, wr.attributeId,
                                     child.aiValue, marketplaceId, wr.productType,
                                     wr.classificationId, wr.classificationDisplayName,
                                     wr.title});
                    break;
                }
            }
        }
    }

    if (toUpload.isEmpty()) {
        QMessageBox::information(this, tr("Upload"),
                                 tr("No AI suggestions to upload."));
        co_return;
    }

    // ----------------------------------------------------------------------
    // Expand the upload list based on per-row "All siblings" / "All countries"
    // checkboxes. Use a "ASIN\tAttributeId\tMarketplaceId" key set to dedupe.
    // ----------------------------------------------------------------------
    auto _dedupeKey = [](const UploadItem &it) {
        return it.asin + QLatin1Char('\t') + it.attributeId
             + QLatin1Char('\t') + it.marketplaceId;
    };

    QSet<QString> seenKeys;
    for (const UploadItem &it : std::as_const(toUpload))
        seenKeys.insert(_dedupeKey(it));

    // Snapshot the seed list before iterating so we can grow toUpload as we go.
    const QList<UploadItem> seedList = toUpload;

    for (const UploadItem &seed : seedList) {
        const bool wantSiblings  = m_model->isAllSiblings(seed.violIdx);
        const bool wantCountries = m_model->isAllCountries(seed.violIdx);

        // --- Step 1: find sibling violations (other vi with same attributeId)
        QList<UploadItem> siblingExpansions;
        if (wantSiblings) {
            for (int vi = 0; vi < m_model->violationCount(); ++vi) {
                if (vi == seed.violIdx) continue;
                const TreeProductWarnings::ViolationNode *vn = m_model->violationAt(vi);
                if (!vn) continue;
                const WarningRow &wr = vn->row;
                if (wr.sku.isEmpty()) continue;
                if (wr.attributeId.compare(seed.attributeId, Qt::CaseInsensitive) != 0) continue;

                UploadItem sib = seed;
                sib.violIdx     = vi;
                sib.sku         = wr.sku;
                sib.asin        = wr.asin;
                sib.productType = wr.productType.isEmpty() ? seed.productType : wr.productType;
                sib.classificationId          = wr.classificationId;
                sib.classificationDisplayName = wr.classificationDisplayName;
                sib.title                     = wr.title;
                // attributeId, value, marketplaceId all carry over from the seed.

                const QString key = _dedupeKey(sib);
                if (seenKeys.contains(key)) continue;
                seenKeys.insert(key);
                siblingExpansions.append(sib);
            }
            for (const UploadItem &s : std::as_const(siblingExpansions))
                toUpload.append(s);
        }

        // --- Step 2: replicate seed + sibling rows across other marketplaces.
        if (wantCountries) {
            // Collect candidate marketplaces (those with a usable seller ID).
            QStringList extraMarketplaces;
            for (const AmazonMarketplace &mp : AmazonMarketplace::all()) {
                const QString mid = mp.marketplaceId();
                if (mid == seed.marketplaceId) continue;
                if (_api()->sellerIdForMarketplace(mid).isEmpty()) continue;
                extraMarketplaces.append(mid);
            }

            // Replicate over seed itself.
            QList<UploadItem> base;
            base.append(seed);
            for (const UploadItem &s : std::as_const(siblingExpansions))
                base.append(s);

            for (const QString &mid : std::as_const(extraMarketplaces)) {
                for (const UploadItem &b : std::as_const(base)) {
                    UploadItem dup = b;
                    dup.marketplaceId = mid;
                    const QString key = _dedupeKey(dup);
                    if (seenKeys.contains(key)) continue;
                    seenKeys.insert(key);
                    toUpload.append(dup);
                }
            }
        }
    }

    // -------------------------------------------------------------------
    // Build progress dialog (same pattern)
    // -------------------------------------------------------------------
    auto *progressDlg = new QDialog(parentWidget());
    progressDlg->setAttribute(Qt::WA_DeleteOnClose);
    // Non-modal: PaneWarnings is disabled via setEnabled(false); other panes stay active.
    {
        const AmazonMarketplace *selMp = AmazonMarketplace::forMarketplaceId(marketplaceId);
        const QString selCc = selMp ? selMp->countryCode() : marketplaceId;
        progressDlg->setWindowTitle(tr("Uploading fixes… [%1]").arg(selCc));
    }
    progressDlg->resize(560, 400);
    auto *pLayout = new QVBoxLayout(progressDlg);

    auto *statusLabel = new QLabel(tr("Starting…"), progressDlg);
    QFont boldFont = statusLabel->font();
    boldFont.setBold(true);
    statusLabel->setFont(boldFont);
    pLayout->addWidget(statusLabel);

    auto *progressBar = new QProgressBar(progressDlg);
    progressBar->setRange(0, toUpload.size());
    progressBar->setValue(0);
    pLayout->addWidget(progressBar);

    auto *logEdit = new QTextEdit(progressDlg);
    logEdit->setReadOnly(true);
    logEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    pLayout->addWidget(logEdit);

    auto *btnLayout = new QHBoxLayout();
    auto *copyBtn = new QPushButton(tr("Copy log"), progressDlg);
    btnLayout->addWidget(copyBtn);
    btnLayout->addStretch();
    auto *closeBtns = new QDialogButtonBox(QDialogButtonBox::Close, progressDlg);
    QPushButton *closeBtn = closeBtns->button(QDialogButtonBox::Close);
    if (closeBtn) closeBtn->setEnabled(false);
    btnLayout->addWidget(closeBtns);
    pLayout->addLayout(btnLayout);

    QPointer<QLabel>       statusLabelPtr(statusLabel);
    QPointer<QProgressBar> progressBarPtr(progressBar);
    QPointer<QTextEdit>    logEditPtr(logEdit);
    QPointer<QPushButton>  closeBtnPtr(closeBtn);

    auto appendLog = [logEditPtr](const QString &line) {
        if (!logEditPtr) return;
        const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
        logEditPtr->append(QStringLiteral("[%1] %2").arg(ts, line));
    };

    connect(copyBtn, &QPushButton::clicked, progressDlg, [logEditPtr]() {
        if (logEditPtr)
            QGuiApplication::clipboard()->setText(logEditPtr->toPlainText());
    });
    connect(closeBtns, &QDialogButtonBox::rejected, progressDlg, &QDialog::close);

    m_progressDlg = progressDlg;
    progressDlg->show();
    setEnabled(false);

    ui->buttonUpload->setEnabled(false);

    AmazonWarningsApi *api = _api();
    const int total = toUpload.size();
    int processed   = 0;

    // Detect local image file paths (POSIX abs path, ~/, Windows drive, or any
    // existing file ending in .jpg/.jpeg/.png).
    static const QRegularExpression kImagePath(
        QStringLiteral("(?i)^(?:[~/]|[A-Za-z]:\\\\).+\\.(?:jpe?g|png)$"));

    // Load AI value cache for this marketplace so we can write to it on SKIP
    // and clear it on successful upload.
    {
        const AmazonMarketplace *selMp = AmazonMarketplace::forMarketplaceId(marketplaceId);
        _loadAiCache(selMp ? selMp->countryCode() : QString{});
    }

    // Apply cached classification mappings to items that still lack productType
    // (e.g. resolved during a previous session via the unknown-classification dialog).
    for (UploadItem &item : toUpload) {
        if (item.productType.isEmpty() && !item.classificationId.isEmpty())
            item.productType = m_classificationMap.productType(item.classificationId);
    }

    // Collected during the loop: items skipped because no productType was found
    // but which carry a classificationId. Resolved via a dialog after the loop.
    QList<UnknownClassification> unknownClassifications;
    QList<int> deferredItemIndices; // indices into toUpload for items skipped due to missing productType

    for (int itemIdx = 0; itemIdx < toUpload.size(); ++itemIdx) {
        UploadItem &item = toUpload[itemIdx];
        ++processed;

        // Resolve the marketplace's country code once for logging + cursor file path.
        const AmazonMarketplace *itemMp = AmazonMarketplace::forMarketplaceId(item.marketplaceId);
        const QString itemCc = itemMp ? itemMp->countryCode() : QString{};

        if (statusLabelPtr)
            statusLabelPtr->setText(tr("Uploading [%1/%2] %3 / %4 [%5]")
                                    .arg(processed).arg(total)
                                    .arg(item.asin, item.attributeId, itemCc));

        if (item.sku.isEmpty()) {
            appendLog(tr("[%1/%2] SKIP %3 / %4 [%5]: SKU unknown — listing is inactive or not in FBA report")
                      .arg(processed).arg(total)
                      .arg(item.asin, item.attributeId, itemCc));
            // Persist the AI value so next Ask AI reuses it without consuming tokens
            if (!item.value.isEmpty() && isValidAiValue(item.value)) {
                m_aiValueCache.insert(item.asin + QLatin1Char(':') + item.attributeId, item.value);
                _saveAiCache();
            }
            if (progressBarPtr) progressBarPtr->setValue(processed);
            continue;
        }

        QString productType = item.productType;
        if (productType.isEmpty()) {
            appendLog(tr("[%1/%2] %3 / %4 [%5] \xe2\x80\x94 fetching productType\xe2\x80\xa6")
                      .arg(processed).arg(total)
                      .arg(item.asin, item.attributeId, itemCc));
            co_await api->fetchListingProductType(item.marketplaceId, item.sku, &productType);
            // Fallback: Catalog Items API works even for inactive/out-of-stock listings
            // where the Listings API omits productType from summaries.
            if (productType.isEmpty()) {
                appendLog(tr("    Listings API returned no productType — trying Catalog API\xe2\x80\xa6"));
                QString catalogClassId;
                QString catalogDisplayName;
                co_await api->fetchProductTypeFromCatalog(item.marketplaceId, item.asin, &productType,
                                                          &catalogClassId, &catalogDisplayName);
                // Propagate to the UploadItem so the post-loop dialog can use it.
                if (item.classificationId.isEmpty()) {
                    item.classificationId          = catalogClassId;
                    item.classificationDisplayName = catalogDisplayName;
                }
                // Check local classification map as fallback.
                if (productType.isEmpty() && !item.classificationId.isEmpty())
                    productType = m_classificationMap.productType(item.classificationId);
            }
        } else {
            appendLog(tr("[%1/%2] %3 / %4 [%5]")
                      .arg(processed).arg(total)
                      .arg(item.asin, item.attributeId, itemCc));
        }
        if (productType.isEmpty()) {
            if (!item.classificationId.isEmpty()) {
                // Will be handled by the post-loop dialog.
                unknownClassifications.append(UnknownClassification{
                    item.classificationId, item.classificationDisplayName,
                    item.asin, item.title});
                deferredItemIndices.append(itemIdx);
            }
            appendLog(tr("    SKIP %1 [%2]: could not determine product type (classificationId: %3)")
                      .arg(item.asin, itemCc, item.classificationId));
            if (progressBarPtr) progressBarPtr->setValue(processed);
            continue;
        }
        appendLog(tr("    productType = %1, patching\xe2\x80\xa6").arg(productType));

        // ------------------------------------------------------------------
        // Detect whether item.value is a local image file path. If so, read
        // (and convert to JPEG if needed) and upload as the MAIN product image
        // via the dedicated AmazonCatalogApi helper.
        // ------------------------------------------------------------------
        const bool isImagePath = kImagePath.match(item.value).hasMatch()
                              || QFileInfo::exists(item.value);

        bool ok = false;
        if (isImagePath) {
            QFile imgFile(item.value);
            if (!imgFile.open(QIODevice::ReadOnly)) {
                appendLog(tr("    FAIL %1 [%2]: cannot open image file %3")
                          .arg(item.asin, itemCc, item.value));
                if (progressBarPtr) progressBarPtr->setValue(processed);
                continue;
            }
            QByteArray jpegData = imgFile.readAll();
            imgFile.close();

            // Convert PNG → JPEG (Amazon rejects PNG for main_product_image_locator).
            if (item.value.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)) {
                QImage img;
                if (img.load(item.value)) {
                    QBuffer buf;
                    buf.open(QIODevice::WriteOnly);
                    if (img.save(&buf, "JPEG", 90))
                        jpegData = buf.data();
                }
            }

            appendLog(tr("    uploading main image %1 (%2 bytes)\xe2\x80\xa6")
                      .arg(QFileInfo(item.value).fileName())
                      .arg(jpegData.size()));
            co_await _catalogApi()->patchListingMainImage(
                item.marketplaceId, item.sku, productType, jpegData, &ok);
        } else {
            co_await api->patchListingAttribute(item.marketplaceId, item.sku, productType,
                                                item.attributeId, item.value, &ok);
        }

        if (ok) {
            appendLog(tr("    OK %1 / %2 [%3] = \"%4\"")
                      .arg(item.asin, item.attributeId, itemCc, item.value));
            // Clear from AI value cache — no longer needed once uploaded successfully
            const QString ck = item.asin + QLatin1Char(':') + item.attributeId;
            if (m_aiValueCache.remove(ck) > 0)
                _saveAiCache();
            // Record the upload so this ASIN+fieldId is suppressed for 4 days.
            if (!itemCc.isEmpty()) {
                const QString path = m_workingDir.filePath(
                    QStringLiteral("warnings/%1/processed_asins.txt").arg(itemCc.toLower()));
                QFile rf(path);
                if (rf.open(QIODevice::Append | QIODevice::Text)) {
                    QTextStream out(&rf);
                    out << item.asin << ',' << item.attributeId << ','
                        << QDate::currentDate().toString(Qt::ISODate) << '\n';
                }
            }
        } else {
            const QString errMsg = isImagePath ? _catalogApi()->lastError()
                                               : api->lastError();
            appendLog(tr("    FAIL %1 / %2 [%3]: %4")
                      .arg(item.asin, item.attributeId, itemCc, errMsg));
        }

        if (progressBarPtr) progressBarPtr->setValue(processed);
    }

    // -------------------------------------------------------------------
    // Post-loop: ask the user to assign product types to any unknown browse
    // classifications encountered. The mapping is persisted and applied on the
    // next upload run (SKIPped items are not re-uploaded in this session).
    // -------------------------------------------------------------------
    {
        // Deduplicate unknowns by classificationId.
        QSet<QString> seen;
        QList<UnknownClassification> deduped;
        for (const UnknownClassification &u : unknownClassifications) {
            if (!seen.contains(u.classificationId)) {
                seen.insert(u.classificationId);
                deduped.append(u);
            }
        }
        unknownClassifications = deduped;
    }

    if (!unknownClassifications.isEmpty()) {
        // Collect known productTypes from the loaded violations this session.
        QStringList knownTypes = m_classificationMap.knownProductTypes();
        for (int vi = 0; vi < m_model->violationCount(); ++vi) {
            const auto *vn = m_model->violationAt(vi);
            if (!vn) continue;
            const QString pt = vn->row.productType;
            if (!pt.isEmpty() && !knownTypes.contains(pt)) knownTypes.append(pt);
        }
        std::sort(knownTypes.begin(), knownTypes.end());

        appendLog(tr("Opening dialog for %1 unknown classification(s)…")
                  .arg(unknownClassifications.size()));
        if (statusLabelPtr)
            statusLabelPtr->setText(tr("Unknown classifications — user input needed"));

        auto *dlg = new DialogClassificationTypes(unknownClassifications, knownTypes, this);
        if (dlg->exec() == QDialog::Accepted) {
            const QHash<QString, QString> mappings = dlg->result();
            for (auto it = mappings.constBegin(); it != mappings.constEnd(); ++it)
                m_classificationMap.setProductType(it.key(), it.value());
            m_classificationMap.save();
            appendLog(tr("  Saved %1 classification mapping(s) \xe2\x80\x94 re-uploading deferred items\xe2\x80\xa6")
                      .arg(mappings.size()));

            for (int di = 0; di < deferredItemIndices.size(); ++di) {
                const int idx = deferredItemIndices.at(di);
                UploadItem &ditem = toUpload[idx];
                const QString newPt = mappings.value(ditem.classificationId);
                if (newPt.isEmpty()) continue;
                ditem.productType = newPt;

                const AmazonMarketplace *dMp = AmazonMarketplace::forMarketplaceId(ditem.marketplaceId);
                const QString dCc = dMp ? dMp->countryCode() : QString{};
                appendLog(tr("[retry] %1 / %2 [%3] \xe2\x80\x94 productType = %4, patching\xe2\x80\xa6")
                          .arg(ditem.asin, ditem.attributeId, dCc, newPt));
                if (statusLabelPtr)
                    statusLabelPtr->setText(tr("Retrying %1 / %2 [%3]")
                                            .arg(ditem.asin, ditem.attributeId, dCc));

                const bool isImagePath = kImagePath.match(ditem.value).hasMatch()
                                      || QFileInfo::exists(ditem.value);
                bool ok = false;
                if (isImagePath) {
                    QFile imgFile(ditem.value);
                    if (!imgFile.open(QIODevice::ReadOnly)) {
                        appendLog(tr("    FAIL %1 [%2]: cannot open image file %3")
                                  .arg(ditem.asin, dCc, ditem.value));
                        continue;
                    }
                    QByteArray jpegData = imgFile.readAll();
                    imgFile.close();
                    if (ditem.value.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)) {
                        QImage img;
                        if (img.load(ditem.value)) {
                            QBuffer buf;
                            buf.open(QIODevice::WriteOnly);
                            if (img.save(&buf, "JPEG", 90))
                                jpegData = buf.data();
                        }
                    }
                    co_await _catalogApi()->patchListingMainImage(
                        ditem.marketplaceId, ditem.sku, newPt, jpegData, &ok);
                } else {
                    co_await api->patchListingAttribute(ditem.marketplaceId, ditem.sku, newPt,
                                                        ditem.attributeId, ditem.value, &ok);
                }
                if (ok) {
                    appendLog(tr("    OK %1 / %2 [%3] = \"%4\"")
                              .arg(ditem.asin, ditem.attributeId, dCc, ditem.value));
                    if (!dCc.isEmpty()) {
                        const QString path = m_workingDir.filePath(
                            QStringLiteral("warnings/%1/processed_asins.txt").arg(dCc.toLower()));
                        QFile rf(path);
                        if (rf.open(QIODevice::Append | QIODevice::Text)) {
                            QTextStream out(&rf);
                            out << ditem.asin << ',' << ditem.attributeId << ','
                                << QDate::currentDate().toString(Qt::ISODate) << '\n';
                        }
                    }
                } else {
                    const QString errMsg = isImagePath ? _catalogApi()->lastError()
                                                       : api->lastError();
                    appendLog(tr("    FAIL %1 / %2 [%3]: %4")
                              .arg(ditem.asin, ditem.attributeId, dCc, errMsg));
                }
            }
        }
        dlg->deleteLater();
    }

    if (statusLabelPtr) statusLabelPtr->setText(tr("Done."));
    if (closeBtnPtr)    closeBtnPtr->setEnabled(true);

    ui->buttonUpload->setEnabled(true);
    setEnabled(true);
}


// ---------------------------------------------------------------------------
// _onRetrieveImages — download every violation row's main image (high-res)
// into {workingDir}/images-main-to-fix/{sanitizedSku}_main.jpg
// ---------------------------------------------------------------------------

QCoro::Task<void> PaneWarnings::_onRetrieveImages()
{
    QListWidgetItem *selItem = ui->listWidgetAmazon->currentItem();
    if (!selItem || !(selItem->flags() & Qt::ItemIsSelectable)) {
        QMessageBox::warning(this, tr("No marketplace"),
                             tr("Please select a marketplace first."));
        co_return;
    }

    // Ensure the output folder exists.
    m_workingDir.mkpath(QStringLiteral("images-main-to-fix"));
    const QString imageDir = m_workingDir.filePath(QStringLiteral("images-main-to-fix"));

    // -------------------------------------------------------------------
    // Get the ASIN from the currently selected tree row.
    // -------------------------------------------------------------------
    const QModelIndex sel = ui->tableViewWarnings->currentIndex();
    if (!sel.isValid()) {
        QMessageBox::warning(this, tr("Retrieve image"),
                             tr("Please select a product row in the warnings table first."));
        co_return;
    }
    // Walk up to the top-level violation row (internalPointer == nullptr).
    QModelIndex topIdx = sel;
    while (topIdx.isValid() && topIdx.internalPointer() != nullptr)
        topIdx = topIdx.parent();
    if (!topIdx.isValid()) {
        QMessageBox::warning(this, tr("Retrieve image"),
                             tr("Could not determine the selected violation."));
        co_return;
    }

    const TreeProductWarnings::ViolationNode *selVn = m_model->violationAt(topIdx.row());
    if (!selVn || selVn->row.mainImageUrl.isEmpty()) {
        QMessageBox::warning(this, tr("Retrieve image"),
                             tr("No main image URL available for the selected product."));
        co_return;
    }

    struct Entry { QString asin; QString sku; QString url; };
    const QList<Entry> entries = {{selVn->row.asin, selVn->row.sku, selVn->row.mainImageUrl}};

    // -------------------------------------------------------------------
    // Build progress dialog (same layout as _onLoadWarnings).
    // -------------------------------------------------------------------
    auto *progressDlg = new QDialog(parentWidget());
    progressDlg->setAttribute(Qt::WA_DeleteOnClose);
    // Non-modal: PaneWarnings is disabled via setEnabled(false); other panes stay active.
    progressDlg->setWindowTitle(tr("Retrieving images…"));
    progressDlg->resize(560, 400);
    auto *pLayout = new QVBoxLayout(progressDlg);

    auto *statusLabel = new QLabel(tr("Starting…"), progressDlg);
    QFont boldFont = statusLabel->font();
    boldFont.setBold(true);
    statusLabel->setFont(boldFont);
    pLayout->addWidget(statusLabel);

    auto *progressBar = new QProgressBar(progressDlg);
    progressBar->setRange(0, entries.size());
    progressBar->setValue(0);
    pLayout->addWidget(progressBar);

    auto *logEdit = new QTextEdit(progressDlg);
    logEdit->setReadOnly(true);
    logEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    pLayout->addWidget(logEdit);

    auto *btnLayout = new QHBoxLayout();
    auto *copyBtn = new QPushButton(tr("Copy log"), progressDlg);
    btnLayout->addWidget(copyBtn);
    btnLayout->addStretch();
    auto *closeBtns = new QDialogButtonBox(QDialogButtonBox::Close, progressDlg);
    QPushButton *closeBtn = closeBtns->button(QDialogButtonBox::Close);
    if (closeBtn) closeBtn->setEnabled(false);
    btnLayout->addWidget(closeBtns);
    pLayout->addLayout(btnLayout);

    QPointer<QLabel>       statusLabelPtr(statusLabel);
    QPointer<QProgressBar> progressBarPtr(progressBar);
    QPointer<QTextEdit>    logEditPtr(logEdit);
    QPointer<QPushButton>  closeBtnPtr(closeBtn);

    auto appendLog = [logEditPtr](const QString &line) {
        if (!logEditPtr) return;
        const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
        logEditPtr->append(QStringLiteral("[%1] %2").arg(ts, line));
    };

    connect(copyBtn, &QPushButton::clicked, progressDlg, [logEditPtr]() {
        if (logEditPtr)
            QGuiApplication::clipboard()->setText(logEditPtr->toPlainText());
    });
    connect(closeBtns, &QDialogButtonBox::rejected, progressDlg, &QDialog::close);

    m_progressDlg = progressDlg;
    progressDlg->show();
    setEnabled(false);

    ui->buttonRetrieveImages->setEnabled(false);

    // Sanitize a SKU for use as a filename — replace invalid path chars with '_'.
    auto sanitize = [](const QString &s) {
        QString out = s;
        static const QString kBad = QStringLiteral("/\\*?\"<>|:");
        for (QChar &c : out) {
            if (kBad.contains(c)) c = QLatin1Char('_');
        }
        return out;
    };

    // Use the selected marketplace ID for the catalog image fetch.
    const QString mpId = _selectedMarketplaceId();

    int downloaded = 0;
    int processed  = 0;
    for (const Entry &e : std::as_const(entries)) {
        ++processed;

        const QString baseName = sanitize(e.sku.isEmpty() ? e.asin : e.sku);
        const QString destPath = QDir(imageDir).filePath(
            QStringLiteral("%1_main.jpg").arg(baseName));

        if (statusLabelPtr)
            statusLabelPtr->setText(tr("[%1/%2] %3 — %4")
                                    .arg(processed).arg(entries.size())
                                    .arg(e.asin, baseName));

        if (QFileInfo::exists(destPath)) {
            appendLog(tr("[%1/%2] %3 — SKIP (already exists)")
                      .arg(processed).arg(entries.size())
                      .arg(e.asin));
            if (progressBarPtr) progressBarPtr->setValue(processed);
            continue;
        }

        // Fetch full-quality image URLs via the Catalog Items API (same source
        // as PaneSizing). imageUrls[0] is the MAIN image at best available size.
        appendLog(tr("[%1/%2] %3 — fetching high-res URL…")
                  .arg(processed).arg(entries.size()).arg(e.asin));
        QStringList imageUrls;
        co_await _catalogApi()->fetchItemImages(mpId, e.asin, &imageUrls);

        QString hiresUrl = imageUrls.isEmpty() ? QString{} : imageUrls.first();
        if (hiresUrl.isEmpty()) {
            appendLog(tr("  ⚠ no image URL returned — skipping"));
            if (progressBarPtr) progressBarPtr->setValue(processed);
            continue;
        }

        appendLog(tr("  GET %1").arg(hiresUrl));
        QNetworkReply *reply = _imageNam()->get(QNetworkRequest(QUrl(hiresUrl)));
        co_await qCoro(reply).waitForFinished();

        const QByteArray data = reply->readAll();
        const QNetworkReply::NetworkError netErr = reply->error();
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString errString = reply->errorString();
        reply->deleteLater();

        if (netErr != QNetworkReply::NoError || data.isEmpty()) {
            appendLog(tr("    FAIL %1 (HTTP %2): %3")
                      .arg(e.asin).arg(httpStatus).arg(errString));
            if (progressBarPtr) progressBarPtr->setValue(processed);
            continue;
        }

        QFile f(destPath);
        if (!f.open(QIODevice::WriteOnly)) {
            appendLog(tr("    FAIL %1: cannot open %2 for write")
                      .arg(e.asin, destPath));
            if (progressBarPtr) progressBarPtr->setValue(processed);
            continue;
        }
        f.write(data);
        f.close();

        ++downloaded;
        appendLog(tr("    OK %1 → %2 (%3 bytes)")
                  .arg(e.asin, destPath).arg(data.size()));

        if (progressBarPtr) progressBarPtr->setValue(processed);
    }

    if (statusLabelPtr)
        statusLabelPtr->setText(tr("Done. %1 image(s) downloaded.").arg(downloaded));
    appendLog(tr("Done. %1 image(s) downloaded into %2")
              .arg(downloaded).arg(imageDir));
    if (closeBtnPtr) closeBtnPtr->setEnabled(true);

    ui->buttonRetrieveImages->setEnabled(true);
    setEnabled(true);
    co_return;
}

// ---------------------------------------------------------------------------
// _onOpenImageDir — open {workingDir}/images-main-to-fix/ in the file manager
// ---------------------------------------------------------------------------

void PaneWarnings::_onOpenImageDir() const
{
    const QString path = m_workingDir.filePath(QStringLiteral("images-main-to-fix"));
    if (!QDir(path).exists())
        QDir().mkpath(path);
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}


void PaneWarnings::hideEvent(QHideEvent *event)
{
    // When the tab is switched away, hide the floating progress dialog so it
    // doesn't linger over other panes. It will reappear when we come back.
    if (m_progressDlg) m_progressDlg->hide();
    QWidget::hideEvent(event);
}

void PaneWarnings::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_progressDlg) m_progressDlg->show();
}

// ---------------------------------------------------------------------------
// _onAskAiUpload — chains Ask AI → Upload in one step.
// ---------------------------------------------------------------------------

QCoro::Task<void> PaneWarnings::_onAskAiUpload()
{
    m_askAiTask = _onAskAi();
    co_await qCoro(this, &PaneWarnings::askAiFinished);

    bool hasAiValues = false;
    for (int vi = 0; vi < m_model->violationCount() && !hasAiValues; ++vi) {
        const auto *vn = m_model->violationAt(vi);
        if (!vn) continue;
        for (const auto &child : vn->children)
            if (!child.isCurrentValue && !child.aiValue.isEmpty())
                { hasAiValues = true; break; }
    }
    if (hasAiValues)
        co_await _onUpload();
}

// ---------------------------------------------------------------------------
// _onLoadAskUpload — chains Load → Ask AI → Upload for the selected marketplace.
// When "Do all Amazon" is checked, iterates every selectable marketplace in order.
// ---------------------------------------------------------------------------

QCoro::Task<void> PaneWarnings::_onLoadAskUpload()
{
    if (m_launchAllRunning) co_return;
    m_launchAllRunning = true;
    ui->buttonLoadAskUpload->setEnabled(false);

    // Validate CLI upfront — Ask AI requires it.
    AbstractCli *cli = ui->comboBoxCli->currentData().value<AbstractCli *>();
    if (!cli) {
        QMessageBox::warning(this, tr("Load/Ask/Upload"),
                             tr("Please select a CLI in the combobox first."));
        m_launchAllRunning = false;
        ui->buttonLoadAskUpload->setEnabled(true);
        co_return;
    }

    // Build ordered list of marketplaces to process.
    // Current selection is always first; remaining items follow top-to-bottom.
    QList<QListWidgetItem *> items;
    QListWidgetItem *curItem = ui->listWidgetAmazon->currentItem();

    if (ui->checkBoxDoAllAmazon->isChecked()) {
        if (curItem && (curItem->flags() & Qt::ItemIsSelectable))
            items.append(curItem);
        for (int i = 0; i < ui->listWidgetAmazon->count(); ++i) {
            QListWidgetItem *it = ui->listWidgetAmazon->item(i);
            if (!it || !(it->flags() & Qt::ItemIsSelectable) || it == curItem) continue;
            items.append(it);
        }
    } else {
        if (!curItem || !(curItem->flags() & Qt::ItemIsSelectable)) {
            QMessageBox::warning(this, tr("Load/Ask/Upload"),
                                 tr("Please select a marketplace first."));
            m_launchAllRunning = false;
            ui->buttonLoadAskUpload->setEnabled(true);
            co_return;
        }
        items.append(curItem);
    }

    for (QListWidgetItem *item : items) {
        ui->listWidgetAmazon->setCurrentItem(item);

        // Step 1: load violations.
        co_await _onLoadWarnings();

        // Step 2: ask AI (only if there are actionable violations).
        if (m_model->violationCount() > 0) {
            m_askAiTask = _onAskAi();
            // _onAskAi() starts asynchronous callbacks; wait for the sentinel to fire
            // the askAiFinished signal (emitted deferred to avoid sync vs. async race).
            co_await qCoro(this, &PaneWarnings::askAiFinished);
        }

        // Step 3: upload any AI-filled values.
        bool hasAiValues = false;
        for (int vi = 0; vi < m_model->violationCount() && !hasAiValues; ++vi) {
            const auto *vn = m_model->violationAt(vi);
            if (!vn) continue;
            for (const auto &child : vn->children)
                if (!child.isCurrentValue && !child.aiValue.isEmpty())
                    { hasAiValues = true; break; }
        }
        if (hasAiValues)
            co_await _onUpload();
    }

    m_launchAllRunning = false;
    ui->buttonLoadAskUpload->setEnabled(true);
}

void PaneWarnings::_connectSlots()
{
    connect(ui->buttonLoadWarnings, &QPushButton::clicked,
            this, [this]() { m_loadTask = _onLoadWarnings(); });

    connect(ui->buttonPasteWarnings, &QPushButton::clicked,
            this, [this]() { m_pasteTask = _onPasteWarnings(); });

    // Show cache stats immediately when a marketplace is selected — no load needed.
    connect(ui->listWidgetAmazon, &QListWidget::currentItemChanged,
            this, [this](QListWidgetItem *current, QListWidgetItem *) {
        if (!current || !(current->flags() & Qt::ItemIsSelectable)) {
            ui->labelWarningsInfo->clear();
            return;
        }
        const QString cc = current->data(Qt::UserRole).toString();
        if (cc.isEmpty()) { ui->labelWarningsInfo->clear(); return; }

        const QString cursorPath = m_workingDir.filePath(
            QStringLiteral("warnings/%1/processed_asins.txt").arg(cc.toLower()));

        int suppressed = 0;
        QFile f(cursorPath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QDate cutoff = QDate::currentDate().addDays(-4);
            QTextStream in(&f);
            while (!in.atEnd()) {
                const QStringList parts = in.readLine().trimmed().split(QLatin1Char(','));
                if (parts.size() < 3) continue;
                const QDate d = QDate::fromString(parts[2].trimmed(), Qt::ISODate);
                if (d.isValid() && d >= cutoff) ++suppressed;
            }
        }

        const int loaded = m_model->violationCount();
        if (loaded > 0 || suppressed > 0) {
            ui->labelWarningsInfo->setText(
                loaded > 0
                ? tr("%1 warning(s) loaded  •  %2 suppressed (uploaded < 4 days)")
                      .arg(loaded).arg(suppressed)
                : tr("%1 suppressed in cache for %2 (uploaded < 4 days)")
                      .arg(suppressed).arg(cc));
        } else {
            ui->labelWarningsInfo->clear();
        }
    });


    connect(ui->buttonAskAi, &QPushButton::clicked,
            this, [this]() { m_askAiTask = _onAskAi(); });

    connect(ui->buttonAskAiUpload, &QPushButton::clicked,
            this, [this]() { m_askAiUploadTask = _onAskAiUpload(); });

    connect(ui->buttonUpload, &QPushButton::clicked,
            this, [this]() { m_uploadTask = _onUpload(); });

    connect(ui->buttonRetrieveImages, &QPushButton::clicked,
            this, [this]() { m_retrieveTask = _onRetrieveImages(); });
    connect(ui->buttonOpenImageDir, &QPushButton::clicked,
            this, [this]() { _onOpenImageDir(); });

    connect(ui->buttonLoadAskUpload, &QPushButton::clicked,
            this, [this]() {
        if (!m_launchAllRunning)
            m_launchAllTask = _onLoadAskUpload();
    });

    // Template folder: browse + manual edit, both persisted to QSettings.
    connect(ui->buttonBrowseTemplateDir, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(
            this, tr("Select templates folder"),
            ui->lineEditTemplateFolder->text());
        if (dir.isEmpty()) return;
        ui->lineEditTemplateFolder->setText(dir);
        QSettings().setValue(QStringLiteral("warnings/templateFolder"), dir);
    });
    connect(ui->lineEditTemplateFolder, &QLineEdit::editingFinished, this, [this]() {
        QSettings appSettings;
        appSettings.setValue(QStringLiteral("warnings/templateFolder"),
                             ui->lineEditTemplateFolder->text());
        appSettings.sync();
    });

    // Clear AI suggestions: selected violations if any, otherwise all
    // violations. Selection on either the violation (top-level) or any of its
    // children counts as selecting that violation.
    connect(ui->buttonClearAiSuggestion, &QPushButton::clicked, this, [this]() {
        QSet<int> violIndexes;
        const auto selected = ui->tableViewWarnings->selectionModel()
                                ? ui->tableViewWarnings->selectionModel()->selectedIndexes()
                                : QModelIndexList{};
        for (const QModelIndex &idx : selected) {
            if (!idx.isValid()) continue;
            if (idx.internalPointer() == nullptr) {
                violIndexes.insert(idx.row());
            } else {
                const QModelIndex p = idx.parent();
                if (p.isValid()) violIndexes.insert(p.row());
            }
        }

        if (violIndexes.isEmpty()) {
            const int count = m_model->violationCount();
            for (int i = 0; i < count; ++i) violIndexes.insert(i);
        }

        for (int vi : std::as_const(violIndexes)) {
            const TreeProductWarnings::ViolationNode *vn = m_model->violationAt(vi);
            if (!vn) continue;
            int aiCounter = 0;
            for (const auto &child : vn->children) {
                if (child.isCurrentValue) continue;
                m_model->setAiValue(vi, aiCounter, QString{});
                ++aiCounter;
            }
        }
    });
}
