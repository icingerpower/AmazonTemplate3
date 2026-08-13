#include "PaneGspr.h"
#include "ui_PaneGspr.h"

#include <algorithm>

#include "AbstractCli.h"
#include "AmazonMarketplace.h"
#include "GsprDoneTable.h"
#include "GsprFailedTable.h"
#include "GsprSkippedTable.h"
#include "ProgressDialog.h"

#include <QCheckBox>
#include <QDateTime>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QTextEdit>
#include <QTextStream>

namespace {
const auto SETTINGS_FACTORY_FOLDER = QStringLiteral("gspr/factoryFolder");
const auto SETTINGS_EC_REP_PATTERN = QStringLiteral("gspr/ecRepPattern");
const auto SETTINGS_SELECTED_CLI   = QStringLiteral("gspr/selectedCli");
const auto SETTINGS_MANUAL_MFR_SAVE = QStringLiteral("gspr/manualManufacturerSave");
const auto GSPR_DUMP_DIR           = QStringLiteral("/tmp/gspr");
// Warning type keys in GsprDoneTable — Amazon's reason labels.
const auto WARNING_PSI = QStringLiteral("GPSR: warning and safety information");
const auto WARNING_RP  = QStringLiteral("GPSR: Responsible Person contact details");
const auto WARNING_MFR = QStringLiteral("GPSR: manufacturer contact details");

// Ledger key for a worker outcome ("psi" is the default/legacy type).
QString warningTypeOf(const GsprWarningOutcome &w)
{
    if (w.type == QLatin1String("rp"))  return WARNING_RP;
    if (w.type == QLatin1String("mfr")) return WARNING_MFR;
    return WARNING_PSI;
}
}

PaneGspr::PaneGspr(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PaneGspr)
{
    ui->setupUi(this);

    const QSettings settings;
    ui->lineEditFactoryFolder->setText(
        settings.value(SETTINGS_FACTORY_FOLDER).toString());
    ui->lineEditECRepPattern->setText(
        settings.value(SETTINGS_EC_REP_PATTERN).toString());

    // GSPR only applies to the European Union.
    for (const AmazonMarketplace *mp : AmazonMarketplace::europeanUnion()) {
        ui->comboBoxAmazon->addItem(
            QStringLiteral("%1 (%2)").arg(mp->countryName(), mp->countryCode()),
            mp->marketplaceId());
    }

    connect(ui->buttonBrowseFactoryFolder, &QPushButton::clicked,
            this, &PaneGspr::_browseFactoryFolder);
    connect(ui->comboBoxCli, &QComboBox::currentIndexChanged,
            this, &PaneGspr::_onCliChanged);
    connect(ui->lineEditECRepPattern, &QLineEdit::textEdited,
            this, [](const QString &text) {
        QSettings().setValue(SETTINGS_EC_REP_PATTERN, text);
    });
    ui->checkBoxManualMfrSave->setChecked(
        settings.value(SETTINGS_MANUAL_MFR_SAVE, true).toBool());
    connect(ui->checkBoxManualMfrSave, &QCheckBox::toggled, this, [](bool on) {
        QSettings().setValue(SETTINGS_MANUAL_MFR_SAVE, on);
    });

    m_runner = new CaseWorkerRunner(this);
    m_doneTable = new GsprDoneTable(this);
    ui->tableViewDone->setModel(m_doneTable);
    ui->tableViewDone->horizontalHeader()->setSectionResizeMode(
        QHeaderView::ResizeToContents);
    ui->tableViewDone->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableViewDone->setSelectionMode(QAbstractItemView::ExtendedSelection);
    connect(ui->buttonRemoveDone, &QPushButton::clicked, this, [this]() {
        QModelIndexList rows = ui->tableViewDone->selectionModel()->selectedRows();
        // Remove bottom-up so the remaining indexes stay valid.
        std::sort(rows.begin(), rows.end(),
                  [](const QModelIndex &a, const QModelIndex &b) { return a.row() > b.row(); });
        for (const QModelIndex &idx : std::as_const(rows))
            m_doneTable->removeAt(idx.row());
    });
    m_failedTable = new GsprFailedTable(this);
    ui->tableViewFailed->setModel(m_failedTable);
    ui->tableViewFailed->horizontalHeader()->setSectionResizeMode(
        QHeaderView::ResizeToContents);
    ui->tableViewFailed->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableViewFailed->setSelectionMode(QAbstractItemView::ExtendedSelection);
    connect(ui->buttonRemoveFailed, &QPushButton::clicked, this, [this]() {
        QModelIndexList rows = ui->tableViewFailed->selectionModel()->selectedRows();
        // Remove bottom-up so the remaining indexes stay valid.
        std::sort(rows.begin(), rows.end(),
                  [](const QModelIndex &a, const QModelIndex &b) { return a.row() > b.row(); });
        for (const QModelIndex &idx : std::as_const(rows))
            m_failedTable->removeAt(idx.row());
    });
    m_skippedTable = new GsprSkippedTable(this);
    ui->tableViewSkipped->setModel(m_skippedTable);
    ui->tableViewSkipped->horizontalHeader()->setSectionResizeMode(
        QHeaderView::ResizeToContents);
    ui->tableViewSkipped->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableViewSkipped->setSelectionMode(QAbstractItemView::ExtendedSelection);
    connect(ui->buttonRemoveSkipped, &QPushButton::clicked, this, [this]() {
        QModelIndexList rows = ui->tableViewSkipped->selectionModel()->selectedRows();
        // Remove bottom-up so the remaining indexes stay valid.
        std::sort(rows.begin(), rows.end(),
                  [](const QModelIndex &a, const QModelIndex &b) { return a.row() > b.row(); });
        for (const QModelIndex &idx : std::as_const(rows))
            m_skippedTable->removeAt(idx.row());
    });
    connect(ui->buttonRun,    &QPushButton::clicked, this, &PaneGspr::_onRun);
    connect(ui->buttonRunAll, &QPushButton::clicked, this, &PaneGspr::_onRunAll);
    connect(ui->buttonStop,   &QPushButton::clicked, this, &PaneGspr::_onStop);
}

