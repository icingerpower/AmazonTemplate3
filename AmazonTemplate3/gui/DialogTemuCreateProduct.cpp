#include "DialogTemuCreateProduct.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSplitter>
#include <QUrlQuery>
#include <QVBoxLayout>

#include <QCoro/QCoroNetworkReply>
#include <QCoro/QCoroTask>

#include <QHeaderView>
#include <QTableWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include "AbstractCli.h"
#include "DialogKeywordTemplates.h"
#include "apis/AmazonPricingApi.h"
#include "AbstractInventorySource.h"
#include "AbstractInventorySourceFactory.h"
#include "MarketplaceTypes.h"
#include "../../common/workingdirectory/WorkingDirectoryManager.h"

namespace {
// Columns of the per-variation SKU table.
enum SkuCol { kColSku = 0, kColAmazon, kColBase, kColRef, kColAmzQty, kColStock,
              kColWeight, kColL, kColW, kColH, kSkuColCount };
} // namespace

#include <QFuture>
#include <QPromise>
#include <QScopeGuard>
#include <QSharedPointer>
#include <QCoro/QCoroFuture>

namespace {
// Working-directory settings key mapping an Amazon product type to a Temu
// category ("catId|Full › Path › Name").
QString catMapKey(const QString &amazonProductType)
{
    QString safe = amazonProductType;
    safe.replace(QLatin1Char('/'), QLatin1Char('_'));
    return QStringLiteral("TemuCategoryMap/") + safe;
}
} // namespace

namespace {
// Prices are entered in euros but sent to Temu in minor units (cents).
int toMinorUnits(double euros) { return static_cast<int>(qRound(euros * 100.0)); }
} // namespace

