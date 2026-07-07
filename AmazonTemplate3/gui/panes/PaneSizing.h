#ifndef PANESIZING_H
#define PANESIZING_H

#include <QWidget>
#include <QDir>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
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
    void onCheckStatusClicked();
    void onBrowseBrokenTemplateClicked();
    void onBrokenAttrMarketChanged(int index);
    // Shows a pre-flight confirmation dialog for parent-fix runs.
    // Returns true if the user confirmed, false if cancelled.
    bool _confirmFixSettings(bool fixParents);

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
    // The exact ASIN string passed to m_treeModel->load() for the in-flight
    // request (set right before calling load()). Used by _findExistingProductDir
    // to recognize a previously-seen product even when the parent ASIN this
    // probe resolves to differs from earlier sessions.
    QString                           m_requestedAsin;
    // Set by onLoadSubFolderClicked to the exact folder the user picked from
    // the list, right before calling load(). When non-empty, the modelReset /
    // attributesFetched handlers use it verbatim instead of re-deriving the
    // folder from ASIN/SKU heuristics — the user already told us which folder
    // this is, so there's nothing to guess. Cleared once consumed.
    QString                           m_pinnedProductDir;
    std::unique_ptr<TreeSizingAsins>  m_treeModel;
    QStandardItemModel               *m_sizeTableModel = nullptr;
    bool                              m_generatedSuccessfully = false;
    QList<MeasurementWidgets>         m_measurementWidgets;
    QList<QImage>                     m_groupImages;

    QDir                m_workingDir;
    QDir                m_productWorkingDir;
    QStringList         m_shoeWidths;
    // Cached pieces of the description box (textEditAttributes), rebuilt by
    // _refreshAttributesText() so excluded colors can be filtered anytime.
    QString             m_attributesBaseText;
    QStringList         m_colorLogs;
    QString              m_productType;
    QString              m_productTitle;
    QStringList         m_variantImagePaths;
    QString             m_variantBrowsedImagePath;
    QCoro::Task<void>   m_variantUploadTask;
    QString             m_mainImageLocalPath;
    QString             m_mainImageUrl; // Amazon URL — identifies the main image's color
    QString             m_currentBrand;
    QStringList         m_currentBulletPoints;

    QStandardItemModel *m_temuStoreSelectModel = nullptr;
    bool                m_temuStoreSelectGuard = false;
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
    // checkOnly: read-only diagnostic — settled listing state, issues,
    // relationships and apparel_size schema rules; NO submission.
    QCoro::Task<void> _runBrokenChildFix(bool fixParents, bool fixImages,
                                         bool checkOnly = false);
    void _refreshTemplateCombo();
    // Looks for an already-existing sizing/ subfolder for this product, without
    // creating one. The parent ASIN returned by the catalog API can differ per
    // marketplace (sellers whose variation family isn't linked consistently
    // across regions), and the Catalog Items API rarely returns a seller SKU
    // from a plain ASIN lookup (`sku` is usually empty at this point) — so the
    // most reliable signal is `requestedAsin`: the exact ASIN string the user
    // asked to load. Any folder that has EVER recorded that ASIN as a family
    // member (sizing/skus/{requestedAsin} key present, from an earlier
    // SKU-resolution pass) is a match, regardless of which parent ASIN this
    // particular probe resolved to. Falls back to sku-value matching (for
    // flows that do provide a real SKU, e.g. loading from an xlsx), then to
    // the legacy {ASIN} / {ASIN}-* folder-name prefix match.
    // Returns an empty string if no folder is found.
    QString _findExistingProductDir(const QString &asin, const QString &sku,
                                     const QString &requestedAsin) const;
    QDir _resolveProductDir(const QString &asin, const QString &title, const QString &sku,
                             const QString &requestedAsin);
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
                                          bool forceRefresh = false,
                                          QHash<QString, QPair<QString,QString>> *asinToGtin = nullptr);
    struct FlatFileChildEntry {
        QString sku;
        QString color;
        QString size;
    };
    // Writes a partial-update flat file txt (parent + children) into m_productWorkingDir.
    // parentAttrs: SP-API attributes object from fetchListingAttributes.
    void _generateParentFlatFile(const QString &marketplaceCode,
                                  const QString &parentSku,
                                  const QJsonObject &parentAttrs,
                                  const QString &productType,
                                  const QString &variationTheme,
                                  const QList<FlatFileChildEntry> &children);
    struct VariationTemplateEntry {
        QString sku;
        QString asin;
        bool    isParent  = false;
        QString gtin;
        QString gtinType;
        QString color;
        QString size;
        QString sizeSource;      // marketplace code where size was fetched (e.g. "FR", "UK")
        QString sizeSystem;      // apparel_size_system
        QString sizeClass;       // apparel_size_class
        QString gender;          // target_gender
        QString ageRange;        // age_range_description
        QString bodyType;        // apparel_body_type
        QString heightType;      // apparel_height_type
    };
    // Opens an Amazon Inventory template (xlsm/xlsx), fills the Vorlage sheet with
    // parent + child variation data, and saves a copy in m_productWorkingDir.
    // attrMarketplaceId: the marketplace the parentAttrs were fetched from (drives size system).
    // Returns the filled file path, or an empty string on failure.
    QString _fillVariationTemplate(const QString &templatePath,
                                    const QString &parentSku,
                                    const QJsonObject &parentAttrs,
                                    const QString &attrMarketplaceId,
                                    const QString &productType,
                                    const QString &variationTheme,
                                    const QList<VariationTemplateEntry> &entries,
                                    const QHash<QString,QString> &attrOverrides = {});
    // Builds JSON_LISTINGS_FEED messages carrying the same complete data as the
    // manual flat file (full parent row + full child rows), localized for ONE
    // marketplace: each SKU's own listing attributes on that marketplace are
    // preferred (raw nested copy), then the collected template data (sizes
    // converted between countries), then familyAttrFallback (first-found among
    // children / parent listing / user dialog).
    // GCC 13 ICE workaround: params by value.
    QCoro::Task<void> _buildFullVariationMessages(QString mpId, QString mpCode,
                                                  QString parentSku,
                                                  QString productType,
                                                  QString variationTheme,
                                                  QJsonObject parentAttrsFallback,
                                                  QList<VariationTemplateEntry> tplEntries,
                                                  QHash<QString,QString> familyAttrFallback,
                                                  // childSku → corrected size in FR/ES numbering
                                                  // (user-reviewed); converted per region.
                                                  QHash<QString,QString> sizeOverridesFr,
                                                  QJsonArray* messagesOut,
                                                  QStringList* logOut);
    void _refreshBrokenAttrCombo();
    // Returns the marketplace ID currently selected in comboBoxBrokenAttrMarket.
    QString _brokenAttrMarketplaceId() const;
    void _downloadMainImage(const QString &url, const QString &asin);
    void _downloadVariantImages(const QList<QPair<QString, QStringList>> &colorImages);
    // Moves excluded colors' reference images to {productDir}/excluded/ and
    // restores re-included ones.
    void _syncExcludedColorFiles();
    // Same for the {ASIN}_main.jpg product photo when it shows an excluded
    // color; repoints the prompt reference to an active color's photo.
    void _syncMainImageExclusion();
    // Child ASINs of excluded colors — skipped by every upload path.
    QSet<QString> _excludedColorAsins() const;
    // A+ element ids of excluded colors — hidden from the A+ tree and menu.
    QSet<QString> _hiddenAplusElementIds() const;
    // Applies the exclusion list everywhere: files, label, A+ tree, menu,
    // description text.
    void _applyColorExclusions();

    // Temu store selection table (one selectable store per country).
    void _initTemuStoreTable();
    QList<QPair<QString, QString>> _selectedTemuStores() const;
    QCoro::Task<void> onTemuCreateOrUpdate();
    // Resolves m_productType (Amazon browse-node key) from cache or a quick API
    // lookup, so the Temu category can be remembered per Amazon category.
    QCoro::Task<void> _ensureProductType();
    QCoro::Task<void> m_temuDialogTask;
    // Rebuilds textEditAttributes from m_attributesBaseText + m_colorLogs,
    // stripping excluded colors.
    void _refreshAttributesText();
    QString _filterColorLog(const QString &log) const;
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
    void           _selectWorkflowForCategory();
    static QString _workflowCategoryKey(const AbstractSizeCategory *cat);
    APlusWorkflow *_currentWorkflow() const;
    QStringList    _stepInstructions() const;
    // Saves tree expansion by stable family ID, calls rebuild(), then restores.
    void           _rebuildAplusModel();
};

#endif
