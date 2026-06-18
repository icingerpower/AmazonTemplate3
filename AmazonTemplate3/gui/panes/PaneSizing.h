#ifndef PANESIZING_H
#define PANESIZING_H

#include <QWidget>
#include <QDir>
#include <QImage>
#include <QList>
#include <QMap>
#include <QMenu>
#include <QPair>
#include <memory>

#include <QCoro/QCoroTask>

#include "AbstractCli.h"
#include "apis/AmazonAplusApi.h"
#include "aplus/APlusContent.h"
#include "aplus/APlusTreeModel.h"
#include "aplus/APlusWorkflow.h"
#include "SizeRangeWidget.h"
#include "sizecategories/SizingTableTemplateModel.h"
#include "BrokenChildTable.h"

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

    struct SizeChartTarget {
        QString groupKey;    // element id suffix: "uk", "com", "fr", …
        QString groupLabel;  // display label: "UK/IE/AU" or language name
        int     groupRow = -1;
        QString language;    // "English", "French", …
        bool    isEnglish = false;
    };

private slots:
    void onAddFromAsinClicked();
    void onLoadSubFolderClicked();
    void onSizeTypeChanged(int index);
    void onGenSizeTablesClicked();
    void onMakeEditableToggled(bool checked);
    void updateButtonStates();
    void onGroupImageSelected(int row);
    void onUploadSizeImageClicked();
    void onVariantTreeSelectionChanged();
    void onBrowseVariantImageClicked();
    void onUploadVariantImageClicked();
    void onOpenSizeTableFolderClicked();
    void onAddSkusFromTemplateClicked();
    void onFactorizeSizeTables();

    // A+ content slots
    void onAplusGenerateAll();
    void onAplusGenerateSelected();
    void onAplusGenerateSizeChart();
    void onAplusGenerateFaq();
    void onAplusGenerateImage(const QString &elementId);
    void onAplusDeleteVersion();
    void onAplusTreeClicked(const QModelIndex &idx);
    void onAplusSelectionChanged(const QModelIndex &current, const QModelIndex &previous);
    void onAplusUploadClicked();
    void onAplusExcludedColors();
    void onEditPromptsClicked();

    void onPickSizeTableTemplateClicked();
    void onGenerateSizeTableXlsxClicked();

    void onSavedSizeAddClicked();
    void onSavedSizeSaveClicked();
    void onSavedSizeLoadClicked();
    void onSavedSizeEditClicked();

    // Broken-child fix workflow (Broken child tab)
    void onFixAllClicked();
    void onFixParentsClicked();
    void onFixImagesClicked();
    void onFixLogClicked();

private:
    struct AsinSku {
        QString asin;
        QString sku;
    };

    struct MeasurementWidgets {
        QString        fieldId;
        QDoubleSpinBox *refSpinBox   = nullptr;
        QDoubleSpinBox *stepSpinBox  = nullptr;
        QDoubleSpinBox *rangeSpinBox = nullptr;
    };

    Ui::PaneSizing   *ui;
    std::unique_ptr<AmazonCatalogApi> m_api;
    std::unique_ptr<AmazonAplusApi>   m_aplusApi;
    QString                           m_currentAsin;
    std::unique_ptr<TreeSizingAsins>  m_treeModel;
    QStandardItemModel               *m_sizeTableModel = nullptr;
    bool                              m_generatedSuccessfully = false;
    QList<MeasurementWidgets>         m_measurementWidgets;
    QList<QImage>                     m_groupImages;

    QDir                m_workingDir;
    QDir                m_productWorkingDir;
    QStringList         m_shoeWidths;
    QString              m_productType;
    QString              m_productTitle;
    QStringList         m_variantImagePaths;
    QString             m_variantBrowsedImagePath;
    QCoro::Task<void>   m_variantUploadTask;
    QString             m_mainImageLocalPath;
    QList<AbstractCli *>  m_availableClis;
    QNetworkAccessManager *m_imageNam = nullptr;
    QList<QPair<QString, QStringList>> m_colorVariants;
    QMap<QString, QStringList>         m_colorAsins;     // color.toLower() → child ASINs
    QStringList                        m_aplusExcludedColors;

    // A+ content state
    std::unique_ptr<APlusContent> m_aplusContent;
    APlusTreeModel               *m_aplusModel   = nullptr;
    bool                          m_aplusDesktop = true;
    QMenu                        *m_aplusMenu    = nullptr;
    // Pinned so the coroutine frame (and its stack-allocated QProcess inside
    // cli->runPrompt) is not GC'd while the task is suspended mid-upload.
    QCoro::Task<void>             m_uploadTask;

    SizingTableTemplateModel     *m_templateModel    = nullptr;
    BrokenChildTable             *m_brokenChildTable = nullptr;
    QString                       m_sizeTableTemplatePath;

    void _ensureModel(const QDir &dir);
    void _refreshApi();
    QCoro::Task<void> _loadBrokenChildData(bool forceRefresh = false);
    void _appendFixLog(const QString &asin, const QString &marketplace, const QString &details);
    // Runs the parent/image fix workflow on the Broken child table.
    QCoro::Task<void> _runBrokenChildFix(bool fixParents, bool fixImages);
    void _refreshTemplateCombo();
    QDir _resolveProductDir(const QString &asin, const QString &title);
    void _saveProductSettings();
    void _loadProductSettings();
    void _populateSizeRangeCombos();
    void _tryGuessSizeRange();
    void _tryGuessBrandRangeFromTitle();
    void _rebuildMeasurementForm();
    bool _rebuildSizeTable();
    const AbstractSizeCategory* _currentCategory() const;
    QCoro::Task<void> _uploadSizeImage(int imageIndex);
    QCoro::Task<void> _uploadVariantImage(int imageIndex);
    QCoro::Task<void> _saveToSizeTableFolder();
    QCoro::Task<void> _addSkusFromTemplate();
    QCoro::Task<void> _uploadAplusContent();
    // Fills missing SKUs: settings.ini → Reports API → manual dialog.
    // Sets *cancelled = true if the user dismissed the manual entry dialog.
    QCoro::Task<void> _resolveSkus(QList<AsinSku> &items,
                                   const QString &marketplaceId,
                                   bool *cancelled);
    QCoro::Task<void> _fetchAllSkusCached(const QString &marketplaceId,
                                          QHash<QString, QString> *asinToSku,
                                          bool forceRefresh = false);
    void _downloadMainImage(const QString &url, const QString &asin);
    void _downloadVariantImages(const QList<QPair<QString, QStringList>> &colorImages);
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
                        const QList<SizeChartTarget> &targets,
                        const QStringList &origRowLabels);
    void _renderAndSaveChart(const AbstractSizeCategory *cat,
                              int groupRow,
                              const QString &elemId,
                              const QString &displayLang,
                              const QStringList &translatedLabels,
                              bool keepInches);
    void _refreshSizeGroupList();
    void _refreshSizeImageUploadStatus();
    bool    _isNarrowOnlyShoe() const;
    QImage  _appendNarrowSizingNote(const QImage &img) const;
    QString _aplusTimestamp() const;
    void    _aplusPushImage(const QImage &img, const QString &elementId,
                            const QString &displayName, APlusElementType type);
    void    _aplusPushSizeChart();

    // A+ workflow helpers
    void           _initWorkflowCombo();
    void           _loadWorkflowPrompts();
    void           _rebuildPromptTabs();
    APlusWorkflow *_currentWorkflow() const;
    QStringList    _stepInstructions() const;
    // Saves tree expansion by stable family ID, calls rebuild(), then restores.
    void           _rebuildAplusModel();
};

#endif
