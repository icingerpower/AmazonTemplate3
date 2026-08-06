#ifndef DIALOGTEMUCREATEPRODUCT_H
#define DIALOGTEMUCREATEPRODUCT_H

#include <QDialog>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QCoro/QCoroTask>

#include "apis/TemuInventoryApi.h"
#include "AbstractCli.h"

// Editable listing text (title/bullets/description) for one country/language.
struct TemuPageText { QString title; QString bullets; QString description; };

class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QComboBox;
class QLabel;
class QFormLayout;
class QPushButton;
class QDoubleSpinBox;
class QSplitter;
class QTableWidget;
class QTreeWidget;

// Review-and-publish dialog for creating/updating a product page on Temu from
// the data loaded in PaneSizing. One product, one or more target stores (one
// per country). See design notes in the code.
class DialogTemuCreateProduct : public QDialog
{
    Q_OBJECT
public:
    // A store to publish to.
    struct StorePick {
        QString country;
        QString label;
        QString token;
        QString proxyHost;
        int     proxyPort = 0;
        QString proxyUser;
        QString proxyPassword;
        // Compliance entities mapped to the product brand for this store.
        QString manufacturerName;
        QString gsprRepName;
        qint64  manufacturerId = 0; // Temu repId (repType 3), for compliance submit
        qint64  gsprRepId = 0;      // Temu repId (repType 2), for compliance submit
    };

    // Amazon credentials + marketplace used to fetch the reference price.
    struct AmazonPricingCtx {
        QString clientId, secret, refreshTokenEu, sellerIdEu;
        QString marketplaceId; // EUR marketplace (e.g. France A13V1IB3VIYZZH)
    };

    // Which FillerSize conversion table applies to this product's sizes,
    // derived from the sizing category selected in PaneSizing (a men's top must
    // convert through the male table — FR/DE/IT offsets differ from women's).
    enum class SizeTable { Unknown, ClothingFemale, ClothingMale, ShoesFemale, ShoesMale };

    // The product data assembled by PaneSizing.
    struct Draft {
        QString     productDir;       // working dir holding the images
        QString     amazonProductType; // Amazon category key (e.g. "NOTEBOOK")
        SizeTable   sizeTable = SizeTable::Unknown; // from the sizing category
        QString     parentSku;        // Amazon parent SKU → Temu outGoodsSn
        QString     title;
        QStringList bulletPoints;
        QString     description;
        QString     brand;
        QStringList imagePaths;       // gallery images, checked by default
        QStringList extraImagePaths;  // A+ (mobile) images, unchecked by default
        QString     sizeChartImagePath; // generated size chart, if any
        // Per-country localized size charts ("FR" → local path), from the A+
        // size_chart_{cc} elements. When a store's country has one, publish
        // sends it as that store's detailImage instead of the shared chart.
        QMap<QString, QString> sizeChartByCountry;
        // Gallery images grouped by base colour (Sku.color) so the dialog can
        // offer per-colour image selection. Key "" = images tied to no colour
        // (main / shared). Every path here also appears in imagePaths.
        QMap<QString, QStringList> galleryByColor;
        // One entry per variation child.
        struct Sku {
            QString outSkuSn;
            QString asin;  // child ASIN (same across EU marketplaces)
            QString color;
            QString size;
            QString gtin; // EAN/UPC/GTIN from Amazon (product identifier)
            // Package weight/dimensions from Amazon (0 = unknown).
            double  weightG = 0, lengthCm = 0, widthCm = 0, heightCm = 0;
            // Localized colour/size names per country code ("FR","DE",…), fetched
            // from each store's own Amazon marketplace. Falls back to color/size.
            QMap<QString, QString> colorByCountry;
            QMap<QString, QString> sizeByCountry;
        };
        QList<Sku>  skus;
        QString     originCountry; // country of origin (e.g. "China")
        // Per-country listing text (title + bullets) from that country's Amazon
        // marketplace, so each store shows its own language. Keyed by country
        // code ("FR","DE",…). Description is CLI-generated per language.
        struct LangText { QString title; QStringList bullets; };
        QMap<QString, LangText> textByCountry;
    };

