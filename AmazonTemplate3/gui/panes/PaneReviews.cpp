#include "PaneReviews.h"
#include "ui_PaneReviews.h"

#include <algorithm>

#include "AbstractCli.h"
#include "AmazonMarketplace.h"
#include "ProgressDialog.h"

#include <QCoro/QCoroFuture>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QFile>
#include <QFuture>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPromise>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QSharedPointer>
#include <QStyledItemDelegate>
#include <QTextEdit>
#include <QTextStream>
#include <QUrl>

#include "../../common/workingdirectory/WorkingDirectoryManager.h"

namespace {
const auto SETTINGS_SELECTED_CLI = QStringLiteral("reviews/selectedCli");
const auto REVIEWS_DUMP_DIR     = QStringLiteral("/tmp/reviews");

struct MarketplaceOption {
    QString label;
    QString region;
    QString countryCode;
    QString countryName;
};

class ReviewTextDelegate : public QStyledItemDelegate
{
public:
    explicit ReviewTextDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        auto *editor = new QPlainTextEdit(parent);
        editor->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        editor->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        editor->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        return editor;
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        auto *edit = qobject_cast<QPlainTextEdit *>(editor);
        if (edit) {
            edit->setPlainText(index.data(Qt::EditRole).toString());
            edit->selectAll();
        }
    }

    void setModelData(QWidget *, QAbstractItemModel *, const QModelIndex &) const override
    {
        // setData is not implemented in TableReviews, so edits are never persisted
    }

    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &) const override
    {
        QRect rect = option.rect;
        if (rect.height() < 160) {
            rect.setHeight(160);
        }
        editor->setGeometry(rect);
    }
};
}