DialogTemuCreateProduct::DialogTemuCreateProduct(
    const QString &appKey, const QString &appSecret, const QString &imgbbKey,
    AbstractCli *cli, Draft draft, QList<StorePick> stores,
    AmazonPricingCtx pricing, QWidget *parent)
    : QDialog(parent)
    , m_appKey(appKey), m_appSecret(appSecret), m_imgbbKey(imgbbKey)
    , m_cli(cli), m_draft(std::move(draft)), m_stores(std::move(stores))
    , m_pricing(std::move(pricing))
{
    setWindowTitle(tr("Create / update on Temu"));
    resize(920, 720);

    // --- Header: store selector + create/update status ---
    m_storeCombo = new QComboBox(this);
    for (const StorePick &s : m_stores)
        m_storeCombo->addItem(QStringLiteral("%1 · %2").arg(s.country, s.label));
    // Default to the French store when present — the source products are French,
    // so French is the expected default listing language (otherwise the first
    // store in the list wins, which can be e.g. Germany). Set before the combo
    // is wired below, so this does not fire _onStoreChanged prematurely.
    for (int i = 0; i < m_stores.size(); ++i) {
        if (m_stores.at(i).country.compare(QStringLiteral("FR"), Qt::CaseInsensitive) == 0) {
            m_storeCombo->setCurrentIndex(i);
            break;
        }
    }
    m_statusLabel = new QLabel(tr("…"), this);

    auto *header = new QHBoxLayout;
    header->addWidget(new QLabel(tr("Store:"), this));
    header->addWidget(m_storeCombo);
    header->addSpacing(16);
    header->addWidget(m_statusLabel, 1);

    // --- Category row ---
    m_catIdEdit  = new QLineEdit(this);
    m_catIdEdit->setPlaceholderText(tr("leaf id"));
    m_catIdEdit->setMaximumWidth(90);
    m_catNameLabel = new QLabel(tr("(no category)"), this);
    m_catNameLabel->setStyleSheet(QStringLiteral("color: gray;"));
    m_suggestBtn = new QPushButton(tr("Suggest"), this);
    m_aiPickBtn  = new QPushButton(tr("Auto-pick (AI)"), this);
    m_browseBtn  = new QPushButton(tr("Browse…"), this);
    m_loadCatBtn = new QPushButton(tr("Load attributes"), this);
    auto *catRow = new QHBoxLayout;
    catRow->addWidget(new QLabel(tr("Category:"), this));
    catRow->addWidget(m_catIdEdit);
    catRow->addWidget(m_catNameLabel, 1);
    catRow->addWidget(m_suggestBtn);
    catRow->addWidget(m_aiPickBtn);
    catRow->addWidget(m_browseBtn);
    catRow->addWidget(m_loadCatBtn);

    // --- Left: images (check to upload, click to preview) ---
    m_imageList = new QListWidget(this);
    m_imageList->setSelectionMode(QAbstractItemView::SingleSelection);
    auto addImage = [this](const QString &path, bool checked, const QString &tag) {
        if (path.isEmpty() || !QFileInfo::exists(path)) return;
        auto *it = new QListWidgetItem(
            tag.isEmpty() ? QFileInfo(path).fileName()
                          : QStringLiteral("%1  [%2]").arg(QFileInfo(path).fileName(), tag),
            m_imageList);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
        it->setData(Qt::UserRole, path);
    };
    for (const QString &p : m_draft.imagePaths)
        addImage(p, true, {});
    addImage(m_draft.sizeChartImagePath, true, tr("size chart"));
    for (const QString &p : m_draft.extraImagePaths)
        addImage(p, false, tr("A+"));

    // Drag to reorder; the upload order follows the list order.
    m_imageList->setDragDropMode(QAbstractItemView::InternalMove);
    m_imageList->setDefaultDropAction(Qt::MoveAction);

    auto *upBtn   = new QPushButton(tr("↑ Up"), this);
    auto *downBtn = new QPushButton(tr("↓ Down"), this);
    auto moveRow = [this](int delta) {
        const int row = m_imageList->currentRow();
        const int dst = row + delta;
        if (row < 0 || dst < 0 || dst >= m_imageList->count())
            return;
        QListWidgetItem *it = m_imageList->takeItem(row);
        m_imageList->insertItem(dst, it);
        m_imageList->setCurrentRow(dst);
    };
    connect(upBtn,   &QPushButton::clicked, this, [moveRow]() { moveRow(-1); });
    connect(downBtn, &QPushButton::clicked, this, [moveRow]() { moveRow(1); });
    auto *reorderRow = new QHBoxLayout;
    reorderRow->addWidget(upBtn);
    reorderRow->addWidget(downBtn);
    reorderRow->addStretch();

    auto *imagesBox = new QGroupBox(tr("Images (check to upload · drag or ↑↓ to reorder)"), this);
    auto *imagesLay = new QVBoxLayout(imagesBox);
    imagesLay->addWidget(m_imageList);
    imagesLay->addLayout(reorderRow);

    // --- Middle: preview of the selected image ---
    m_imagePreview = new QLabel(tr("(select an image)"), this);
    m_imagePreview->setAlignment(Qt::AlignCenter);
    m_imagePreview->setMinimumWidth(240);
    m_imagePreview->setStyleSheet(QStringLiteral("QLabel { background:#f0f0f0; }"));
    auto *previewBox = new QGroupBox(tr("Preview"), this);
    auto *previewLay = new QVBoxLayout(previewBox);
    previewLay->addWidget(m_imagePreview);

    // --- Right: attributes ---
    m_attrContainer = new QWidget;
    m_attrForm = new QFormLayout(m_attrContainer);
    auto *attrScroll = new QScrollArea(this);
    attrScroll->setWidgetResizable(true);
    attrScroll->setWidget(m_attrContainer);
    auto *attrBox = new QGroupBox(tr("Attributes (missing required ones in red)"), this);
    auto *attrBoxLay = new QVBoxLayout(attrBox);
    attrBoxLay->addWidget(attrScroll);

    auto *topSplit = new QSplitter(Qt::Horizontal, this);
    topSplit->addWidget(imagesBox);
    topSplit->addWidget(previewBox);
    topSplit->addWidget(attrBox);
    topSplit->setSizes({260, 320, 380});

    // --- Text (each field individually regenerable via the CLI) ---
    m_titleEdit = new QLineEdit(m_draft.title, this);
    m_bulletsEdit = new QPlainTextEdit(m_draft.bulletPoints.join(QLatin1Char('\n')), this);
    m_bulletsEdit->setMaximumHeight(90);
    m_descEdit = new QPlainTextEdit(m_draft.description, this);
    m_descEdit->setMaximumHeight(70);
    auto *genBtn = new QPushButton(tr("Generate all text"), this);
    auto *regenTitleBtn   = new QPushButton(tr("Regenerate"), this);
    auto *regenBulletsBtn = new QPushButton(tr("Regenerate"), this);
    auto *regenDescBtn    = new QPushButton(tr("Regenerate"), this);

    auto fieldRow = [](QWidget *editor, QPushButton *btn) {
        auto *row = new QHBoxLayout;
        row->addWidget(editor, 1);
        auto *col = new QVBoxLayout;
        col->addWidget(btn);
        col->addStretch();
        row->addLayout(col);
        return row;
    };

    // Title keyword template (Temu ranks titles by keywords): a template picks
    // a per-country keyword set to force into the generated title.
    m_keywordTemplateCombo = new QComboBox(this);
    auto *editKwBtn = new QPushButton(tr("Edit keyword templates…"), this);
    auto *kwRow = new QHBoxLayout;
    kwRow->addWidget(new QLabel(tr("Title keywords:"), this));
    kwRow->addWidget(m_keywordTemplateCombo, 1);
    kwRow->addWidget(editKwBtn);

    auto *textForm = new QFormLayout;
    textForm->addRow(QString{}, kwRow);
    textForm->addRow(tr("Title:"), fieldRow(m_titleEdit, regenTitleBtn));
    textForm->addRow(tr("Bullets:"), fieldRow(m_bulletsEdit, regenBulletsBtn));
    textForm->addRow(tr("Description:"), fieldRow(m_descEdit, regenDescBtn));

    // --- Per-variation table: price + packaging, one row per SKU ---
    m_skuTable = new QTableWidget(m_draft.skus.size(), kSkuColCount, this);
    m_skuTable->setHorizontalHeaderLabels({
        tr("SKU"), tr("Amazon €"), tr("Base € (retailPrice)"), tr("Reference € (listPrice)"),
        tr("Amz Qty"), tr("Stock"), tr("Weight g"), tr("L cm"), tr("W cm"), tr("H cm")});
    m_skuTable->horizontalHeader()->setStretchLastSection(false);
    m_skuTable->verticalHeader()->setVisible(false);
    for (int r = 0; r < m_draft.skus.size(); ++r) {
        auto *skuItem = new QTableWidgetItem(m_draft.skus[r].outSkuSn);
        skuItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable); // read-only
        m_skuTable->setItem(r, kColSku, skuItem);
        auto *amz = new QTableWidgetItem();
        amz->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);     // filled by fetch
        m_skuTable->setItem(r, kColAmazon, amz);
        for (int c = kColBase; c < kSkuColCount; ++c)
            m_skuTable->setItem(r, c, new QTableWidgetItem());
        // Amazon quantity is fetched, not edited.
        m_skuTable->item(r, kColAmzQty)->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_skuTable->item(r, kColStock)->setText(QStringLiteral("0")); // default stock
        // Prefill packaging from Amazon (blank if unknown).
        const auto &ds = m_draft.skus.at(r);
        auto set1 = [&](int col, double v) {
            if (v > 0) m_skuTable->item(r, col)->setText(QString::number(v, 'f', 1));
        };
        set1(kColWeight, ds.weightG);
        set1(kColL, ds.lengthCm);
        set1(kColW, ds.widthCm);
        set1(kColH, ds.heightCm);
    }
    m_skuTable->resizeColumnsToContents();

    // --- Per-country variation-names tree: SKU → one child per selected country
    // with that country's localized Colour / Size (editable). ---
    QStringList variantCountries; // deduped, in selection order
    for (const StorePick &sp : m_stores) {
        const QString cc = sp.country.toUpper();
        if (!cc.isEmpty() && !variantCountries.contains(cc))
            variantCountries << cc;
    }
    m_variantTree = new QTreeWidget(this);
    m_variantTree->setColumnCount(3);
    m_variantTree->setHeaderLabels({tr("SKU / Country"), tr("Color"), tr("Size")});
    m_variantTree->setEditTriggers(QAbstractItemView::DoubleClicked
                                   | QAbstractItemView::SelectedClicked
                                   | QAbstractItemView::EditKeyPressed);
    for (int r = 0; r < m_draft.skus.size(); ++r) {
        const auto &ds = m_draft.skus.at(r);
        auto *top = new QTreeWidgetItem(m_variantTree);
        top->setText(0, ds.outSkuSn);
        top->setText(1, ds.color);
        top->setText(2, ds.size);
        top->setFlags(top->flags() & ~Qt::ItemIsEditable); // source row read-only
        for (const QString &cc : variantCountries) {
            auto *child = new QTreeWidgetItem(top);
            child->setText(0, cc);
            child->setText(1, ds.colorByCountry.value(cc, ds.color));
            child->setText(2, ds.sizeByCountry.value(cc, ds.size));
            child->setFlags(child->flags() | Qt::ItemIsEditable); // colour/size editable
        }
        top->setExpanded(true);
    }
    m_variantTree->resizeColumnToContents(0);
    m_variantTree->resizeColumnToContents(1);
    m_variantTree->setMinimumHeight(90);

    m_originEdit = new QLineEdit(m_draft.originCountry.isEmpty() ? QStringLiteral("China")
                                                                 : m_draft.originCountry, this);
    m_originEdit->setMaximumWidth(160);
    auto *fetchPriceBtn = new QPushButton(tr("Fetch Amazon prices + stock"), this);
    auto *applyAllBtn   = new QPushButton(tr("Apply selected row to all"), this);
    auto *priceRow = new QHBoxLayout;
    priceRow->addWidget(new QLabel(tr("Country of origin:"), this));
    priceRow->addWidget(m_originEdit);
    priceRow->addSpacing(16);
    priceRow->addWidget(fetchPriceBtn);
    priceRow->addWidget(applyAllBtn);
    priceRow->addStretch();
    priceRow->addWidget(genBtn);

    // --- Log + buttons ---
    m_logEdit = new QPlainTextEdit(this);
    m_logEdit->setReadOnly(true);
    m_logEdit->setMaximumHeight(110);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_publishBtn = buttons->addButton(tr("Create / Update"), QDialogButtonBox::AcceptRole);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // Stack the resizable sections in a vertical splitter so the user can drag a
    // handle to give more room to whichever section they're working in — e.g.
    // enlarge the images/preview/attributes row when reordering images.
    auto *mainSplit = new QSplitter(Qt::Vertical, this);
    mainSplit->setChildrenCollapsible(false);

    mainSplit->addWidget(topSplit); // images | preview | attributes

    auto *textWidget = new QWidget(mainSplit);
    textWidget->setLayout(textForm);
    mainSplit->addWidget(textWidget);

    auto *varWidget = new QWidget(mainSplit);
    auto *varLay = new QVBoxLayout(varWidget);
    varLay->setContentsMargins(0, 0, 0, 0);
    varLay->addWidget(new QLabel(tr("Variations (price = base/selling; reference = base +20%):"),
                                 varWidget));
    varLay->addWidget(m_skuTable);
    varLay->addLayout(priceRow);
    mainSplit->addWidget(varWidget);

    auto *treeWidget = new QWidget(mainSplit);
    auto *treeLay = new QVBoxLayout(treeWidget);
    treeLay->setContentsMargins(0, 0, 0, 0);
    treeLay->addWidget(new QLabel(tr("Per-country variation names (from each Amazon marketplace — "
                                     "edit any colour/size):"), treeWidget));
    treeLay->addWidget(m_variantTree);
    mainSplit->addWidget(treeWidget);

    auto *logWidget = new QWidget(mainSplit);
    auto *logLay = new QVBoxLayout(logWidget);
    logLay->setContentsMargins(0, 0, 0, 0);
    logLay->addWidget(new QLabel(tr("Log:"), logWidget));
    logLay->addWidget(m_logEdit);
    mainSplit->addWidget(logWidget);

    // Sensible default share (images area gets the most).
    mainSplit->setStretchFactor(0, 4); // images / preview / attributes
    mainSplit->setStretchFactor(1, 2); // text
    mainSplit->setStretchFactor(2, 3); // variations table
    mainSplit->setStretchFactor(3, 2); // per-country tree
    mainSplit->setStretchFactor(4, 1); // log

    auto *lay = new QVBoxLayout(this);
    lay->addLayout(header);
    lay->addLayout(catRow);
    lay->addWidget(mainSplit, 1);
    lay->addWidget(buttons);

    // --- Wiring ---
    connect(m_storeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { m_storeTask = _onStoreChanged(); });
    connect(m_loadCatBtn, &QPushButton::clicked, this, [this]() { m_storeTask = _onStoreChanged(); });
    connect(m_suggestBtn, &QPushButton::clicked, this, [this]() { m_catTask = _suggestCategory(); });
    connect(m_aiPickBtn,  &QPushButton::clicked, this, [this]() { m_catTask = _aiPickCategory(); });
    connect(m_browseBtn,  &QPushButton::clicked, this, [this]() { m_catTask = _browseCategory(); });
    connect(genBtn, &QPushButton::clicked, this, [this]() { m_textTask = _generateText(); });
    connect(fetchPriceBtn, &QPushButton::clicked, this, [this]() { m_priceTask = _fetchAmazonData(); });
    connect(applyAllBtn, &QPushButton::clicked, this, [this]() { _applyRowToAll(); });
    connect(editKwBtn, &QPushButton::clicked, this, [this]() {
        DialogKeywordTemplates dlg(this);
        dlg.exec();
        _reloadKeywordTemplates();
    });
    connect(m_keywordTemplateCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                WorkingDirectoryManager::instance()->settings()->setValue(
                    QStringLiteral("TemuTitleKeywordTemplateId"),
                    m_keywordTemplateCombo->currentData().toString());
            });
    connect(regenTitleBtn,   &QPushButton::clicked, this, [this]() { m_textTask = _regenerateField(0); });
    connect(regenBulletsBtn, &QPushButton::clicked, this, [this]() { m_textTask = _regenerateField(1); });
    connect(regenDescBtn,    &QPushButton::clicked, this, [this]() { m_textTask = _regenerateField(2); });
    connect(m_publishBtn, &QPushButton::clicked, this, [this]() { m_publishTask = _publish(); });
    connect(m_imageList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *cur, QListWidgetItem *) {
                if (!cur) { m_imagePreview->setText(tr("(select an image)")); return; }
                const QPixmap pm(cur->data(Qt::UserRole).toString());
                if (pm.isNull()) { m_imagePreview->setText(tr("(cannot load image)")); return; }
                m_imagePreview->setPixmap(pm.scaled(m_imagePreview->size(),
                    Qt::KeepAspectRatio, Qt::SmoothTransformation));
            });

    // Seed the per-country text map from the Amazon-fetched localized text.
    for (auto it = m_draft.textByCountry.cbegin(); it != m_draft.textByCountry.cend(); ++it)
        m_pageText.insert(it.key(), {it.value().title, it.value().bullets.join(QLatin1Char('\n')),
                                     QString{}});

    _loadCatPathCache();
    _loadImageUrlCache();
    _reloadKeywordTemplates();
    _applySavedCategory();
    if (!m_stores.isEmpty())
        m_storeTask = _onStoreChanged();
    m_priceTask = _fetchAmazonData(); // prefill base/reference prices + Amazon stock
}

// Swaps the title/bullets/description editors to the given country's text,
// saving the current editors back to their country first.
void DialogTemuCreateProduct::_loadCountryText(const QString &country)
{
    if (m_curTextCountry == country)
        return;
    if (!m_curTextCountry.isEmpty())
        m_pageText[m_curTextCountry] = { m_titleEdit->text(), m_bulletsEdit->toPlainText(),
                                         m_descEdit->toPlainText() };
    m_curTextCountry = country;
    if (!m_pageText.contains(country)) {
        // No localized text for this country — fall back to the source draft text.
        m_pageText.insert(country, { m_draft.title,
                                     m_draft.bulletPoints.join(QLatin1Char('\n')), QString{} });
    }
    const TemuPageText &t = m_pageText.value(country);
    m_titleEdit->setText(t.title);
    m_bulletsEdit->setPlainText(t.bullets);
    m_descEdit->setPlainText(t.description);

    if (m_draft.textByCountry.contains(country))
        m_logEdit->appendPlainText(tr("Loaded %1 listing text from Amazon.").arg(country));
    else
        m_logEdit->appendPlainText(tr("No %1 text found on Amazon — showing source text; "
            "click Regenerate/Generate to write it in %2.")
            .arg(country, _storeLanguage().isEmpty() ? country : _storeLanguage()));
}