    DialogTemuCreateProduct(const QString &appKey, const QString &appSecret,
                            const QString &imgbbKey, AbstractCli *cli,
                            Draft draft, QList<StorePick> stores,
                            AmazonPricingCtx pricing,
                            QWidget *parent = nullptr);

protected:
    // Save the manual setup (images + attributes) whenever the dialog closes,
    // so Cancel / window-close also preserves the work.
    void done(int r) override;

private:
    QCoro::Task<void> _onStoreChanged();     // lookup create/update + load template
    // Loads the Temu attribute template for the category currently in m_catIdEdit.
    // Split out of _onStoreChanged so the category pickers can refresh attributes
    // WITHOUT re-running the store lookup (which would overwrite the just-picked
    // category with the existing product's category).
    QCoro::Task<void> _loadCategoryTemplate();
    // Runs the CLI via the async API bridged to a QFuture — co_awaiting
    // cli->runPrompt() directly crashes (GCC frees the Task frame while the
    // QProcess::finished signal is still queued).
    // workingDir (optional): run the CLI there so it can read local files (e.g.
    // the product images) by name — used by the AI attribute picker.
    QCoro::Task<CliRunResult> _runCli(const QString &prompt, const QString &workingDir = {});
    // Looks at the product images (+ text) and fills the attributes it can
    // confidently determine, choosing strictly from each attribute's allowed
    // values and skipping anything it can't guess.
    QCoro::Task<void> _aiPickAttributes();
    QCoro::Task<void> _generateText();       // CLI-generate title/bullets/description
    QCoro::Task<void> _regenerateField(int which); // 0=title 1=bullets 2=description
    // Regenerates title+bullets+description for EVERY selected country, each in
    // its own language, persisting the result into m_pageText per country.
    QCoro::Task<void> _regenerateAllText();
    // Regenerates ONE field (0=title 1=bullets 2=description) for every selected
    // country, each in its own language.
    QCoro::Task<void> _regenerateFieldAllLangs(int which);
    void    _reloadKeywordTemplates();       // fill the combo from storage
    QString _titleKeywordInstruction() const; // keyword clause for the current store
    QString _variationInstruction() const;    // colour/size clause for the title
    QString _storeLanguage() const;           // language name for the current store
    QString _textCountry() const;             // country code whose language is shown
    // Strong "write ONLY in <lang>, translate every foreign word" clause for the
    // given field description (e.g. "the title" / "the title, bullets and
    // description"). Empty when the language is unknown.
    QString _languageInstruction(const QString &what) const;
    // Title-only rules: polished + complete, SEO-keyword-rich.
    QString _titleGuidance() const;
    // Forbids the brand name in every field (title, bullets, description).
    QString _noBrandInstruction() const;
    // Post-processes a generated title: strips any trailing size (which the CLI
    // tends to copy from the source despite the prompt) and Title-Cases every
    // word. Applied to every title we accept.
    QString _finalizeTitle(QString title) const;
    // Drops bullet lines that pin a specific size ("Taille M correspondant au
    // 40") when the product has SEVERAL sizes — Temu bullets are shared across
    // every variation, so a single-size claim is wrong for the others. Applied
    // to the bullets fed to the CLI as context (so the model can't copy the
    // claim back) AND to the CLI's bullet output (deterministic backstop).
    QString _sanitizeBullets(const QString &bullets) const;
    // "Do not mention any specific size" clause, empty for single-size products.
    QString _noSizeInBulletsInstruction() const;
    QCoro::Task<void> _suggestCategory();    // category.recommend → set catId
    QCoro::Task<void> _aiPickCategory();     // CLI walks the named tree to a leaf
    QCoro::Task<void> _browseCategory();     // cascading cats.get picker
    QCoro::Task<void> _fetchAmazonPrices();  // per-SKU Amazon price → base/reference
    QCoro::Task<void> _fetchAmazonStock();   // per-SKU Amazon qty → Amz Qty col + stock
    QCoro::Task<void> _fetchAmazonData();     // prices then stock, in sequence
    // True once the Amazon stock fetch completed without error — publish uses
    // it to avoid shipping the default quantity 0 when the async fetch hasn't
    // finished (or failed) yet.
    bool m_stockFetched = false;
    void _applyRowToAll();                   // copy current row's price+packaging to all
    // Persist / restore the image selection+order and the filled attributes to
    // the product's settings.ini, so a failed upload doesn't cost the ~3 min of
    // manual setup when re-opening the dialog.
    void _saveWorkState();
    void _restoreImageState();               // after the image tree is built
    void _applySavedAttributes();            // after the attribute form is built
    QCoro::Task<void> _publish();            // assemble payload + create/update
    QCoro::Task<bool> _submitCompliance(qint64 goodsId); // GPSR manufacturer + EU rep

