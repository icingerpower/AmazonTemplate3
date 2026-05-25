#ifndef PANESIZING_H
#define PANESIZING_H

#include <QWidget>
#include <QDir>
#include <QImage>
#include <QList>
#include <QMenu>
#include <memory>

#include <QCoro/QCoroTask>

#include "AbstractCli.h"
#include "aplus/APlusContent.h"
#include "aplus/APlusTreeModel.h"

QT_BEGIN_NAMESPACE
namespace Ui { class PaneSizing; }
QT_END_NAMESPACE

class AmazonCatalogApi;
class TreeSizingAsins;
class QStandardItemModel;
class QDoubleSpinBox;
class AbstractSizeCategory;
class QNetworkAccessManager;

class PaneSizing : public QWidget
{
    Q_OBJECT
public:
    explicit PaneSizing(QWidget *parent = nullptr);
    ~PaneSizing();
    void setWorkingDir(const QDir &workingDir);
    void setAvailableClis(const QList<AbstractCli *> &clis);

    struct CliTask {
        QString                           label;    // shown in progress dialog
        QString                           prompt;      // static prompt
        std::function<QString()>          promptFn;    // dynamic prompt — evaluated at dispatch, after onBefore (takes precedence over prompt if set)
        QString                           workDir;
        std::function<void()>             onBefore; // called just before the CLI runs
        std::function<void(CliRunResult)> onDone;
    };

private slots:
    void onAddFromAsinClicked();
    void onAddFromTemplateClicked();
    void onSizeTypeChanged(int index);
    void onGenSizeTablesClicked();
    void onMakeEditableToggled(bool checked);
    void updateButtonStates();
    void onSizeModeChanged();
    void onGroupImageSelected(int row);
    void onUploadSizeTableClicked();
    void onVariantImageSelected(int row);

    // A+ content slots
    void onAplusGenerateAll();
    void onAplusGenerateSizeChart();
    void onAplusGenerateFaq();
    void onAplusGenerateImage(const QString &elementId);
    void onAplusDeleteVersion();
    void onAplusAddImageSlot();
    void onAplusTreeClicked(const QModelIndex &idx);
    void onAplusSelectionChanged(const QModelIndex &current, const QModelIndex &previous);

private:
    struct MeasurementWidgets {
        QString        fieldId;
        QDoubleSpinBox *refSpinBox   = nullptr;
        QDoubleSpinBox *stepSpinBox  = nullptr;
        QDoubleSpinBox *rangeSpinBox = nullptr;
    };

    Ui::PaneSizing   *ui;
    std::unique_ptr<AmazonCatalogApi> m_api;
    std::unique_ptr<TreeSizingAsins>  m_treeModel;
    QStandardItemModel               *m_sizeTableModel = nullptr;
    bool                              m_generatedSuccessfully = false;
    QList<MeasurementWidgets>         m_measurementWidgets;
    QList<QImage>                     m_groupImages;

    QDir                m_workingDir;
    QDir                m_productWorkingDir;
    QStringList         m_variantImagePaths;
    QString             m_mainImageLocalPath;
    QList<AbstractCli *>  m_availableClis;
    QNetworkAccessManager *m_imageNam = nullptr;

    // A+ content state
    std::unique_ptr<APlusContent> m_aplusContent;
    APlusTreeModel               *m_aplusModel   = nullptr;
    bool                          m_aplusDesktop = true;
    QMenu                        *m_aplusMenu    = nullptr;

    void _ensureModel(const QDir &dir);
    void _refreshApi();
    QDir _resolveProductDir(const QString &asin, const QString &title);
    void _saveProductSettings();
    void _loadProductSettings();
    void _populateSizeRangeCombos();
    void _tryGuessSizeRange();
    void _rebuildMeasurementForm();
    const AbstractSizeCategory* _currentCategory() const;
    QCoro::Task<void> _uploadSizeChart(QString marketplaceId, QString productType);
    void _downloadMainImage(const QString &url, const QString &asin);
    void _downloadVariantImages(const QStringList &imageUrls);
    void _runCliPrompt(const QString &executable, const QStringList &args,
                       const QByteArray &stdinData, const QString &workDir,
                       QObject *guard, std::function<void(QString)> callback);

    void _runSequentially(
        QList<CliTask> tasks,
        std::function<void(int /*step*/, int /*total*/, const QString & /*label*/)>
            onTaskStart = {},
        std::function<void(int /*step*/, int /*total*/, const QString & /*label*/,
                           CliRunResult)>
            onTaskDone = {});

    // A+ content helpers
    void    _initAplusContent();
    void    _rebuildAplusMenu();
    // Appends a format task + a validate task to `tasks`.
    // On completion *textHolder holds the final clean text, or "" on failure.
    // onFinalText (optional) is called once from the validate task's onDone.
    void    _appendFaqFormatValidateTasks(
                QList<CliTask> &tasks,
                QSharedPointer<QString> textHolder,
                const QString &workDir,
                std::function<void(const QString &)> onFinalText = {});
    void    _refreshAplusPreview(const QModelIndex &idx = {});
    void    _updateLangCombo(const QString &family, const QString &currentId);
    void    _showAplusFile(const QString &absPath);
    QList<CliTask> _buildSizeChartTranslationTasks(
                        const QList<QPair<QString, QString>> &targetLangs,
                        const QStringList &origRowLabels);
    void _refreshSizeGroupList();
    QString _aplusTimestamp() const;
    void    _aplusPushImage(const QImage &img, const QString &elementId,
                            const QString &displayName, APlusElementType type);
    void    _aplusPushSizeChart();
};

#endif
