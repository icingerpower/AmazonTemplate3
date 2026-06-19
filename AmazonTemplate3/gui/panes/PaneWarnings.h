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
#include "ClassificationTypeMap.h"

QT_BEGIN_NAMESPACE
namespace Ui { class PaneWarnings; }
QT_END_NAMESPACE

#include "TreeProductWarnings.h"

class AmazonWarningsApi;
class AmazonCatalogApi;
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

signals:
    void askAiFinished();

private:
    Ui::PaneWarnings        *ui;
    QDir                     m_workingDir;
    QList<AbstractCli *>     m_availableClis;
    AmazonWarningsApi       *m_api     = nullptr;
    AmazonCatalogApi        *m_catalogApi = nullptr;
    QNetworkAccessManager   *m_imageNam = nullptr;
    TreeProductWarnings     *m_model   = nullptr;
    AttributeFlagsTable     *m_flagsTable = nullptr;
    WarningsValueDelegate   *m_valueDelegate = nullptr;
    QHash<QString, QStringList> m_validValues; // attrId (lower) → enum list
    ClassificationTypeMap   m_classificationMap; // classificationId → productType (persisted)
    QHash<QString, QString> m_aiValueCache;      // "asin:attributeId" → aiValue (CC-scoped)
    QString                 m_aiCacheCc;         // which CC is currently loaded in m_aiValueCache

    QPointer<QDialog>  m_progressDlg; // active progress dialog — hidden/shown with this pane

    bool m_launchAllRunning = false; // guards buttonLoadAskUpload against re-entry

    // Held alive so the coroutine frame is not destroyed mid-execution.
    QCoro::Task<void> m_loadTask;
    QCoro::Task<void> m_askAiTask;
    QCoro::Task<void> m_uploadTask;
    QCoro::Task<void> m_retrieveTask;
    QCoro::Task<void> m_launchAllTask;
    QCoro::Task<void> m_askAiUploadTask;
    QCoro::Task<void> m_pasteTask;

    void _populateMarketplaces();
    void _loadSettings();

    AmazonWarningsApi *_api();
    AmazonCatalogApi *_catalogApi();
    QNetworkAccessManager *_imageNam();
    QString _selectedMarketplaceId() const;
    void _downloadMainImage(const QString &url, const QString &asin, const QString &mktSubdir);

    void _loadAiCache(const QString &cc);
    void _saveAiCache() const;

    QCoro::Task<void> _onLoadWarnings();
    QCoro::Task<void> _onAskAi();
    QCoro::Task<void> _onUpload();
    QCoro::Task<void> _onRetrieveImages();
    QCoro::Task<void> _onAskAiUpload();
    QCoro::Task<void> _onLoadAskUpload();
    QCoro::Task<void> _onPasteWarnings();
    void _onOpenImageDir() const;
    void _connectSlots();

    void hideEvent(QHideEvent *event) override;
    void showEvent(QShowEvent *event) override;
};

#endif // PANEWARNINGS_H