PaneGspr::~PaneGspr()
{
    delete ui;
}

void PaneGspr::setWorkingDir(const QDir &workingDir)
{
    m_workingDir = workingDir;
}

void PaneGspr::setAvailableClis(const QList<AbstractCli *> &clis)
{
    m_availableClis = clis;

    QSignalBlocker b(ui->comboBoxCli);
    ui->comboBoxCli->clear();
    for (AbstractCli *cli : clis)
        ui->comboBoxCli->addItem(cli->getName(), QVariant::fromValue(cli));

    const QString saved = QSettings().value(SETTINGS_SELECTED_CLI).toString();
    int restored = -1;
    for (int i = 0; i < clis.size(); ++i)
        if (clis[i]->getName() == saved) { restored = i; break; }

    // Default to a text-oriented CLI (as in PaneCases): image/agentic CLIs are
    // a poor fit for drafting GSPR documents.
    int fallback = 0;
    for (int i = 0; i < clis.size(); ++i)
        if (!clis[i]->canGenImages()) { fallback = i; break; }

    ui->comboBoxCli->setCurrentIndex(restored >= 0 ? restored : fallback);
}

void PaneGspr::_onCliChanged(int index)
{
    if (index < 0 || index >= m_availableClis.size()) return;
    QSettings().setValue(SETTINGS_SELECTED_CLI, m_availableClis[index]->getName());
}

AbstractCli *PaneGspr::_selectedCli() const
{
    return ui->comboBoxCli->currentData().value<AbstractCli *>();
}

void PaneGspr::_onRun()
{
    const AmazonMarketplace *mp = AmazonMarketplace::forMarketplaceId(
        ui->comboBoxAmazon->currentData().toString());
    if (!mp)
        return;
    _runTargets({GsprTarget{mp->countryCode(), mp->countryName()}});
}

