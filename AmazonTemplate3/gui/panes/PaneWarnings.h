#ifndef PANEWARNINGS_H
#define PANEWARNINGS_H

#include <QDir>
#include <QHash>
#include <QList>
#include <QStringList>
#include <QWidget>

#include <QCoro/QCoroTask>

#include "AbstractCli.h"
#include "AttributeFlagsTable.h"

QT_BEGIN_NAMESPACE
namespace Ui { class PaneWarnings; }
QT_END_NAMESPACE

#include "TreeProductWarnings.h"

class AmazonWarningsApi;
class QNetworkAccessManager;
class WarningsValueDelegate;

class PaneWarnings : public QWidget
{
    Q_OBJECT

public:
    explicit PaneWarnings(QWidget *parent = nullptr);
    ~PaneWarnings();
    void setWorkingDir(const QDir &workingDir);
    void setAvailableClis(const QList<AbstractCli *> &clis);

private:
    Ui::PaneWarnings        *ui;
    QDir                     m_workingDir;
    QList<AbstractCli *>     m_availableClis;
    AmazonWarningsApi       *m_api     = nullptr;
    QNetworkAccessManager   *m_imageNam = nullptr;
    TreeProductWarnings     *m_model   = nullptr;
    AttributeFlagsTable     *m_flagsTable = nullptr;
    WarningsValueDelegate   *m_valueDelegate = nullptr;
    QHash<QString, QStringList> m_validValues; // attrId (lower) → enum list

    // Held alive so the coroutine frame is not destroyed mid-execution.
    QCoro::Task<void> m_loadTask;
    QCoro::Task<void> m_askAiTask;
    QCoro::Task<void> m_uploadTask;

    void _populateMarketplaces();
    void _loadSettings();

    AmazonWarningsApi *_api();
    QNetworkAccessManager *_imageNam();
    QString _selectedMarketplaceId() const;
    void _downloadMainImage(const QString &url, const QString &asin, const QString &mktSubdir);

    QCoro::Task<void> _onLoadWarnings();
    QCoro::Task<void> _onAskAi();
    QCoro::Task<void> _onUpload();
    void _connectSlots();

};

#endif // PANEWARNINGS_H