PaneReviews::PaneReviews(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PaneReviews)
{
    ui->setupUi(this);

    m_runner = new CaseWorkerRunner(this);
    m_nam = new QNetworkAccessManager(this);
    m_reviewsTable = new TableReviews(this);

    ui->tableViewReviews->setModel(m_reviewsTable);
    ui->tableViewReviews->verticalHeader()->setDefaultSectionSize(64);
    ui->tableViewReviews->horizontalHeader()->setSectionResizeMode(TableReviews::ColImage, QHeaderView::ResizeToContents);
    ui->tableViewReviews->horizontalHeader()->setSectionResizeMode(TableReviews::ColCountry, QHeaderView::ResizeToContents);
    ui->tableViewReviews->horizontalHeader()->setSectionResizeMode(TableReviews::ColStars, QHeaderView::ResizeToContents);
    ui->tableViewReviews->horizontalHeader()->setSectionResizeMode(TableReviews::ColLink, QHeaderView::ResizeToContents);
    ui->tableViewReviews->horizontalHeader()->setSectionResizeMode(TableReviews::ColAsin, QHeaderView::ResizeToContents);
    ui->tableViewReviews->horizontalHeader()->setSectionResizeMode(TableReviews::ColReview, QHeaderView::Stretch);
    ui->tableViewReviews->horizontalHeader()->setSectionResizeMode(TableReviews::ColTranslation, QHeaderView::Stretch);
    ui->tableViewReviews->horizontalHeader()->setSectionResizeMode(TableReviews::ColDate, QHeaderView::ResizeToContents);

    ui->tableViewReviews->setItemDelegateForColumn(TableReviews::ColReview, new ReviewTextDelegate(this));
    ui->tableViewReviews->setItemDelegateForColumn(TableReviews::ColTranslation, new ReviewTextDelegate(this));
    ui->tableViewReviews->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    ui->tableViewReviews->setSortingEnabled(true);
    ui->tableViewReviews->sortByColumn(TableReviews::ColDate, Qt::DescendingOrder);

    // Populate marketplaces in combo box
    ui->comboBoxAmazon->addItem(tr("All Amazon (EU, NA, JP)"), QStringLiteral("ALL"));
    ui->comboBoxAmazon->addItem(tr("European Union (All)"), QStringLiteral("EU_ALL"));

    // Add individual marketplaces
    const QList<MarketplaceOption> options = {
        {QStringLiteral("United Kingdom (GB)"), QStringLiteral("eu"), QStringLiteral("GB"), QStringLiteral("United Kingdom")},
        {QStringLiteral("Germany (DE)"), QStringLiteral("eu"), QStringLiteral("DE"), QStringLiteral("Germany")},
        {QStringLiteral("France (FR)"), QStringLiteral("eu"), QStringLiteral("FR"), QStringLiteral("France")},
        {QStringLiteral("Italy (IT)"), QStringLiteral("eu"), QStringLiteral("IT"), QStringLiteral("Italy")},
        {QStringLiteral("Spain (ES)"), QStringLiteral("eu"), QStringLiteral("ES"), QStringLiteral("Spain")},
        {QStringLiteral("Netherlands (NL)"), QStringLiteral("eu"), QStringLiteral("NL"), QStringLiteral("Netherlands")},
        {QStringLiteral("Sweden (SE)"), QStringLiteral("eu"), QStringLiteral("SE"), QStringLiteral("Sweden")},
        {QStringLiteral("Poland (PL)"), QStringLiteral("eu"), QStringLiteral("PL"), QStringLiteral("Poland")},
        {QStringLiteral("Belgium (BE)"), QStringLiteral("eu"), QStringLiteral("BE"), QStringLiteral("Belgium")},
        {QStringLiteral("United States (US)"), QStringLiteral("na"), QStringLiteral("US"), QStringLiteral("United States")},
        {QStringLiteral("Canada (CA)"), QStringLiteral("na"), QStringLiteral("CA"), QStringLiteral("Canada")},
        {QStringLiteral("Mexico (MX)"), QStringLiteral("na"), QStringLiteral("MX"), QStringLiteral("Mexico")},
        {QStringLiteral("Japan (JP)"), QStringLiteral("jp"), QStringLiteral("JP"), QStringLiteral("Japan")}
    };

    for (const auto &opt : options) {
        QJsonObject o{
            {QStringLiteral("region"), opt.region},
            {QStringLiteral("countryCode"), opt.countryCode},
            {QStringLiteral("countryName"), opt.countryName}
        };
        ui->comboBoxAmazon->addItem(opt.label, o);
    }

    connect(ui->comboBoxCli, &QComboBox::currentIndexChanged, this, &PaneReviews::_onCliChanged);
    connect(ui->buttonRun, &QPushButton::clicked, this, &PaneReviews::_onRun);
    connect(ui->buttonRunAll, &QPushButton::clicked, this, &PaneReviews::_onRunAll);
    connect(ui->buttonStop, &QPushButton::clicked, this, &PaneReviews::_onStop);
    connect(ui->buttonTranslate, &QPushButton::clicked, this, &PaneReviews::_onTranslate);
    connect(ui->buttonRemove, &QPushButton::clicked, this, &PaneReviews::_onRemove);
    connect(ui->tableViewReviews, &QTableView::doubleClicked, this, &PaneReviews::_onTableDoubleClicked);
}

PaneReviews::~PaneReviews()
{
    _saveCache();
    delete ui;
}

void PaneReviews::_ensureReviewsDir()
{
    if (m_workingDir.path().isEmpty() || m_workingDir.path() == QStringLiteral(".")) {
        m_workingDir = WorkingDirectoryManager::instance()->workingDir();
    }
    m_reviewsDir = QDir(m_workingDir.filePath(QStringLiteral("reviews")));
    m_imagesDir  = QDir(m_reviewsDir.filePath(QStringLiteral("images")));
    m_reviewsDir.mkpath(QStringLiteral("."));
    m_imagesDir.mkpath(QStringLiteral("."));
}

void PaneReviews::setWorkingDir(const QDir &workingDir)
{
    m_workingDir = workingDir;
    _ensureReviewsDir();
    _loadCache();
}

void PaneReviews::setAvailableClis(const QList<AbstractCli *> &clis)
{
    m_availableClis = clis;

    QSignalBlocker b(ui->comboBoxCli);
    ui->comboBoxCli->clear();
    for (AbstractCli *cli : clis)
        ui->comboBoxCli->addItem(cli->getName(), QVariant::fromValue(cli));

    const QString saved = QSettings().value(SETTINGS_SELECTED_CLI).toString();
    int restored = -1;
    for (int i = 0; i < clis.size(); ++i) {
        if (clis[i]->getName() == saved) {
            restored = i;
            break;
        }
    }

    int fallback = 0;
    for (int i = 0; i < clis.size(); ++i) {
        if (!clis[i]->canGenImages()) {
            fallback = i;
            break;
        }
    }

    ui->comboBoxCli->setCurrentIndex(restored >= 0 ? restored : fallback);
}