void PaneGspr::_onRunAll()
{
    QList<GsprTarget> targets;
    for (const AmazonMarketplace *mp : AmazonMarketplace::europeanUnion())
        targets.append(GsprTarget{mp->countryCode(), mp->countryName()});
    _runTargets(targets);
}

void PaneGspr::_onStop()
{
    if (!m_running)
        return;
    m_stopRequested = true;
    ui->buttonStop->setEnabled(false);
    _logToFile(QStringLiteral("stop requested by user"));
    m_runner->stopAll();
}

void PaneGspr::_setRunning(bool running)
{
    m_running = running;
    ui->buttonRun->setEnabled(!running);
    ui->buttonRunAll->setEnabled(!running);
    ui->buttonStop->setEnabled(running);
}

void PaneGspr::_runTargets(const QList<GsprTarget> &targets)
{
    if (m_running || targets.isEmpty())
        return;

    QString why;
    if (!m_runner->isConfigured(&why)) {
        QMessageBox::warning(this, tr("GSPR"), why);
        return;
    }

    m_logFilePath = QStringLiteral("/tmp/gspr-run-%1.log")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss")));

    ProgressDlgHandles h;
    QDialog *dlg = makeProgressDlg(this, tr("GSPR — Seller Central"), &h);
    if (h.status)
        h.status->setText(tr("Processing %n marketplace(s)…", nullptr, targets.size()));
    dlg->show();

    // Every worker stderr line goes to the dialog AND to the /tmp log file,
    // so failed runs can be analysed afterwards. "@@gspr-result" lines are
    // per-ASIN outcome events — recorded immediately so an interrupted run
    // keeps the progress made so far.
    auto appendLog = [this, h](const QString &line) {
        const auto marker = QLatin1String("@@gspr-result ");
        if (line.startsWith(marker)) {
            _applyLiveOutcome(line.mid(marker.size()));
            _logToFile(line);
            return; // worker already logs a human-readable line for it
        }
        const auto askMarker = QLatin1String("@@gspr-ask ");
        if (line.startsWith(askMarker)) {
            _logToFile(line);
            _handleWorkerAsk(line.mid(askMarker.size())); // modal — worker waits
            return;
        }
        if (h.log) h.log->append(line);
        _logToFile(line);
    };
    QMetaObject::Connection logConn =
        connect(m_runner, &CaseWorkerRunner::logMessage, this, appendLog);

    // ASINs already recorded done are never re-attempted by the worker.
    QList<GsprTarget> targetsWithSkip = targets;
    for (GsprTarget &t : targetsWithSkip) {
        t.skipAsins    = m_doneTable->asinsDone(t.countryCode, WARNING_PSI);
        t.skipAsinsRp  = m_doneTable->asinsDone(t.countryCode, WARNING_RP);
        t.skipAsinsMfr = m_doneTable->asinsDone(t.countryCode, WARNING_MFR);
    }

    // Manufacturer map from the supplier xlsx files (mtime-cached).
    m_manufacturers.setFolder(QDir(ui->lineEditFactoryFolder->text()));
    QString mfrError;
    if (!m_manufacturers.reload(&mfrError))
        appendLog(tr("manufacturer store: %1").arg(mfrError));
    appendLog(tr("manufacturer store: %1 entries, %2 unparseable row(s)")
                  .arg(m_manufacturers.entries().size())
                  .arg(m_manufacturers.unparseableRows().size()));
    for (const QString &row : m_manufacturers.unparseableRows())
        appendLog(tr("  fix data: %1").arg(row));

    QStringList names;
    for (const GsprTarget &t : targetsWithSkip) names << t.countryCode;
    appendLog(tr("Run started for: %1 (dumps under %2, log %3)")
                  .arg(names.join(QStringLiteral(", ")), GSPR_DUMP_DIR, m_logFilePath));

    m_stopRequested = false;
    _setRunning(true);
    m_runner->gspr(QStringLiteral("safety"), targetsWithSkip, GSPR_DUMP_DIR,
                   ui->lineEditECRepPattern->text().trimmed(),
                   m_manufacturers.entriesJson(), m_skippedTable->asins(),
                   ui->checkBoxManualMfrSave->isChecked(), this,
                   [this, h, logConn, appendLog](const QList<GsprResult> &results) {
        _setRunning(false);

        if (results.isEmpty())
            appendLog(tr("Worker returned no results — see the log above."));
        const QString stamp =
            QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss"));
        for (const GsprResult &r : results) {
            int submitted = 0, failed = 0, pending = 0, skipped = 0;
            for (const GsprWarningOutcome &w : r.warnings) {
                // Normally already applied live; idempotent re-application
                // covers events lost mid-run.
                _applyOutcome(r.countryCode, w);
                if (w.status == QLatin1String("submitted"))
                    ++submitted;
                else if (w.status == QLatin1String("failed")) {
                    ++failed;
                    appendLog(tr("  %1 %2 — %3").arg(r.countryCode, w.asin, w.reason));
                } else {
                    (w.status == QLatin1String("pending")) ? ++pending : ++skipped;
                }
            }
            const QString line = r.ok
                ? tr("%1 %2 — OK — %3 submitted, %4 failed, %5 pending, %6 skipped — %7")
                      .arg(stamp, r.countryCode).arg(submitted).arg(failed)
                      .arg(pending).arg(skipped).arg(r.dumpDir)
                : tr("%1 %2 — FAILED — %3").arg(stamp, r.countryCode, r.error);
            appendLog(line);
            ui->listWidgetDone->addItem(line);
        }

        disconnect(logConn);
        if (h.status)   h.status->setText(m_stopRequested ? tr("Stopped.") : tr("Done."));
        if (h.bar)      { h.bar->setRange(0, 1); h.bar->setValue(1); }
        if (h.closeBtn) h.closeBtn->setEnabled(true);
    });
}