void DialogTemuCreateProduct::_loadCatPathCache()
{
    const QByteArray json = WorkingDirectoryManager::instance()->settings()
        ->value(QStringLiteral("TemuCatPathCacheJson")).toByteArray();
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    for (auto it = o.begin(); it != o.end(); ++it)
        m_catPath.insert(it.key().toLongLong(), it.value().toString());
}

QString DialogTemuCreateProduct::_imageCacheKey(const QString &localPath) const
{
    // Store + file identity (size & mtime) so a re-generated image re-uploads.
    const QFileInfo fi(localPath);
    const QString store = m_stores.isEmpty() ? QString{}
        : (_currentStore().country + QLatin1Char('/') + _currentStore().label);
    return QStringLiteral("%1|%2|%3|%4").arg(store, fi.fileName())
        .arg(fi.size()).arg(fi.lastModified().toMSecsSinceEpoch());
}

void DialogTemuCreateProduct::_loadImageUrlCache()
{
    m_imageUrlCache.clear();
    if (m_draft.productDir.isEmpty())
        return;
    QFile f(QDir(m_draft.productDir).filePath(QStringLiteral("temu_image_urls.json")));
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = o.begin(); it != o.end(); ++it)
        m_imageUrlCache.insert(it.key(), it.value().toString());
}

