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
#include <QRegularExpression>
#include <QScrollArea>
#include <QSet>
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
#include "../TemuStoreModel.h"
#include "apis/AmazonPricingApi.h"
#include "AbstractInventorySource.h"
#include "AbstractInventorySourceFactory.h"
#include "MarketplaceTypes.h"
#include "fillers/FillerSize.h"
#include "../../common/workingdirectory/WorkingDirectoryManager.h"

namespace {
// Columns of the per-variation SKU table.
enum SkuCol { kColSku = 0, kColAmazon, kColBase, kColRef, kColAmzQty, kColStock,
              kColWeight, kColL, kColW, kColH, kSkuColCount };

// Language name for a country code, for CLI language instructions.
QString languageForCountry(const QString &cc)
{
    static const QHash<QString, QString> kLang = {
        {"FR","French"},{"DE","German"},{"IT","Italian"},{"ES","Spanish"},
        {"NL","Dutch"},{"SE","Swedish"},{"PL","Polish"},{"BE","French"},
        {"IE","English"},{"UK","English"},{"TR","Turkish"},{"PT","Portuguese"}};
    return kLang.value(cc.toUpper());
}

// Letter sizes are the same across the EU (and expandable for COM/CA/IE), so
// they can be carried over between countries as-is via FillerSize::convertSize.
bool isLetterSize(const QString &t)
{
    static const QRegularExpression re(
        QStringLiteral("^(?:XXS|XS|S|M|L|XL|XXL|XXXL|[2-8]XL)$"),
        QRegularExpression::CaseInsensitiveOption);
    return re.match(t.trimmed()).hasMatch();
}

// True when the size value is a WORD label ("Einheitsgröße", "Taille unique")
// rather than something the tables can convert — those go to CLI translation.
bool isTextualSizeLabel(const QString &s)
{
    static const QRegularExpression wordRe(QStringLiteral("[A-Za-zÀ-ÿ]{2,}"));
    auto it = wordRe.globalMatch(s);
    while (it.hasNext())
        if (!isLetterSize(it.next().captured()))
            return true;
    return false;
}

// Converts one numeric size via the FillerSize table selected by the product's
// sizing category, with an explicit success flag: unlike FillerSize::convertSize
// (which returns the input unchanged on a miss), a miss here must NOT fill the
// cell — publishing a number from the wrong country's size system is worse than
// leaving it empty.
using SizeTable = DialogTemuCreateProduct::SizeTable;
bool convertNumericSize(double num, const QString &from, const QString &to,
                        SizeTable table, QString *out)
{
    auto norm = [](const QString &c) {
        return c == QLatin1String("US") ? QStringLiteral("COM") : c;
    };
    const QString f = norm(from), t = norm(to);
    if (table == SizeTable::ShoesFemale || table == SizeTable::ShoesMale) {
        const auto &rows = table == SizeTable::ShoesMale
            ? FillerSize::SHOE_MALE_ADULT_SIZES : FillerSize::SHOE_FEMALE_ADULT_SIZES;
        for (const auto &row : rows) {
            if (row.contains(f) && row.contains(t) && qFuzzyCompare(row[f], num)) {
                *out = QString::number(row[t], 'g', 4);
                return true;
            }
        }
        return false;
    }
    const int inum = qRound(num);
    if (double(inum) != num) // exact: qFuzzyCompare breaks on 0 (US size 0)
        return false;        // clothing tables are integer-only
    const auto &rows = table == SizeTable::ClothingMale
        ? FillerSize::CLOTHE_MALE_ADULT_SIZES : FillerSize::CLOTHE_FEMALE_ADULT_SIZES;
    for (const auto &row : rows) {
        if (row.contains(f) && row.contains(t) && row[f] == inum) {
            *out = QString::number(row[t]);
            return true;
        }
    }
    return false;
}

// Converts a whole size value between countries: every numeric token is
// converted through the tables ("34-40" DE → "36-42" FR), letter sizes pass
// through convertSize, separators are kept. Returns an empty string when ANY
// token can't be converted — a partially/wrongly converted size must never be
// filled in.
QString convertSizeValue(const QString &src, const QString &from,
                         const QString &to, SizeTable table)
{
    static const QRegularExpression tokRe(
        QStringLiteral("[0-9]+(?:[.,][0-9]+)?|[A-Za-zÀ-ÿ]+"));
    const bool isShoes = table == SizeTable::ShoesFemale || table == SizeTable::ShoesMale;
    const QString gender = (table == SizeTable::ClothingMale
                            || table == SizeTable::ShoesMale)
        ? QStringLiteral("male") : QStringLiteral("female");
    QString result;
    int pos = 0;
    bool any = false;
    auto it = tokRe.globalMatch(src);
    while (it.hasNext()) {
        const auto m = it.next();
        result += src.mid(pos, m.capturedStart() - pos);
        const QString tok = m.captured();
        bool okNum = false;
        const double num = QString(tok).replace(QLatin1Char(','), QLatin1Char('.'))
                               .toDouble(&okNum);
        QString conv;
        if (okNum) {
            if (!convertNumericSize(num, from, to, table, &conv))
                return {};
        } else if (isLetterSize(tok)) {
            conv = FillerSize::convertSize(tok.toUpper(), from, to, gender, isShoes);
        } else {
            return {}; // word label — CLI translation territory
        }
        result += conv;
        any = true;
        pos = m.capturedEnd();
    }
    result += src.mid(pos);
    return any ? result : QString{};
}
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

    // --- Left: images grouped by colour (check to upload, click to preview) ---
    m_imageTree = new QTreeWidget(this);
    m_imageTree->setHeaderHidden(true);
    m_imageTree->setSelectionMode(QAbstractItemView::SingleSelection);
    // Adds a checkable image leaf under `parent`. Roles: UserRole=local path.
    auto addImageLeaf = [](QTreeWidgetItem *parent, const QString &path,
                           bool checked, const QString &tag) {
        if (path.isEmpty() || !QFileInfo::exists(path)) return;
        auto *it = new QTreeWidgetItem(parent);
        it->setText(0, tag.isEmpty() ? QFileInfo(path).fileName()
                                     : QStringLiteral("%1  [%2]").arg(QFileInfo(path).fileName(), tag));
        it->setFlags((it->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsDropEnabled);
        it->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
        it->setData(0, Qt::UserRole, path);
    };
    // node UserRole encodes what the node's images are for: a base colour name,
    // "" for the single "All images" node (applies to every SKU), or "##product"
    // for product-level images (the size chart → goods detailImage).
    auto addColorNode = [this](const QString &label, const QString &role) {
        auto *node = new QTreeWidgetItem(m_imageTree);
        node->setText(0, label);
        node->setData(0, Qt::UserRole, role);
        node->setFlags(node->flags() & ~Qt::ItemIsUserCheckable & ~Qt::ItemIsDragEnabled);
        node->setExpanded(true);
        return node;
    };
    // Common (shared) images — the main image and anything not tied to a colour.
    // They're duplicated into every colour node so their order relative to the
    // colour images can be chosen per colour (and unchecked per colour).
    const QStringList commonImgs = m_draft.galleryByColor.value(QString{});
    QStringList colors;
    for (auto it = m_draft.galleryByColor.constBegin(); it != m_draft.galleryByColor.constEnd(); ++it)
        if (!it.key().isEmpty())
            colors << it.key();

    if (colors.isEmpty()) {
        // Single-/no-colour product: one node with everything, applies to all.
        auto *node = addColorNode(tr("All images"), QString{});
        for (const QString &p : commonImgs)
            addImageLeaf(node, p, true, {});
        for (const QString &p : m_draft.extraImagePaths)
            addImageLeaf(node, p, false, tr("A+"));
    } else {
        for (const QString &color : colors) {
            auto *node = addColorNode(color, color);
            for (const QString &p : commonImgs)
                addImageLeaf(node, p, true, tr("common"));
            for (const QString &p : m_draft.galleryByColor.value(color))
                addImageLeaf(node, p, true, {});
            for (const QString &p : m_draft.extraImagePaths)
                addImageLeaf(node, p, false, tr("A+"));
        }
    }
    // Size chart is a product-level image (goods detailImage), not a per-SKU
    // gallery image, so it lives in its own node.
    if (!m_draft.sizeChartImagePath.isEmpty() && QFileInfo::exists(m_draft.sizeChartImagePath)) {
        auto *scNode = addColorNode(tr("Product-level"), QStringLiteral("##product"));
        addImageLeaf(scNode, m_draft.sizeChartImagePath, true, tr("size chart"));
    }
    // Restore any previously-saved image selection/order for this product.
    _restoreImageState();

    // Reorder a leaf within its own colour group (Temu upload order = tree order).
    auto *upBtn   = new QPushButton(tr("↑ Up"), this);
    auto *downBtn = new QPushButton(tr("↓ Down"), this);
    auto moveRow = [this](int delta) {
        QTreeWidgetItem *cur = m_imageTree->currentItem();
        if (!cur || !cur->parent()) return; // only image leaves move
        QTreeWidgetItem *parent = cur->parent();
        const int idx = parent->indexOfChild(cur);
        const int dst = idx + delta;
        if (dst < 0 || dst >= parent->childCount()) return;
        const bool checked = cur->checkState(0) == Qt::Checked;
        parent->takeChild(idx);
        parent->insertChild(dst, cur);
        cur->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked); // keep state
        m_imageTree->setCurrentItem(cur);
    };
    connect(upBtn,   &QPushButton::clicked, this, [moveRow]() { moveRow(-1); });
    connect(downBtn, &QPushButton::clicked, this, [moveRow]() { moveRow(1); });
    auto *reorderRow = new QHBoxLayout;
    reorderRow->addWidget(upBtn);
    reorderRow->addWidget(downBtn);
    reorderRow->addStretch();

