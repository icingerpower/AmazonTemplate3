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

    // The product data assembled by PaneSizing.
    struct Draft {
        QString     productDir;       // working dir holding the images
        QString     amazonProductType; // Amazon category key (e.g. "NOTEBOOK")
        QString     parentSku;        // Amazon parent SKU → Temu outGoodsSn
        QString     title;
        QStringList bulletPoints;
        QString     description;
        QString     brand;
        QStringList imagePaths;       // gallery images, checked by default
        QStringList extraImagePaths;  // A+ (mobile) images, unchecked by default
        QString     sizeChartImagePath; // generated size chart, if any
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

private:
    QCoro::Task<void> _onStoreChanged();     // lookup create/update + load template
    // Runs the CLI via the async API bridged to a QFuture — co_awaiting
    // cli->runPrompt() directly crashes (GCC frees the Task frame while the
    // QProcess::finished signal is still queued).
    QCoro::Task<CliRunResult> _runCli(const QString &prompt);
    QCoro::Task<void> _generateText();       // CLI-generate title/bullets/description
    QCoro::Task<void> _regenerateField(int which); // 0=title 1=bullets 2=description
    void    _reloadKeywordTemplates();       // fill the combo from storage
    QString _titleKeywordInstruction() const; // keyword clause for the current store
    QString _variationInstruction() const;    // colour/size clause for the title
    QString _storeLanguage() const;           // language name for the current store
    QCoro::Task<void> _suggestCategory();    // category.recommend → set catId
    QCoro::Task<void> _aiPickCategory();     // CLI walks the named tree to a leaf
    QCoro::Task<void> _browseCategory();     // cascading cats.get picker
    QCoro::Task<void> _fetchAmazonPrices();  // per-SKU Amazon price → base/reference
    QCoro::Task<void> _fetchAmazonStock();   // per-SKU Amazon qty → Amz Qty col + stock
    QCoro::Task<void> _fetchAmazonData();     // prices then stock, in sequence
    void _applyRowToAll();                   // copy current row's price+packaging to all
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
    QCoro::Task<QString> _hostLocalImage(QString path); // local file → imgbb public URL
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
    QListWidget    *m_imageList = nullptr;
    QLabel         *m_imagePreview = nullptr;
    QFormLayout    *m_attrForm = nullptr;
    QWidget        *m_attrContainer = nullptr;
    // Per-country editable text (title/bullets/description), swapped when the
    // store changes so each country shows its own language.
    QMap<QString, TemuPageText> m_pageText;
    QString                     m_curTextCountry;
    void _loadCountryText(const QString &country);

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
    QPlainTextEdit *m_logEdit = nullptr;
    QPushButton    *m_publishBtn = nullptr;

    // Fire-and-forget coroutines must be kept alive or their frames are freed
    // mid-flight (→ crash when a slow await resumes). One member per entry point.
    QCoro::Task<void> m_storeTask;
    QCoro::Task<void> m_catTask;    // suggest / ai-pick / browse
    QCoro::Task<void> m_textTask;   // generate / regenerate
    QCoro::Task<void> m_priceTask;  // fetch Amazon prices
    QCoro::Task<void> m_publishTask;
};

#endif // DIALOGTEMUCREATEPRODUCT_H