void DialogTemuCreateProduct::_saveImageUrlCache()
{
    if (m_draft.productDir.isEmpty())
        return;
    QJsonObject o;
    for (auto it = m_imageUrlCache.cbegin(); it != m_imageUrlCache.cend(); ++it)
        o.insert(it.key(), it.value());
    QFile f(QDir(m_draft.productDir).filePath(QStringLiteral("temu_image_urls.json")));
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

void DialogTemuCreateProduct::_saveCatPathCache()
{
    QJsonObject o;
    for (auto it = m_catPath.cbegin(); it != m_catPath.cend(); ++it)
        o.insert(QString::number(it.key()), it.value());
    WorkingDirectoryManager::instance()->settings()->setValue(
        QStringLiteral("TemuCatPathCacheJson"),
        QJsonDocument(o).toJson(QJsonDocument::Compact));
}

void DialogTemuCreateProduct::_applySavedCategory()
{
    // 1. Per-product category saved in this product's settings.ini — most
    // reliable when reopening the same product (works even without a known
    // Amazon product type).
    if (!m_draft.productDir.isEmpty()) {
        QSettings ps(QDir(m_draft.productDir).filePath(QStringLiteral("settings.ini")),
                     QSettings::IniFormat);
        const qint64 id = ps.value(QStringLiteral("temu/catId")).toLongLong();
        if (id > 0) {
            const QString name = ps.value(QStringLiteral("temu/catName")).toString();
            m_catIdEdit->setText(QString::number(id));
            m_catNameLabel->setText(name.isEmpty() ? tr("(saved for this product)") : name);
            m_logEdit->appendPlainText(tr("Using this product's saved category: %1 %2")
                                           .arg(id).arg(name));
            return;
        }
    }

    auto apply = [this](const QString &saved, const QString &sourceLabel) -> bool {
        if (saved.isEmpty()) return false;
        const int sep = saved.indexOf(QLatin1Char('|'));
        const QString id   = sep >= 0 ? saved.left(sep) : saved;
        const QString name = sep >= 0 ? saved.mid(sep + 1) : QString{};
        if (id.toLongLong() <= 0) return false;
        m_catIdEdit->setText(id);
        m_catNameLabel->setText(name.isEmpty() ? tr("(saved)") : name);
        m_logEdit->appendPlainText(tr("Category from %1: %2 %3").arg(sourceLabel, id, name));
        return true;
    };

    auto settings = WorkingDirectoryManager::instance()->settings();

    // 2. Per-Amazon-type mapping (exact same Amazon product type).
    if (!m_draft.amazonProductType.isEmpty()
        && apply(settings->value(catMapKey(m_draft.amazonProductType)).toString(),
                 tr("Amazon type \"%1\"").arg(m_draft.amazonProductType)))
        return;

    // 3. Fall back to the LAST category you used — Amazon product types are
    // inconsistent for similar items (e.g. FILE_FOLDER vs BOOK_COVER), so this
    // offers your previous pick as an editable default.
    apply(settings->value(QStringLiteral("TemuCategoryLastUsed")).toString(),
          tr("your last used category (change if wrong)"));
}

void DialogTemuCreateProduct::_saveCategoryMapping(qint64 catId, const QString &catName)
{
    if (catId <= 0)
        return;
    // Always remember it for THIS product (survives even without an Amazon type).
    if (!m_draft.productDir.isEmpty()) {
        QSettings ps(QDir(m_draft.productDir).filePath(QStringLiteral("settings.ini")),
                     QSettings::IniFormat);
        ps.setValue(QStringLiteral("temu/catId"), catId);
        ps.setValue(QStringLiteral("temu/catName"), catName);
    }
    const QString val = QStringLiteral("%1|%2").arg(catId).arg(catName);
    auto settings = WorkingDirectoryManager::instance()->settings();
    // Map it to the Amazon product type so new products of that type default to it.
    if (!m_draft.amazonProductType.isEmpty())
        settings->setValue(catMapKey(m_draft.amazonProductType), val);
    // Remember it as the global last-used category (cross-type fallback).
    settings->setValue(QStringLiteral("TemuCategoryLastUsed"), val);
}

const DialogTemuCreateProduct::StorePick &DialogTemuCreateProduct::_currentStore() const
{
    return m_stores.at(qMax(0, m_storeCombo->currentIndex()));
}

QCoro::Task<void> DialogTemuCreateProduct::_onStoreChanged()
{
    if (m_stores.isEmpty())
        co_return;
    const StorePick &store = _currentStore();

    // Show this store's country/language text in the editors.
    _loadCountryText(store.country.toUpper());

    delete m_api;
    m_api = new TemuInventoryApi(m_appKey, m_appSecret, store.token,
                                 store.proxyHost, store.proxyPort,
                                 store.proxyUser, store.proxyPassword, this);

    m_statusLabel->setText(tr("Looking up existing product…"));
    QStringList sns;
    for (const auto &sku : m_draft.skus)
        if (!sku.outSkuSn.isEmpty())
            sns << sku.outSkuSn;

    co_await m_api->lookupGoods(sns, &m_existing);
    if (!m_api->lastError().isEmpty()) {
        m_statusLabel->setText(tr("Lookup failed: %1").arg(m_api->lastError()));
        co_return;
    }

    if (m_existing.found) {
        m_statusLabel->setText(tr("● will UPDATE (goodsId %1)").arg(m_existing.goodsId));
        if (!m_existing.catId.isEmpty()) {
            m_catIdEdit->setText(m_existing.catId);
            m_catNameLabel->setText(m_existing.catName.isEmpty()
                ? tr("(existing product's category)") : m_existing.catName);
        }
    } else {
        m_statusLabel->setText(tr("○ will CREATE (SKU not found on this store)"));
    }

    const qint64 catId = m_catIdEdit->text().trimmed().toLongLong();
    if (catId <= 0) {
        m_logEdit->appendPlainText(tr("Set a category id, then click \"Load attributes\"."));
        co_return;
    }
    co_await m_api->fetchCategoryTemplate(catId, &m_attrs);
    if (!m_api->lastError().isEmpty()) {
        m_logEdit->appendPlainText(tr("Template load failed: %1").arg(m_api->lastError()));
        co_return;
    }
    _rebuildAttributeForm();
    m_logEdit->appendPlainText(tr("Loaded %1 attribute(s) for category %2.")
                                   .arg(m_attrs.size()).arg(catId));
}

void DialogTemuCreateProduct::_rebuildAttributeForm()
{
    // Clear the form.
    QLayoutItem *item;
    while ((item = m_attrForm->takeAt(0))) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    m_attrCombos.clear();
    m_attrInputs.clear();

    for (int i = 0; i < m_attrs.size(); ++i) {
        const auto &attr = m_attrs[i];
        // Conditional attributes (dependent on a parent) are shown for now
        // only when top-level; a fuller UI would reveal them on parent choice.
        if (attr.parentTemplatePid != 0)
            continue;

        // Derive a known value from the product where we can (brand only for now).
        QString known;
        if (attr.name.compare(QStringLiteral("Brand"), Qt::CaseInsensitive) == 0
            && !m_draft.brand.isEmpty()) {
            for (const auto &v : attr.values)
                if (v.first.compare(m_draft.brand, Qt::CaseInsensitive) == 0) {
                    known = v.first;
                    break;
                }
        }

        auto *label = new QLabel(attr.name, m_attrContainer);
        const bool missingRequired = attr.required && known.isEmpty();
        if (missingRequired)
            label->setStyleSheet(QStringLiteral("color: red; font-weight: bold;"));
        else if (attr.required)
            label->setStyleSheet(QStringLiteral("font-weight: bold;"));

        if (attr.controlType == 1) {
            auto *combo = new QComboBox(m_attrContainer);
            combo->addItem(QString{}, QVariant(qlonglong(0)));
            for (const auto &v : attr.values)
                combo->addItem(v.first, QVariant(qlonglong(v.second)));
            if (!known.isEmpty())
                combo->setCurrentText(known);
            m_attrCombos.insert(i, combo);
            m_attrForm->addRow(label, combo);
        } else {
            auto *edit = new QLineEdit(m_attrContainer);
            edit->setText(known);
            m_attrInputs.insert(i, edit);
            m_attrForm->addRow(label, edit);
        }
    }
}

bool DialogTemuCreateProduct::_validateRequired(QStringList *missing) const
{
    missing->clear();
    for (int i = 0; i < m_attrs.size(); ++i) {
        const auto &attr = m_attrs[i];
        if (!attr.required || attr.parentTemplatePid != 0)
            continue;
        bool filled = false;
        if (auto *c = m_attrCombos.value(i))
            filled = c->currentIndex() > 0;
        else if (auto *e = m_attrInputs.value(i))
            filled = !e->text().trimmed().isEmpty();
        if (!filled)
            *missing << attr.name;
    }
    return missing->isEmpty();
}

QCoro::Task<QString> DialogTemuCreateProduct::_hostLocalImage(QString path)
{
    if (m_imgbbKey.isEmpty() || !QFileInfo::exists(path))
        co_return QString{};

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        co_return QString{};
    const QByteArray b64 = f.readAll().toBase64();
    f.close();

    QUrl url(QStringLiteral("https://api.imgbb.com/1/upload"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("key"), m_imgbbKey);
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/x-www-form-urlencoded"));
    QByteArray body = "image=" + QUrl::toPercentEncoding(QString::fromLatin1(b64));

    auto *nam = new QNetworkAccessManager;
    QNetworkReply *reply = nam->post(req, body);
    co_await qCoro(reply).waitForFinished();
    const QByteArray data = reply->readAll();
    reply->deleteLater();
    nam->deleteLater();

    const QJsonObject root = QJsonDocument::fromJson(data).object();
    co_return root.value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("url")).toString();
}

void DialogTemuCreateProduct::_setCatBusy(bool busy)
{
    m_suggestBtn->setEnabled(!busy);
    m_aiPickBtn->setEnabled(!busy);
    m_browseBtn->setEnabled(!busy);
    m_loadCatBtn->setEnabled(!busy);
}

void DialogTemuCreateProduct::_reloadKeywordTemplates()
{
    QSignalBlocker b(m_keywordTemplateCombo);
    m_keywordTemplateCombo->clear();
    m_keywordTemplateCombo->addItem(tr("(none)"), QString{});
    const QString savedId = WorkingDirectoryManager::instance()->settings()
        ->value(QStringLiteral("TemuTitleKeywordTemplateId")).toString();
    int selectIdx = 0;
    for (const KeywordTemplate &t : DialogKeywordTemplates::load()) {
        m_keywordTemplateCombo->addItem(t.name, t.id);
        if (t.id == savedId)
            selectIdx = m_keywordTemplateCombo->count() - 1;
    }
    m_keywordTemplateCombo->setCurrentIndex(selectIdx);
}

// Keyword clause for the CURRENT store's country from the selected template.
QString DialogTemuCreateProduct::_titleKeywordInstruction() const
{
    const QString id = m_keywordTemplateCombo->currentData().toString();
    if (id.isEmpty() || m_stores.isEmpty())
        return {};
    const QStringList kws = DialogKeywordTemplates::keywordsFor(id, _currentStore().country);
    if (kws.isEmpty())
        return {};
    return tr("\n\nThe title MUST naturally include ALL of these keywords "
              "(Temu ranks titles by keywords): %1").arg(kws.join(QStringLiteral(", ")));
}

// Language of the current store's country, for CLI generation.
QString DialogTemuCreateProduct::_storeLanguage() const
{
    static const QHash<QString, QString> kLang = {
        {"FR","French"},{"DE","German"},{"IT","Italian"},{"ES","Spanish"},
        {"NL","Dutch"},{"SE","Swedish"},{"PL","Polish"},{"BE","French"},
        {"IE","English"},{"UK","English"},{"TR","Turkish"},{"PT","Portuguese"}};
    if (m_stores.isEmpty())
        return {};
    return kLang.value(_currentStore().country.toUpper());
}

// Asks the CLI to weave the product's variation values (colour/size) into the
// title, so a single-variation product's title names its colour/size.
QString DialogTemuCreateProduct::_variationInstruction() const
{
    QStringList colors, sizes;
    for (const auto &s : m_draft.skus) {
        if (!s.color.isEmpty() && !colors.contains(s.color)) colors << s.color;
        if (!s.size.isEmpty()  && !sizes.contains(s.size))   sizes  << s.size;
    }
    QStringList parts;
    if (!colors.isEmpty()) parts << tr("colour(s): %1").arg(colors.join(QStringLiteral(", ")));
    if (!sizes.isEmpty())  parts << tr("size(s): %1").arg(sizes.join(QStringLiteral(", ")));
    if (parts.isEmpty())
        return {};
    return tr("\n\nThe title should mention the product's %1 (when a single value, "
              "include it naturally in the title).").arg(parts.join(QStringLiteral("; ")));
}

// Base (selling) price = the Amazon price.
static double baseFromAmazon(double amazon)
{
    return amazon > 0 ? amazon : 0;
}

QCoro::Task<void> DialogTemuCreateProduct::_fetchAmazonPrices()
{
    if (m_pricing.clientId.isEmpty() || m_pricing.marketplaceId.isEmpty()) {
        m_logEdit->appendPlainText(tr("Amazon pricing not configured — enter prices manually."));
        co_return;
    }
    auto *api = new AmazonPricingApi(m_pricing.clientId, m_pricing.secret,
                                     m_pricing.refreshTokenEu, QString{},
                                     m_pricing.sellerIdEu, QString{}, this);
    m_logEdit->appendPlainText(tr("Fetching Amazon prices…"));
    for (int r = 0; r < m_skuTable->rowCount(); ++r) {
        const QString sku = m_skuTable->item(r, kColSku)->text();
        if (sku.isEmpty()) continue;
        double price = -1.0; bool exists = false;
        co_await api->fetchListingPrice(m_pricing.marketplaceId, sku, &price, &exists);
        if (price <= 0) {
            m_logEdit->appendPlainText(tr("  %1: no price").arg(sku));
            continue;
        }
        const double base = baseFromAmazon(price);
        m_skuTable->item(r, kColAmazon)->setText(QString::number(price, 'f', 2));
        m_skuTable->item(r, kColBase)->setText(QString::number(base, 'f', 2));
        m_skuTable->item(r, kColRef)->setText(QString::number(base * 1.20, 'f', 2));
        m_logEdit->appendPlainText(tr("  %1: Amazon %2 → base %3 / ref %4")
            .arg(sku).arg(price, 0, 'f', 2).arg(base, 0, 'f', 2).arg(base * 1.20, 0, 'f', 2));
    }
    api->deleteLater();
    m_logEdit->appendPlainText(tr("Prices ready. Edit any cell, or 'Apply selected row to all'."));
}

// Fetches the Amazon on-hand quantity per SKU via the shared inventory source
// (the same AbstractInventorySource used by PaneMarketplaces — Amazon FBA today,
// Octopia later), fills the "Amz Qty" column, and derives the Temu stock:
// 1 unit when Amazon has at least 2 on hand, 0 otherwise.
QCoro::Task<void> DialogTemuCreateProduct::_fetchAmazonStock()
{
    auto settings = WorkingDirectoryManager::instance()->settings();
    QList<AbstractInventorySource *> sources =
        AbstractInventorySourceFactory::buildAllInstances(settings.data());
    auto cleanup = qScopeGuard([&] { qDeleteAll(sources); });
    if (sources.isEmpty()) {
        m_logEdit->appendPlainText(tr("No inventory source configured — stock left at 0."));
        co_return;
    }
    AbstractInventorySource *src = sources.first();

    QStringList skus;
    for (int r = 0; r < m_skuTable->rowCount(); ++r) {
        const QString sku = m_skuTable->item(r, kColSku)->text();
        if (!sku.isEmpty()) skus << sku;
    }
    if (skus.isEmpty())
        co_return;

    m_logEdit->appendPlainText(tr("Fetching Amazon stock (%1)…").arg(src->displayName()));
    QList<StockRecord> records;
    co_await src->fetchInventory(skus, &records,
        [this](const QString &m) { m_logEdit->appendPlainText(QStringLiteral("  ") + m); });

    QHash<QString, int> availBySku;
    for (const StockRecord &rec : records)
        availBySku.insert(rec.sku, rec.available);

    for (int r = 0; r < m_skuTable->rowCount(); ++r) {
        const QString sku = m_skuTable->item(r, kColSku)->text();
        if (!availBySku.contains(sku)) {
            m_skuTable->item(r, kColAmzQty)->setText(QStringLiteral("?"));
            continue;
        }
        const int avail = availBySku.value(sku);
        m_skuTable->item(r, kColAmzQty)->setText(avail < 0 ? QStringLiteral("?")
                                                           : QString::number(avail));
        // 1 in stock when Amazon has ≥ 2 units, else 0.
        m_skuTable->item(r, kColStock)->setText(avail >= 2 ? QStringLiteral("1")
                                                           : QStringLiteral("0"));
    }
    m_logEdit->appendPlainText(tr("Stock set: 1 where Amazon ≥ 2 units, else 0 "
                                  "(edit any cell to override)."));
}

// Prices then stock, in sequence, so the log stays readable.
QCoro::Task<void> DialogTemuCreateProduct::_fetchAmazonData()
{
    co_await _fetchAmazonPrices();
    co_await _fetchAmazonStock();
}

void DialogTemuCreateProduct::_applyRowToAll()
{
    const int src = m_skuTable->currentRow();
    if (src < 0) {
        QMessageBox::information(this, tr("Apply to all"), tr("Select a row first."));
        return;
    }
    // Copy the editable columns (base, reference, packaging) to every other row.
    for (int c : {kColBase, kColRef, kColStock, kColWeight, kColL, kColW, kColH}) {
        const QString v = m_skuTable->item(src, c)->text();
        for (int r = 0; r < m_skuTable->rowCount(); ++r)
            if (r != src)
                m_skuTable->item(r, c)->setText(v);
    }
    m_logEdit->appendPlainText(tr("Applied row %1's price + packaging to all variations.").arg(src + 1));
}

QCoro::Task<CliRunResult> DialogTemuCreateProduct::_runCli(const QString &prompt)
{
    QPromise<CliRunResult> promise;
    promise.start();
    QFuture<CliRunResult> future = promise.future();
    {
        auto sp = QSharedPointer<QPromise<CliRunResult>>::create(std::move(promise));
        m_cli->runPromptAsync(prompt, this, [sp](CliRunResult r) mutable {
            sp->addResult(std::move(r));
            sp->finish();
        });
    }
    co_return co_await qCoro(future).result();
}

QCoro::Task<void> DialogTemuCreateProduct::_generateText()
{
    if (!m_cli) {
        QMessageBox::warning(this, tr("Generate text"), tr("No CLI selected."));
        co_return;
    }
    const QString prompt = tr(
        "You write Temu product listings. From the data below, produce STRICT JSON "
        "{\"title\":\"…\",\"bullets\":[\"…\"],\"description\":\"…\"} — a concise selling "
        "title (max 100 chars), 3-5 bullet points, and a short description. No markdown.\n\n"
        "Product: %1\nBrand: %2\nExisting bullets:\n%3")
        .arg(m_draft.title, m_draft.brand, m_draft.bulletPoints.join(QLatin1Char('\n')))
        + _titleKeywordInstruction() + _variationInstruction()
        + (_storeLanguage().isEmpty() ? QString{}
           : tr("\n\nCRITICAL: Write the title, bullets and description in %1 ONLY, even if the "
                "source text above is in another language — translate as needed.").arg(_storeLanguage()));

    m_logEdit->appendPlainText(_storeLanguage().isEmpty()
        ? tr("Generating text with %1…").arg(m_cli->getName())
        : tr("Generating %1 text with %2…").arg(_storeLanguage(), m_cli->getName()));
    const CliRunResult r = co_await _runCli(prompt);
    QString out = r.output.trimmed();
    const int a = out.indexOf(QLatin1Char('{'));
    const int b = out.lastIndexOf(QLatin1Char('}'));
    if (a >= 0 && b > a)
        out = out.mid(a, b - a + 1);
    const QJsonObject o = QJsonDocument::fromJson(out.toUtf8()).object();
    if (o.isEmpty()) {
        m_logEdit->appendPlainText(tr("CLI returned no parseable JSON; left text unchanged."));
        co_return;
    }
    if (o.contains(QStringLiteral("title")))
        m_titleEdit->setText(o.value(QStringLiteral("title")).toString());
    if (o.contains(QStringLiteral("bullets"))) {
        QStringList bl;
        for (const QJsonValue &v : o.value(QStringLiteral("bullets")).toArray())
            bl << v.toString();
        m_bulletsEdit->setPlainText(bl.join(QLatin1Char('\n')));
    }
    if (o.contains(QStringLiteral("description")))
        m_descEdit->setPlainText(o.value(QStringLiteral("description")).toString());
    m_logEdit->appendPlainText(tr("Text generated."));
}

QCoro::Task<void> DialogTemuCreateProduct::_regenerateField(int which)
{
    if (!m_cli) {
        QMessageBox::warning(this, tr("Regenerate"), tr("No CLI selected."));
        co_return;
    }
    const QString field = which == 0 ? QStringLiteral("title")
                        : which == 1 ? QStringLiteral("bullets")
                                     : QStringLiteral("description");
    const QString current = which == 0 ? m_titleEdit->text()
                          : which == 1 ? m_bulletsEdit->toPlainText()
                                       : m_descEdit->toPlainText();
    // Only bullets need structured (JSON) output. Title and description are
    // single free-text fields — asking for JSON there is fragile (long text with
    // newlines/quotes routinely produces invalid JSON), so we request plain text
    // and accept it directly, while still tolerating a JSON reply.
    const QString shape = which == 1
        ? QStringLiteral("STRICT JSON {\"bullets\":[\"…\"]} (3-5 concise selling bullet points), no markdown")
        : which == 0 ? QStringLiteral("plain text only — a concise selling title, max 100 chars, no quotes, no markdown")
                     : QStringLiteral("plain text only — a short compelling description, no markdown, no JSON");

    const QString prompt = tr(
        "You improve Temu product copy. Produce a BETTER %1 as %2.\n\n"
        "Product: %3\nBrand: %4\nSource bullets:\n%5\n\nCurrent %1 (improve on it):\n%6")
        .arg(field, shape, m_draft.title, m_draft.brand,
             m_draft.bulletPoints.join(QLatin1Char('\n')), current)
        + (which == 0 ? _titleKeywordInstruction() + _variationInstruction() : QString{})
        + (_storeLanguage().isEmpty() ? QString{}
           : tr("\n\nCRITICAL: Write the %1 in %2 ONLY, even if the source text above is in "
                "another language — translate as needed.").arg(field, _storeLanguage()));

    m_logEdit->appendPlainText(_storeLanguage().isEmpty()
        ? tr("Regenerating %1…").arg(field)
        : tr("Regenerating %1 in %2…").arg(field, _storeLanguage()));
    const CliRunResult r = co_await _runCli(prompt);
    const QString out = r.output.trimmed();

    // Strips a leading ```lang fence and trailing ``` from a block.
    auto stripFences = [](QString s) {
        s = s.trimmed();
        if (s.startsWith(QStringLiteral("```"))) {
            const int nl = s.indexOf(QLatin1Char('\n'));
            if (nl >= 0) s = s.mid(nl + 1);
            if (s.endsWith(QStringLiteral("```"))) s.chop(3);
        }
        return s.trimmed();
    };

    // Try to pull a JSON object out of the reply (may be absent for plain text).
    QJsonObject o;
    {
        const int a = out.indexOf(QLatin1Char('{'));
        const int b = out.lastIndexOf(QLatin1Char('}'));
        if (a >= 0 && b > a)
            o = QJsonDocument::fromJson(out.mid(a, b - a + 1).toUtf8()).object();
    }

    if (which == 1) {
        QStringList bl;
        if (o.contains(QStringLiteral("bullets"))) {
            for (const QJsonValue &v : o.value(QStringLiteral("bullets")).toArray())
                bl << v.toString();
        }
        if (bl.isEmpty()) {
            // Fallback: treat each non-empty line as a bullet, stripping any
            // leading "-", "*", "•" or "1." marker.
            const QStringList lines = stripFences(out).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            for (QString line : lines) {
                line = line.trimmed();
                while (!line.isEmpty()
                       && (line.startsWith(QLatin1Char('-')) || line.startsWith(QLatin1Char('*'))
                           || line.startsWith(QStringLiteral("•"))))
                    line = line.mid(1).trimmed();
                if (!line.isEmpty()) bl << line;
            }
        }
        if (bl.isEmpty()) {
            m_logEdit->appendPlainText(tr("  no usable output; unchanged."));
            co_return;
        }
        m_bulletsEdit->setPlainText(bl.join(QLatin1Char('\n')));
    } else {
        // Title / description: prefer the JSON field if present, else the raw text.
        const QString key = which == 0 ? QStringLiteral("title") : QStringLiteral("description");
        QString text = o.contains(key) ? o.value(key).toString() : stripFences(out);
        text = text.trimmed();
        if (text.size() >= 2 && text.startsWith(QLatin1Char('"')) && text.endsWith(QLatin1Char('"')))
            text = text.mid(1, text.size() - 2).trimmed();
        if (text.isEmpty()) {
            m_logEdit->appendPlainText(tr("  empty output; unchanged."));
            co_return;
        }
        if (which == 0) m_titleEdit->setText(text);
        else            m_descEdit->setPlainText(text);
    }
    m_logEdit->appendPlainText(tr("  %1 updated.").arg(field));
}

QCoro::Task<void> DialogTemuCreateProduct::_suggestCategory()
{
    if (!m_api) co_return;
    _setCatBusy(true);
    auto busyGuard = qScopeGuard([this] { _setCatBusy(false); });
    m_logEdit->appendPlainText(tr("Asking Temu for category suggestions…"));

    QList<qint64> candidates;
    co_await m_api->recommendCategory(m_draft.title, m_draft.bulletPoints.join(QLatin1Char(' ')),
                                      QString{}, &candidates);
    if (candidates.isEmpty()) {
        m_logEdit->appendPlainText(tr("  no suggestion (%1).").arg(m_api->lastError()));
        co_return;
    }

    // Temu returns candidate leaf ids only and has no id→name endpoint, so we
    // name them from the path cache grown by Browse (instant). Unknown ones
    // show as ids you can copy and look up, or resolve by browsing once.
    QHash<qint64, QString> names;
    int known = 0;
    for (qint64 id : candidates) {
        const QString n = m_catPath.value(id);
        if (!n.isEmpty()) { names.insert(id, n); ++known; }
    }
    m_logEdit->appendPlainText(tr("  %1/%2 candidate(s) named from cache "
        "(browse a category once to name the rest).").arg(known).arg(candidates.size()));

    // Chooser listing named candidates.
    QDialog chooser(this);
    chooser.setWindowTitle(tr("Suggested Temu categories"));
    chooser.resize(520, 300);
    auto *lay = new QVBoxLayout(&chooser);
    auto *hint = new QLabel(
        tr("Temu recommends these categories (best match first). Named rows come "
           "from categories you've browsed before — Temu offers no way to look up "
           "a category by its id (its search matches names only). If a row shows "
           "only a number, use \"Browse instead\" to find it by name. Your pick is "
           "remembered for every Amazon \"%1\" product.").arg(m_draft.amazonProductType), &chooser);
    hint->setWordWrap(true);
    lay->addWidget(hint);
    auto *list = new QListWidget(&chooser);
    for (qint64 id : candidates) {
        const QString name = names.value(id);
        auto *it = new QListWidgetItem(
            name.isEmpty() ? tr("category %1  (name unavailable — use Browse)").arg(id)
                           : QStringLiteral("%1   [%2]").arg(name).arg(id), list);
        it->setData(Qt::UserRole, QVariant::fromValue(id));
        it->setData(Qt::UserRole + 1, name);
    }
    list->setCurrentRow(0);
    auto *copyHint = new QLabel(
        tr("Double-click a row to copy its id+name. Select one and press OK to use it, "
           "or Browse to pick by name."), &chooser);
    copyHint->setWordWrap(true);
    copyHint->setStyleSheet(QStringLiteral("color: gray;"));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &chooser);
    auto *browseBtn = buttons->addButton(tr("Browse instead"), QDialogButtonBox::ActionRole);
    bool wantBrowse = false;
    QObject::connect(browseBtn, &QPushButton::clicked, &chooser, [&]() {
        wantBrowse = true;
        chooser.reject();
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &chooser, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &chooser, &QDialog::reject);
    // Double-click copies rather than selects, so the categories can be looked
    // up on Temu before committing to one.
    QObject::connect(list, &QListWidget::itemDoubleClicked, &chooser, [this](QListWidgetItem *it) {
        if (!it) return;
        const qint64 id = it->data(Qt::UserRole).toLongLong();
        const QString name = it->data(Qt::UserRole + 1).toString();
        const QString clip = name.isEmpty() ? QString::number(id)
                                            : QStringLiteral("%1\t%2").arg(id).arg(name);
        QApplication::clipboard()->setText(clip);
        m_logEdit->appendPlainText(tr("Copied to clipboard: %1").arg(clip));
    });
    lay->addWidget(list, 1);
    lay->addWidget(copyHint);
    lay->addWidget(buttons);

    const int rc = chooser.exec();
    if (wantBrowse) {
        co_await _browseCategory();
        co_return;
    }
    if (rc != QDialog::Accepted || !list->currentItem())
        co_return;
    const qint64 chosen = list->currentItem()->data(Qt::UserRole).toLongLong();
    const QString chosenName = list->currentItem()->data(Qt::UserRole + 1).toString();
    m_catIdEdit->setText(QString::number(chosen));
    m_catNameLabel->setText(chosenName.isEmpty() ? tr("(suggested)") : chosenName);
    m_logEdit->appendPlainText(tr("  chose %1 %2").arg(chosen).arg(chosenName));
    _saveCategoryMapping(chosen, chosenName); // remember for this Amazon type
    co_await _onStoreChanged(); // load attributes for the chosen category
}

QCoro::Task<void> DialogTemuCreateProduct::_aiPickCategory()
{
    if (!m_api) co_return;
    if (!m_cli) {
        QMessageBox::warning(this, tr("Auto-pick"), tr("No CLI selected."));
        co_return;
    }
    _setCatBusy(true);
    auto busyGuard = qScopeGuard([this] { _setCatBusy(false); });

    // BEAM SEARCH: keep several promising branches alive at once instead of
    // committing to one. Each round the CLI picks the best W nodes to keep
    // exploring across ALL current branches; leaves it keeps become final
    // candidates. At the end the CLI compares the candidate leaves and picks one.
    QProgressDialog progress(tr("Exploring Temu categories with %1…").arg(m_cli->getName()),
                             tr("Cancel"), 0, 0, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setValue(0);
    progress.show();

    m_logEdit->appendPlainText(tr("Auto-picking a category with %1 (beam search)…").arg(m_cli->getName()));

    // Shared context + the key classification principle: categorise by what the
    // product physically IS (its form/function — a cover, case, accessory…), not
    // by the theme or user of what it holds. This stops a "health record book
    // cover" landing under Baby/Health instead of Book Covers.
    const QString principle = tr(
        "IMPORTANT: Categorise by what the product physically IS — its form and "
        "primary function (e.g. a cover, case, sleeve, organiser, accessory) — NOT "
        "by the subject, theme, or intended user of the item it holds or relates to. "
        "Example: a \"health record book cover\" is a book cover / book accessory, "
        "not a baby or health-care product.");
    const QString productCtx = tr("Product: %1\nBrand: %2\nAmazon category: %3\nDetails: %4")
        .arg(m_draft.title,
             m_draft.brand.isEmpty() ? tr("(none)") : m_draft.brand,
             m_draft.amazonProductType.isEmpty() ? tr("(unknown)") : m_draft.amazonProductType,
             m_draft.bulletPoints.join(QLatin1Char(' ')));

    struct Node { qint64 id; QString path; };
    const int kBeamWidth = 3;
    const int kMaxRounds = 7;

    QList<Node> frontier;           // branch nodes to expand next round
    frontier.append({0, QString{}}); // root
    QList<Node> candidates;         // leaf categories collected along the way

    for (int round = 0; round < kMaxRounds && !frontier.isEmpty(); ++round) {
        if (progress.wasCanceled()) { m_logEdit->appendPlainText(tr("  cancelled.")); co_return; }
        progress.setLabelText(tr("Round %1: exploring %2 branch(es)…")
                                  .arg(round + 1).arg(frontier.size()));

        // Gather the children of every frontier branch.
        struct Opt { qint64 id; QString path; bool leaf; };
        QList<Opt> opts;
        for (const Node &fn : frontier) {
            QList<TemuInventoryApi::CatNode> children;
            co_await m_api->fetchCategories(fn.id, &children);
            if (!m_api->lastError().isEmpty())
                continue;
            for (const auto &c : children) {
                const QString full = fn.path.isEmpty() ? c.catName
                                                       : (fn.path + QStringLiteral(" › ") + c.catName);
                m_catPath.insert(c.catId, full);
                opts.append({c.catId, full, c.leaf});
            }
        }
        if (opts.isEmpty())
            break;

        if (progress.wasCanceled()) { m_logEdit->appendPlainText(tr("  cancelled.")); co_return; }

        // Ask the CLI which options (up to beam width) are worth keeping.
        QStringList optionLines;
        for (int i = 0; i < opts.size(); ++i)
            optionLines << QStringLiteral("%1. %2%3").arg(i + 1).arg(opts[i].path,
                opts[i].leaf ? tr("  (FINAL category)") : QString{});

        const QString prompt = tr(
            "You are classifying a product into Temu's category tree. From the options "
            "below (full paths), choose up to %1 that are most worth keeping to reach the "
            "single most specific correct category. Prefer FINAL categories that truly fit; "
            "keep branches only if they may plausibly contain a better fit. The listing "
            "may be written in another language.\n\n%2\n\n%3\n\nOptions:\n%4\n\n"
            "Reply STRICT JSON {\"choices\": [N, ...]} with the option numbers.")
            .arg(kBeamWidth)
            .arg(principle, productCtx, optionLines.join(QLatin1Char('\n')));

        const CliRunResult r = co_await _runCli(prompt);
        QString out = r.output.trimmed();
        const int a = out.indexOf(QLatin1Char('{'));
        const int b = out.lastIndexOf(QLatin1Char('}'));
        if (a >= 0 && b > a) out = out.mid(a, b - a + 1);
        const QJsonArray chosen = QJsonDocument::fromJson(out.toUtf8()).object()
                                      .value(QStringLiteral("choices")).toArray();

        QList<Node> nextFrontier;
        for (const QJsonValue &cv : chosen) {
            const int idx = cv.toInt(0) - 1;
            if (idx < 0 || idx >= opts.size())
                continue;
            const Opt &o = opts[idx];
            if (o.leaf) {
                candidates.append({o.id, o.path});
                m_logEdit->appendPlainText(tr("  ✓ candidate: %1").arg(o.path));
            } else if (nextFrontier.size() < kBeamWidth) {
                nextFrontier.append({o.id, o.path});
                m_logEdit->appendPlainText(tr("  → exploring: %1").arg(o.path));
            }
        }
        frontier = nextFrontier;
    }

    _saveCatPathCache();

    if (candidates.isEmpty()) {
        progress.close();
        m_logEdit->appendPlainText(tr("  no final category found — pick manually with Browse."));
        co_return;
    }

    // Ask the CLI which candidate it recommends — used only to pre-select a row.
    int preselect = 0;
    if (candidates.size() > 1) {
        if (progress.wasCanceled()) { m_logEdit->appendPlainText(tr("  cancelled.")); co_return; }
        progress.setLabelText(tr("Ranking %1 candidates…").arg(candidates.size()));
        QStringList lines;
        for (int i = 0; i < candidates.size(); ++i)
            lines << QStringLiteral("%1. %2").arg(i + 1).arg(candidates[i].path);
        const QString prompt = tr(
            "Choose the SINGLE best Temu category for this product from these final "
            "candidates.\n\n%1\n\n%2\n\nCandidates:\n%3\n\n"
            "Reply STRICT JSON {\"choice\": N}.")
            .arg(principle, productCtx, lines.join(QLatin1Char('\n')));
        const CliRunResult r = co_await _runCli(prompt);
        QString out = r.output.trimmed();
        const int a = out.indexOf(QLatin1Char('{'));
        const int b = out.lastIndexOf(QLatin1Char('}'));
        if (a >= 0 && b > a) out = out.mid(a, b - a + 1);
        const int choice = QJsonDocument::fromJson(out.toUtf8()).object()
                               .value(QStringLiteral("choice")).toInt(1);
        if (choice >= 1 && choice <= candidates.size())
            preselect = choice - 1;
    }
    progress.close();

    // Let the USER pick among the explored candidates (AI's pick pre-selected).
    QDialog chooser(this);
    chooser.setWindowTitle(tr("AI-explored categories"));
    chooser.resize(560, 320);
    auto *lay = new QVBoxLayout(&chooser);
    auto *hint = new QLabel(
        tr("The AI explored several branches and suggests these categories "
           "(its top pick is highlighted). Pick the one you want:"), &chooser);
    hint->setWordWrap(true);
    lay->addWidget(hint);
    auto *list = new QListWidget(&chooser);
    for (int i = 0; i < candidates.size(); ++i) {
        auto *it = new QListWidgetItem(candidates[i].path, list);
        it->setData(Qt::UserRole, QVariant::fromValue(candidates[i].id));
        if (i == preselect)
            it->setText(candidates[i].path + tr("   ← AI's pick"));
    }
    list->setCurrentRow(preselect);
    QObject::connect(list, &QListWidget::itemDoubleClicked, &chooser, [this](QListWidgetItem *it) {
        if (!it) return;
        QApplication::clipboard()->setText(it->text());
        m_logEdit->appendPlainText(tr("Copied: %1").arg(it->text()));
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &chooser);
    auto *browseBtn = buttons->addButton(tr("Browse instead"), QDialogButtonBox::ActionRole);
    bool wantBrowse = false;
    QObject::connect(browseBtn, &QPushButton::clicked, &chooser, [&]() { wantBrowse = true; chooser.reject(); });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &chooser, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &chooser, &QDialog::reject);
    lay->addWidget(list, 1);
    lay->addWidget(buttons);

    const int rc = chooser.exec();
    if (wantBrowse) { co_await _browseCategory(); co_return; }
    if (rc != QDialog::Accepted || !list->currentItem())
        co_return;

    const Node best{ list->currentItem()->data(Qt::UserRole).toLongLong(),
                     candidates[list->currentRow()].path };
    m_catIdEdit->setText(QString::number(best.id));
    m_catNameLabel->setText(best.path);
    _saveCategoryMapping(best.id, best.path);
    m_logEdit->appendPlainText(tr("Chosen: %1").arg(best.path));
    co_await _onStoreChanged();
}

QCoro::Task<void> DialogTemuCreateProduct::_browseCategory()
{
    if (!m_api) co_return;
    _setCatBusy(true);
    auto busyGuard = qScopeGuard([this] { _setCatBusy(false); });

    // Cascading picker: drill down from the top until a leaf is chosen. Each
    // level fetches with co_await, then a local event loop waits for the user
    // to either drill into a branch or pick a leaf.
    QDialog picker(this);
    picker.setWindowTitle(tr("Pick a Temu category"));
    picker.resize(420, 460);
    auto *lay = new QVBoxLayout(&picker);
    auto *crumb = new QLabel(tr("Top categories"), &picker);
    crumb->setWordWrap(true);
    auto *list = new QListWidget(&picker);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &picker);
    auto *okBtn = buttons->addButton(tr("Use this category"), QDialogButtonBox::AcceptRole);
    okBtn->setEnabled(false);
    lay->addWidget(crumb);
    lay->addWidget(list, 1);
    lay->addWidget(buttons);
    picker.setModal(true);
    picker.show();

    qint64 parentId = 0;
    QString path;
    bool cancelled = false;
    qint64 chosenId = 0;
    QString chosenName;

    QObject::connect(list, &QListWidget::itemClicked, &picker, [&](QListWidgetItem *it) {
        okBtn->setEnabled(it && it->data(Qt::UserRole + 1).toBool());
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &picker, [&]() {
        cancelled = true;
        picker.close();
    });

    while (!cancelled) {
        list->clear();
        okBtn->setEnabled(false);
        QList<TemuInventoryApi::CatNode> nodes;
        co_await m_api->fetchCategories(parentId, &nodes);
        if (!m_api->lastError().isEmpty()) {
            m_logEdit->appendPlainText(tr("Category load failed: %1").arg(m_api->lastError()));
            break;
        }
        for (const auto &n : nodes) {
            auto *it = new QListWidgetItem(
                n.leaf ? QStringLiteral("%1  ✓").arg(n.catName) : (n.catName + QStringLiteral("  ›")), list);
            it->setData(Qt::UserRole, QVariant::fromValue(n.catId));
            it->setData(Qt::UserRole + 1, n.leaf);
            it->setData(Qt::UserRole + 2, n.catName);
            // Learn the full path name for this node so Suggest can name it later.
            const QString full = path.isEmpty() ? n.catName
                                                 : (path + QStringLiteral(" › ") + n.catName);
            m_catPath.insert(n.catId, full);
        }

        // Wait for one user decision on this level.
        QEventLoop loop;
        qint64 pickedId = 0; bool pickedLeaf = false; QString pickedName; bool acted = false;
        auto act = [&](QListWidgetItem *it) {
            if (!it) return;
            pickedId = it->data(Qt::UserRole).toLongLong();
            pickedLeaf = it->data(Qt::UserRole + 1).toBool();
            pickedName = it->data(Qt::UserRole + 2).toString();
            acted = true;
            loop.quit();
        };
        auto c1 = QObject::connect(list, &QListWidget::itemDoubleClicked, &picker,
                                   [&](QListWidgetItem *it) { act(it); });
        auto c2 = QObject::connect(okBtn, &QPushButton::clicked, &picker,
                                   [&]() { act(list->currentItem()); });
        auto c3 = QObject::connect(buttons, &QDialogButtonBox::rejected, &loop, &QEventLoop::quit);
        loop.exec();
        QObject::disconnect(c1); QObject::disconnect(c2); QObject::disconnect(c3);

        if (cancelled || !acted)
            break;
        if (pickedLeaf) {
            chosenId = pickedId; chosenName = pickedName;
            break;
        }
        parentId = pickedId; // drill down
        path += (path.isEmpty() ? QString{} : QStringLiteral(" › ")) + pickedName;
        crumb->setText(path);
    }

    picker.close();
    _saveCatPathCache(); // persist the names learned while browsing
    if (!cancelled && chosenId != 0) {
        m_catIdEdit->setText(QString::number(chosenId));
        m_catNameLabel->setText(m_catPath.value(chosenId, chosenName));
        _saveCategoryMapping(chosenId, m_catPath.value(chosenId, chosenName));
        co_await _onStoreChanged();
    }
}

QCoro::Task<void> DialogTemuCreateProduct::_publish()
{
    if (!m_api) co_return;

    QStringList missing;
    if (!_validateRequired(&missing)) {
        QMessageBox::warning(this, tr("Missing attributes"),
            tr("Fill the required attributes first:\n%1").arg(missing.join(QStringLiteral(", "))));
        co_return;
    }
    const qint64 catId = m_catIdEdit->text().trimmed().toLongLong();
    if (catId <= 0) {
        QMessageBox::warning(this, tr("Category"), tr("Set a valid category id."));
        co_return;
    }
    // Record the category up front so it's remembered even if this create fails.
    _saveCategoryMapping(catId, m_catNameLabel->text());
    for (int r = 0; r < m_skuTable->rowCount(); ++r) {
        if (m_skuTable->item(r, kColBase)->text().trimmed().toDouble() <= 0) {
            QMessageBox::warning(this, tr("Price"),
                tr("Set a base price for every variation (row %1 is empty).").arg(r + 1));
            co_return;
        }
    }

    m_publishBtn->setEnabled(false);

    // --- Host checked images to public URLs (Temu V3 fetches them itself;
    //     no CDN upload needed). Cache the public URL per image. ---
    m_logEdit->appendPlainText(tr("Preparing images…"));
    QStringList temuImages;      // gallery image URLs
    QString sizeChartTemuUrl;
    for (int i = 0; i < m_imageList->count(); ++i) {
        QListWidgetItem *it = m_imageList->item(i);
        if (it->checkState() != Qt::Checked)
            continue;
        const QString local = it->data(Qt::UserRole).toString();
        const QString fname = QFileInfo(local).fileName();

        QString url = m_imageUrlCache.value(_imageCacheKey(local));
        if (!url.isEmpty()) {
            m_logEdit->appendPlainText(tr("  reused hosted image: %1").arg(fname));
        } else {
            url = co_await _hostLocalImage(local);
            if (url.isEmpty()) {
                m_logEdit->appendPlainText(tr("  ! could not host %1").arg(fname));
                continue;
            }
            m_imageUrlCache.insert(_imageCacheKey(local), url);
            _saveImageUrlCache();
        }

        if (local == m_draft.sizeChartImagePath)
            sizeChartTemuUrl = url;
        else
            temuImages << url;
    }
    if (temuImages.isEmpty()) {
        m_logEdit->appendPlainText(tr("No images available — aborting."));
        m_publishBtn->setEnabled(true);
        co_return;
    }

    // --- Attributes as free name/value pairs (V3 maps them to Temu itself) ---
    QJsonArray attrsArr;
    for (int i = 0; i < m_attrs.size(); ++i) {
        const auto &attr = m_attrs[i];
        if (attr.parentTemplatePid != 0)
            continue;
        QString value;
        if (auto *c = m_attrCombos.value(i)) {
            if (c->currentIndex() <= 0) continue;
            value = c->currentText();
        } else if (auto *e = m_attrInputs.value(i)) {
            value = e->text().trimmed();
            if (value.isEmpty()) continue;
        } else {
            continue;
        }
        QJsonObject a;
        a.insert(QStringLiteral("name"), attr.name);
        a.insert(QStringLiteral("value"), QJsonArray{value});
        attrsArr.append(a);
    }
    // Compliance-relevant attributes V3 can map: origin, manufacturer, EU rep.
    auto addAttr = [&attrsArr](const QString &name, const QString &val) {
        if (val.isEmpty()) return;
        QJsonObject a;
        a.insert(QStringLiteral("name"), name);
        a.insert(QStringLiteral("value"), QJsonArray{val});
        attrsArr.append(a);
    };
    addAttr(QStringLiteral("Country of Origin"), m_originEdit->text().trimmed());
    // NOTE: manufacturer + EU responsible person are NOT product attributes —
    // they are GPSR compliance and are submitted via submitCompliance()
    // (bg.local.goods.compliance.edit / gpsrInfo) after the goods is created.

    // --- Category name for extCatName (the picked path, "/"-separated) ---
    QString extCatName = m_catNameLabel->text();
    extCatName.replace(QStringLiteral(" › "), QStringLiteral(" / "));
    if (extCatName.startsWith(QLatin1Char('(')))
        extCatName.clear(); // placeholder like "(saved…)" — let Temu infer

    // --- SKUs (V3 schema) ---
    auto eurPrice = [](double euros) {
        QJsonObject o;
        o.insert(QStringLiteral("amount"), QString::number(euros, 'f', 2)); // FR/EUR: 2 decimals
        o.insert(QStringLiteral("currency"), QStringLiteral("EUR"));
        return o;
    };
    auto pkgVal = [](double v) {
        return v > 0 ? QString::number(v, 'f', 1) : QStringLiteral("0"); // 0 → Temu default
    };
    QJsonArray skuArr;
    for (int r = 0; r < m_skuTable->rowCount(); ++r) {
        const double base = m_skuTable->item(r, kColBase)->text().trimmed().toDouble();
        double ref = m_skuTable->item(r, kColRef)->text().trimmed().toDouble();
        if (ref <= 0) ref = base * 1.20;

        const DialogTemuCreateProduct::Draft::Sku &ds = m_draft.skus.value(r);
        // Colour/size for THIS store's country, from the per-country tree (which
        // holds the localized, possibly hand-edited names); fall back to source.
        const QString cc = _currentStore().country.toUpper();
        QString varColor = ds.colorByCountry.value(cc, ds.color);
        QString varSize  = ds.sizeByCountry.value(cc, ds.size);
        if (m_variantTree && r < m_variantTree->topLevelItemCount()) {
            QTreeWidgetItem *top = m_variantTree->topLevelItem(r);
            for (int k = 0; k < top->childCount(); ++k) {
                if (top->child(k)->text(0).compare(cc, Qt::CaseInsensitive) == 0) {
                    varColor = top->child(k)->text(1).trimmed();
                    varSize  = top->child(k)->text(2).trimmed();
                    break;
                }
            }
        }

        QJsonObject price;
        price.insert(QStringLiteral("basePrice"), eurPrice(base));
        if (ref > 0) price.insert(QStringLiteral("listPrice"), eurPrice(ref));

        QJsonObject pkg;
        pkg.insert(QStringLiteral("weight"), pkgVal(m_skuTable->item(r, kColWeight)->text().trimmed().toDouble()));
        pkg.insert(QStringLiteral("length"), pkgVal(m_skuTable->item(r, kColL)->text().trimmed().toDouble()));
        pkg.insert(QStringLiteral("width"),  pkgVal(m_skuTable->item(r, kColW)->text().trimmed().toDouble()));
        pkg.insert(QStringLiteral("height"), pkgVal(m_skuTable->item(r, kColH)->text().trimmed().toDouble()));

        // Send BOTH variation dimensions when present (a colour+size product
        // like Blue-S must not collapse all "Blue-*" onto one Color=Blue).
        QJsonArray variations;
        if (!varColor.isEmpty()) {
            QJsonObject v;
            v.insert(QStringLiteral("name"), QStringLiteral("Color"));
            v.insert(QStringLiteral("value"), varColor);
            variations.append(v);
        }
        if (!varSize.isEmpty()) {
            QJsonObject v;
            v.insert(QStringLiteral("name"), QStringLiteral("Size"));
            v.insert(QStringLiteral("value"), varSize);
            variations.append(v);
        }
        if (variations.isEmpty()) {
            QJsonObject v;
            v.insert(QStringLiteral("name"), QStringLiteral("Color"));
            v.insert(QStringLiteral("value"), QStringLiteral("Standard"));
            variations.append(v);
        }

        QJsonArray imgs;
        for (const QString &u : temuImages) imgs.append(u);

        QJsonObject s;
        s.insert(QStringLiteral("externalSkuId"), m_skuTable->item(r, kColSku)->text());
        s.insert(QStringLiteral("images"), imgs);
        s.insert(QStringLiteral("price"), price);
        s.insert(QStringLiteral("quantity"), m_skuTable->item(r, kColStock)->text().trimmed().toInt());
        s.insert(QStringLiteral("packageInfo"), pkg);
        s.insert(QStringLiteral("variations"), variations);
        // Product identifier (GTIN/EAN) from Amazon, when present.
        if (!ds.gtin.isEmpty()) {
            QJsonObject bc;
            bc.insert(QStringLiteral("barCodeType"),
                      ds.gtin.length() == 14 ? QStringLiteral("GTIN-14") : QStringLiteral("EAN"));
            bc.insert(QStringLiteral("barCodeId"), QJsonArray{ds.gtin});
            s.insert(QStringLiteral("barCode"), bc);
        }
        skuArr.append(s);
    }

    // --- goodsBasic ---
    QJsonObject goodsBasic;
    goodsBasic.insert(QStringLiteral("goodsName"), m_titleEdit->text().trimmed());
    const QString extGoodsId = !m_draft.parentSku.isEmpty()
        ? m_draft.parentSku
        : (m_draft.skus.isEmpty() ? m_titleEdit->text().trimmed() : m_draft.skus.first().outSkuSn);
    goodsBasic.insert(QStringLiteral("externalGoodsId"), extGoodsId);
    if (!m_descEdit->toPlainText().trimmed().isEmpty())
        goodsBasic.insert(QStringLiteral("goodsDesc"), m_descEdit->toPlainText().trimmed());
    if (!extCatName.isEmpty())
        goodsBasic.insert(QStringLiteral("extCatName"), extCatName);
    QJsonArray carousel;
    for (const QString &u : temuImages) carousel.append(u);
    goodsBasic.insert(QStringLiteral("goodsCarouselImage"), carousel);
    if (!sizeChartTemuUrl.isEmpty())
        goodsBasic.insert(QStringLiteral("detailImage"), QJsonArray{sizeChartTemuUrl});

    // --- UPDATE path: the product already exists on this store ---
    if (m_existing.found && m_existing.goodsId != 0) {
        // partial.update edits text + images (prices/stock have dedicated flows;
        // it also refuses edits until Temu finishes processing the product).
        QJsonObject upBasic;
        upBasic.insert(QStringLiteral("goodsName"), m_titleEdit->text().trimmed());
        if (!m_descEdit->toPlainText().trimmed().isEmpty())
            upBasic.insert(QStringLiteral("goodsDesc"), m_descEdit->toPlainText().trimmed());
        upBasic.insert(QStringLiteral("goodsCarouselImage"), carousel);
        QJsonObject upFields;
        upFields.insert(QStringLiteral("goodsBasic"), upBasic);
        QJsonArray bulletsUp;
        for (const QString &b : m_bulletsEdit->toPlainText().split(QLatin1Char('\n'), Qt::SkipEmptyParts))
            bulletsUp.append(b.trimmed());
        if (!bulletsUp.isEmpty())
            upFields.insert(QStringLiteral("bulletPoints"), bulletsUp);

        m_logEdit->appendPlainText(tr("Updating existing product (goodsId %1)…").arg(m_existing.goodsId));
        const bool ok = co_await m_api->updateGoodsPartial(m_existing.goodsId, upFields);
        if (!ok) {
            const QString err = m_api->lastError();
            m_logEdit->appendPlainText(tr("Update failed: %1").arg(err));
            if (err.contains(QStringLiteral("150010205")))
                m_logEdit->appendPlainText(tr("  (Temu is still processing this product — "
                                              "wait ~10 min after creation before editing.)"));
            m_publishBtn->setEnabled(true);
            co_return;
        }
        m_logEdit->appendPlainText(tr("Updated goodsId %1.").arg(m_existing.goodsId));
        co_await _submitCompliance(m_existing.goodsId);
        QMessageBox::information(this, tr("Create / Update"),
            tr("Updated %1 (goodsId %2).").arg(_currentStore().label).arg(m_existing.goodsId));
        m_publishBtn->setEnabled(true);
        co_return;
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("goodsBasic"), goodsBasic);
    if (!attrsArr.isEmpty())
        payload.insert(QStringLiteral("attributes"), attrsArr);
    payload.insert(QStringLiteral("skuList"), skuArr);

    m_logEdit->appendPlainText(tr("Creating product (V3)…"));
    qDebug().noquote() << "Temu V3 publish payload:"
                       << QJsonDocument(payload).toJson(QJsonDocument::Compact);

    const qint64 id = co_await m_api->publishGoodsV3(payload);
    if (id == 0) {
        const QString err = m_api->lastError();
        m_logEdit->appendPlainText(tr("FAILED: %1").arg(err));
        if (err.contains(QStringLiteral("150010090"))) // SKU duplicated
            m_logEdit->appendPlainText(tr("  This SKU already exists on Temu — the product was "
                "created earlier. Wait ~10 min for Temu to finish processing it, then re-open "
                "this dialog: it will detect the product and switch to Update mode."));
        m_publishBtn->setEnabled(true);
        co_return;
    }
    _saveCategoryMapping(catId, m_catNameLabel->text());
    m_logEdit->appendPlainText(tr("OK — goodsId %1 on %2.").arg(id).arg(_currentStore().label));

    // Submit GPSR compliance (manufacturer + EU responsible person) via the
    // verified gpsrInfo shape. Product Identification (GTIN) and country of
    // origin are NOT yet settable via the open API (gated value format), so
    // they still need to be filled in the Seller Center.
    const bool complianceOk = co_await _submitCompliance(id);

    QString extra;
    if (complianceOk)
        extra = tr("\n\nCompliance submitted via the API (manufacturer, EU responsible person, "
                   "product identification).");
    else
        extra = tr("\n\nIMPORTANT: fill the compliance section (manufacturer, EU responsible "
                   "person, product identification) manually in the Temu Seller Center.");

    QMessageBox::information(this, tr("Create / Update"),
        tr("Published to %1 (goodsId %2).\n\nTemu is enriching it (category, attributes…) — "
           "check status in ~10 min.").arg(_currentStore().label).arg(id) + extra);
    m_publishBtn->setEnabled(true);
}

// Submits manufacturer + EU responsible person for the current store's brand
// mapping via bg.local.goods.compliance.edit (gpsrInfo). Logs the outcome.
QCoro::Task<bool> DialogTemuCreateProduct::_submitCompliance(qint64 goodsId)
{
    const StorePick &s = _currentStore();

    // Product Identification (templateId 51): prefer the Amazon GTIN, otherwise
    // fall back to a SKU/model reference (the field accepts free text). Product
    // Identification is per-goods, so pick the first available identifier.
    QString productId;
    for (const Draft::Sku &sku : m_draft.skus) {
        if (!sku.gtin.trimmed().isEmpty()) { productId = sku.gtin.trimmed(); break; }
    }
    if (productId.isEmpty())
        productId = !m_draft.parentSku.isEmpty()
                        ? m_draft.parentSku
                        : (m_draft.skus.isEmpty() ? QString() : m_draft.skus.first().outSkuSn);

    if (s.manufacturerId == 0 && s.gsprRepId == 0 && productId.isEmpty()) {
        m_logEdit->appendPlainText(tr("  No manufacturer / EU responsible person mapped for "
            "brand \"%1\" and no product identifier — fill compliance manually in the "
            "Seller Center.").arg(m_draft.brand));
        co_return false;
    }
    m_logEdit->appendPlainText(tr("Submitting GPSR compliance (manufacturer / EU responsible "
                                  "person / product identification)…"));
    const bool ok = co_await m_api->submitCompliance(goodsId, s.manufacturerId, s.gsprRepId,
                                                      productId);
    if (ok) {
        m_logEdit->appendPlainText(tr("  ✓ Compliance submitted (manufacturer + EU responsible "
            "person%1).").arg(productId.isEmpty() ? QString()
                                                   : tr(" + product identification \"%1\"").arg(productId)));
    } else {
        m_logEdit->appendPlainText(tr("  Compliance submit failed: %1").arg(m_api->lastError()));
    }
    co_return ok;
}
