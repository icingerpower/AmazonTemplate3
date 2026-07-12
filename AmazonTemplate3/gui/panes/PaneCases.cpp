#include "PaneCases.h"
#include "ui_PaneCases.h"

#include "AbstractCli.h"
#include "CaseWorkerRunner.h"
#include "DialogReviewReplies.h"
#include "ProgressDialog.h"

#include <algorithm>

#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QFile>
#include <QFont>
#include <QGuiApplication>
#include <QHideEvent>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QTextEdit>
#include <QUrl>

// Item-data roles on the tree items.
namespace {
constexpr int RoleType      = Qt::UserRole + 1; // "group" | "case"
constexpr int RoleGroupName = Qt::UserRole + 2; // owning group's name (both node kinds)
constexpr int RoleCaseId    = Qt::UserRole + 3; // case id (case nodes only)

// Region case-lobby URLs shown in comboBoxCasesUrl; userData carries the region.
struct RegionUrl { const char *region; const char *url; };
const RegionUrl kRegionUrls[] = {
    {"eu", "https://sellercentral.amazon.co.uk/cu/case-lobby"},
    {"na", "https://sellercentral.amazon.com/cu/case-lobby"},
    {"jp", "https://sellercentral.amazon.co.jp/cu/case-lobby"},
};

// Marketplaces offered in comboBoxAccount per region (the "Select an account"
// country names). The first entry is always an empty placeholder.
QStringList accountsForRegion(const QString &region)
{
    if (region == QLatin1String("na"))
        return {QStringLiteral("United States"), QStringLiteral("Canada"), QStringLiteral("Mexico"), QStringLiteral("Brazil")};
    if (region == QLatin1String("jp"))
        return {QStringLiteral("Japan")};
    return {QStringLiteral("United Kingdom"), QStringLiteral("Germany"), QStringLiteral("France"),
            QStringLiteral("Italy"), QStringLiteral("Spain"), QStringLiteral("Netherlands"),
            QStringLiteral("Belgium"), QStringLiteral("Ireland"), QStringLiteral("Poland"),
            QStringLiteral("Sweden"), QStringLiteral("Turkey")};
}
} // namespace

PaneCases::PaneCases(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PaneCases)
{
    ui->setupUi(this);

    m_runner = new CaseWorkerRunner(this);
    connect(m_runner, &CaseWorkerRunner::logMessage, this, [this](const QString &msg) {
        if (!m_progressLog) return;
        const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
        m_progressLog->append(QStringLiteral("[%1] %2").arg(ts, msg));
    });

    m_model = new QStandardItemModel(this);
    ui->treeViewCases->setModel(m_model);
    ui->treeViewCases->setUniformRowHeights(true);
    ui->treeViewCases->setAlternatingRowColors(true);

    _populateUrlCombo();
    _connectSlots();
    _syncDetailToSelection();
}

PaneCases::~PaneCases()
{
    delete ui;
}

// ---------------------------------------------------------------------------
// setup
// ---------------------------------------------------------------------------

void PaneCases::_populateUrlCombo()
{
    QSignalBlocker b(ui->comboBoxCasesUrl);
    for (const RegionUrl &r : kRegionUrls)
        ui->comboBoxCasesUrl->addItem(QString::fromLatin1(r.url), QString::fromLatin1(r.region));
}

void PaneCases::_populateAccountCombo(const QString &region)
{
    QSignalBlocker b(ui->comboBoxAccount);
    ui->comboBoxAccount->clear();
    ui->comboBoxAccount->addItem(tr("— select marketplace —"), QString()); // empty placeholder
    for (const QString &country : accountsForRegion(region))
        ui->comboBoxAccount->addItem(country, country);
}

