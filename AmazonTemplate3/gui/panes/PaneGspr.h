#ifndef PANEGSPR_H
#define PANEGSPR_H

#include <QDir>
#include <QList>
#include <QString>
#include <QWidget>

#include "CaseWorkerRunner.h"
#include "GsprManufacturerStore.h"

QT_BEGIN_NAMESPACE
namespace Ui { class PaneGspr; }
QT_END_NAMESPACE

class AbstractCli;
class GsprDoneTable;
class GsprFailedTable;
class GsprSkippedTable;

// GSPR (General Product Safety Regulation) pane. UI to be designed.
class PaneGspr : public QWidget
{
    Q_OBJECT
public:
    explicit PaneGspr(QWidget *parent = nullptr);
    ~PaneGspr();

    void setWorkingDir(const QDir &workingDir);
    void setAvailableClis(const QList<AbstractCli *> &clis);

private:
    Ui::PaneGspr        *ui;
    QDir                 m_workingDir;
    QList<AbstractCli *> m_availableClis;
    CaseWorkerRunner      *m_runner       = nullptr;
    GsprDoneTable         *m_doneTable    = nullptr;
    GsprFailedTable       *m_failedTable  = nullptr;
    GsprSkippedTable      *m_skippedTable = nullptr;
    GsprManufacturerStore  m_manufacturers;
    bool                 m_running = false;
    bool                 m_stopRequested = false;
    QString              m_logFilePath; // /tmp log of the current/last run

    void _browseFactoryFolder();
    void _onCliChanged(int index);
    AbstractCli *_selectedCli() const;

    void _onRun();
    void _onRunAll();
    void _onStop();
    void _setRunning(bool running);
    void _runTargets(const QList<GsprTarget> &targets);
    void _logToFile(const QString &line);
    // Bookkeeping for one per-ASIN outcome (idempotent — applied live from
    // worker events AND from the final results, so interrupted runs keep
    // their progress).
    void _applyOutcome(const QString &country, const GsprWarningOutcome &w);
    void _applyLiveOutcome(const QString &json);
    // "@@gspr-ask" pause: modal dialog asking the user to complete the
    // supplier xlsx files, then reload (mtime-based) and answer the worker.
    void _handleWorkerAsk(const QString &json);
};

#endif // PANEGSPR_H