    auto *imagesBox = new QGroupBox(tr("Images (per colour · check to upload · ↑↓ to reorder)"), this);
    auto *imagesLay = new QVBoxLayout(imagesBox);
    imagesLay->addWidget(m_imageTree);
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
    auto *aiPickAttrsBtn = new QPushButton(tr("Pick by AI (from images)"), this);
    aiPickAttrsBtn->setToolTip(tr("Looks at the product images (and text) and fills the "
                                  "attributes it can confidently determine, choosing only "
                                  "from each attribute's allowed values. Skips the rest."));
    connect(aiPickAttrsBtn, &QPushButton::clicked, this,
            [this]() { m_attrAiTask = _aiPickAttributes(); });
    attrBoxLay->addWidget(aiPickAttrsBtn);
    attrBoxLay->addWidget(attrScroll);

    m_topSplit = new QSplitter(Qt::Horizontal, this);
    m_topSplit->addWidget(imagesBox);
    m_topSplit->addWidget(previewBox);
    m_topSplit->addWidget(attrBox);
    m_topSplit->setSizes({260, 320, 380});

    // --- Text (each field individually regenerable via the CLI) ---
    // No maximum heights on the text editors / log: a hard cap makes the
    // vertical splitter handles immovable (the section can't grow), so the
    // initial compactness comes from the splitter sizes instead.
    m_titleEdit = new QLineEdit(m_draft.title, this);
    m_bulletsEdit = new QPlainTextEdit(m_draft.bulletPoints.join(QLatin1Char('\n')), this);
    m_descEdit = new QPlainTextEdit(m_draft.description, this);
    auto *genBtn = new QPushButton(tr("Generate all text"), this);
    auto *regenAllBtn = new QPushButton(tr("Regenerate all (every language)"), this);
    auto *resetTextBtn = new QPushButton(tr("Reset"), this);
    resetTextBtn->setToolTip(tr("Discard the AI-generated text and restore the original "
                                "(source / Amazon) title and bullets for every language."));
    auto *regenTitleBtn   = new QPushButton(tr("Regenerate"), this);
    auto *regenBulletsBtn = new QPushButton(tr("Regenerate"), this);
    auto *regenDescBtn    = new QPushButton(tr("Regenerate"), this);
    // Per-field "regenerate this one field in every language" buttons.
    auto *allTitleBtn   = new QPushButton(tr("Reg. all langs."), this);
    auto *allBulletsBtn = new QPushButton(tr("Reg. all langs."), this);
    auto *allDescBtn    = new QPushButton(tr("Reg. all langs."), this);

    // Left-hand picker of the selected stores' country codes: click one to view
    // and edit that country's language in the fields on the right. Deduped &
    // ordered by the store list so it mirrors the publish targets.
    m_textCountryList = new QListWidget(this);
    m_textCountryList->setMaximumWidth(70);
    {
        QStringList seen;
        for (const StorePick &s : m_stores) {
            const QString cc = s.country.toUpper();
            if (!cc.isEmpty() && !seen.contains(cc)) {
                seen << cc;
                new QListWidgetItem(cc, m_textCountryList);
            }
        }
    }