void PaneCases::_connectSlots()
{
    connect(ui->treeViewCases->selectionModel(), &QItemSelectionModel::currentChanged,
            this, [this](const QModelIndex &, const QModelIndex &) { _onSelectionChanged(); });
    connect(m_model, &QStandardItemModel::itemChanged, this, [this](QStandardItem *item) {
        if (m_populating || item->column() != 0) return;
        if (item->data(RoleType).toString() != QLatin1String("case")) return;
        CaseGroup *g = _group(item->data(RoleGroupName).toString());
        if (!g) return;
        const bool done = item->checkState() == Qt::Checked;
        g->setCaseDone(item->data(RoleCaseId).toString(), done);
        g->save();
        // Reflect done state as strikeout on both columns (guarded — setFont
        // re-emits itemChanged).
        m_populating = true;
        QFont f = item->font();
        f.setStrikeOut(done);
        item->setFont(f);
        if (QStandardItem *parent = item->parent()) {
            if (QStandardItem *sib = parent->child(item->row(), 1))
                sib->setFont(f);
        }
        m_populating = false;
    });

    connect(ui->buttonAddGroup,      &QPushButton::clicked, this, &PaneCases::_onAddGroup);
    connect(ui->buttonAddCase,       &QPushButton::clicked, this, &PaneCases::_onAddCase);
    connect(ui->buttonMarkCaseDone,  &QPushButton::clicked, this, &PaneCases::_onMarkCaseDone);
    connect(ui->buttonOpenFolder,    &QPushButton::clicked, this, &PaneCases::_onOpenFolder);
    connect(ui->buttonCopyUrl,       &QPushButton::clicked, this, &PaneCases::_onCopyUrl);
    connect(ui->buttonRunSelected,   &QPushButton::clicked, this, &PaneCases::_onRunSelected);
    connect(ui->buttonRunAll,        &QPushButton::clicked, this, &PaneCases::_onRunAll);

    connect(ui->comboBoxCasesUrl, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { _onRegionChanged(); });
    connect(ui->comboBoxAccount, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { _onAccountChanged(); });
    connect(ui->textEditPrompt, &QTextEdit::textChanged, this, &PaneCases::_onPromptChanged);
    connect(ui->comboBoxCli, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PaneCases::_onCliChanged);
}

void PaneCases::setWorkingDir(const QDir &workingDir)
{
    m_workingDir = workingDir;
    _reloadGroups();
}

void PaneCases::setAvailableClis(const QList<AbstractCli *> &clis)
{
    m_availableClis = clis;

    QSignalBlocker b(ui->comboBoxCli);
    ui->comboBoxCli->clear();
    for (AbstractCli *cli : clis)
        ui->comboBoxCli->addItem(cli->getName(), QVariant::fromValue(cli));

    const QString saved = QSettings().value(QStringLiteral("cases/selectedCli")).toString();
    int restored = -1;
    for (int i = 0; i < clis.size(); ++i)
        if (clis[i]->getName() == saved) { restored = i; break; }

    // Default to a text-oriented CLI: image/agentic CLIs (e.g. Antigravity) are a
    // poor fit for drafting text replies and can be very slow. Prefer one that
    // is not primarily an image generator.
    int fallback = 0;
    for (int i = 0; i < clis.size(); ++i)
        if (!clis[i]->canGenImages()) { fallback = i; break; }

    ui->comboBoxCli->setCurrentIndex(restored >= 0 ? restored : fallback);
}

// ---------------------------------------------------------------------------
// groups / tree
// ---------------------------------------------------------------------------

void PaneCases::_reloadGroups()
{
    m_groups = CaseGroup::loadAll(m_workingDir);
    m_currentGroupName.clear();
    _rebuildTree();
    _syncDetailToSelection();
}

void PaneCases::_rebuildTree()
{
    m_populating = true;
    m_model->clear();
    m_model->setHorizontalHeaderLabels({tr("Case"), tr("Subject")});

    for (const CaseGroup &g : m_groups) {
        auto *gi = new QStandardItem(g.name());
        QFont bold = gi->font();
        bold.setBold(true);
        gi->setFont(bold);
        gi->setEditable(false);
        gi->setData(QStringLiteral("group"), RoleType);
        gi->setData(g.name(), RoleGroupName);

        auto *gr = new QStandardItem(g.region().toUpper());
        gr->setEditable(false);
        gr->setData(QStringLiteral("group"), RoleType);
        gr->setData(g.name(), RoleGroupName);

        for (const CaseGroup::Case &c : g.cases()) {
            auto *ci = new QStandardItem(c.caseId);
            ci->setEditable(false);
            ci->setCheckable(true);
            ci->setCheckState(c.done ? Qt::Checked : Qt::Unchecked);
            ci->setData(QStringLiteral("case"), RoleType);
            ci->setData(g.name(), RoleGroupName);
            ci->setData(c.caseId, RoleCaseId);

            auto *cs = new QStandardItem(c.subject);
            cs->setEditable(false);
            cs->setData(QStringLiteral("case"), RoleType);
            cs->setData(g.name(), RoleGroupName);
            cs->setData(c.caseId, RoleCaseId);

            if (c.done) {
                QFont sf = ci->font();
                sf.setStrikeOut(true);
                ci->setFont(sf);
                cs->setFont(sf);
            }
            gi->appendRow({ci, cs});
        }
        m_model->appendRow({gi, gr});
    }

    ui->treeViewCases->expandAll();
    ui->treeViewCases->resizeColumnToContents(0);
    m_populating = false;
}