void PaneGspr::_applyOutcome(const QString &country, const GsprWarningOutcome &w)
{
    const QString type = warningTypeOf(w);
    if (w.status == QLatin1String("submitted")) {
        m_doneTable->recordDone(w.asin, country, type);
        m_failedTable->removeFailure(w.asin, country); // retry succeeded
    } else if (w.status == QLatin1String("failed")) {
        m_failedTable->recordFailure(w.asin, country, w.reason);
    } else if (w.status == QLatin1String("pending")
               || w.status == QLatin1String("skipped")) {
        // A row whose status explicitly asks for a submission is NEVER
        // adopted as done — and if the ledger claims it is done, that is a
        // submission Amazon dropped after confirming it (observed: 5 of 15
        // rapid RP saves). Un-record stale mismatches so the next run
        // resubmits. Threshold per type: warning/safety rows keep this
        // status for the WHOLE review (up to 7 days, per Amazon); RP and
        // manufacturer rows update within the hour.
        if (w.statusText.contains(QLatin1String("submission is required"),
                                  Qt::CaseInsensitive)) {
            const qint64 staleSecs = (type == WARNING_PSI) ? 7 * 24 * 3600 : 3600;
            if (m_doneTable->isDone(w.asin, country, type)
                    && m_doneTable->dateDone(w.asin, country, type)
                           < QDateTime::currentDateTime().addSecs(-staleSecs)) {
                m_doneTable->removeDone(w.asin, country, type);
                _logToFile(tr("%1 %2 [%3] — recorded done but Amazon still asks"
                              " for a submission — un-recorded, will be"
                              " resubmitted on the next run")
                               .arg(country, w.asin, type));
            }
            return;
        }
        if (!m_doneTable->isDone(w.asin, country, type)) {
            // First seen as pending/under review: submitted outside the app.
            m_doneTable->recordDone(w.asin, country, type);
        } else if (m_doneTable->dateDone(w.asin, country, type)
                       < QDateTime::currentDateTime().addMonths(-1)) {
            // Done > 1 month ago yet still listed on Amazon: investigate.
            m_doneTable->markLongToProcess(w.asin, country, type);
            _logToFile(tr("%1 %2 — done > 1 month ago and still listed"
                          " — marked long to process (%3)")
                           .arg(country, w.asin, w.statusText));
        }
    }
}