void PaneReviews::_onCliChanged(int index)
{
    if (index < 0 || index >= m_availableClis.size()) return;
    QSettings().setValue(SETTINGS_SELECTED_CLI, m_availableClis[index]->getName());
}

AbstractCli *PaneReviews::_selectedCli() const
{
    return ui->comboBoxCli->currentData().value<AbstractCli *>();
}

void PaneReviews::_setRunning(bool running)
{
    m_running = running;
    ui->buttonRun->setEnabled(!running);
    ui->buttonRunAll->setEnabled(!running);
    ui->buttonStop->setEnabled(running);
    ui->buttonTranslate->setEnabled(!running);
    ui->buttonRemove->setEnabled(!running);

    ui->labelStatus->setText(running ? tr("Running…") : tr("Ready (%n review(s))", nullptr, m_reviewsTable->rowCount()));
}

void PaneReviews::_onRun()
{
    const QVariant data = ui->comboBoxAmazon->currentData();
    QList<ReviewTarget> targets;

    if (data.typeId() == QMetaType::QString) {
        const QString s = data.toString();
        if (s == QLatin1String("ALL")) {
            _onRunAll();
            return;
        }
        if (s == QLatin1String("EU_ALL")) {
            const QList<QPair<QString, QString>> euList = {
                {QStringLiteral("GB"), QStringLiteral("United Kingdom")},
                {QStringLiteral("DE"), QStringLiteral("Germany")},
                {QStringLiteral("FR"), QStringLiteral("France")},
                {QStringLiteral("IT"), QStringLiteral("Italy")},
                {QStringLiteral("ES"), QStringLiteral("Spain")},
                {QStringLiteral("NL"), QStringLiteral("Netherlands")},
                {QStringLiteral("SE"), QStringLiteral("Sweden")},
                {QStringLiteral("PL"), QStringLiteral("Poland")},
                {QStringLiteral("BE"), QStringLiteral("Belgium")},
            };
            for (const auto &p : euList) {
                targets.append(ReviewTarget{QStringLiteral("eu"), p.first, p.second});
            }
            _runTargets(targets);
            return;
        }
    }

    if (data.canConvert<QJsonObject>()) {
        const QJsonObject o = data.toJsonObject();
        targets.append(ReviewTarget{
            o.value(QStringLiteral("region")).toString(),
            o.value(QStringLiteral("countryCode")).toString(),
            o.value(QStringLiteral("countryName")).toString()
        });
        _runTargets(targets);
    }
}

void PaneReviews::_onRunAll()
{
    QList<ReviewTarget> targets = {
        // EU
        {QStringLiteral("eu"), QStringLiteral("GB"), QStringLiteral("United Kingdom")},
        {QStringLiteral("eu"), QStringLiteral("DE"), QStringLiteral("Germany")},
        {QStringLiteral("eu"), QStringLiteral("FR"), QStringLiteral("France")},
        {QStringLiteral("eu"), QStringLiteral("IT"), QStringLiteral("Italy")},
        {QStringLiteral("eu"), QStringLiteral("ES"), QStringLiteral("Spain")},
        {QStringLiteral("eu"), QStringLiteral("NL"), QStringLiteral("Netherlands")},
        {QStringLiteral("eu"), QStringLiteral("SE"), QStringLiteral("Sweden")},
        {QStringLiteral("eu"), QStringLiteral("PL"), QStringLiteral("Poland")},
        {QStringLiteral("eu"), QStringLiteral("BE"), QStringLiteral("Belgium")},
        // NA
        {QStringLiteral("na"), QStringLiteral("US"), QStringLiteral("United States")},
        {QStringLiteral("na"), QStringLiteral("CA"), QStringLiteral("Canada")},
        {QStringLiteral("na"), QStringLiteral("MX"), QStringLiteral("Mexico")},
        // JP
        {QStringLiteral("jp"), QStringLiteral("JP"), QStringLiteral("Japan")}
    };

    _runTargets(targets);
}