CaseGroup *PaneCases::_group(const QString &name)
{
    for (CaseGroup &g : m_groups)
        if (g.name() == name)
            return &g;
    return nullptr;
}

CaseGroup *PaneCases::_selectedGroup()
{
    const QModelIndex idx = ui->treeViewCases->currentIndex();
    if (!idx.isValid()) return nullptr;
    QStandardItem *item = m_model->itemFromIndex(idx.sibling(idx.row(), 0));
    if (!item) return nullptr;
    return _group(item->data(RoleGroupName).toString());
}

QString PaneCases::_selectedCaseId() const
{
    const QModelIndex idx = ui->treeViewCases->currentIndex();
    if (!idx.isValid()) return {};
    QStandardItem *item = m_model->itemFromIndex(idx.sibling(idx.row(), 0));
    if (!item || item->data(RoleType).toString() != QLatin1String("case")) return {};
    return item->data(RoleCaseId).toString();
}

void PaneCases::_selectGroupInTree(const QString &name)
{
    for (int r = 0; r < m_model->rowCount(); ++r) {
        QStandardItem *gi = m_model->item(r, 0);
        if (gi && gi->data(RoleGroupName).toString() == name) {
            const QModelIndex idx = gi->index();
            ui->treeViewCases->setCurrentIndex(idx);
            ui->treeViewCases->expand(idx);
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// selection ↔ detail panel
// ---------------------------------------------------------------------------

void PaneCases::_onSelectionChanged()
{
    if (m_populating) return;
    _flushCurrentPrompt();
    _syncDetailToSelection();
}

void PaneCases::_syncDetailToSelection()
{
    CaseGroup *g = _selectedGroup();

    QSignalBlocker b1(ui->comboBoxCasesUrl);
    QSignalBlocker b2(ui->textEditPrompt);
    QSignalBlocker b3(ui->comboBoxAccount);
    QString account;
    if (!g) {
        m_currentGroupName.clear();
        ui->textEditPrompt->clear();
        ui->comboBoxCasesUrl->setCurrentIndex(0);
        _populateAccountCombo(QStringLiteral("eu"));
    } else {
        m_currentGroupName = g->name();
        ui->textEditPrompt->setPlainText(g->instructions());
        const int i = ui->comboBoxCasesUrl->findData(g->region());
        ui->comboBoxCasesUrl->setCurrentIndex(i >= 0 ? i : 0);
        _populateAccountCombo(g->region());
        account = g->account();
        const int ai = ui->comboBoxAccount->findData(account);
        ui->comboBoxAccount->setCurrentIndex(ai >= 0 ? ai : 0);
    }

    const bool hasGroup = g != nullptr;
    const bool hasCase  = !_selectedCaseId().isEmpty();
    ui->buttonAddCase->setEnabled(hasGroup);
    ui->buttonMarkCaseDone->setEnabled(hasCase);
    ui->buttonOpenFolder->setEnabled(hasGroup);
    ui->comboBoxAccount->setEnabled(hasGroup);
    // Run needs a group AND a chosen marketplace.
    ui->buttonRunSelected->setEnabled(hasGroup && !account.isEmpty());
    ui->textEditPrompt->setEnabled(hasGroup);
}

void PaneCases::_flushCurrentPrompt()
{
    if (m_currentGroupName.isEmpty()) return;
    if (CaseGroup *g = _group(m_currentGroupName))
        g->save();
}

// ---------------------------------------------------------------------------
// actions
// ---------------------------------------------------------------------------

void PaneCases::_onAddGroup()
{
    bool ok = false;
    const QString raw = QInputDialog::getText(this, tr("Add group"), tr("Group name:"),
                                              QLineEdit::Normal, {}, &ok);
    if (!ok) return;
    const QString name = CaseGroup::sanitizeName(raw);
    if (name.isEmpty()) {
        QMessageBox::warning(this, tr("Add group"), tr("Please enter a valid group name."));
        return;
    }
    if (_group(name)) {
        QMessageBox::information(this, tr("Add group"), tr("A group named \"%1\" already exists.").arg(name));
        _selectGroupInTree(name);
        return;
    }

    CaseGroup g(m_workingDir, name);
    const QString region = ui->comboBoxCasesUrl->currentData().toString();
    g.setRegion(region.isEmpty() ? QStringLiteral("eu") : region);
    g.save();

    m_groups.append(g);
    std::sort(m_groups.begin(), m_groups.end(),
              [](const CaseGroup &a, const CaseGroup &b) { return a.name() < b.name(); });
    _rebuildTree();
    _selectGroupInTree(name);
}

void PaneCases::_onAddCase()
{
    CaseGroup *g = _selectedGroup();
    if (!g) {
        QMessageBox::warning(this, tr("Add case"), tr("Select a group first."));
        return;
    }
    bool ok = false;
    const QString id = QInputDialog::getText(this, tr("Add case"), tr("Amazon case ID:"),
                                             QLineEdit::Normal, {}, &ok).trimmed();
    if (!ok || id.isEmpty()) return;

    // Reject duplicates across every group, not just the selected one — the same
    // case must never be tracked twice.
    for (const CaseGroup &other : m_groups) {
        if (other.findCase(id)) {
            QMessageBox::warning(this, tr("Add case"),
                other.name() == g->name()
                    ? tr("Case %1 is already in this group.").arg(id)
                    : tr("Case %1 is already in group \"%2\".").arg(id, other.name()));
            return;
        }
    }

    if (!g->addCase(id)) { // trims internally; defensive against any residual dup
        QMessageBox::warning(this, tr("Add case"), tr("Case %1 is already in this group.").arg(id));
        return;
    }
    const QString gn = g->name();
    g->save();
    _rebuildTree();
    _selectGroupInTree(gn);
}

void PaneCases::_onMarkCaseDone()
{
    const QModelIndex idx = ui->treeViewCases->currentIndex();
    if (!idx.isValid()) return;
    QStandardItem *item = m_model->itemFromIndex(idx.sibling(idx.row(), 0));
    if (!item || item->data(RoleType).toString() != QLatin1String("case")) {
        QMessageBox::warning(this, tr("Mark case"), tr("Select a case (not a group)."));
        return;
    }
    // Toggling the check state triggers the itemChanged handler, which persists.
    item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
}

void PaneCases::_onOpenFolder()
{
    CaseGroup *g = _selectedGroup();
    if (!g) return;
    g->ensureFolder();
    QDesktopServices::openUrl(QUrl::fromLocalFile(g->dir().absolutePath()));
}

void PaneCases::_onCopyUrl()
{
    QGuiApplication::clipboard()->setText(ui->comboBoxCasesUrl->currentText());
}

void PaneCases::_onRegionChanged()
{
    if (m_currentGroupName.isEmpty()) return;
    CaseGroup *g = _group(m_currentGroupName);
    if (!g) return;
    g->setRegion(ui->comboBoxCasesUrl->currentData().toString());

    // The marketplace list depends on the region — repopulate and reset the
    // choice (Run stays disabled until a marketplace is picked again).
    _populateAccountCombo(g->region());
    g->setAccount(QString());
    ui->buttonRunSelected->setEnabled(false);

    g->save();
    // Update the region cell in the tree.
    for (int r = 0; r < m_model->rowCount(); ++r) {
        QStandardItem *gi = m_model->item(r, 0);
        if (gi && gi->data(RoleGroupName).toString() == g->name()) {
            if (QStandardItem *gr = m_model->item(r, 1)) {
                m_populating = true;
                gr->setText(g->region().toUpper());
                m_populating = false;
            }
            break;
        }
    }
}

void PaneCases::_onAccountChanged()
{
    if (m_currentGroupName.isEmpty()) return;
    CaseGroup *g = _group(m_currentGroupName);
    if (!g) return;
    const QString account = ui->comboBoxAccount->currentData().toString();
    g->setAccount(account);
    g->save();
    ui->buttonRunSelected->setEnabled(!account.isEmpty());
}

void PaneCases::_onPromptChanged()
{
    if (m_currentGroupName.isEmpty()) return;
    if (CaseGroup *g = _group(m_currentGroupName))
        g->setInstructions(ui->textEditPrompt->toPlainText());
}

void PaneCases::_onCliChanged(int index)
{
    if (index < 0 || index >= m_availableClis.size()) return;
    QSettings().setValue(QStringLiteral("cases/selectedCli"), m_availableClis[index]->getName());
}

// ---------------------------------------------------------------------------
// run flow
// ---------------------------------------------------------------------------

AbstractCli *PaneCases::_selectedCli() const
{
    return ui->comboBoxCli->currentData().value<AbstractCli *>();
}

QString PaneCases::_renderThread(const CaseWorkerThread &t)
{
    QString out;
    if (!t.subject.isEmpty()) out += QStringLiteral("Subject: %1\n").arg(t.subject);
    if (!t.status.isEmpty())  out += QStringLiteral("Status: %1\n").arg(t.status);
    out += QLatin1Char('\n');
    for (const CaseWorkerThreadMessage &m : t.messages)
        out += QStringLiteral("[%1] %2:\n%3\n\n").arg(m.from, m.author, m.text);
    return out;
}

QList<RunTarget> PaneCases::_collectTargets(bool allGroups)
{
    QList<RunTarget> targets;
    if (allGroups) {
        for (const CaseGroup &g : m_groups)
            for (const CaseGroup::Case &c : g.cases())
                if (!c.done) targets.append({g.name(), g.region(), c.caseId, g.account()});
        return targets;
    }
    CaseGroup *g = _selectedGroup();
    if (!g) return targets;
    const QString cid = _selectedCaseId();
    if (!cid.isEmpty()) {
        targets.append({g->name(), g->region(), cid, g->account()}); // explicit single case, even if done
    } else {
        for (const CaseGroup::Case &c : g->cases())
            if (!c.done) targets.append({g->name(), g->region(), c.caseId, g->account()});
    }
    return targets;
}

void PaneCases::_onRunSelected() { _runTargets(_collectTargets(false)); }
void PaneCases::_onRunAll()      { _runTargets(_collectTargets(true)); }

void PaneCases::_runTargets(const QList<RunTarget> &targets)
{
    if (m_running) return;
    if (targets.isEmpty()) {
        QMessageBox::information(this, tr("Run"), tr("No cases to run (all done, or nothing selected)."));
        return;
    }
    AbstractCli *cli = _selectedCli();
    if (!cli) {
        QMessageBox::warning(this, tr("Run"), tr("Select a CLI in the combo box first."));
        return;
    }
    QString why;
    if (!m_runner->isConfigured(&why)) {
        QMessageBox::warning(this, tr("Run"), tr("The case worker is not ready:\n\n%1").arg(why));
        return;
    }
    // Every case must have a marketplace chosen for the account-switcher.
    QStringList missing;
    for (const RunTarget &t : targets)
        if (t.account.isEmpty() && !missing.contains(t.groupName))
            missing.append(t.groupName);
    if (!missing.isEmpty()) {
        QMessageBox::warning(this, tr("Run"),
            tr("Pick a marketplace for these group(s) before running:\n\n%1")
                .arg(missing.join(QStringLiteral(", "))));
        return;
    }
    _flushCurrentPrompt();
    m_runCli = cli;
    m_lastDrafts.clear();
    m_runSkipped.clear();
    m_runScraped.clear();

    ProgressDlgHandles h;
    QDialog *dlg = makeProgressDlg(parentWidget(), tr("Running %1 case(s)…").arg(targets.size()), &h);
    m_progressDlg = dlg;
    m_progressLog = h.log;
    if (h.bar) h.bar->setRange(0, 0); // indeterminate during the scrape phase

    QPointer<QTextEdit> logPtr = h.log;
    auto appendLog = [logPtr](const QString &line) {
        if (!logPtr) return;
        const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
        logPtr->append(QStringLiteral("[%1] %2").arg(ts, line));
    };

    dlg->show();
    setEnabled(false);
    m_running = true;

    // Phase 1: scrape every target in a single worker invocation.
    QList<WorkerCase> cases;
    for (const RunTarget &t : targets)
        cases.append({t.region, t.caseId, t.account});

    if (h.status) h.status->setText(tr("Scraping %1 case(s)…").arg(targets.size()));
    appendLog(tr("scraping %1 case(s) via the browser worker…").arg(targets.size()));

    m_runner->scrape(cases, this,
        [this, targets, appendLog, status = h.status, bar = h.bar, closeBtn = h.closeBtn]
        (QList<ScrapeResult> results) {
        if (results.isEmpty()) {
            appendLog(tr("  ✗ worker returned no results (failed to launch or crashed)."));
            m_running = false;
            setEnabled(true);
            if (status) status->setText(tr("Worker failed — see log."));
            if (closeBtn) closeBtn->setEnabled(true);
            return;
        }

        // The worker pauses on the login page until you sign in, so by the time
        // results arrive we're logged in (or login genuinely timed out, in which
        // case those cases carry an error and flow through as failures).
        m_runScraped.clear();
        for (const ScrapeResult &r : results)
            m_runScraped.insert(r.caseId, r);

        // Phase 2: draft each eligible case sequentially via the CLI.
        if (bar) bar->setRange(0, targets.size());
        _draftStep(targets, 0, status, bar, appendLog, closeBtn);
    });
}

void PaneCases::_draftStep(QList<RunTarget> targets, int index,
                           QPointer<QLabel> status, QPointer<QProgressBar> bar,
                           std::function<void(const QString &)> appendLog,
                           QPointer<QPushButton> closeBtn)
{
    if (index >= targets.size()) {
        _finishRun(status, bar, closeBtn);
        return;
    }

    const RunTarget t = targets.at(index);
    if (bar) bar->setValue(index);

    CaseGroup *g = _group(t.groupName);
    const ScrapeResult sr = m_runScraped.value(t.caseId);

    if (!g) {
        m_lastDrafts.append({t.groupName, t.region, t.account, t.caseId, {}, {}, {}, tr("group not found")});
        _draftStep(targets, index + 1, status, bar, appendLog, closeBtn);
        return;
    }
    if (!sr.ok) {
        const QString err = sr.error.isEmpty() ? tr("scrape failed") : sr.error;
        appendLog(tr("[%1] %2 — ✗ %3").arg(t.region, t.caseId, err));
        m_lastDrafts.append({t.groupName, t.region, t.account, t.caseId, {}, {}, {}, err});
        _draftStep(targets, index + 1, status, bar, appendLog, closeBtn);
        return;
    }

    // Persist the raw thread JSON and cache the subject on the case.
    g->ensureFolder();
    if (QFile f(g->threadPath(t.caseId)); f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(sr.raw).toJson(QJsonDocument::Indented));
    g->setCaseSubject(t.caseId, sr.thread.subject);

    // Only draft when it is our turn: the latest message must be from Amazon.
    if (!sr.thread.isAmazonTurn()) {
        const QString why = sr.thread.messages.isEmpty()
            ? tr("no Amazon message yet") : tr("waiting for Amazon reply");
        appendLog(tr("[%1] %2 — ⏭ skipped (%3)").arg(t.region, t.caseId, why));
        m_runSkipped.append(t.caseId);
        _draftStep(targets, index + 1, status, bar, appendLog, closeBtn);
        return;
    }

    const CaseWorkerThread thread = sr.thread;
    const QString prompt =
        g->instructions().trimmed() +
        QStringLiteral("\n\n--- CASE THREAD (region %1, case %2) ---\n").arg(t.region, t.caseId) +
        _renderThread(thread) +
        QStringLiteral("--- END THREAD ---\n\n"
                       "Write only the reply message to send to Amazon for this case, "
                       "with no preamble or explanation.");

    if (status) status->setText(tr("Drafting reply for %1…").arg(t.caseId));
    appendLog(tr("[%1] %2 — drafting via %3…").arg(t.region, t.caseId, m_runCli->getName()));

    m_runCli->runPromptAsync(prompt, g->dir().absolutePath(), this,
        [this, targets, index, status, bar, appendLog, closeBtn, t, thread](CliRunResult cr) {
        CaseDraft d;
        d.groupName       = t.groupName;
        d.region          = t.region;
        d.account         = t.account;
        d.caseId          = t.caseId;
        d.subject         = thread.subject;
        d.amazonLastReply = thread.lastAmazonReply();
        if (CaseGroup *dg = _group(t.groupName)) d.attachments = dg->attachmentFiles();
        if (!cr.processStarted) {
            d.error = tr("CLI executable not found / failed to start");
            appendLog(tr("  ✗ %1").arg(d.error));
        } else {
            d.draft = cr.output.trimmed();
            if (d.draft.isEmpty()) {
                d.error = tr("CLI returned an empty draft (timeout or rate limit?)");
                appendLog(tr("  ✗ %1").arg(d.error));
            } else {
                appendLog(tr("  ✓ draft ready (%1 chars)").arg(d.draft.size()));
            }
        }
        m_lastDrafts.append(d);
        _draftStep(targets, index + 1, status, bar, appendLog, closeBtn);
    });
}

void PaneCases::_finishRun(QPointer<QLabel> status, QPointer<QProgressBar> bar,
                           QPointer<QPushButton> closeBtn)
{
    m_running = false;
    setEnabled(true);
    if (status) status->setText(tr("Done — %1 draft(s), %2 skipped (waiting for Amazon).")
                                    .arg(m_lastDrafts.size()).arg(m_runSkipped.size()));
    if (bar) { bar->setRange(0, 1); bar->setValue(1); }
    if (closeBtn) closeBtn->setEnabled(true);
    for (CaseGroup &g : m_groups) g.save();
    _rebuildTree();
    _openReview(m_lastDrafts);
}

void PaneCases::_openReview(const QList<CaseDraft> &drafts)
{
    if (drafts.isEmpty()) {
        if (!m_runSkipped.isEmpty())
            QMessageBox::information(this, tr("Nothing to review"),
                tr("%1 case(s) skipped (waiting for Amazon); no drafts produced.")
                    .arg(m_runSkipped.size()));
        return;
    }

    DialogReviewReplies dlg(drafts, this);
    if (dlg.exec() != QDialog::Accepted) return;

    _sendReplies(dlg.approvedDrafts(), dlg.letUserSend());
}

void PaneCases::_sendReplies(const QList<CaseDraft> &approved, bool manualSend)
{
    if (approved.isEmpty() || m_running) return;

    ProgressDlgHandles h;
    QDialog *dlg = makeProgressDlg(parentWidget(),
        tr("Sending %1 repl(y/ies)…").arg(approved.size()), &h);
    m_progressDlg = dlg;
    m_progressLog = h.log;
    if (h.bar) h.bar->setRange(0, 0);
    dlg->show();
    setEnabled(false);
    m_running = true;

    QList<ReplyJob> jobs;
    for (const CaseDraft &d : approved)
        jobs.append({d.region, d.caseId, d.draft, d.account, d.attachments});

    m_runner->reply(jobs, manualSend, this,
        [this, status = h.status, closeBtn = h.closeBtn](QList<ReplyResult> results) {
        m_running = false;
        setEnabled(true);
        int sent = 0, fail = 0;
        for (const ReplyResult &r : results)
            r.sent ? ++sent : ++fail;
        // NOTE: cases are deliberately NOT auto-marked done. After we reply the
        // case is "waiting for Amazon" (auto-skipped next run); it re-drafts when
        // Amazon replies again. Only the user marks a case done, by hand.
        if (status) status->setText(tr("Sent %1, not sent %2.").arg(sent).arg(fail));
        if (closeBtn) closeBtn->setEnabled(true);
        _rebuildTree();
    });
}

// ---------------------------------------------------------------------------

void PaneCases::hideEvent(QHideEvent *event)
{
    _flushCurrentPrompt();
    QWidget::hideEvent(event);
}
