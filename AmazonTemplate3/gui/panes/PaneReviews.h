#ifndef PANEREVIEWS_H
#define PANEREVIEWS_H

#include <QDir>
#include <QList>
#include <QString>
#include <QWidget>
#include <QCoro/QCoroTask>

#include "CaseWorkerRunner.h"
#include "TableReviews.h"

QT_BEGIN_NAMESPACE
namespace Ui { class PaneReviews; }
QT_END_NAMESPACE

class AbstractCli;
class QNetworkAccessManager;

class PaneReviews : public QWidget
{
    Q_OBJECT

public:
    explicit PaneReviews(QWidget *parent = nullptr);
    ~PaneReviews();

    void setWorkingDir(const QDir &workingDir);
    void setAvailableClis(const QList<AbstractCli *> &clis);

private:
    Ui::PaneReviews        *ui;
    QDir                    m_workingDir;
    QDir                    m_reviewsDir;
    QDir                    m_imagesDir;
    QList<AbstractCli *>    m_availableClis;
    CaseWorkerRunner       *m_runner       = nullptr;
    TableReviews           *m_reviewsTable = nullptr;
    QNetworkAccessManager  *m_nam          = nullptr;
    bool                    m_running      = false;
    bool                    m_stopRequested = false;
    QString                 m_logFilePath;
    QCoro::Task<void>       m_translateTask;

    void _onCliChanged(int index);
    AbstractCli *_selectedCli() const;

    void _onRun();
    void _onRunAll();
    void _onStop();
    void _onTranslate();
    void _onRemove();
    void _onTableDoubleClicked(const QModelIndex &index);

    void _setRunning(bool running);
    void _runTargets(const QList<ReviewTarget> &targets);
    void _ensureReviewsDir();
    void _loadCache();
    void _saveCache();
    void _cacheImageAsync(const QString &idOrAsin, const QString &imageUrl);
    bool _applyLiveReview(const QString &json);
    void _logToFile(const QString &line);

    QCoro::Task<void> _translateReviews();
};

#endif // PANEREVIEWS_H