void PaneReviews::_onStop()
{
    if (!m_running) return;
    m_stopRequested = true;
    ui->buttonStop->setEnabled(false);
    _logToFile(QStringLiteral("stop requested by user"));
    m_runner->stopAll();
}

void PaneReviews::_onRemove()
{
    QModelIndexList rows = ui->tableViewReviews->selectionModel()->selectedRows();
    if (rows.isEmpty()) return;

    std::sort(rows.begin(), rows.end(),
              [](const QModelIndex &a, const QModelIndex &b) { return a.row() > b.row(); });

    for (const QModelIndex &idx : rows) {
        m_reviewsTable->removeAt(idx.row());
    }
    _saveCache();
    ui->labelStatus->setText(tr("%n review(s)", nullptr, m_reviewsTable->rowCount()));
}

void PaneReviews::_onTableDoubleClicked(const QModelIndex &index)
{
    if (!index.isValid() || index.row() >= m_reviewsTable->rowCount()) return;
    if (index.column() == TableReviews::ColLink) {
        const ReviewItem &r = m_reviewsTable->reviewAt(index.row());
        if (!r.link.isEmpty()) {
            QDesktopServices::openUrl(QUrl(r.link));
        }
    }
}

void PaneReviews::_logToFile(const QString &line)
{
    if (m_logFilePath.isEmpty()) return;
    QFile f(m_logFilePath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&f);
        out << line << "\n";
    }
}

bool PaneReviews::_applyLiveReview(const QString &json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject()) return false;
    const ReviewItem r = ReviewItem::parse(doc.object());
    if (r.id.isEmpty() && r.title.isEmpty() && r.text.isEmpty()) return false;

    const bool isNew = m_reviewsTable->addOrUpdateReview(r);

    // Cache image if available
    const QString key = !r.asin.isEmpty() ? r.asin : r.id;
    if (!r.imageUrl.isEmpty()) {
        _cacheImageAsync(key, r.imageUrl);
    }

    _saveCache();
    ui->labelStatus->setText(tr("Scraping… %n review(s) in table", nullptr, m_reviewsTable->rowCount()));
    return isNew;
}