    auto fieldRow = [](QWidget *editor, QPushButton *btn, QPushButton *allBtn) {
        auto *row = new QHBoxLayout;
        row->addWidget(editor, 1);
        auto *col = new QVBoxLayout;
        col->addWidget(btn);
        col->addWidget(allBtn);
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
    {
        auto *allRow = new QHBoxLayout;
        allRow->addWidget(regenAllBtn);
        allRow->addWidget(resetTextBtn);
        allRow->addStretch();
        textForm->addRow(QString{}, allRow);
    }
    textForm->addRow(QString{}, kwRow);
    textForm->addRow(tr("Title:"), fieldRow(m_titleEdit, regenTitleBtn, allTitleBtn));
    textForm->addRow(tr("Bullets:"), fieldRow(m_bulletsEdit, regenBulletsBtn, allBulletsBtn));
    textForm->addRow(tr("Description:"), fieldRow(m_descEdit, regenDescBtn, allDescBtn));

    // --- Per-variation table: price + packaging, one row per SKU ---
    m_skuTable = new QTableWidget(m_draft.skus.size(), kSkuColCount, this);
    m_skuTable->setHorizontalHeaderLabels({
        tr("SKU"), tr("Amazon €"), tr("Base € (retailPrice)"), tr("Reference € (listPrice)"),
        tr("Amz Qty"), tr("Stock"), tr("Weight g"), tr("L cm"), tr("W cm"), tr("H cm")});
    m_skuTable->horizontalHeader()->setStretchLastSection(false);
    m_skuTable->verticalHeader()->setVisible(false);
    for (int r = 0; r < m_draft.skus.size(); ++r) {
        auto *skuItem = new QTableWidgetItem(m_draft.skus[r].outSkuSn);
        // Editable so the SKU can be double-clicked, selected and copied; any
        // actual edit is reverted by the itemChanged guard wired below (the SKU
        // is an identifier and must not change).
        skuItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
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

    // Keep the SKU column copyable (editable) but immutable: revert any edit to
    // the original SKU. Wired after population so the initial setText calls above
    // don't trip it.
    connect(m_skuTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *it) {
        if (!it || it->column() != kColSku)
            return;
        const int r = it->row();
        const QString orig = (r >= 0 && r < m_draft.skus.size())
                                 ? m_draft.skus.at(r).outSkuSn : QString();
        if (it->text() != orig) {
            QSignalBlocker block(m_skuTable);
            it->setText(orig);
        }
    });

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
            // NEVER fall back to another country's size — a wrong size must not be
            // shown or published. If this country's size wasn't retrieved, leave
            // it blank so it's visibly unset (and editable).
            child->setText(2, ds.sizeByCountry.value(cc));
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

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_publishBtn = buttons->addButton(tr("Create / Update"), QDialogButtonBox::AcceptRole);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // Stack the resizable sections in a vertical splitter so the user can drag a
    // handle to give more room to whichever section they're working in — e.g.
    // enlarge the images/preview/attributes row when reordering images.
    m_mainSplit = new QSplitter(Qt::Vertical, this);
    QSplitter *mainSplit = m_mainSplit;
    mainSplit->setChildrenCollapsible(false);

    mainSplit->addWidget(m_topSplit); // images | preview | attributes

    auto *textWidget = new QWidget(mainSplit);
    auto *textRow = new QHBoxLayout(textWidget);
    textRow->setContentsMargins(0, 0, 0, 0);
    {
        auto *listCol = new QVBoxLayout;
        listCol->setContentsMargins(0, 0, 0, 0);
        listCol->addWidget(new QLabel(tr("Language:"), textWidget));
        listCol->addWidget(m_textCountryList, 1);
        textRow->addLayout(listCol);
    }
    textRow->addLayout(textForm, 1);
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
    {
        auto *treeHead = new QHBoxLayout;
        m_completeVariantsBtn = new QPushButton(tr("Complete"), treeWidget);
        m_completeVariantsBtn->setToolTip(
            tr("Fills the missing cells from the countries that have a value:\n"
               "• sizes are converted between the countries' size systems using "
               "the product's sizing category (e.g. women: FR 40 = DE 38 = IT 44);\n"
               "• colours (and textual size labels) are translated by the AI CLI "
               "into each country's language.\n"
               "Cells you edited are never overwritten."));
        treeHead->addWidget(m_completeVariantsBtn);
        treeHead->addWidget(new QLabel(tr("Per-country variation names (from each Amazon "
                                          "marketplace — edit any colour/size):"), treeWidget), 1);
        treeLay->addLayout(treeHead);
    }
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
    // Initial compact distribution (the editors no longer carry max-height
    // caps, so this is what keeps the first-open look tidy).
    mainSplit->setSizes({340, 210, 200, 150, 110});

    // Restore the splitter positions from the previous session (saved in done()).
    {
        QSettings st;
        const QByteArray mainState = st.value(QStringLiteral("temuCreateDialog/mainSplitter")).toByteArray();
        if (!mainState.isEmpty()) mainSplit->restoreState(mainState);
        const QByteArray topState = st.value(QStringLiteral("temuCreateDialog/topSplitter")).toByteArray();
        if (!topState.isEmpty()) m_topSplit->restoreState(topState);
    }

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
    connect(m_completeVariantsBtn, &QPushButton::clicked, this,
            [this]() { m_completeTask = _completeVariantNames(); });
    connect(editKwBtn, &QPushButton::clicked, this, [this]() {
        DialogKeywordTemplates dlg(this);
        // Offer the country codes of the configured Temu stores as the pick-list.
        QStringList countries;
        TemuStoreModel storeModel;
        for (const TemuStore &st : storeModel.stores()) {
            const QString cc = st.country.toUpper();
            if (!cc.isEmpty() && !countries.contains(cc))
                countries << cc;
        }
        dlg.setAvailableCountries(countries);
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
    connect(regenAllBtn,     &QPushButton::clicked, this, [this]() { m_textAllTask = _regenerateAllText(); });
    connect(resetTextBtn,    &QPushButton::clicked, this, [this]() { _resetText(); });
    connect(allTitleBtn,     &QPushButton::clicked, this, [this]() { m_textAllTask = _regenerateFieldAllLangs(0); });
    connect(allBulletsBtn,   &QPushButton::clicked, this, [this]() { m_textAllTask = _regenerateFieldAllLangs(1); });
    connect(allDescBtn,      &QPushButton::clicked, this, [this]() { m_textAllTask = _regenerateFieldAllLangs(2); });
    // Picking a language in the list swaps the editors to that country's text.
    // Independent of the top Store dropdown (which stays the publish target).
    connect(m_textCountryList, &QListWidget::currentTextChanged, this,
            [this](const QString &cc) { if (!cc.isEmpty()) _loadCountryText(cc); });
    // Controls locked out while a text (re)generation runs, so the shown language
    // can't change mid-flight and misfile the result.
    m_textControls = { m_textCountryList, m_storeCombo, genBtn, regenAllBtn, resetTextBtn,
                       regenTitleBtn, regenBulletsBtn, regenDescBtn,
                       allTitleBtn, allBulletsBtn, allDescBtn };
    connect(m_publishBtn, &QPushButton::clicked, this, [this]() { m_publishTask = _publish(); });
    connect(m_imageTree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *cur, QTreeWidgetItem *) {
                const QString path = cur ? cur->data(0, Qt::UserRole).toString() : QString{};
                if (path.isEmpty()) { m_imagePreview->setText(tr("(select an image)")); return; }
                const QPixmap pm(path);
                if (pm.isNull()) { m_imagePreview->setText(tr("(cannot load image)")); return; }
                m_imagePreview->setPixmap(pm.scaled(m_imagePreview->size(),
                    Qt::KeepAspectRatio, Qt::SmoothTransformation));
            });

    // Seed the per-country text map from the Amazon-fetched localized text, then
    // let any previously-saved (regenerated) text override it.
    for (auto it = m_draft.textByCountry.cbegin(); it != m_draft.textByCountry.cend(); ++it)
        m_pageText.insert(it.key(), {it.value().title, it.value().bullets.join(QLatin1Char('\n')),
                                     QString{}});
    _restoreTextState();

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
    // Keep the side list highlight in sync (e.g. when the store dropdown drives
    // the change) without re-entering this slot.
    if (m_textCountryList) {
        const QList<QListWidgetItem*> hits = m_textCountryList->findItems(country, Qt::MatchExactly);
        if (!hits.isEmpty() && m_textCountryList->currentItem() != hits.first()) {
            QSignalBlocker b(m_textCountryList);
            m_textCountryList->setCurrentItem(hits.first());
        }
    }
    if (!m_pageText.contains(country)) {
        // NEVER fall back to the source draft here — that showed (and published)
        // the source language (e.g. German) on the FR/IT/ES page. Missing text
        // stays EMPTY until the user generates it in the right language.
        m_pageText.insert(country, TemuPageText{});
    }
    const TemuPageText &t = m_pageText.value(country);
    m_titleEdit->setText(t.title);
    m_bulletsEdit->setPlainText(t.bullets);
    m_descEdit->setPlainText(t.description);

    if (m_draft.textByCountry.contains(country))
        m_logEdit->appendPlainText(tr("Loaded %1 listing text from Amazon.").arg(country));
    else
        m_logEdit->appendPlainText(tr("No %1 text found on Amazon — fields left empty; "
            "click Regenerate/Generate to write them in %2.")
            .arg(country, _storeLanguage().isEmpty() ? country : _storeLanguage()));
}

// Writes a regenerated field into the given country's stored text and reflects
// it in the editors only when that country is still on screen (see header).
void DialogTemuCreateProduct::_applyTextResult(const QString &country, int which,
                                               const QString &value)
{
    TemuPageText &pt = m_pageText[country];
    const bool live = (m_curTextCountry == country);
    switch (which) {
    case 0:  pt.title = value;       if (live) m_titleEdit->setText(value);      break;
    case 1:  pt.bullets = value;     if (live) m_bulletsEdit->setPlainText(value); break;
    default: pt.description = value; if (live) m_descEdit->setPlainText(value);  break;
    }
    m_textRegenerated = true; // any AI write counts as "regenerated"
}

void DialogTemuCreateProduct::_setTextBusy(bool busy)
{
    m_textBusy = qMax(0, m_textBusy + (busy ? 1 : -1));
    const bool locked = m_textBusy > 0;
    for (QWidget *w : m_textControls)
        if (w) w->setEnabled(!locked);
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

    co_await _loadCategoryTemplate();
}

QCoro::Task<void> DialogTemuCreateProduct::_loadCategoryTemplate()
{
    if (!m_api)
        co_return;
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
    // Re-apply attribute values the user filled on a previous (failed) attempt.
    _applySavedAttributes();
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

// --- Persist the manual setup so a failed upload doesn't cost it again -------

void DialogTemuCreateProduct::_saveWorkState()
{
    if (m_draft.productDir.isEmpty())
        return;
    QSettings ps(QDir(m_draft.productDir).filePath(QStringLiteral("settings.ini")),
                 QSettings::IniFormat);

    // Image tree: per node role → ordered list of {path, checked}.
    if (m_imageTree && m_imageTree->topLevelItemCount() > 0) {
        QJsonObject imgObj;
        for (int t = 0; t < m_imageTree->topLevelItemCount(); ++t) {
            QTreeWidgetItem *node = m_imageTree->topLevelItem(t);
            QJsonArray arr;
            for (int c = 0; c < node->childCount(); ++c) {
                QTreeWidgetItem *it = node->child(c);
                QJsonObject o;
                o.insert(QStringLiteral("p"), it->data(0, Qt::UserRole).toString());
                o.insert(QStringLiteral("c"), it->checkState(0) == Qt::Checked);
                arr.append(o);
            }
            imgObj.insert(node->data(0, Qt::UserRole).toString(), arr);
        }
        ps.setValue(QStringLiteral("temu/imageState"),
                    QString::fromUtf8(QJsonDocument(imgObj).toJson(QJsonDocument::Compact)));
    }

    // Attributes: name → value. Only when the template is loaded, so we never
    // wipe a good saved set with an empty one (e.g. template failed to load).
    if (!m_attrs.isEmpty()) {
        QJsonObject attrObj;
        for (int i = 0; i < m_attrs.size(); ++i) {
            QString val;
            if (auto *c = m_attrCombos.value(i)) {
                if (c->currentIndex() <= 0) continue;
                val = c->currentText();
            } else if (auto *e = m_attrInputs.value(i)) {
                val = e->text().trimmed();
                if (val.isEmpty()) continue;
            } else {
                continue;
            }
            attrObj.insert(m_attrs[i].name, val);
        }
        ps.setValue(QStringLiteral("temu/attributes"),
                    QString::fromUtf8(QJsonDocument(attrObj).toJson(QJsonDocument::Compact)));
    }

    // Per-country listing text (regenerated content). Flush the live editors into
    // the current country first so the latest edits are captured.
    if (!m_curTextCountry.isEmpty())
        m_pageText[m_curTextCountry] = { m_titleEdit->text(), m_bulletsEdit->toPlainText(),
                                         m_descEdit->toPlainText() };
    if (!m_pageText.isEmpty()) {
        QJsonObject txtObj;
        for (auto it = m_pageText.cbegin(); it != m_pageText.cend(); ++it) {
            QJsonObject t;
            t.insert(QStringLiteral("title"), it.value().title);
            t.insert(QStringLiteral("bullets"), it.value().bullets);
            t.insert(QStringLiteral("desc"), it.value().description);
            txtObj.insert(it.key(), t);
        }
        ps.setValue(QStringLiteral("temu/pageText"),
                    QString::fromUtf8(QJsonDocument(txtObj).toJson(QJsonDocument::Compact)));
    }
    ps.setValue(QStringLiteral("temu/textRegenerated"), m_textRegenerated);
}

void DialogTemuCreateProduct::_restoreImageState()
{
    if (m_draft.productDir.isEmpty() || !m_imageTree)
        return;
    QSettings ps(QDir(m_draft.productDir).filePath(QStringLiteral("settings.ini")),
                 QSettings::IniFormat);
    const QJsonObject imgObj = QJsonDocument::fromJson(
        ps.value(QStringLiteral("temu/imageState")).toString().toUtf8()).object();
    if (imgObj.isEmpty())
        return;

    for (int t = 0; t < m_imageTree->topLevelItemCount(); ++t) {
        QTreeWidgetItem *node = m_imageTree->topLevelItem(t);
        const QString role = node->data(0, Qt::UserRole).toString();
        if (!imgObj.contains(role))
            continue;
        // Detach all children, then re-add them in the saved order (matched by
        // path), setting each saved check state; children not in the saved list
        // (new images) keep their default state and go last.
        QHash<QString, QTreeWidgetItem*> byPath;
        QList<QTreeWidgetItem*> remaining;
        while (node->childCount() > 0) {
            QTreeWidgetItem *ch = node->takeChild(0);
            byPath.insert(ch->data(0, Qt::UserRole).toString(), ch);
            remaining.append(ch);
        }
        for (const QJsonValue &v : imgObj.value(role).toArray()) {
            const QJsonObject o = v.toObject();
            auto it = byPath.find(o.value(QStringLiteral("p")).toString());
            if (it == byPath.end())
                continue;
            QTreeWidgetItem *ch = it.value();
            ch->setCheckState(0, o.value(QStringLiteral("c")).toBool() ? Qt::Checked : Qt::Unchecked);
            node->addChild(ch);
            remaining.removeOne(ch);
            byPath.erase(it);
        }
        for (QTreeWidgetItem *ch : remaining)
            node->addChild(ch);
        node->setExpanded(true);
    }
}

void DialogTemuCreateProduct::_applySavedAttributes()
{
    if (m_draft.productDir.isEmpty() || m_attrs.isEmpty())
        return;
    QSettings ps(QDir(m_draft.productDir).filePath(QStringLiteral("settings.ini")),
                 QSettings::IniFormat);
    const QJsonObject o = QJsonDocument::fromJson(
        ps.value(QStringLiteral("temu/attributes")).toString().toUtf8()).object();
    if (o.isEmpty())
        return;

    for (int i = 0; i < m_attrs.size(); ++i) {
        if (!o.contains(m_attrs[i].name))
            continue;
        const QString val = o.value(m_attrs[i].name).toString();
        if (val.isEmpty())
            continue;
        QWidget *field = nullptr;
        if (auto *c = m_attrCombos.value(i)) {
            const int idx = c->findText(val);
            if (idx >= 0) c->setCurrentIndex(idx);
            field = c;
        } else if (auto *e = m_attrInputs.value(i)) {
            e->setText(val);
            field = e;
        }
        // Clear the red "required" hint now that the field is filled.
        if (field)
            if (auto *lbl = qobject_cast<QLabel*>(m_attrForm->labelForField(field)))
                lbl->setStyleSheet(m_attrs[i].required ? QStringLiteral("font-weight: bold;")
                                                       : QString{});
    }
}

void DialogTemuCreateProduct::done(int r)
{
    _saveWorkState(); // preserve the manual setup on Cancel / window close too
    // Remember the splitter positions for the next dialog open.
    QSettings st;
    if (m_mainSplit) st.setValue(QStringLiteral("temuCreateDialog/mainSplitter"), m_mainSplit->saveState());
    if (m_topSplit)  st.setValue(QStringLiteral("temuCreateDialog/topSplitter"),  m_topSplit->saveState());
    QDialog::done(r);
}

// Original (pre-regeneration) text for a country: the Amazon-localized title +
// bullets when we have them. Description starts empty. Without localized text
// the original is EMPTY — never the source draft, whose language is wrong for
// every other country.
TemuPageText DialogTemuCreateProduct::_originalCountryText(const QString &country) const
{
    if (m_draft.textByCountry.contains(country)) {
        const auto &lt = m_draft.textByCountry.value(country);
        return { lt.title, lt.bullets.join(QLatin1Char('\n')), QString{} };
    }
    return TemuPageText{};
}

void DialogTemuCreateProduct::_restoreTextState()
{
    if (m_draft.productDir.isEmpty())
        return;
    QSettings ps(QDir(m_draft.productDir).filePath(QStringLiteral("settings.ini")),
                 QSettings::IniFormat);
    m_textRegenerated = ps.value(QStringLiteral("temu/textRegenerated"), false).toBool();
    const QJsonObject txtObj = QJsonDocument::fromJson(
        ps.value(QStringLiteral("temu/pageText")).toString().toUtf8()).object();
    for (const QString &key : txtObj.keys()) {
        const QJsonObject t = txtObj.value(key).toObject();
        m_pageText.insert(key, { t.value(QStringLiteral("title")).toString(),
                                 t.value(QStringLiteral("bullets")).toString(),
                                 t.value(QStringLiteral("desc")).toString() });
    }
}

void DialogTemuCreateProduct::_resetText()
{
    if (QMessageBox::question(this, tr("Reset text"),
            tr("Discard the generated title/bullets/description and restore the "
               "original text for every language?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    // Rebuild the map from the originals for every known country.
    QStringList countries = m_pageText.keys();
    if (m_textCountryList)
        for (int i = 0; i < m_textCountryList->count(); ++i)
            countries << m_textCountryList->item(i)->text();
    countries.removeDuplicates();

    m_pageText.clear();
    for (const QString &cc : countries)
        m_pageText.insert(cc, _originalCountryText(cc));
    m_textRegenerated = false;

    // Reload the currently-viewed country into the editors (clearing the
    // "current" first so _loadCountryText actually re-applies it).
    const QString cur = m_curTextCountry;
    m_curTextCountry.clear();
    if (!cur.isEmpty())
        _loadCountryText(cur);
    else if (!m_stores.isEmpty())
        _loadCountryText(_currentStore().country.toUpper());

    _saveWorkState(); // persist the cleared/original state
    m_logEdit->appendPlainText(tr("Text reset to the original (source / Amazon)."));
}

QCoro::Task<void> DialogTemuCreateProduct::_aiPickAttributes()
{
    if (m_attrAiBusy)
        co_return;
    if (!m_cli) {
        QMessageBox::warning(this, tr("Pick by AI"), tr("No CLI selected."));
        co_return;
    }
    if (m_attrs.isEmpty()) {
        QMessageBox::information(this, tr("Pick by AI"),
            tr("Load the category attributes first."));
        co_return;
    }
    if (m_draft.productDir.isEmpty()) {
        QMessageBox::warning(this, tr("Pick by AI"), tr("No product folder."));
        co_return;
    }

    // Real product photos (main + per-colour), excluding A+ and the size chart.
    QStringList imgFiles;
    QSet<QString> seenImg;
    for (auto it = m_draft.galleryByColor.constBegin(); it != m_draft.galleryByColor.constEnd(); ++it)
        for (const QString &p : it.value()) {
            const QString fn = QFileInfo(p).fileName();
            if (!seenImg.contains(fn) && QFileInfo::exists(p)) { seenImg.insert(fn); imgFiles << fn; }
        }
    if (imgFiles.isEmpty()) {
        QMessageBox::information(this, tr("Pick by AI"), tr("No product images to analyse."));
        co_return;
    }

    // Attribute list with allowed values (only the top-level, editable ones).
    QStringList attrLines;
    for (int i = 0; i < m_attrs.size(); ++i) {
        const auto &attr = m_attrs[i];
        if (attr.parentTemplatePid != 0)
            continue;
        if (attr.controlType == 1 && !attr.values.isEmpty()) {
            QStringList allowed;
            for (const auto &v : attr.values) allowed << v.first;
            attrLines << tr("- \"%1\" — allowed: [%2]").arg(attr.name,
                            QStringLiteral("\"") + allowed.join(QStringLiteral("\", \"")) + QStringLiteral("\""));
        } else {
            attrLines << tr("- \"%1\" — free text").arg(attr.name);
        }
    }

    const QString prompt = tr(
        "You are an e-commerce cataloguer. FIRST open and visually inspect EVERY "
        "product image file listed below (they are in the current folder), then "
        "decide the product's attribute values.\n\n"
        "Product images (open each one): %1\n\n"
        "Product text (also use it — fabric/material is often stated here):\n"
        "Title: %2\nBrand: %3\nBullets:\n%4\n\n"
        "Fill AS MANY attributes as you reasonably can. Most clothing attributes "
        "are visible in the photos — pattern, sheerness, sleeve length, neckline, "
        "silhouette/fit, length, closure, decoration, style, occasion, season, "
        "collar, etc. — infer those from the images. For material/fabric "
        "composition, use the TEXT only (do not guess fibre content from a photo). "
        "Only OMIT an attribute when neither the images nor the text give any "
        "reasonable basis. Prefer a well-justified choice over omitting.\n\n"
        "For an attribute that has an allowed list, you MUST return one of the "
        "listed values EXACTLY (same spelling). Use the attribute names EXACTLY as "
        "written below as the JSON keys.\n\n"
        "Attributes:\n%5\n\n"
        "Return ONLY strict JSON mapping each attribute name to its chosen value, "
        "e.g. {\"Material\":\"Polyester\",\"Occasion\":\"Beach\"}. No markdown, no comments.")
        .arg(imgFiles.join(QStringLiteral(", ")), m_draft.title, m_draft.brand,
             m_draft.bulletPoints.join(QLatin1Char('\n')), attrLines.join(QLatin1Char('\n')));

    m_attrAiBusy = true;
    auto busyGuard = qScopeGuard([this] { m_attrAiBusy = false; });
    m_logEdit->appendPlainText(tr("Asking %1 to pick attributes from the images…")
                                   .arg(m_cli->getName()));

    const CliRunResult r = co_await _runCli(prompt, m_draft.productDir);
    QString out = r.output.trimmed();
    const int a = out.indexOf(QLatin1Char('{'));
    const int b = out.lastIndexOf(QLatin1Char('}'));
    if (a >= 0 && b > a)
        out = out.mid(a, b - a + 1);
    const QJsonObject o = QJsonDocument::fromJson(out.toUtf8()).object();
    if (o.isEmpty()) {
        m_logEdit->appendPlainText(tr("  AI returned no usable attributes."));
        co_return;
    }

    // Map the AI's JSON keys to attributes case-insensitively (the model often
    // varies casing/spacing), so a near-match name still applies instead of
    // being silently dropped.
    QHash<QString, int> byName;
    for (int i = 0; i < m_attrs.size(); ++i)
        if (m_attrs[i].parentTemplatePid == 0)
            byName.insert(m_attrs[i].name.trimmed().toLower(), i);

    QSet<int> filledIdx;      // top-level attrs the AI successfully filled
    QStringList badValues;    // "attr=value" the AI returned but not in allowed list
    QStringList unmatchedKeys; // JSON keys that map to no attribute in this category
    for (const QString &key : o.keys()) {
        const int i = byName.value(key.trimmed().toLower(), -1);
        if (i < 0) { unmatchedKeys << key; continue; }
        const auto &attr = m_attrs[i];
        const QString val = o.value(key).toString().trimmed();
        if (val.isEmpty()) continue;
        QWidget *field = nullptr;
        if (auto *c = m_attrCombos.value(i)) {
            int idx = c->findText(val, Qt::MatchFixedString); // case-insensitive exact
            if (idx < 0) idx = c->findText(val);
            if (idx < 0) {
                badValues << QStringLiteral("%1=\"%2\"").arg(attr.name, val);
                continue;
            }
            c->setCurrentIndex(idx);
            field = c;
        } else if (auto *e = m_attrInputs.value(i)) {
            e->setText(val);
            field = e;
        }
        if (field) {
            if (auto *lbl = qobject_cast<QLabel*>(m_attrForm->labelForField(field)))
                lbl->setStyleSheet(attr.required ? QStringLiteral("font-weight: bold;") : QString{});
            filledIdx.insert(i);
        }
    }

    // Everything the AI did NOT fill (the whole point of the summary the user
    // wants): list them by name so it's clear what still needs attention.
    QStringList skippedNames, skippedRequired;
    int total = 0;
    for (int i = 0; i < m_attrs.size(); ++i) {
        if (m_attrs[i].parentTemplatePid != 0)
            continue;
        ++total;
        if (filledIdx.contains(i))
            continue;
        skippedNames << m_attrs[i].name;
        if (m_attrs[i].required)
            skippedRequired << m_attrs[i].name;
    }

    if (!unmatchedKeys.isEmpty())
        m_logEdit->appendPlainText(tr("  AI returned %1 name(s) not in this category: %2")
                                       .arg(QString::number(unmatchedKeys.size()),
                                            unmatchedKeys.join(QStringLiteral(", "))));
    if (!badValues.isEmpty())
        m_logEdit->appendPlainText(tr("  Not an allowed value (kept empty): %1")
                                       .arg(badValues.join(QStringLiteral(", "))));
    m_logEdit->appendPlainText(tr("AI filled %1 of %2 attribute(s).")
                                   .arg(filledIdx.size()).arg(total));
    if (!skippedNames.isEmpty())
        m_logEdit->appendPlainText(tr("  Skipped %1 (not determined): %2")
                                       .arg(QString::number(skippedNames.size()),
                                            skippedNames.join(QStringLiteral(", "))));
    if (!skippedRequired.isEmpty())
        m_logEdit->appendPlainText(tr("  ⚠ Required still empty: %1")
                                       .arg(skippedRequired.join(QStringLiteral(", "))));
    _saveWorkState(); // persist the AI picks immediately
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

// Country whose language is currently shown in the text editors (independent of
// the publish-target store dropdown), for per-language title/keyword generation.
QString DialogTemuCreateProduct::_textCountry() const
{
    return !m_curTextCountry.isEmpty() ? m_curTextCountry.toUpper()
         : (m_stores.isEmpty() ? QString{} : _currentStore().country.toUpper());
}

// Keyword clause for the currently-viewed country from the selected template.
QString DialogTemuCreateProduct::_titleKeywordInstruction() const
{
    const QString id = m_keywordTemplateCombo->currentData().toString();
    if (id.isEmpty() || _textCountry().isEmpty())
        return {};
    const QStringList kws = DialogKeywordTemplates::keywordsFor(id, _textCountry());
    if (kws.isEmpty())
        return {};
    return tr("\n\nThe title MUST naturally include ALL of these keywords "
              "(Temu ranks titles by keywords): %1").arg(kws.join(QStringLiteral(", ")));
}

// Language of the current store's country, for CLI generation.
QString DialogTemuCreateProduct::_storeLanguage() const
{
    // Key off the country currently shown in the text editors (the language the
    // user is viewing/regenerating), not the publish-target store dropdown — the
    // two are independent now that a language can be picked in the side list.
    return languageForCountry(_textCountry());
}

// Asks the CLI to weave the product's variation values (colour/size) into the
// title. Uses the LOCALIZED colour/size names of the country whose language is
// being generated (so the DE title says "Königs Blau", the FR one "Bleu Royal"),
// taken from the same source the publish uses: the per-country tree first (which
// holds any hand-edited names), then the fetched per-country maps, then the base.
QString DialogTemuCreateProduct::_variationInstruction() const
{
    const QString cc = _textCountry();
    QStringList colors, sizes;
    for (int r = 0; r < m_draft.skus.size(); ++r) {
        const auto &s = m_draft.skus.at(r);
        QString color = s.colorByCountry.value(cc, s.color);
        QString size  = s.sizeByCountry.value(cc, s.size);
        if (m_variantTree && r < m_variantTree->topLevelItemCount()) {
            QTreeWidgetItem *top = m_variantTree->topLevelItem(r);
            for (int k = 0; k < top->childCount(); ++k) {
                if (top->child(k)->text(0).compare(cc, Qt::CaseInsensitive) == 0) {
                    const QString tc  = top->child(k)->text(1).trimmed();
                    const QString tsz = top->child(k)->text(2).trimmed();
                    if (!tc.isEmpty())  color = tc;
                    if (!tsz.isEmpty()) size  = tsz;
                    break;
                }
            }
        }
        if (!color.isEmpty() && !colors.contains(color)) colors << color;
        if (!size.isEmpty()  && !sizes.contains(size))   sizes  << size;
    }
    // The Temu title is SHARED across every variation, so we never put the size
    // in it (size is a per-variation dimension), and we only name the colour when
    // the product has a SINGLE colour — otherwise the title would wrongly name
    // just one variation's colour.
    Q_UNUSED(sizes);
    if (colors.size() != 1)
        return {};
    return tr("\n\nThe title should naturally include the product's colour %1 — "
              "keep this exact word (it is already in the title's language; do "
              "not translate it). Do NOT put any size in the title.")
        .arg(colors.first());
}

// Strong single-language rule: the whole field must be in the target language,
// with every foreign source word translated — copying e.g. a French phrase into
// a German listing is the exact bug this forbids.
QString DialogTemuCreateProduct::_languageInstruction(const QString &what) const
{
    const QString lang = _storeLanguage();
    if (lang.isEmpty())
        return {};
    return tr("\n\nCRITICAL LANGUAGE RULE: Write %1 ENTIRELY in %2. The source "
              "text above may be in French or another language — TRANSLATE every "
              "word into %2; do NOT copy any foreign word or phrase verbatim. The "
              "result must contain ZERO words from any language other than %2 "
              "(the product's own colour/size names, already given in %2, are the "
              "only exception).").arg(what, lang);
}

// Title-only guidance shared by generate + title-regenerate.
QString DialogTemuCreateProduct::_titleGuidance() const
{
    return tr("\n\nTITLE RULES:\n"
              "- Make the title polished, fluent and complete (aim for 80-100 "
              "characters, never exceed 100).\n"
              "- Enrich it with relevant SEO search keywords a shopper would type "
              "(product type, style, occasion, material, target audience) in "
              "addition to any keywords listed above, while keeping it natural and "
              "readable.");
}

// Forbids the brand name in every generated field. Naming the brand explicitly
// is far more reliable than merely omitting it from the prompt.
QString DialogTemuCreateProduct::_noBrandInstruction() const
{
    const QString brand = m_draft.brand.trimmed();
    if (brand.isEmpty())
        return {};
    return tr("\n\nDo NOT mention the brand name \"%1\" anywhere — not in the "
              "title, bullets or description.").arg(brand);
}

// Strips a trailing size from a generated title and Title-Cases every word.
QString DialogTemuCreateProduct::_finalizeTitle(QString title) const
{
    title = title.trimmed();

    // Every known size value across all variations/countries — so localized and
    // source sizes ("38-42", "42-46", letter sizes) are all removable.
    QStringList knownSizes;
    for (const auto &s : m_draft.skus) {
        if (!s.size.isEmpty()) knownSizes << s.size.trimmed();
        for (const QString &v : s.sizeByCountry.values())
            if (!v.trimmed().isEmpty()) knownSizes << v.trimmed();
    }
    knownSizes.removeDuplicates();

    // A trailing numeric range ("38-42"), and a dangling size keyword in any of
    // the marketplace languages ("taglia unica", "Gr.", "Taille", "Talla", …).
    static const QRegularExpression numTail(
        QStringLiteral("[\\s,;:.·–-]*\\d{1,3}\\s*[-–/]\\s*\\d{1,3}\\s*$"));
    static const QRegularExpression kwTail(
        QStringLiteral("[\\s,;:.·–-]*(?:taille|talla|taglia(?:\\s+unica)?|gr\\.?|"
                       "gr(?:ö|oe)sse|gr(?:ö|oe)ße|einheitsgr(?:ö|oe)sse|"
                       "einheitsgr(?:ö|oe)ße|size|one[\\s-]?size|unica|unique|"
                       "einheitsgr(?:ö|oe)ße|tg\\.?|maat)\\s*$"),
        QRegularExpression::CaseInsensitiveOption);

    // Peel trailing size tokens repeatedly ("… taglia unica 38-42" → "…").
    for (int i = 0; i < 4; ++i) {
        const QString before = title;
        for (const QString &sz : knownSizes) {
            const QRegularExpression szTail(
                QStringLiteral("[\\s,;:.·–-]*%1\\s*$").arg(QRegularExpression::escape(sz)),
                QRegularExpression::CaseInsensitiveOption);
            title.remove(szTail);
        }
        title.remove(numTail);
        title.remove(kwTail);
        title = title.trimmed();
        while (!title.isEmpty() && QStringLiteral(",;:.-–·").contains(title.back()))
            title.chop(1);
        title = title.trimmed();
        if (title == before)
            break;
    }

    // Title-case: capitalize the first letter of every whitespace-separated word.
    bool atStart = true;
    for (int i = 0; i < title.size(); ++i) {
        if (title[i].isSpace()) { atStart = true; continue; }
        if (atStart && title[i].isLetter())
            title[i] = title[i].toUpper();
        atStart = false;
    }
    return title.trimmed();
}

// Drops bullet lines pinning a specific size when the product has several sizes.
QString DialogTemuCreateProduct::_sanitizeBullets(const QString &bullets) const
{
    // Distinct sizes across variations — with 0/1 size, a size mention is fine.
    QSet<QString> sizes;
    for (const auto &s : m_draft.skus)
        if (!s.size.trimmed().isEmpty())
            sizes.insert(s.size.trimmed().toLower());
    if (sizes.size() <= 1)
        return bullets;

    // A size keyword followed by a value ("Taille M", "Größe 40", "size M=40",
    // "taglia 42") pins one variation's size — wrong for all the others.
    static const QRegularExpression kSizeClaim(
        QStringLiteral("\\b(?:taille|talla|taglia|gr(?:ö|oe)?(?:ss|ß)e|size|maat|"
                       "rozmiar|storlek)\\b\\s*[:=]?\\s*[0-9A-Za-z]"),
        QRegularExpression::CaseInsensitiveOption);

    QStringList kept, dropped;
    for (const QString &line : bullets.split(QLatin1Char('\n'))) {
        if (line.trimmed().isEmpty())
            continue;
        if (kSizeClaim.match(line).hasMatch())
            dropped << line.trimmed();
        else
            kept << line;
    }
    if (!dropped.isEmpty() && m_logEdit)
        m_logEdit->appendPlainText(tr("  dropped size-pinned bullet(s): %1")
                                       .arg(dropped.join(QStringLiteral(" | "))));
    return kept.join(QLatin1Char('\n'));
}

QString DialogTemuCreateProduct::_noSizeInBulletsInstruction() const
{
    QSet<QString> sizes;
    for (const auto &s : m_draft.skus)
        if (!s.size.trimmed().isEmpty())
            sizes.insert(s.size.trimmed().toLower());
    if (sizes.size() <= 1)
        return {};
    return tr("\n\nDo NOT mention any specific size or size correspondence in the "
              "bullets or description — this product exists in several sizes and "
              "the text is shared by all of them.");
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

    // Key case-insensitively and SUM duplicate records for the same SKU — a
    // second record must not overwrite the first (that turned a positive
    // Amazon quantity into 0).
    QHash<QString, int> availBySku; // lower-cased SKU → summed available
    for (const StockRecord &rec : records) {
        const QString key = rec.sku.toLower();
        if (rec.available > 0)
            availBySku[key] = qMax(0, availBySku.value(key)) + rec.available;
        else if (!availBySku.contains(key))
            availBySku.insert(key, rec.available);
    }

    for (int r = 0; r < m_skuTable->rowCount(); ++r) {
        const QString sku = m_skuTable->item(r, kColSku)->text();
        if (!availBySku.contains(sku.toLower())) {
            m_skuTable->item(r, kColAmzQty)->setText(QStringLiteral("?"));
            continue;
        }
        const int avail = availBySku.value(sku.toLower());
        m_skuTable->item(r, kColAmzQty)->setText(avail < 0 ? QStringLiteral("?")
                                                           : QString::number(avail));
        // 1 in stock when Amazon has ≥ 2 units, else 0.
        m_skuTable->item(r, kColStock)->setText(avail >= 2 ? QStringLiteral("1")
                                                           : QStringLiteral("0"));
    }
    if (src->lastError().isEmpty())
        m_stockFetched = true;
    m_logEdit->appendPlainText(tr("Stock set: 1 where Amazon ≥ 2 units, else 0 "
                                  "(edit any cell to override)."));
}

// Stock first (one batched call, fast), then prices (one call per SKU, slow) —
// so the quantities are correct even if the user publishes while the price
// fetch is still running.
QCoro::Task<void> DialogTemuCreateProduct::_fetchAmazonData()
{
    co_await _fetchAmazonStock();
    co_await _fetchAmazonPrices();
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

// "Complete" on the per-country tree: fill only what's missing. Sizes convert
// mechanically through the FillerSize country tables from any country that has
// a value; what can't be converted mechanically (colours, textual size labels
// like "Einheitsgröße") is translated by the CLI into each country's language
// in ONE batched call. Hand-edited cells are never overwritten.
QCoro::Task<void> DialogTemuCreateProduct::_completeVariantNames()
{
    if (!m_variantTree || !m_completeVariantsBtn || !m_completeVariantsBtn->isEnabled())
        co_return;
    m_completeVariantsBtn->setEnabled(false);
    auto guard = qScopeGuard([this] { m_completeVariantsBtn->setEnabled(true); });

    // The conversion table comes from the sizing category selected in
    // PaneSizing (women/men × clothing/shoes have different FR/DE/IT offsets —
    // e.g. women FR 40 = DE 38 = IT 44, but men FR 40 = DE 46). Only when no
    // category was recorded do we fall back to guessing from title keywords.
    SizeTable table = m_draft.sizeTable;
    if (table == SizeTable::Unknown) {
        const QString hints = (m_draft.title + QLatin1Char(' ') + m_draft.amazonProductType
                               + QLatin1Char(' ')
                               + (m_catNameLabel ? m_catNameLabel->text() : QString{})).toUpper();
        const bool isShoes = hints.contains(QLatin1String("SHOE"))
                          || hints.contains(QLatin1String("SANDAL"))
                          || hints.contains(QLatin1String("BOOT"))
                          || hints.contains(QLatin1String("SNEAKER"))
                          || hints.contains(QLatin1String("SCHUH"));
        const bool isMale = hints.contains(QLatin1String("HERREN"))
                         || hints.contains(QLatin1String(" MEN"))
                         || hints.contains(QLatin1String("HOMME"))
                         || hints.contains(QLatin1String("UOMO"))
                         || hints.contains(QLatin1String("HOMBRE"));
        table = isShoes ? (isMale ? SizeTable::ShoesMale : SizeTable::ShoesFemale)
                        : (isMale ? SizeTable::ClothingMale : SizeTable::ClothingFemale);
        m_logEdit->appendPlainText(tr("Complete: no sizing category recorded — guessed "
                                      "the %1 %2 size table from the title.")
                                       .arg(isMale ? tr("men's") : tr("women's"),
                                            isShoes ? tr("shoe") : tr("clothing")));
    } else {
        const QString name = table == SizeTable::ShoesFemale ? tr("women's shoes")
                           : table == SizeTable::ShoesMale   ? tr("men's shoes")
                           : table == SizeTable::ClothingMale ? tr("men's clothing")
                                                              : tr("women's clothing");
        m_logEdit->appendPlainText(tr("Complete: converting sizes with the %1 table "
                                      "(from the sizing category).").arg(name));
    }

    // A CLI-translation job for one cell, addressed by indices (not pointers —
    // the tree may change while the CLI runs).
    struct CliItem {
        int row, child, column;
        QString language, value, kind, before;
    };
    QList<CliItem> cliItems;
    int converted = 0;

    for (int r = 0; r < m_variantTree->topLevelItemCount(); ++r) {
        QTreeWidgetItem *top = m_variantTree->topLevelItem(r);

        // --- Sizes: mechanical conversion from any country that has one ---
        QList<QPair<QString, QString>> sizeSources; // (country, value)
        for (int k = 0; k < top->childCount(); ++k) {
            const QString v = top->child(k)->text(2).trimmed();
            if (!v.isEmpty())
                sizeSources.append({top->child(k)->text(0).toUpper(), v});
        }
        for (int k = 0; k < top->childCount(); ++k) {
            QTreeWidgetItem *child = top->child(k);
            if (!child->text(2).trimmed().isEmpty())
                continue;
            const QString cc = child->text(0).toUpper();
            QString filled;
            QString labelSource; // untranslatable word label → CLI fallback
            for (const auto &srcPair : sizeSources) {
                if (srcPair.first == cc)
                    continue;
                filled = convertSizeValue(srcPair.second, srcPair.first, cc, table);
                if (!filled.isEmpty())
                    break;
                if (labelSource.isEmpty() && isTextualSizeLabel(srcPair.second))
                    labelSource = srcPair.second;
            }
            if (!filled.isEmpty()) {
                child->setText(2, filled);
                ++converted;
            } else if (!labelSource.isEmpty()) {
                const QString lang = languageForCountry(cc);
                if (!lang.isEmpty())
                    cliItems.append({r, k, 2, lang, labelSource,
                                     QStringLiteral("size label"), QString{}});
            }
        }

        // --- Colours: translate wherever no localized value exists ---
        const Draft::Sku ds = m_draft.skus.value(r);
        // Best available source wording: the draft colour, else any filled cell.
        QString srcColor = ds.color.trimmed();
        for (int k = 0; k < top->childCount() && srcColor.isEmpty(); ++k)
            srcColor = top->child(k)->text(1).trimmed();
        if (srcColor.isEmpty())
            continue;
        for (int k = 0; k < top->childCount(); ++k) {
            QTreeWidgetItem *child = top->child(k);
            const QString cc  = child->text(0).toUpper();
            const QString cur = child->text(1).trimmed();
            // A cell is localized when Amazon delivered a per-country value or
            // the user typed something different from the generic fallback.
            if (!cur.isEmpty()
                && (cur != ds.color.trimmed() || ds.colorByCountry.contains(cc)))
                continue;
            const QString lang = languageForCountry(cc);
            if (lang.isEmpty())
                continue;
            cliItems.append({r, k, 1, lang, srcColor, QStringLiteral("colour"), QString{}});
        }
    }

    if (converted > 0)
        m_logEdit->appendPlainText(tr("Complete: %1 size cell(s) converted via the "
                                      "size tables.").arg(converted));
    if (cliItems.isEmpty()) {
        if (converted == 0)
            m_logEdit->appendPlainText(tr("Complete: nothing to fill — every cell already "
                                          "has a value (or no source value exists)."));
        co_return;
    }
    if (!m_cli) {
        m_logEdit->appendPlainText(tr("Complete: no CLI selected — %1 colour/label "
                                      "value(s) left untranslated.").arg(cliItems.size()));
        co_return;
    }

    // Snapshot the pre-CLI cell text so an edit made while the CLI runs wins.
    for (CliItem &it : cliItems)
        it.before = m_variantTree->topLevelItem(it.row)->child(it.child)->text(it.column);

    QString prompt = tr(
        "Translate each product variation value below into its target language.\n"
        "Reply with STRICT JSON only: {\"items\":[{\"id\":0,\"value\":\"…\"}]} — one entry "
        "per input id, no other keys, no markdown, no explanations.\n"
        "Each value is a short colour or size name shown on a shopping site: translate "
        "it, capitalize it like a product listing variation, and return it unchanged "
        "when it is already in the target language.\n\nItems:\n");
    for (int i = 0; i < cliItems.size(); ++i)
        prompt += QStringLiteral("id=%1 | %2 | target language: %3 | value: %4\n")
                      .arg(QString::number(i), cliItems.at(i).kind,
                           cliItems.at(i).language, cliItems.at(i).value);

    m_logEdit->appendPlainText(tr("Complete: translating %1 value(s) with %2…")
                                   .arg(cliItems.size()).arg(m_cli->getName()));
    const CliRunResult res = co_await _runCli(prompt);
    QString out = res.output.trimmed();
    const int a = out.indexOf(QLatin1Char('{'));
    const int b = out.lastIndexOf(QLatin1Char('}'));
    if (a >= 0 && b > a)
        out = out.mid(a, b - a + 1);
    const QJsonArray items = QJsonDocument::fromJson(out.toUtf8()).object()
                                 .value(QStringLiteral("items")).toArray();
    if (items.isEmpty()) {
        m_logEdit->appendPlainText(tr("Complete: CLI returned no parseable JSON; "
                                      "cells left unchanged."));
        co_return;
    }
    int applied = 0;
    for (const QJsonValue &v : items) {
        const QJsonObject o = v.toObject();
        const QJsonValue idv = o.value(QStringLiteral("id"));
        bool okId = idv.isDouble();
        const int id = okId ? idv.toInt() : idv.toString().toInt(&okId);
        const QString value = o.value(QStringLiteral("value")).toString().trimmed();
        if (!okId || id < 0 || id >= cliItems.size() || value.isEmpty())
            continue;
        const CliItem &it = cliItems.at(id);
        if (it.row >= m_variantTree->topLevelItemCount())
            continue;
        QTreeWidgetItem *top = m_variantTree->topLevelItem(it.row);
        if (it.child >= top->childCount())
            continue;
        QTreeWidgetItem *cell = top->child(it.child);
        if (cell->text(it.column) != it.before)
            continue; // edited while the CLI ran — the user's value wins
        cell->setText(it.column, value);
        ++applied;
    }
    m_logEdit->appendPlainText(tr("Complete: %1 value(s) translated and filled.").arg(applied));
}

QCoro::Task<CliRunResult> DialogTemuCreateProduct::_runCli(const QString &prompt,
                                                           const QString &workingDir)
{
    QPromise<CliRunResult> promise;
    promise.start();
    QFuture<CliRunResult> future = promise.future();
    {
        auto sp = QSharedPointer<QPromise<CliRunResult>>::create(std::move(promise));
        auto cb = [sp](CliRunResult r) mutable {
            sp->addResult(std::move(r));
            sp->finish();
        };
        if (workingDir.isEmpty())
            m_cli->runPromptAsync(prompt, this, cb);
        else
            m_cli->runPromptAsync(prompt, workingDir, this, cb);
    }
    co_return co_await qCoro(future).result();
}

QCoro::Task<void> DialogTemuCreateProduct::_generateText()
{
    if (!m_cli) {
        QMessageBox::warning(this, tr("Generate text"), tr("No CLI selected."));
        co_return;
    }
    // Capture the country now; the result is filed against it even if the user
    // switches language while the CLI runs.
    const QString target = _textCountry();
    _setTextBusy(true);
    auto busyGuard = qScopeGuard([this] { _setTextBusy(false); });
    const QString prompt = tr(
        "You write Temu product listings. From the data below, produce STRICT JSON "
        "{\"title\":\"…\",\"bullets\":[\"…\"],\"description\":\"…\"} — a concise selling "
        "title (max 100 chars), 3-5 bullet points (each bullet MUST start with exactly one "
        "relevant emoji, then a space), and a short description. No markdown.\n\n"
        "Product: %1\nBrand: %2\nExisting bullets:\n%3")
        .arg(m_draft.title, m_draft.brand,
             _sanitizeBullets(m_draft.bulletPoints.join(QLatin1Char('\n'))))
        + _titleKeywordInstruction() + _variationInstruction() + _titleGuidance()
        + _noBrandInstruction() + _noSizeInBulletsInstruction()
        + _languageInstruction(tr("the title, bullets and description"));

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
        _applyTextResult(target, 0, _finalizeTitle(o.value(QStringLiteral("title")).toString()));
    if (o.contains(QStringLiteral("bullets"))) {
        QStringList bl;
        for (const QJsonValue &v : o.value(QStringLiteral("bullets")).toArray())
            bl << v.toString();
        _applyTextResult(target, 1, _sanitizeBullets(bl.join(QLatin1Char('\n'))));
    }
    if (o.contains(QStringLiteral("description")))
        _applyTextResult(target, 2, o.value(QStringLiteral("description")).toString());
    m_logEdit->appendPlainText(tr("Text generated."));
}

QCoro::Task<void> DialogTemuCreateProduct::_regenerateField(int which)
{
    if (!m_cli) {
        QMessageBox::warning(this, tr("Regenerate"), tr("No CLI selected."));
        co_return;
    }
    // File the result against the country shown now, even if the language is
    // switched while the CLI runs.
    const QString target = _textCountry();
    _setTextBusy(true);
    auto busyGuard = qScopeGuard([this] { _setTextBusy(false); });
    const QString field = which == 0 ? QStringLiteral("title")
                        : which == 1 ? QStringLiteral("bullets")
                                     : QStringLiteral("description");
    // For bullets, feed the CLI only SANITIZED context: a size-pinned line in
    // the current/source bullets would be dutifully carried back into the
    // rewrite (e.g. "📏 Taille M correspondant au 40").
    const QString current = which == 0 ? m_titleEdit->text()
                          : which == 1 ? _sanitizeBullets(m_bulletsEdit->toPlainText())
                                       : m_descEdit->toPlainText();
    // Only bullets need structured (JSON) output. Title and description are
    // single free-text fields — asking for JSON there is fragile (long text with
    // newlines/quotes routinely produces invalid JSON), so we request plain text
    // and accept it directly, while still tolerating a JSON reply.
    const QString shape = which == 1
        ? QStringLiteral("STRICT JSON {\"bullets\":[\"…\"]} (3-5 concise selling bullet points, "
                         "each starting with exactly one relevant emoji followed by a space), no markdown")
        : which == 0 ? QStringLiteral("plain text only — a concise selling title, max 100 chars, no quotes, no markdown")
                     : QStringLiteral("plain text only — a short compelling description, no markdown, no JSON");

    const QString prompt = tr(
        "You improve Temu product copy. Produce a BETTER %1 as %2.\n\n"
        "Product: %3\nBrand: %4\nSource bullets:\n%5\n\nCurrent %1 (improve on it):\n%6")
        .arg(field, shape, m_draft.title, m_draft.brand,
             _sanitizeBullets(m_draft.bulletPoints.join(QLatin1Char('\n'))), current)
        + (which == 0 ? _titleKeywordInstruction() + _variationInstruction() + _titleGuidance()
                      : QString{})
        + _noBrandInstruction()
        + (which != 0 ? _noSizeInBulletsInstruction() : QString{})
        + _languageInstruction(field);

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
        // Backstop: drop any size-pinned bullet the model reintroduced anyway.
        const QString cleaned = _sanitizeBullets(bl.join(QLatin1Char('\n')));
        if (cleaned.trimmed().isEmpty()) {
            m_logEdit->appendPlainText(tr("  all bullets were size-pinned; unchanged."));
            co_return;
        }
        _applyTextResult(target, 1, cleaned);
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
        _applyTextResult(target, which, which == 0 ? _finalizeTitle(text) : text);
    }
    m_logEdit->appendPlainText(tr("  %1 updated.").arg(field));
}

// Regenerates the title, bullets and description for every selected country,
// each in its own language. Loading a country swaps the editors (and updates
// _storeLanguage()), so each field is regenerated in the right language; the
// freshly-written editors are persisted back into m_pageText per country.
QCoro::Task<void> DialogTemuCreateProduct::_regenerateAllText()
{
    if (!m_cli) {
        QMessageBox::warning(this, tr("Regenerate all"), tr("No CLI selected."));
        co_return;
    }
    QStringList countries;
    for (int i = 0; i < m_textCountryList->count(); ++i)
        countries << m_textCountryList->item(i)->text();
    if (countries.isEmpty() && !m_curTextCountry.isEmpty())
        countries << m_curTextCountry;
    if (countries.isEmpty()) {
        m_logEdit->appendPlainText(tr("No countries to regenerate."));
        co_return;
    }

    _setTextBusy(true);
    auto busyGuard = qScopeGuard([this] { _setTextBusy(false); });

    m_logEdit->appendPlainText(tr("Regenerating all text for %1 language(s)…")
                                   .arg(countries.size()));
    for (const QString &cc : countries) {
        // Show this country (persists the previous one's editors and switches
        // the language used by _storeLanguage()). Each _regenerateField files
        // its result into m_pageText[cc] via the captured country, so no manual
        // persist is needed here.
        _loadCountryText(cc);
        co_await _regenerateField(0);
        co_await _regenerateField(1);
        co_await _regenerateField(2);
    }
    m_logEdit->appendPlainText(tr("All text regenerated."));
}

// Regenerates a single field for every selected country, each in its language.
QCoro::Task<void> DialogTemuCreateProduct::_regenerateFieldAllLangs(int which)
{
    if (!m_cli) {
        QMessageBox::warning(this, tr("Regenerate all languages"), tr("No CLI selected."));
        co_return;
    }
    QStringList countries;
    for (int i = 0; i < m_textCountryList->count(); ++i)
        countries << m_textCountryList->item(i)->text();
    if (countries.isEmpty() && !m_curTextCountry.isEmpty())
        countries << m_curTextCountry;
    if (countries.isEmpty()) {
        m_logEdit->appendPlainText(tr("No countries to regenerate."));
        co_return;
    }

    _setTextBusy(true);
    auto busyGuard = qScopeGuard([this] { _setTextBusy(false); });

    const QString fieldName = which == 0 ? tr("title")
                            : which == 1 ? tr("bullets")
                                         : tr("description");
    m_logEdit->appendPlainText(tr("Regenerating %1 for %2 language(s)…")
                                   .arg(fieldName).arg(countries.size()));
    for (const QString &cc : countries) {
        _loadCountryText(cc);
        co_await _regenerateField(which);
    }
    m_logEdit->appendPlainText(tr("%1 regenerated for all languages.").arg(fieldName));
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
    co_await _loadCategoryTemplate(); // load attributes for the chosen category
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
    co_await _loadCategoryTemplate();
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
        co_await _loadCategoryTemplate();
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

    // The stock fetch runs asynchronously at dialog-open; publishing before it
    // finished (or after it failed) would create the page with the default
    // quantity 0 even though Amazon has stock. Fetch it now when needed.
    if (!m_stockFetched) {
        m_logEdit->appendPlainText(tr("Amazon stock not fetched yet — fetching before publish…"));
        co_await _fetchAmazonStock();
    }
    // Rule: quantity is at least 1 whenever Amazon has more than 1 unit. A 0
    // there means the fetch raced or a stale default — fix it, don't ship it.
    for (int r = 0; r < m_skuTable->rowCount(); ++r) {
        const int amz = m_skuTable->item(r, kColAmzQty)->text().trimmed().toInt();
        const int qty = m_skuTable->item(r, kColStock)->text().trimmed().toInt();
        if (qty <= 0 && amz > 1) {
            m_skuTable->item(r, kColStock)->setText(QStringLiteral("1"));
            m_logEdit->appendPlainText(tr("  %1: quantity 0 → 1 (Amazon has %2)")
                .arg(m_skuTable->item(r, kColSku)->text()).arg(amz));
        }
    }

    // Warn if the listing text was never (re)generated with AI — the original
    // Amazon/source wording would be uploaded as-is.
    if (!m_textRegenerated) {
        if (QMessageBox::question(this, tr("Content not regenerated"),
                tr("You haven't regenerated the listing content (title / bullets / "
                   "description) with AI.\n\nUpload the current (original) content anyway?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            co_return;
    }

    // Persist the manual setup (images + attributes + text) up front, so if the
    // upload fails the work is preserved when the dialog is re-opened.
    _saveWorkState();

    // When several stores are selected, ask whether stores that already have the
    // product should be updated or skipped (create-only where missing). With a
    // single store we always update — no need to ask.
    bool skipExisting = false;
    if (m_stores.size() > 1) {
        QMessageBox box(this);
        box.setWindowTitle(tr("Existing products"));
        box.setText(tr("Publishing to %1 stores.\n\nFor stores that already have "
                       "this product, update the existing page or skip it "
                       "(only create where missing)?").arg(m_stores.size()));
        QPushButton *updateBtn = box.addButton(tr("Update existing"), QMessageBox::AcceptRole);
        QPushButton *skipBtn   = box.addButton(tr("Skip existing"), QMessageBox::ActionRole);
        QPushButton *cancelBtn = box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(updateBtn);
        box.exec();
        if (box.clickedButton() == cancelBtn)
            co_return;
        skipExisting = (box.clickedButton() == skipBtn);
    }

    m_publishBtn->setEnabled(false);
    auto pubGuard = qScopeGuard([this] { m_publishBtn->setEnabled(true); });

    // --- Host checked images to public URLs (Temu V3 fetches them itself;
    //     no CDN upload needed). Cache the public URL per image. Each colour
    //     node carries its own ordered image list (common images duplicated in);
    //     the "" node applies to every SKU; the "##product" node is the size
    //     chart (goods detailImage). ---
    m_logEdit->appendPlainText(tr("Preparing images…"));
    QHash<QString, QStringList> colorNodeUrls; // node role (colour / "") → URLs
    QString sizeChartTemuUrl;
    int galleryCount = 0;
    for (int t = 0; t < m_imageTree->topLevelItemCount(); ++t) {
        QTreeWidgetItem *node = m_imageTree->topLevelItem(t);
        const QString role = node->data(0, Qt::UserRole).toString();
        for (int c = 0; c < node->childCount(); ++c) {
            QTreeWidgetItem *it = node->child(c);
            if (it->checkState(0) != Qt::Checked)
                continue;
            const QString local = it->data(0, Qt::UserRole).toString();
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

            if (role == QStringLiteral("##product") || local == m_draft.sizeChartImagePath) {
                sizeChartTemuUrl = url;
                continue;
            }
            colorNodeUrls[role] << url;
            ++galleryCount;
        }
    }
    if (galleryCount == 0) {
        m_logEdit->appendPlainText(tr("No images available — aborting."));
        co_return;
    }
    // First non-empty gallery list, used as a fallback / for the carousel.
    QStringList anyGalleryUrls;
    for (auto it = colorNodeUrls.constBegin(); it != colorNodeUrls.constEnd(); ++it)
        if (!it.value().isEmpty()) { anyGalleryUrls = it.value(); break; }

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

    // --- Publish to EVERY selected store, each in its own language / localized
    //     colours / compliance mapping. The images, attributes, prices and
    //     category above are store-independent and reused; only the text
    //     (per country) and the create-vs-update decision differ per store. ---
    QStringList summary;
    QSet<QString> publishedCountries; // countries that end up with a Temu page
    const int originalStoreIndex = m_storeCombo->currentIndex();
    for (int si = 0; si < m_stores.size(); ++si) {
        const StorePick &store = m_stores.at(si);
        { QSignalBlocker b(m_storeCombo); m_storeCombo->setCurrentIndex(si); }
        // Switch context WITHOUT rebuilding the attribute form (that would wipe
        // the values just filled): load this country's text, rebuild the API for
        // this store, and look up whether the product already exists here.
        _loadCountryText(store.country.toUpper());
        delete m_api;
        m_api = new TemuInventoryApi(m_appKey, m_appSecret, store.token,
                                     store.proxyHost, store.proxyPort,
                                     store.proxyUser, store.proxyPassword, this);
        m_logEdit->appendPlainText(tr("── %1 · %2 ──").arg(store.country, store.label));
        QStringList sns;
        for (const auto &sku : m_draft.skus)
            if (!sku.outSkuSn.isEmpty()) sns << sku.outSkuSn;
        co_await m_api->lookupGoods(sns, &m_existing);
        if (!m_api->lastError().isEmpty()) {
            m_logEdit->appendPlainText(tr("  lookup failed: %1").arg(m_api->lastError()));
            summary << tr("%1: lookup FAILED (%2)").arg(store.label, m_api->lastError());
            continue;
        }

    QJsonArray skuArr;
    QStringList carouselUrls;
    for (int r = 0; r < m_skuTable->rowCount(); ++r) {
        const double base = m_skuTable->item(r, kColBase)->text().trimmed().toDouble();
        double ref = m_skuTable->item(r, kColRef)->text().trimmed().toDouble();
        if (ref <= 0) ref = base * 1.20;

        const DialogTemuCreateProduct::Draft::Sku &ds = m_draft.skus.value(r);
        // Colour/size for THIS store's country, from the per-country tree (which
        // holds the localized, possibly hand-edited names). Colour falls back to
        // the source, but SIZE must NOT: publishing another country's size is
        // wrong, so an unretrieved size stays empty and no Size variation is sent.
        const QString cc = _currentStore().country.toUpper();
        QString varColor = ds.colorByCountry.value(cc, ds.color);
        QString varSize  = ds.sizeByCountry.value(cc);
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

        // This SKU's images = its colour node's checked images (which already
        // include the common images in the order the user chose). Falls back to
        // the "" all-node, then to any gallery, so a SKU never ships empty.
        QStringList skuUrls = colorNodeUrls.contains(ds.color)
                                  ? colorNodeUrls.value(ds.color)
                                  : colorNodeUrls.value(QString{});
        if (skuUrls.isEmpty())
            skuUrls = anyGalleryUrls;
        if (r == 0)
            carouselUrls = skuUrls; // the first variation represents the goods gallery
        QJsonArray imgs;
        for (const QString &u : skuUrls) imgs.append(u);

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

        // --- goodsBasic (this store's language) ---
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
        for (const QString &u : (carouselUrls.isEmpty() ? anyGalleryUrls : carouselUrls))
            carousel.append(u);
        goodsBasic.insert(QStringLiteral("goodsCarouselImage"), carousel);
        if (!sizeChartTemuUrl.isEmpty())
            goodsBasic.insert(QStringLiteral("detailImage"), QJsonArray{sizeChartTemuUrl});

        // --- UPDATE path: the product already exists on this store ---
        if (m_existing.found && m_existing.goodsId != 0) {
            publishedCountries.insert(store.country.toUpper()); // it has a page here
            if (skipExisting) {
                m_logEdit->appendPlainText(tr("  already exists — skipped."));
                summary << tr("%1: skipped (already exists, goodsId %2)")
                               .arg(store.label).arg(m_existing.goodsId);
                continue;
            }
            // partial.update edits text + images (prices/stock have dedicated
            // flows; it refuses edits until Temu finishes processing the product).
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

            m_logEdit->appendPlainText(tr("  updating goodsId %1…").arg(m_existing.goodsId));
            const bool ok = co_await m_api->updateGoodsPartial(m_existing.goodsId, upFields);
            if (!ok) {
                const QString err = m_api->lastError();
                m_logEdit->appendPlainText(tr("  update failed: %1").arg(err));
                if (err.contains(QStringLiteral("150010205")))
                    m_logEdit->appendPlainText(tr("  (Temu still processing — wait ~10 min then retry.)"));
                summary << tr("%1: update FAILED (%2)").arg(store.label, err);
                continue;
            }
            co_await _submitCompliance(m_existing.goodsId);
            m_logEdit->appendPlainText(tr("  updated goodsId %1.").arg(m_existing.goodsId));
            summary << tr("%1: updated (goodsId %2)").arg(store.label).arg(m_existing.goodsId);
            continue;
        }

        // --- CREATE path ---
        QJsonObject payload;
        payload.insert(QStringLiteral("goodsBasic"), goodsBasic);
        if (!attrsArr.isEmpty())
            payload.insert(QStringLiteral("attributes"), attrsArr);
        payload.insert(QStringLiteral("skuList"), skuArr);

        m_logEdit->appendPlainText(tr("  creating product (V3)…"));
        qDebug().noquote() << "Temu V3 publish payload (" << store.country << "):"
                           << QJsonDocument(payload).toJson(QJsonDocument::Compact);

        const qint64 id = co_await m_api->publishGoodsV3(payload);
        if (id == 0) {
            const QString err = m_api->lastError();
            m_logEdit->appendPlainText(tr("  FAILED: %1").arg(err));
            if (err.contains(QStringLiteral("150010090"))) // SKU duplicated
                m_logEdit->appendPlainText(tr("  This SKU already exists on Temu — created earlier. "
                    "Wait ~10 min, then re-open: it will switch to Update mode."));
            summary << tr("%1: create FAILED (%2)").arg(store.label, err);
            continue;
        }

        // Submit GPSR compliance (manufacturer + EU responsible person). Product
        // Identification / country of origin still need the Seller Center.
        const bool complianceOk = co_await _submitCompliance(id);
        publishedCountries.insert(store.country.toUpper());
        m_logEdit->appendPlainText(tr("  OK — goodsId %1.").arg(id));
        summary << tr("%1: created (goodsId %2)%3").arg(store.label).arg(id)
                       .arg(complianceOk ? QString{} : tr(" — compliance needs manual entry"));
    } // end per-store loop

    // Restore the originally-selected store in the dropdown and its text.
    { QSignalBlocker b(m_storeCombo); m_storeCombo->setCurrentIndex(qMax(0, originalStoreIndex)); }
    if (!m_stores.isEmpty())
        _loadCountryText(m_stores.at(qMax(0, originalStoreIndex)).country.toUpper());

    // Record where this product now has a Temu page (union with any previous
    // record — pages aren't deleted here), so the "Load Sub Folder" list can show
    // the published countries. Stored sorted for a stable "DE FR IT" display.
    if (!m_draft.productDir.isEmpty() && !publishedCountries.isEmpty()) {
        QSettings ps(QDir(m_draft.productDir).filePath(QStringLiteral("settings.ini")),
                     QSettings::IniFormat);
        const QStringList prev = ps.value(QStringLiteral("temu/publishedCountries")).toStringList();
        for (const QString &cc : prev)
            publishedCountries.insert(cc.toUpper());
        QStringList list(publishedCountries.constBegin(), publishedCountries.constEnd());
        list.sort();
        ps.setValue(QStringLiteral("temu/publishedCountries"), list);
    }

    QMessageBox::information(this, tr("Create / Update"),
        tr("Publish results:\n\n%1\n\nTemu is enriching new products (category, attributes…) — "
           "check status in ~10 min.").arg(summary.join(QLatin1Char('\n'))));
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