void PaneGspr::_applyLiveOutcome(const QString &json)
{
    const QJsonObject o = QJsonDocument::fromJson(json.toUtf8()).object();
    if (o.isEmpty())
        return;
    _applyOutcome(o.value(QStringLiteral("country")).toString(),
                  GsprWarningOutcome{
                      o.value(QStringLiteral("asin")).toString(),
                      o.value(QStringLiteral("ok")).toBool(),
                      o.value(QStringLiteral("status")).toString(),
                      o.value(QStringLiteral("reason")).toString(),
                      o.value(QStringLiteral("statusText")).toString(),
                      o.value(QStringLiteral("type")).toString(),
                  });
}

void PaneGspr::_handleWorkerAsk(const QString &json)
{
    const QJsonObject o = QJsonDocument::fromJson(json.toUtf8()).object();
    const QString sku     = o.value(QStringLiteral("sku")).toString();
    const QString asin    = o.value(QStringLiteral("asin")).toString();
    const QString country = o.value(QStringLiteral("country")).toString();
    // Set for BOTH "no entry matches this SKU" and "entry found but missing
    // a field the form requires (email/URL, address)" — same dialog either way.
    const QString issue   = o.value(QStringLiteral("issue")).toString();

    // The worker is paused on its stdin until we answer.
    QMessageBox box(this);
    box.setWindowTitle(tr("GSPR — manufacturer missing or incomplete"));
    box.setIcon(QMessageBox::Question);
    box.setText(tr("SKU \"%1\" (ASIN %2, %3): %4.")
                    .arg(sku, asin, country,
                         issue.isEmpty() ? tr("no manufacturer found") : issue));
    box.setInformativeText(tr("Fix it in one of the supplier xlsx files in\n%1\n"
                              "save the file, then click Done. Only files saved "
                              "since the last read are re-parsed.\n\n"
                              "Skip excludes this product permanently (any "
                              "country) — it can be un-skipped from the "
                              "skipped-products table.")
                               .arg(ui->lineEditFactoryFolder->text()));
    QPushButton *doneBtn = box.addButton(tr("Done"), QMessageBox::AcceptRole);
    QPushButton *skipBtn = box.addButton(tr("Skip"), QMessageBox::ActionRole);
    box.addButton(tr("Stop"), QMessageBox::RejectRole);
    box.setDefaultButton(doneBtn);
    box.exec();

    if (box.clickedButton() == doneBtn) {
        QString error;
        if (!m_manufacturers.reload(&error))
            _logToFile(tr("manufacturer store: %1").arg(error));
        const QJsonObject reply{
            {QStringLiteral("cmd"), QStringLiteral("done")},
            {QStringLiteral("manufacturers"), m_manufacturers.entriesJson()}};
        m_runner->sendLineToWorker(QJsonDocument(reply).toJson(QJsonDocument::Compact));
    } else if (box.clickedButton() == skipBtn) {
        m_skippedTable->recordSkip(asin, sku);
        m_runner->sendLineToWorker(
            QJsonDocument(QJsonObject{{QStringLiteral("cmd"), QStringLiteral("skip")}})
                .toJson(QJsonDocument::Compact));
    } else {
        m_stopRequested = true;
        m_runner->sendLineToWorker(
            QJsonDocument(QJsonObject{{QStringLiteral("cmd"), QStringLiteral("stop")}})
                .toJson(QJsonDocument::Compact));
    }
}

void PaneGspr::_logToFile(const QString &line)
{
    if (m_logFilePath.isEmpty())
        return;
    QFile f(m_logFilePath);
    if (!f.open(QIODevice::Append | QIODevice::Text))
        return;
    QTextStream(&f) << QDateTime::currentDateTime().toString(Qt::ISODate)
                    << ' ' << line << '\n';
}

void PaneGspr::_browseFactoryFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this,
        tr("Select manufacturer folder"),
        ui->lineEditFactoryFolder->text());
    if (dir.isEmpty())
        return;
    ui->lineEditFactoryFolder->setText(dir);
    QSettings().setValue(SETTINGS_FACTORY_FOLDER, dir);
}
