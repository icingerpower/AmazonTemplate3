#ifndef PANECASES_H
#define PANECASES_H

#include <functional>

#include <QDir>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QWidget>

#include "CaseGroup.h"
#include "CaseWorkerRunner.h"
#include "CaseDraft.h"

QT_BEGIN_NAMESPACE
namespace Ui { class PaneCases; }
class QDialog;
class QLabel;
class QProgressBar;
class QPushButton;
class QStandardItem;
class QStandardItemModel;
class QTextEdit;
QT_END_NAMESPACE

class AbstractCli;

// A case to run through the worker + CLI (resolved from the tree selection).
struct RunTarget {
    QString groupName;
    QString region;
    QString caseId;
    QString account;
};

// Amazon Seller Central case auto-reply pane.
//
// Cases are organised into region-scoped groups (CaseGroup); each group owns a
// folder that is both the "Open folder" target and the working directory the
// selected AI CLI runs in. "Run" scrapes the cases via the one-shot Node worker
// (CaseWorkerRunner, launched per phase — no server), drafts a reply with the
// CLI for every case where it is our turn, then (next step) opens a review
// dialog before anything is sent.
class PaneCases : public QWidget
{
    Q_OBJECT
public:
    explicit PaneCases(QWidget *parent = nullptr);
    ~PaneCases();

    void setWorkingDir(const QDir &workingDir);
    void setAvailableClis(const QList<AbstractCli *> &clis);

private:
    Ui::PaneCases       *ui;
    QDir                 m_workingDir;
    QList<AbstractCli *> m_availableClis;
    CaseWorkerRunner    *m_runner = nullptr;
    QStandardItemModel  *m_model  = nullptr;

    QList<CaseGroup>     m_groups;
    QString              m_currentGroupName; // group whose detail panel is shown

    QList<CaseDraft>     m_lastDrafts;       // results of the most recent run
    QStringList          m_runSkipped;       // case ids skipped (waiting for Amazon)
    QHash<QString, ScrapeResult> m_runScraped; // caseId → scrape result for this run

    QPointer<QDialog>    m_progressDlg;
    QPointer<QTextEdit>  m_progressLog;      // live worker-stderr sink
    bool                 m_running    = false;
    bool                 m_populating = false; // suppress selection/itemChanged handlers
    AbstractCli         *m_runCli     = nullptr;

    // -- setup / persistence ----------------------------------------------
    void _connectSlots();
    void _populateUrlCombo();
    void _populateAccountCombo(const QString &region); // fill marketplaces for a region
    void _reloadGroups();
    void _rebuildTree();

    // -- selection helpers -------------------------------------------------
    CaseGroup *_group(const QString &name);
    CaseGroup *_selectedGroup();          // group of the current selection (or its parent)
    QString    _selectedCaseId() const;   // empty if a group (not a case) is selected
    void       _selectGroupInTree(const QString &name);
    void       _syncDetailToSelection();
    void       _flushCurrentPrompt();     // persist textEditPrompt into m_currentGroupName

    // -- actions -----------------------------------------------------------
    void _onSelectionChanged();
    void _onAddGroup();
    void _onAddCase();
    void _onMarkCaseDone();
    void _onOpenFolder();
    void _onCopyUrl();
    void _onRegionChanged();
    void _onAccountChanged();
    void _onPromptChanged();
    void _onCliChanged(int index);
    void _onRunSelected();
    void _onRunAll();

    // -- run flow ----------------------------------------------------------
    QList<RunTarget> _collectTargets(bool allGroups);
    void _runTargets(const QList<RunTarget> &targets);
    void _draftStep(QList<RunTarget> targets, int index,
                    QPointer<QLabel> status, QPointer<QProgressBar> bar,
                    std::function<void(const QString &)> appendLog,
                    QPointer<QPushButton> closeBtn);
    void _finishRun(QPointer<QLabel> status, QPointer<QProgressBar> bar,
                    QPointer<QPushButton> closeBtn);
    void _openReview(const QList<CaseDraft> &drafts);
    void _sendReplies(const QList<CaseDraft> &approved, bool manualSend);

    AbstractCli *_selectedCli() const;
    static QString _renderThread(const CaseWorkerThread &t);

    void hideEvent(QHideEvent *event) override;
};

#endif // PANECASES_H
