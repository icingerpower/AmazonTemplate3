#pragma GCC optimize("O1")
#include "PaneGSPR.h"
#include "ui_PaneGSPR.h"

#include "AmazonMarketplace.h"
#include "SettingsTable.h"
#include "TableGpsrManufacturers.h"
#include "TableProductWarnings.h"   // full WarningRow definition
#include "TableWarningsManufacturer.h"
#include "apis/AmazonWarningsApi.h"

#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QClipboard>
#include <QSet>
#include <QStringList>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QTextEdit>
#include <QVBoxLayout>

namespace {

// Editor delegate for the Manufacturer column of the warnings table.
// The combo is populated lazily from TableGpsrManufacturers at edit time,
// so it always reflects the manufacturers currently loaded.
class ManufacturerComboDelegate : public QStyledItemDelegate
{
public:
    explicit ManufacturerComboDelegate(TableGpsrManufacturers *mfr, QObject *parent = nullptr)
        : QStyledItemDelegate(parent), m_mfr(mfr) {}

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &opt,
                          const QModelIndex &idx) const override
    {
        Q_UNUSED(opt);
        Q_UNUSED(idx);
        auto *combo = new QComboBox(parent);
        combo->addItem(QString()); // empty = unassigned
        for (int r = 0; r < m_mfr->rowCount(); ++r)
            combo->addItem(m_mfr->data(
                               m_mfr->index(r, TableGpsrManufacturers::ColCompanyName)).toString());
        return combo;
    }

    void setEditorData(QWidget *editor, const QModelIndex &idx) const override
    {
        auto *combo = qobject_cast<QComboBox *>(editor);
        if (!combo)
            return;
        const int i = combo->findText(idx.data(Qt::EditRole).toString());
        combo->setCurrentIndex(i >= 0 ? i : 0);
    }

    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &idx) const override
    {
        auto *combo = qobject_cast<QComboBox *>(editor);
        if (combo)
            model->setData(idx, combo->currentText(), Qt::EditRole);
    }

    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &opt,
                              const QModelIndex &) const override
    {
        editor->setGeometry(opt.rect);
    }

private:
    TableGpsrManufacturers *m_mfr;
};

} // namespace

PaneGSPR::PaneGSPR(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PaneGSPR)
{
    ui->setupUi(this);

    m_manufacturers        = new TableGpsrManufacturers(this);
    m_warningsManufacturer = new TableWarningsManufacturer(this);

    ui->tableViewManufacturerWarnings->setItemDelegateForColumn(
        TableWarningsManufacturer::ColManufacturer,
        new ManufacturerComboDelegate(m_manufacturers, this));

    ui->tableViewManufacturerWarnings->setModel(m_warningsManufacturer);
    ui->tableViewManufacturers->setModel(m_manufacturers);

    connect(ui->buttonLoadAll, &QPushButton::clicked, this,
            [this] { m_loadAllTask = _onLoadAll(); });
    connect(ui->buttonLoadManufacturers, &QPushButton::clicked, this,
            [this] { m_loadManufacturersTask = _onLoadManufacturers(); });
}

PaneGSPR::~PaneGSPR()
{
    delete ui;
}

void PaneGSPR::setWorkingDir(const QDir &workingDir)
{
    m_workingDir = workingDir;
    m_manufacturers->load(workingDir);
}

void PaneGSPR::setAvailableClis(const QList<AbstractCli *> &clis)
{
    m_availableClis = clis;
}

AmazonWarningsApi *PaneGSPR::_api()
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

QCoro::Task<void> PaneGSPR::_onLoadManufacturers()
{
    m_manufacturers->load(m_workingDir);
    co_return;
}

QCoro::Task<void> PaneGSPR::_onLoadAll()
{
    co_await _onLoadManufacturers();

    // -----------------------------------------------------------------------
    // Progress dialog
    // -----------------------------------------------------------------------
    auto *progressDlg = new QDialog(parentWidget());
    progressDlg->setAttribute(Qt::WA_DeleteOnClose);
    progressDlg->setWindowTitle(tr("Loading GSPR data…"));
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

    auto *btnLayout  = new QHBoxLayout();
    auto *copyBtn    = new QPushButton(tr("Copy log"), progressDlg);
    auto *closeBtns  = new QDialogButtonBox(QDialogButtonBox::Close, progressDlg);
    QPushButton *closeBtn = closeBtns->button(QDialogButtonBox::Close);
    if (closeBtn) closeBtn->setEnabled(false);
    btnLayout->addWidget(copyBtn);
    btnLayout->addStretch();
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

    connect(copyBtn,   &QPushButton::clicked,        progressDlg, [logEditPtr] {
        if (logEditPtr) QGuiApplication::clipboard()->setText(logEditPtr->toPlainText());
    });
    connect(closeBtns, &QDialogButtonBox::rejected,  progressDlg, &QDialog::close);

    m_progressDlg = progressDlg;
    progressDlg->show();
    setEnabled(false);

    // -----------------------------------------------------------------------
    // Fetch manufacturer_contact violations for all configured marketplaces
    // -----------------------------------------------------------------------
    m_warningsManufacturer->clear();
    AmazonWarningsApi *api = _api();

    for (const AmazonMarketplace &mp : AmazonMarketplace::all()) {
        if (!mp.isEu())  // GSPR only applies to EU member states (not GB, US, CA, JP…)
            continue;
        if (api->sellerIdForMarketplace(mp.marketplaceId()).isEmpty())
            continue;

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

        if (statusLabelPtr)
            statusLabelPtr->setText(tr("Fetching violations for %1…").arg(mp.countryCode()));

        QList<WarningRow> violations;
        co_await api->fetchViolations(mp.marketplaceId(), &violations);

        disconnect(api, &AmazonWarningsApi::logMessage,      progressDlg, nullptr);
        disconnect(api, &AmazonWarningsApi::progressChanged, progressDlg, nullptr);

        // Log unique attributeIds for diagnosis
        QSet<QString> seenIds;
        for (const WarningRow &vr : violations)
            seenIds.insert(vr.attributeId);
        if (!seenIds.isEmpty())
            appendLog(tr("  attributeIds found: %1").arg(QStringList(seenIds.begin(), seenIds.end()).join(", ")));

        for (const WarningRow &vr : violations) {
            // Only keep GSPR manufacturer violations: code 100527, or message
            // containing "manufacturer" or "GSPR" (case-insensitive).
            const bool isGspr =
                vr.attributeId == QStringLiteral("100527") ||
                vr.issueMessage.contains(QStringLiteral("manufacturer"), Qt::CaseInsensitive) ||
                vr.issueMessage.contains(QStringLiteral("GSPR"),         Qt::CaseInsensitive);
            if (!isGspr)
                continue;

            ManufacturerWarningRow row;
            row.sku          = vr.sku;
            row.asin         = vr.asin;
            row.title        = vr.title;
            row.countryCode  = mp.countryCode();
            row.attributeId  = vr.attributeId;
            row.issueMessage = vr.issueMessage;
            m_warningsManufacturer->addRow(row);
        }
    }

    // -----------------------------------------------------------------------
    // Done
    // -----------------------------------------------------------------------
    setEnabled(true);
    if (statusLabelPtr) statusLabelPtr->setText(tr("Done. %1 violations found.")
        .arg(m_warningsManufacturer->rowCount()));
    if (progressBarPtr) { progressBarPtr->setRange(0, 1); progressBarPtr->setValue(1); }
    if (closeBtnPtr)    closeBtnPtr->setEnabled(true);
}