    void _rebuildAttributeForm();            // red required fields + combos
    bool _validateRequired(QStringList *missing) const;
    // Amazon product type → Temu category, cached in the working directory so
    // the mapping is reused across every product of the same Amazon category.
    void _applySavedCategory();
    void _saveCategoryMapping(qint64 catId, const QString &catName);
    // Persistent catId → "A › B › Leaf" name cache, grown by Browse and read
    // by Suggest (Temu has no id→name endpoint, so names are learned by
    // navigating). Stored in the working directory.
    void _loadCatPathCache();
    void _saveCatPathCache();
    // Local file → imgbb public URL. Empty on failure; errorOut then carries
    // the reason (network error, HTTP status, imgbb's message — e.g. quota).
    QCoro::Task<QString> _hostLocalImage(QString path, QString *errorOut = nullptr);
    const StorePick &_currentStore() const;
    // Caches the Temu CDN URL for each local image (per store) in the product
    // working dir so repeated publish attempts don't re-upload every time.
    void    _loadImageUrlCache();
    void    _saveImageUrlCache();
    QString _imageCacheKey(const QString &localPath) const;

    QString      m_appKey, m_appSecret, m_imgbbKey;
    AbstractCli *m_cli = nullptr;
    Draft        m_draft;
    QList<StorePick> m_stores;
    AmazonPricingCtx m_pricing;

    // Per-current-store state.
    TemuInventoryApi              *m_api = nullptr; // rebuilt per store
    TemuInventoryApi::ExistingGoods m_existing;
    QList<TemuInventoryApi::CategoryAttr> m_attrs;
    QHash<qint64, QString>         m_catPath;      // catId → full path name (persistent)
    QHash<QString, QString>        m_imageUrlCache; // image key → Temu CDN URL (persistent)
    QHash<int, QComboBox*>         m_attrCombos;   // m_attrs index → editor combo
    QHash<int, QLineEdit*>         m_attrInputs;   // m_attrs index → free input