void PaneReviews::_cacheImageAsync(const QString &idOrAsin, const QString &imageUrl)
{
    if (idOrAsin.isEmpty() || imageUrl.isEmpty()) return;

    _ensureReviewsDir();
    const QString localFile = m_imagesDir.filePath(idOrAsin + QStringLiteral(".jpg"));
    if (QFile::exists(localFile)) {
        QPixmap px(localFile);
        if (!px.isNull()) {
            m_reviewsTable->updateImage(idOrAsin, px.scaled(58, 58, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            return;
        }
    }

    if (!imageUrl.startsWith(QLatin1String("http://")) && !imageUrl.startsWith(QLatin1String("https://")))
        return;

    QNetworkReply *reply = m_nam->get(QNetworkRequest(QUrl(imageUrl)));
    connect(reply, &QNetworkReply::finished, this, [this, reply, idOrAsin, localFile]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            const QByteArray data = reply->readAll();
            QPixmap px;
            if (px.loadFromData(data)) {
                QFile f(localFile);
                if (f.open(QIODevice::WriteOnly)) {
                    f.write(data);
                    f.close();
                }
                m_reviewsTable->updateImage(idOrAsin, px.scaled(58, 58, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            }
        }
    });
}

void PaneReviews::_loadCache()
{
    _ensureReviewsDir();
    const QString cachePath = m_reviewsDir.filePath(QStringLiteral("reviews.json"));
    QFile f(cachePath);
    if (!f.exists() || !f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isArray()) return;

    QList<ReviewItem> list;
    for (const QJsonValue &v : doc.array()) {
        ReviewItem r = ReviewItem::parse(v.toObject());
        list.append(r);

        const QString key = !r.asin.isEmpty() ? r.asin : r.id;
        const QString imgPath = m_imagesDir.filePath(key + QStringLiteral(".jpg"));
        if (QFile::exists(imgPath)) {
            QPixmap px(imgPath);
            if (!px.isNull()) {
                m_reviewsTable->updateImage(key, px.scaled(58, 58, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            }
        } else if (!r.imageUrl.isEmpty()) {
            _cacheImageAsync(key, r.imageUrl);
        }
    }

    m_reviewsTable->setReviews(list);
    ui->labelStatus->setText(tr("Loaded %n review(s) from cache", nullptr, list.size()));
}

void PaneReviews::_saveCache()
{
    _ensureReviewsDir();
    const QString cachePath = m_reviewsDir.filePath(QStringLiteral("reviews.json"));
    QJsonArray arr;
    for (const ReviewItem &r : m_reviewsTable->reviews()) {
        arr.append(r.toJson());
    }

    QSaveFile sf(cachePath);
    if (sf.open(QIODevice::WriteOnly | QIODevice::Text)) {
        sf.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
        sf.commit();
    }
}

void PaneReviews::_runTargets(const QList<ReviewTarget> &targets)
{
    if (m_running || targets.isEmpty()) return;

    QString why;
    if (!m_runner->isConfigured(&why)) {
        QMessageBox::warning(this, tr("Reviews"), why);
        return;
    }

    m_logFilePath = QStringLiteral("/tmp/reviews-run-%1.log")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss")));

    ProgressDlgHandles h;
    QDialog *dlg = makeProgressDlg(this, tr("Customer Reviews — Seller Central"), &h);
    if (h.status)
        h.status->setText(tr("Processing %n marketplace(s)…", nullptr, targets.size()));
    dlg->show();

    struct RunStats {
        int retrievedCount = 0;
        int newCount = 0;
    };
    auto stats = std::make_shared<RunStats>();

    auto appendLog = [this, h, stats](const QString &line) {
        const auto marker = QLatin1String("@@review-result ");
        if (line.startsWith(marker)) {
            stats->retrievedCount++;
            if (_applyLiveReview(line.mid(marker.size()))) {
                stats->newCount++;
            }
            _logToFile(line);
            return;
        }
        if (h.log) h.log->append(line);
        _logToFile(line);
    };

    QMetaObject::Connection logConn =
        connect(m_runner, &CaseWorkerRunner::logMessage, this, appendLog);

    QStringList names;
    for (const ReviewTarget &t : targets) names << t.countryCode;
    appendLog(tr("Scraping reviews started for: %1 (log: %2)")
                  .arg(names.join(QStringLiteral(", ")), m_logFilePath));

    m_stopRequested = false;
    _setRunning(true);

    m_runner->reviews(targets, REVIEWS_DUMP_DIR, this,
                      [this, h, logConn, stats](const QList<ReviewsResult> &results) {
        _setRunning(false);
        disconnect(logConn);

        int totalFromResults = 0;
        for (const ReviewsResult &r : results) {
            totalFromResults += r.reviews.size();
            for (const ReviewItem &item : r.reviews) {
                if (m_reviewsTable->addOrUpdateReview(item)) {
                    stats->newCount++;
                }
                const QString key = !item.asin.isEmpty() ? item.asin : item.id;
                if (!item.imageUrl.isEmpty())
                    _cacheImageAsync(key, item.imageUrl);
            }
        }

        const int totalRetrieved = std::max(stats->retrievedCount, totalFromResults);
        _saveCache();

        if (h.status) {
            if (totalRetrieved > 0) {
                h.status->setText(tr("Finished: %1 review(s) retrieved (%2 new, %3 total in table).")
                                      .arg(totalRetrieved).arg(stats->newCount).arg(m_reviewsTable->rowCount()));
            } else {
                h.status->setText(tr("Finished: no new reviews retrieved (%1 total in table).")
                                      .arg(m_reviewsTable->rowCount()));
            }
        }
        if (h.closeBtn)
            h.closeBtn->setEnabled(true);

        ui->labelStatus->setText(tr("Finished: %n review(s) in table", nullptr, m_reviewsTable->rowCount()));
    });
}

void PaneReviews::_onTranslate()
{
    if (m_running) return;

    AbstractCli *cli = _selectedCli();
    if (!cli || !cli->isInstalled()) {
        QMessageBox::warning(this, tr("Translation"),
            tr("Please select an installed AI CLI in the combo box first."));
        return;
    }

    // Keep the coroutine Task in a member to prevent early frame destruction
    m_translateTask = _translateReviews();
}

QCoro::Task<void> PaneReviews::_translateReviews()
{
    AbstractCli *cli = _selectedCli();
    if (!cli) co_return;

    const auto &reviews = m_reviewsTable->reviews();
    QList<int> pendingIndices;
    for (int i = 0; i < reviews.size(); ++i) {
        if (!reviews[i].translationChecked && reviews[i].translation.isEmpty()) {
            pendingIndices.append(i);
        }
    }

    if (pendingIndices.isEmpty()) {
        QMessageBox::information(this, tr("Translation"),
            tr("All reviews already have translations or are in original English/French."));
        co_return;
    }

    _setRunning(true);
    ui->labelStatus->setText(tr("Translating %n review(s)…", nullptr, pendingIndices.size()));

    ProgressDlgHandles h;
    QDialog *dlg = makeProgressDlg(this, tr("Translating Reviews — %1").arg(cli->getName()), &h);
    if (h.status)
        h.status->setText(tr("Translating %n review(s) into English…", nullptr, pendingIndices.size()));
    if (h.bar) {
        h.bar->setRange(0, pendingIndices.size());
        h.bar->setValue(0);
    }
    dlg->show();

    int doneCount = 0;
    for (int row : pendingIndices) {
        if (m_stopRequested) break;

        const ReviewItem &item = m_reviewsTable->reviewAt(row);
        if (h.status) {
            h.status->setText(tr("[%1/%2] Translating review from %3 (%4)…")
                                  .arg(doneCount + 1)
                                  .arg(pendingIndices.size())
                                  .arg(item.country, item.asin));
        }

        const QString prompt = QStringLiteral(
            "You are a translation assistant for product customer reviews.\n"
            "Task:\n"
            "Examine the following customer review from Amazon (%1).\n"
            "1. If the review is ALREADY written in English or in French, output exactly and ONLY:\n"
            "[NO_TRANSLATION_NEEDED]\n\n"
            "2. If the review is in ANY OTHER LANGUAGE (e.g. German, Italian, Spanish, Japanese, Chinese, Dutch, Polish, etc.), "
            "translate it into fluent English.\n"
            "Output format:\n"
            "Title: <translated title>\n"
            "Review: <translated body>\n"
            "Do NOT include any commentary, explanations, or quotes.\n\n"
            "Original Title: %2\n"
            "Original Review: %3\n"
        ).arg(item.country, item.title, item.text);

        // Safe async bridge via QPromise/QFuture (GEMINI.md rule #1)
        QPromise<CliRunResult> promise;
        promise.start();
        QFuture<CliRunResult> future = promise.future();
        {
            auto sp = QSharedPointer<QPromise<CliRunResult>>::create(std::move(promise));
            cli->runPromptAsync(prompt, m_workingDir.absolutePath(), this, [sp](CliRunResult r) mutable {
                sp->addResult(std::move(r));
                sp->finish();
            });
        }

        const CliRunResult r = co_await qCoro(future).result();
        if (r.exitCode == 0) {
            QString out = r.output.trimmed();
            if (out.contains(QLatin1String("[NO_TRANSLATION_NEEDED]"))) {
                m_reviewsTable->updateTranslation(row, QString());
            } else {
                m_reviewsTable->updateTranslation(row, out);
            }
            if (h.log) {
                const QString displaySummary = out.contains(QLatin1String("[NO_TRANSLATION_NEEDED]"))
                    ? tr("(original in EN/FR — left empty)")
                    : out.left(80);
                h.log->append(tr("[%1] %2 -> %3").arg(item.country, item.asin, displaySummary));
            }
        } else {
            if (h.log) {
                h.log->append(tr("[%1] %2 ERROR: %3").arg(item.country, item.asin, r.errorOutput.trimmed()));
            }
        }

        doneCount++;
        if (h.bar) h.bar->setValue(doneCount);
        _saveCache();
    }

    _setRunning(false);
    if (h.status)
        h.status->setText(tr("Translation completed (%1/%2 processed).").arg(doneCount).arg(pendingIndices.size()));
    if (h.closeBtn)
        h.closeBtn->setEnabled(true);

    ui->labelStatus->setText(tr("Translation finished (%n review(s))", nullptr, m_reviewsTable->rowCount()));
}