    // Widgets.
    QComboBox      *m_storeCombo = nullptr;
    QLabel         *m_statusLabel = nullptr;
    QLineEdit      *m_catIdEdit = nullptr;
    QLabel         *m_catNameLabel = nullptr;
    QPushButton    *m_loadCatBtn = nullptr;
    QPushButton    *m_suggestBtn = nullptr;
    QPushButton    *m_aiPickBtn = nullptr;
    QPushButton    *m_browseBtn = nullptr;
    void _setCatBusy(bool busy); // disable category buttons while one runs
    // Gallery images grouped in a tree: one top-level node per colour (plus a
    // "Common" node for shared images), each with checkable, reorderable image
    // children. Publish gives each variation its colour's checked images.
    QTreeWidget    *m_imageTree = nullptr;
    QLabel         *m_imagePreview = nullptr;
    QFormLayout    *m_attrForm = nullptr;
    QWidget        *m_attrContainer = nullptr;
    // Per-country editable text (title/bullets/description), swapped when the
    // store changes so each country shows its own language.
    QMap<QString, TemuPageText> m_pageText;
    QString                     m_curTextCountry;
    void _loadCountryText(const QString &country);
    // True once the listing text has been (re)generated with AI this product's
    // lifetime (set on any successful field write, persisted). Publish warns when
    // false; Reset clears it back to false.
    bool m_textRegenerated = false;
    void _restoreTextState(); // load saved per-country text + the regen flag
    void _resetText();        // restore the original text, drop regenerated content
    // Original (source / Amazon-localized) text for a country, used by Reset.
    TemuPageText _originalCountryText(const QString &country) const;
    // Writes a regenerated field (0=title 1=bullets 2=description) into the
    // GIVEN country's stored text, and mirrors it into the live editors only if
    // that country is still the one on screen — so switching language while a
    // CLI regeneration is in flight can never land text in the wrong country.
    void _applyTextResult(const QString &country, int which, const QString &value);
    // Disables/enables language switching + text buttons while a text task runs
    // (ref-counted, so nested regenerate-all → regenerate-field stays disabled).
    void _setTextBusy(bool busy);
    int  m_textBusy = 0;
    QList<QWidget*> m_textControls; // language list + store combo + text buttons
    // Left-hand list of the selected stores' country codes; picking one shows
    // that country's language in the title/bullets/description editors. The
    // top Store dropdown stays the publish target and is independent of this.
    QListWidget                *m_textCountryList = nullptr;

    QComboBox      *m_keywordTemplateCombo = nullptr;
    QLineEdit      *m_originEdit = nullptr; // country of origin
    QLineEdit      *m_titleEdit = nullptr;
    QPlainTextEdit *m_bulletsEdit = nullptr;
    QPlainTextEdit *m_descEdit = nullptr;
    QTableWidget   *m_skuTable = nullptr; // one row per variation SKU
    // Per-country variation names: top-level per SKU, one child per selected
    // country showing that country's localized Color / Size (editable). Row r
    // aligns with m_skuTable row r and m_draft.skus[r].
    QTreeWidget    *m_variantTree = nullptr;
    // Fills the empty / not-yet-localized cells of m_variantTree: sizes are
    // converted mechanically from any country that already has a value
    // (FillerSize tables, e.g. DE 34-40 → FR 36-42); colours and textual size
    // labels ("Einheitsgröße") are translated by the CLI into each country's
    // language, all in one batched call. Cells the user edited are never touched.
    QCoro::Task<void> _completeVariantNames();
    QPushButton    *m_completeVariantsBtn = nullptr;
    QPlainTextEdit *m_logEdit = nullptr;
    QPushButton    *m_publishBtn = nullptr;
    // Splitters kept as members so their positions persist across sessions
    // (restored in the ctor, saved in done()).
    QSplitter      *m_topSplit = nullptr;
    QSplitter      *m_mainSplit = nullptr;

    // Fire-and-forget coroutines must be kept alive or their frames are freed
    // mid-flight (→ crash when a slow await resumes). One member per entry point.
    QCoro::Task<void> m_storeTask;
    QCoro::Task<void> m_catTask;    // suggest / ai-pick / browse
    QCoro::Task<void> m_attrAiTask; // AI attribute picker
    bool m_attrAiBusy = false;      // re-entrancy guard for the AI picker
    QCoro::Task<void> m_textTask;   // generate / regenerate
    QCoro::Task<void> m_textAllTask; // regenerate-all (every language)
    QCoro::Task<void> m_priceTask;  // fetch Amazon prices
    QCoro::Task<void> m_publishTask;
    QCoro::Task<void> m_completeTask; // Complete button (variant names)
};

#endif // DIALOGTEMUCREATEPRODUCT_H
