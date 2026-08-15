#pragma GCC optimize("O1")
#include "PaneStore.h"
#include "ui_PaneStore.h"
#include "SettingsTable.h"
#include "TableStoreAsin.h"
#include "TreeBrandCategories.h"
#include "AmazonMarketplace.h"
#include "AbstractCli.h"
#include "AbstractInventorySource.h"
#include "AbstractInventorySourceFactory.h"
#include "MarketplaceTypes.h"

#include "../../common/workingdirectory/WorkingDirectoryManager.h"
#include <QScopeGuard>

#include <algorithm>
#include <climits>

#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QMessageBox>
#include <QRadioButton>
#include <QRegularExpression>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QUrl>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QTextEdit>
#include <QInputDialog>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>

#include <QCoro/QCoroNetworkReply>
#include <QCoro/QCoroSignal>
#include <QCoro/QCoroTimer>

PaneStore::PaneStore(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PaneStore)
{
    ui->setupUi(this);
    ui->splitterMain->setStretchFactor(0, 1);
    ui->splitterMain->setStretchFactor(1, 3);

    // Left: brand/category tree
    m_treeModel = new TreeBrandCategories(this);
    ui->treeViewBrandCategory->setModel(m_treeModel);
    ui->treeViewBrandCategory->header()->setStretchLastSection(true);
    ui->treeViewBrandCategory->setRootIsDecorated(true);
    ui->treeViewBrandCategory->setAlternatingRowColors(true);
    ui->treeViewBrandCategory->setSelectionMode(QAbstractItemView::ExtendedSelection);
    // Persist English-name edits (double-click the "English name" column) as
    // soon as they're made — setData() is the only thing that ever emits
    // dataChanged() on this model, so no filtering by column is needed here.
    connect(m_treeModel, &QAbstractItemModel::dataChanged, this, [this]() {
        _saveEnglishCategoryNames();
    });

    // Right top: horizontal countries chip-bar
    m_countriesModel = new QStandardItemModel(this);
    ui->listViewCountries->setModel(m_countriesModel);
    ui->listViewCountries->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->listViewCountries->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->listViewCountries->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui->listViewCountries->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui->listViewCountries->installEventFilter(this);

    // Right bottom: ASIN table (manual order, no column-click sort)
    m_storeModel = new TableStoreAsin(this);
    ui->tableViewAsins->setModel(m_storeModel);
    ui->tableViewAsins->horizontalHeader()->setStretchLastSection(true);
    ui->tableViewAsins->verticalHeader()->hide();
    ui->tableViewAsins->setSelectionBehavior(QAbstractItemView::SelectRows);
    // Only the ASIN column is flagged Qt::ItemIsEditable (see TableStoreAsin::flags),
    // so this only opens a text editor there — double-click to select/copy the ASIN.
    ui->tableViewAsins->setEditTriggers(QAbstractItemView::DoubleClicked);
    ui->tableViewAsins->setAlternatingRowColors(true);
    ui->tableViewAsins->setSortingEnabled(false);
    ui->tableViewAsins->verticalHeader()->setDefaultSectionSize(58);

    // Countries chip-bar depends on m_storeModel (sets existence columns).
    _populateCountriesList();

    connect(ui->buttonRetrieve, &QPushButton::clicked, this, [this]() {
        m_retrieveTask = _onRetrieve();
    });
    connect(ui->buttonRefreshSalesStock, &QPushButton::clicked, this, [this]() {
        m_stockRefreshTask = _onRefreshSalesStock();
    });
    connect(ui->buttonSortBySales, &QPushButton::clicked, this, &PaneStore::_onSortBySales);
    connect(ui->buttonOpenStorefrontFolder, &QPushButton::clicked, this, [this]() {
        m_workingDir.mkpath(QStringLiteral("stores/storefront"));
        QDesktopServices::openUrl(QUrl::fromLocalFile(
            m_workingDir.filePath(QStringLiteral("stores/storefront"))));
    });
    connect(ui->buttonExportFolder, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(
            this, tr("Select export folder"), ui->lineEditExportFolder->text());
        if (dir.isEmpty()) return;
        ui->lineEditExportFolder->setText(dir);
        WorkingDirectoryManager::instance()->settings()->setValue(
            QStringLiteral("store/exportFolder"), dir);
    });
    connect(ui->buttonExportProducts, &QPushButton::clicked, this, &PaneStore::_onExportProducts);
    connect(ui->buttonMerge, &QPushButton::clicked, this, &PaneStore::_onMerge);
    connect(ui->buttonCopyAsins,      &QPushButton::clicked, this, &PaneStore::_onCopyAsins);
    connect(ui->buttonMoveToTop,  &QPushButton::clicked, this, &PaneStore::_onMoveToTop);
    connect(ui->buttonMoveUp,     &QPushButton::clicked, this, &PaneStore::_onMoveUp);
    connect(ui->buttonMoveDown,   &QPushButton::clicked, this, &PaneStore::_onMoveDown);
    connect(ui->buttonMoveToBottom, &QPushButton::clicked, this, &PaneStore::_onMoveToBottom);

    connect(ui->buttonGenStorefrontImage, &QPushButton::clicked,
            this, &PaneStore::_onGenStorefrontImage);
    connect(ui->buttonMoveProducts, &QPushButton::clicked,
            this, &PaneStore::_onMoveProducts);
    connect(ui->buttonRemoveProducts, &QPushButton::clicked,
            this, &PaneStore::_onRemoveProducts);
    connect(ui->buttonAddCategory, &QPushButton::clicked,
            this, &PaneStore::_onAddCategory);
    connect(ui->buttonRemoveCategory, &QPushButton::clicked,
            this, &PaneStore::_onRemoveCategory);
    connect(ui->listVersionStrip, &QListWidget::currentRowChanged, this, [this](int row) {
        ui->buttonDeleteVersion->setEnabled(row >= 0);
        ui->buttonCopyVersionPath->setEnabled(row >= 0);
        if (row >= 0) {
            const QString path =
                ui->listVersionStrip->item(row)->data(Qt::UserRole).toString();
            _showStorefrontImage(path);
        }
    });
    connect(ui->buttonDeleteVersion, &QPushButton::clicked, this, [this]() {
        _deleteSelectedVersion();
    });
    connect(ui->buttonCopyVersionPath, &QPushButton::clicked, this, [this]() {
        const int row = ui->listVersionStrip->currentRow();
        if (row < 0) return;
        const QString path = ui->listVersionStrip->item(row)->data(Qt::UserRole).toString();
        if (!path.isEmpty())
            QGuiApplication::clipboard()->setText(path);
    });

    // Enable Up/Down only when a table row is selected
    connect(ui->tableViewAsins->selectionModel(),
            &QItemSelectionModel::selectionChanged,
            this, [this]() {
                const bool hasSel = !_selectedTableRows().isEmpty();
                ui->buttonMoveToTop->setEnabled(hasSel);
                ui->buttonMoveUp->setEnabled(hasSel);
                ui->buttonMoveDown->setEnabled(hasSel);
                ui->buttonMoveToBottom->setEnabled(hasSel);
                ui->buttonMoveProducts->setEnabled(hasSel);
                ui->buttonRemoveProducts->setEnabled(hasSel);
                ui->buttonExportProducts->setEnabled(hasSel);
            });

    connect(ui->treeViewBrandCategory->selectionModel(),
            &QItemSelectionModel::currentChanged,
            this, [this](const QModelIndex &, const QModelIndex &) {
                _onTreeSelectionChanged();
                ui->buttonAddCategory->setEnabled(
                    ui->treeViewBrandCategory->currentIndex().isValid());
                ui->buttonRemoveCategory->setEnabled(_isCurrentNodeCustom());
            });
    connect(ui->treeViewBrandCategory->selectionModel(),
            &QItemSelectionModel::selectionChanged,
            this, [this]() {
                const QModelIndexList sel =
                    ui->treeViewBrandCategory->selectionModel()->selectedIndexes();
                ui->buttonMerge->setEnabled(
                    sel.size() == 2
                    && sel.at(0).parent() == sel.at(1).parent());
            });
    connect(ui->listViewCountries->selectionModel(),
            &QItemSelectionModel::currentChanged,
            this, [this](const QModelIndex &, const QModelIndex &) {
                _onCountrySelectionChanged();
            });
}

PaneStore::~PaneStore()
{
    delete ui;
}

void PaneStore::setWorkingDir(const QDir &workingDir)
{
    m_workingDir = workingDir;
    m_workingDir.mkpath(QStringLiteral("stores"));
    _loadCustomPaths();
    _loadStockCache();
    // Must run before _loadFromDisk(), which triggers the first
    // m_treeModel->setItems() — that's when nodes pick up their English name.
    _loadEnglishCategoryNames();
    _loadFromDisk(_marketplaceId());
    _loadStorefrontVersions();

    ui->lineEditExportFolder->setText(
        WorkingDirectoryManager::instance()->settings()->value(
            QStringLiteral("store/exportFolder")).toString());
}

void PaneStore::setAvailableClis(const QList<AbstractCli *> &clis)
{
    m_availableClis = clis;

    ui->comboBoxGenCli->blockSignals(true);
    ui->comboBoxGenCli->clear();
    for (AbstractCli *cli : clis) {
        if (cli->canGenImages())
            ui->comboBoxGenCli->addItem(cli->getName(), QVariant::fromValue(cli));
    }

    int defaultIndex = 0;
    // (no filtering needed since we only added canGenImages ones)

    const QString saved = QSettings().value(QStringLiteral("store/selectedCli")).toString();
    int restoredIndex = -1;
    for (int i = 0; i < ui->comboBoxGenCli->count(); ++i) {
        if (ui->comboBoxGenCli->itemText(i) == saved) { restoredIndex = i; break; }
    }
    ui->comboBoxGenCli->setCurrentIndex(restoredIndex >= 0 ? restoredIndex : defaultIndex);
    ui->comboBoxGenCli->blockSignals(false);

    connect(ui->comboBoxGenCli, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        if (index < 0 || index >= ui->comboBoxGenCli->count()) return;
        QSettings().setValue(QStringLiteral("store/selectedCli"),
                             ui->comboBoxGenCli->itemText(index));
    });
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

AmazonCatalogApi *PaneStore::_catalogApi()
{
    if (!m_catalogApi) {
        auto *st = SettingsTable::instance();
        m_catalogApi = new AmazonCatalogApi(
            st->value(SettingsTable::KEY_LWA_CLIENT_ID),
            st->value(SettingsTable::KEY_LWA_CLIENT_SECRET),
            st->value(SettingsTable::KEY_EU_LWA_REFRESH_TOKEN),
            st->value(SettingsTable::KEY_NA_LWA_REFRESH_TOKEN),
            st->value(SettingsTable::KEY_JP_LWA_REFRESH_TOKEN),
            st->value(SettingsTable::KEY_EU_SELLER_ID),
            st->value(SettingsTable::KEY_NA_SELLER_ID),
            st->value(SettingsTable::KEY_JP_SELLER_ID),
            st->value(SettingsTable::KEY_IMGBB_API_KEY),
            this);
    }
    return m_catalogApi;
}

QString PaneStore::_marketplaceId() const
{
    return QSettings().value(QStringLiteral("store/marketplaceId"),
                             QStringLiteral("A1PA6795UKMFR9")).toString();
}

void PaneStore::_loadOrder()
{
    const QString path = m_workingDir.filePath(
        QStringLiteral("stores/%1_order.json").arg(_marketplaceId()));
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    m_savedOrder.clear();
    for (const QJsonValue &v : QJsonDocument::fromJson(f.readAll()).array()) {
        QString entry = v.toString();
        // Migrate legacy entries (a representative ASIN) to the stable group
        // key. Group keys always contain the \x1f separator; a bare ASIN
        // never does. Old entries are converted on first load so upgrading
        // doesn't reset everyone's custom order — only entries for ASINs no
        // longer in the catalog are left as unmatched (harmless) leftovers.
        if (!entry.contains(QLatin1Char('\x1f'))) {
            const AmazonCatalogApi::StoreItem &it = m_asinToItem.value(entry);
            if (!it.asin.isEmpty())
                entry = TreeBrandCategories::colorGroupKey(it);
        }
        m_savedOrder.append(entry);
    }
}

void PaneStore::_saveOrder()
{
    QJsonArray arr;
    for (const QString &asin : std::as_const(m_savedOrder))
        arr.append(asin);
    const QString path = m_workingDir.filePath(
        QStringLiteral("stores/%1_order.json").arg(_marketplaceId()));
    QSaveFile sf(path);
    if (sf.open(QIODevice::WriteOnly)) {
        sf.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
        sf.commit();
    }
}

void PaneStore::_populateCountriesList()
{
    m_countriesModel->clear();
    for (const AmazonMarketplace &mp : AmazonMarketplace::all()) {
        if (_catalogApi()->sellerIdForMarketplace(mp.marketplaceId()).isEmpty()) continue;
        auto *item = new QStandardItem(
            QStringLiteral("%1 (%2)").arg(mp.countryName(), mp.countryCode()));
        item->setData(mp.marketplaceId(), Qt::UserRole);
        item->setEditable(false);
        m_countriesModel->appendRow(item);
    }

    // Build ordered mp IDs and labels for the table's sales columns
    QStringList mpIds, mpLabels;
    for (int i = 0; i < m_countriesModel->rowCount(); ++i) {
        const QModelIndex idx = m_countriesModel->index(i, 0);
        mpIds   << idx.data(Qt::UserRole).toString();
        mpLabels << AmazonMarketplace::forMarketplaceId(mpIds.last())->countryCode();
    }
    m_storeModel->setMarketplaces(mpIds, mpLabels);

    _adjustCountriesHeight();
}

QStringList PaneStore::_configuredLanguageLabels()
{
    // Same "has a seller ID configured" filter _populateCountriesList() uses
    // to build the table's per-marketplace existence columns — kept as a
    // separate small pass rather than reading m_countriesModel back out,
    // since that model's row order isn't guaranteed to match countryCode().
    // Grouped by language code so e.g. GB/US/CA/IE (all English) produce one
    // "EN (GB, US, CA, IE)" label instead of four identical translations.
    QStringList langOrder;
    QHash<QString, QStringList> countriesByLang;
    for (const AmazonMarketplace &mp : AmazonMarketplace::all()) {
        if (_catalogApi()->sellerIdForMarketplace(mp.marketplaceId()).isEmpty()) continue;
        const QString code = mp.languageCode();
        if (!countriesByLang.contains(code)) langOrder << code;
        countriesByLang[code] << mp.countryCode();
    }

    QStringList labels;
    for (const QString &code : std::as_const(langOrder))
        labels << QStringLiteral("%1 (%2)").arg(code, countriesByLang.value(code).join(QStringLiteral(", ")));
    return labels;
}

void PaneStore::_adjustCountriesHeight()
{
    // Deferred: measure after the list view has completed its layout pass.
    QTimer::singleShot(0, this, [this]() {
        QListView *lv = ui->listViewCountries;
        const int n = m_countriesModel->rowCount();
        if (n == 0) {
            lv->setFixedHeight(0);
            return;
        }
        // Use the bottom edge of the last item's visual rect as the needed height.
        const QModelIndex last = m_countriesModel->index(n - 1, 0);
        const QRect r = lv->visualRect(last);
        if (r.isValid())
            lv->setFixedHeight(r.bottom() + lv->frameWidth() + 2);
    });
}

bool PaneStore::eventFilter(QObject *obj, QEvent *event)
{
    // Re-compute height whenever the list view is resized (splitter drag changes
    // its width, which changes how items wrap and thus how many rows are needed).
    if (obj == ui->listViewCountries && event->type() == QEvent::Resize)
        _adjustCountriesHeight();
    return QWidget::eventFilter(obj, event);
}

void PaneStore::_loadFromDisk(const QString &marketplaceId)
{
    const QString path = m_workingDir.filePath(
        QStringLiteral("stores/%1.json").arg(marketplaceId));
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;

    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    QList<AmazonCatalogApi::StoreItem> items;
    items.reserve(arr.size());
    for (const QJsonValue &v : arr) {
        const QJsonObject obj = v.toObject();
        AmazonCatalogApi::StoreItem item;
        item.asin      = obj.value(QStringLiteral("asin")).toString();
        item.sku       = obj.value(QStringLiteral("sku")).toString();
        item.title     = obj.value(QStringLiteral("title")).toString();
        item.brand     = obj.value(QStringLiteral("brand")).toString();
        item.category  = obj.value(QStringLiteral("category")).toString();
        item.gender    = obj.value(QStringLiteral("gender")).toString();
        item.age       = obj.value(QStringLiteral("age")).toString();
        item.color     = obj.value(QStringLiteral("color")).toString();
        item.sizeValue    = obj.value(QStringLiteral("sizeValue")).toString();
        item.mainImageUrl = obj.value(QStringLiteral("mainImageUrl")).toString();
        const QString dateStr = obj.value(QStringLiteral("createdDate")).toString();
        if (!dateStr.isEmpty())
            item.createdDate = QDate::fromString(dateStr, Qt::ISODate);
        item.inventory = obj.value(QStringLiteral("inventory")).toInt(0);
        for (const QJsonValue &mpv : obj.value(QStringLiteral("existsIn")).toArray())
            item.existsInMarketplaces.insert(mpv.toString());
        item.manuallyMoved = obj.value(QStringLiteral("manuallyMoved")).toBool(false);
        items.append(item);
    }
    _applyItems(items);
    _loadOrder();
}

void PaneStore::_saveToDisk(const QString &marketplaceId,
                             const QList<AmazonCatalogApi::StoreItem> &items)
{
    m_workingDir.mkpath(QStringLiteral("stores"));
    const QString path = m_workingDir.filePath(
        QStringLiteral("stores/%1.json").arg(marketplaceId));

    QJsonArray arr;
    for (const AmazonCatalogApi::StoreItem &item : items) {
        QJsonObject obj;
        obj[QStringLiteral("asin")]      = item.asin;
        obj[QStringLiteral("sku")]       = item.sku;
        obj[QStringLiteral("title")]     = item.title;
        obj[QStringLiteral("brand")]     = item.brand;
        obj[QStringLiteral("category")]  = item.category;
        obj[QStringLiteral("gender")]    = item.gender;
        obj[QStringLiteral("age")]       = item.age;
        obj[QStringLiteral("color")]     = item.color;
        obj[QStringLiteral("sizeValue")] = item.sizeValue;
        obj[QStringLiteral("mainImageUrl")] = item.mainImageUrl;
        if (item.createdDate.isValid())
            obj[QStringLiteral("createdDate")] = item.createdDate.toString(Qt::ISODate);
        if (item.inventory > 0)
            obj[QStringLiteral("inventory")] = item.inventory;
        if (!item.existsInMarketplaces.isEmpty()) {
            QJsonArray mpArr;
            for (const QString &mpId : std::as_const(item.existsInMarketplaces))
                mpArr.append(mpId);
            obj[QStringLiteral("existsIn")] = mpArr;
        }
        if (item.manuallyMoved)
            obj[QStringLiteral("manuallyMoved")] = true;
        arr.append(obj);
    }

    QSaveFile sf(path);
    if (sf.open(QIODevice::WriteOnly | QIODevice::Text)) {
        sf.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
        sf.commit();
    }
}

void PaneStore::_applyItems(const QList<AmazonCatalogApi::StoreItem> &items)
{
    m_items = items;
    m_asinToItem.clear();
    for (const AmazonCatalogApi::StoreItem &item : items)
        m_asinToItem.insert(item.asin, item);

    // Pre-load cached images: sizing folder first, then stores/thumbs/ fallback.
    m_asinToPixmap.clear();
    if (!m_workingDir.path().isEmpty()) {
        const QDir sizingDir(m_workingDir.filePath(QStringLiteral("sizing")));
        const QDir thumbsDir(m_workingDir.filePath(QStringLiteral("stores/thumbs")));
        for (const AmazonCatalogApi::StoreItem &item : items) {
            // 1) Check sizing/{ASIN}-*/{ASIN}_main.jpg
            const QStringList dirs = sizingDir.entryList(
                {item.asin + QStringLiteral("-*")}, QDir::Dirs);
            for (const QString &d : dirs) {
                QPixmap px(sizingDir.filePath(
                    d + QLatin1Char('/') + item.asin + QStringLiteral("_main.jpg")));
                if (!px.isNull()) {
                    m_asinToPixmap.insert(item.asin,
                        px.scaled(54, 54, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                    break;
                }
            }
            if (m_asinToPixmap.contains(item.asin)) continue;

            // 2) Fallback: stores/thumbs/{ASIN}.jpg (saved by _loadImages)
            QPixmap px(thumbsDir.filePath(item.asin + QStringLiteral(".jpg")));
            if (!px.isNull())
                m_asinToPixmap.insert(item.asin, px);
        }
    }

    m_treeModel->setItems(items, m_customPaths);
    m_storeModel->clear();
}

QStringList PaneStore::_currentNodePath() const
{
    QStringList path;
    QModelIndex idx = ui->treeViewBrandCategory->currentIndex();
    while (idx.isValid()) {
        path.prepend(m_treeModel->nodeNameForIndex(idx));
        idx = idx.parent();
    }
    return path;
}

void PaneStore::_onTreeSelectionChanged()
{
    // Restore last-used country for Copy ASINs context (does not filter the table).
    const QString lastCountry = QSettings().value(
        QStringLiteral("store/lastSelectedCountry")).toString();

    for (int i = 0; i < m_countriesModel->rowCount(); ++i) {
        const QModelIndex idx = m_countriesModel->index(i, 0);
        if (idx.data(Qt::UserRole).toString() == lastCountry) {
            ui->listViewCountries->setCurrentIndex(idx);
            break;
        }
    }
    if (!ui->listViewCountries->currentIndex().isValid()
            && m_countriesModel->rowCount() > 0)
        ui->listViewCountries->setCurrentIndex(m_countriesModel->index(0, 0));

    _updateTableForCurrentSelection();
    _loadStorefrontVersions();
}

void PaneStore::_onCountrySelectionChanged()
{
    const QModelIndex idx = ui->listViewCountries->currentIndex();
    if (!idx.isValid()) return;
    QSettings().setValue(QStringLiteral("store/lastSelectedCountry"),
                         idx.data(Qt::UserRole).toString());
    // Does NOT refresh the table — the table shows all countries regardless.
}

void PaneStore::_updateTableForCurrentSelection()
{
    const QModelIndex treeIdx = ui->treeViewBrandCategory->currentIndex();
    const QStringList asins   = m_treeModel->asinsForIndex(treeIdx);
    if (asins.isEmpty()) { m_storeModel->clear(); return; }
    _buildTable(asins);
}

void PaneStore::_onMerge()
{
    const QModelIndexList sel =
        ui->treeViewBrandCategory->selectionModel()->selectedIndexes();
    if (sel.size() != 2 || sel.at(0).parent() != sel.at(1).parent()) return;

    const QModelIndex idx0 = sel.at(0);
    const QModelIndex idx1 = sel.at(1);

    const QString name0  = m_treeModel->nodeNameForIndex(idx0);
    const QString name1  = m_treeModel->nodeNameForIndex(idx1);
    const int     count0 = m_treeModel->colorCountForIndex(idx0);
    const int     count1 = m_treeModel->colorCountForIndex(idx1);
    const int     depth  = TreeBrandCategories::depthOfIndex(idx0);

    static const char *kDimNames[] = {"brand", "category", "gender", "age"};
    const QString dimLabel = (depth >= 0 && depth <= 3)
        ? QString::fromLatin1(kDimNames[depth]) : QStringLiteral("value");

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Merge %1 nodes").arg(dimLabel));
    auto *layout = new QVBoxLayout(&dlg);
    layout->addWidget(new QLabel(
        tr("Which node should receive all items? (the other is removed)"), &dlg));

    auto *rb0 = new QRadioButton(
        QStringLiteral("%1  (%2)").arg(name0).arg(count0), &dlg);
    auto *rb1 = new QRadioButton(
        QStringLiteral("%1  (%2)").arg(name1).arg(count1), &dlg);
    (count0 >= count1 ? rb0 : rb1)->setChecked(true);

    layout->addWidget(rb0);
    layout->addWidget(rb1);

    auto *btns = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    layout->addWidget(btns);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;

    const QString winner     = rb0->isChecked() ? name0 : name1;
    const QString loser      = rb0->isChecked() ? name1 : name0;
    const QModelIndex winIdx = rb0->isChecked() ? idx0  : idx1;

    // Build a name-based path for the winner so we can re-locate it after rebuild.
    // (Row indices shift when the loser node is removed.)
    auto namePath = [&](QModelIndex idx) -> QStringList {
        QStringList parts;
        while (idx.isValid()) {
            parts.prepend(m_treeModel->nodeNameForIndex(idx));
            idx = idx.parent();
        }
        return parts;
    };
    const QStringList winnerPath = namePath(winIdx);

    // Save expansion state as a set of name-paths.
    QSet<QString> expanded;
    std::function<void(QModelIndex)> saveExpanded = [&](QModelIndex parent) {
        for (int i = 0; i < m_treeModel->rowCount(parent); ++i) {
            const QModelIndex child = m_treeModel->index(i, 0, parent);
            if (ui->treeViewBrandCategory->isExpanded(child)) {
                expanded.insert(namePath(child).join(QLatin1Char('\0')));
                saveExpanded(child);
            }
        }
    };
    saveExpanded({});
    // Ensure the winner's ancestors stay expanded.
    for (QModelIndex p = winIdx.parent(); p.isValid(); p = p.parent())
        expanded.insert(namePath(p).join(QLatin1Char('\0')));

    // The tree shows placeholder labels for empty fields (e.g. "(unknown gender)").
    // Normalize those back to "" so the comparison against raw item fields works.
    static const QHash<QString, QString> kPlaceholders = {
        {tr("(unknown brand)"),    {}},
        {tr("(unknown category)"), {}},
        {tr("(unknown gender)"),   {}},
        {tr("(unknown age)"),      {}},
    };
    const QString rawLoser  = kPlaceholders.value(loser,  loser);
    const QString rawWinner = kPlaceholders.value(winner, winner);

    // Remap: change loser value → winner for the relevant field. Same as Move,
    // this must be flagged manuallyMoved or the next Retrieve will silently
    // revert it back to whatever Amazon's API returns for that field.
    for (AmazonCatalogApi::StoreItem &item : m_items) {
        QString *field = nullptr;
        switch (depth) {
        case 0: field = &item.brand;    break;
        case 1: field = &item.category; break;
        case 2: field = &item.gender;   break;
        case 3: field = &item.age;      break;
        }
        if (field && *field == rawLoser) {
            *field = rawWinner;
            item.manuallyMoved = true;
        }
    }

    // Rebuild (sales cache preserved intentionally).
    m_asinToItem.clear();
    for (const AmazonCatalogApi::StoreItem &item : m_items)
        m_asinToItem.insert(item.asin, item);
    m_treeModel->setItems(m_items, m_customPaths);
    m_storeModel->clear();
    _saveToDisk(_marketplaceId(), m_items);

    // Restore expansion state.
    std::function<void(QModelIndex)> restoreExpanded = [&](QModelIndex parent) {
        for (int i = 0; i < m_treeModel->rowCount(parent); ++i) {
            const QModelIndex child = m_treeModel->index(i, 0, parent);
            if (expanded.contains(namePath(child).join(QLatin1Char('\0')))) {
                ui->treeViewBrandCategory->setExpanded(child, true);
                restoreExpanded(child);
            }
        }
    };
    restoreExpanded({});

    // Re-locate the winner node and select it (triggers table refresh).
    std::function<QModelIndex(QModelIndex, const QStringList &, int)> findNode =
        [&](QModelIndex parent, const QStringList &path, int lvl) -> QModelIndex {
        if (lvl == path.size()) return parent;
        for (int i = 0; i < m_treeModel->rowCount(parent); ++i) {
            const QModelIndex child = m_treeModel->index(i, 0, parent);
            if (m_treeModel->nodeNameForIndex(child) == path.at(lvl))
                return findNode(child, path, lvl + 1);
        }
        return {};
    };
    const QModelIndex newWinner = findNode({}, winnerPath, 0);
    if (newWinner.isValid())
        ui->treeViewBrandCategory->setCurrentIndex(newWinner);

    ui->buttonMerge->setEnabled(false);
}

// ---------------------------------------------------------------------------
// _buildAsinGroups — maps every ASIN in visibleAsins to the full (product,color)
// group it belongs to (same key _buildTable() groups rows by), so Move/Remove/
// Copy expand a selected representative ASIN to ALL its sibling sizes/ASINs
// instead of just the one shown in the table.
// ---------------------------------------------------------------------------

QHash<QString, QStringList> PaneStore::_buildAsinGroups(const QStringList &visibleAsins) const
{
    QHash<QString, QStringList> groupsByKey;
    QHash<QString, QString> keyByAsin;
    for (const QString &asin : visibleAsins) {
        const QString key = TreeBrandCategories::colorGroupKey(m_asinToItem.value(asin));
        groupsByKey[key].append(asin);
        keyByAsin.insert(asin, key);
    }

    QHash<QString, QStringList> asinToGroup;
    for (const QString &asin : visibleAsins)
        asinToGroup.insert(asin, groupsByKey.value(keyByAsin.value(asin)));
    return asinToGroup;
}

void PaneStore::_onMoveProducts()
{
    // Collect selected representative ASINs from the table.
    const QList<int> tableSel = _selectedTableRows();
    if (tableSel.isEmpty()) return;

    QStringList repAsins;
    for (int row : tableSel)
        repAsins << m_storeModel->data(
            m_storeModel->index(row, TableStoreAsin::ColAsin)).toString();

    // Expand each rep ASIN to its full (product,color) group within the current
    // visible ASIN list — a selected row can represent several sibling ASINs.
    const QModelIndex treeIdx    = ui->treeViewBrandCategory->currentIndex();
    const QStringList visibleAsins = m_treeModel->asinsForIndex(treeIdx);
    const QHash<QString, QStringList> groups = _buildAsinGroups(visibleAsins);

    QSet<QString> asinsToMove;
    for (const QString &repAsin : std::as_const(repAsins)) {
        const QStringList group = groups.value(repAsin);
        if (group.isEmpty())
            asinsToMove.insert(repAsin);
        else
            for (const QString &a : group) asinsToMove.insert(a);
    }

    // Build dialog.
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Move %n product(s) to…", "", repAsins.size()));
    dlg.resize(400, 500);
    auto *layout = new QVBoxLayout(&dlg);
    layout->addWidget(new QLabel(tr("Select the destination node (exactly one):"), &dlg));

    auto *destTree = new QTreeView(&dlg);
    destTree->setModel(m_treeModel);
    destTree->setSelectionMode(QAbstractItemView::SingleSelection);
    destTree->setRootIsDecorated(true);
    destTree->setAlternatingRowColors(true);
    destTree->expandAll();
    layout->addWidget(destTree, 1);

    auto *warningLabel = new QLabel(&dlg);
    warningLabel->setStyleSheet(QStringLiteral("color: red;"));
    warningLabel->hide();
    layout->addWidget(warningLabel);

    auto *btns = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    layout->addWidget(btns);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(btns, &QDialogButtonBox::accepted, &dlg, [&]() {
        if (destTree->selectionModel()->selectedIndexes().isEmpty()) {
            warningLabel->setText(tr("Please select a destination node first."));
            warningLabel->show();
            return;
        }
        dlg.accept();
    });

    if (dlg.exec() != QDialog::Accepted) return;

    const QModelIndexList destSel = destTree->selectionModel()->selectedIndexes();
    if (destSel.isEmpty()) return;
    const QModelIndex destIdx   = destSel.first();
    const int         destDepth = TreeBrandCategories::depthOfIndex(destIdx);

    // Build the name path from root to the selected node.
    auto namePath = [&](QModelIndex idx) -> QStringList {
        QStringList parts;
        while (idx.isValid()) {
            parts.prepend(m_treeModel->nodeNameForIndex(idx));
            idx = idx.parent();
        }
        return parts;
    };
    const QStringList destPath = namePath(destIdx);

    // Normalize display placeholders back to raw empty strings.
    static const QHash<QString, QString> kPlaceholders = {
        {tr("(unknown brand)"),    {}},
        {tr("(unknown category)"), {}},
        {tr("(unknown gender)"),   {}},
        {tr("(unknown age)"),      {}},
    };
    auto normalize = [&](const QString &s) -> QString {
        return kPlaceholders.value(s, s);
    };

    // Apply: update only the fields that the destination depth covers.
    for (AmazonCatalogApi::StoreItem &item : m_items) {
        if (!asinsToMove.contains(item.asin)) continue;
        if (destDepth >= 0 && destPath.size() > 0) item.brand    = normalize(destPath[0]);
        if (destDepth >= 1 && destPath.size() > 1) item.category = normalize(destPath[1]);
        if (destDepth >= 2 && destPath.size() > 2) item.gender   = normalize(destPath[2]);
        if (destDepth >= 3 && destPath.size() > 3) item.age      = normalize(destPath[3]);
        item.manuallyMoved = true;
    }

    // Rebuild.
    m_asinToItem.clear();
    for (const AmazonCatalogApi::StoreItem &item : m_items)
        m_asinToItem.insert(item.asin, item);
    m_treeModel->setItems(m_items, m_customPaths);
    m_storeModel->clear();
    _saveToDisk(_marketplaceId(), m_items);

    // Re-select the destination node in the rebuilt tree.
    std::function<QModelIndex(QModelIndex, const QStringList &, int)> findNode =
        [&](QModelIndex parent, const QStringList &path, int lvl) -> QModelIndex {
        if (lvl == static_cast<int>(path.size())) return parent;
        for (int i = 0; i < m_treeModel->rowCount(parent); ++i) {
            const QModelIndex child = m_treeModel->index(i, 0, parent);
            if (m_treeModel->nodeNameForIndex(child) == path[lvl])
                return findNode(child, path, lvl + 1);
        }
        return {};
    };
    const QModelIndex newDest = findNode({}, destPath, 0);
    if (newDest.isValid())
        ui->treeViewBrandCategory->setCurrentIndex(newDest);
}

void PaneStore::_onRemoveProducts()
{
    const QList<int> tableSel = _selectedTableRows();
    if (tableSel.isEmpty()) return;

    QStringList repAsins;
    for (int row : tableSel)
        repAsins << m_storeModel->data(
            m_storeModel->index(row, TableStoreAsin::ColAsin)).toString();

    // Expand each rep ASIN to its full (product,color) group within the current
    // visible ASIN list — a selected row can represent several sibling ASINs.
    const QStringList visibleAsins =
        m_treeModel->asinsForIndex(ui->treeViewBrandCategory->currentIndex());
    const QHash<QString, QStringList> groups = _buildAsinGroups(visibleAsins);

    QSet<QString> asinsToRemove;
    for (const QString &repAsin : std::as_const(repAsins)) {
        const QStringList group = groups.value(repAsin);
        if (group.isEmpty())
            asinsToRemove.insert(repAsin);
        else
            for (const QString &a : group) asinsToRemove.insert(a);
    }

    const auto res = QMessageBox::question(
        this, tr("Remove products"),
        tr("Remove %n product(s) (%2 ASIN(s)) from the store list?\n"
           "This only removes the local entry — the listing on Amazon is not affected.",
           "", repAsins.size()).arg(asinsToRemove.size()),
        QMessageBox::Yes | QMessageBox::Cancel);
    if (res != QMessageBox::Yes) return;

    m_items.removeIf([&](const AmazonCatalogApi::StoreItem &item) {
        return asinsToRemove.contains(item.asin);
    });

    m_asinToItem.clear();
    for (const AmazonCatalogApi::StoreItem &item : m_items)
        m_asinToItem.insert(item.asin, item);
    m_treeModel->setItems(m_items, m_customPaths);
    m_storeModel->clear();
    _saveToDisk(_marketplaceId(), m_items);
}

// ---------------------------------------------------------------------------
// Custom path helpers
// ---------------------------------------------------------------------------

void PaneStore::_loadCustomPaths()
{
    m_customPaths.clear();
    QFile f(m_workingDir.filePath(QStringLiteral("stores/custom_paths.json")));
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    for (const QJsonValue &v : arr) {
        QStringList path;
        for (const QJsonValue &s : v.toArray())
            path << s.toString();
        if (!path.isEmpty()) m_customPaths.append(path);
    }
}

void PaneStore::_saveCustomPaths()
{
    QJsonArray arr;
    for (const QStringList &path : std::as_const(m_customPaths)) {
        QJsonArray pa;
        for (const QString &s : path) pa.append(s);
        arr.append(pa);
    }
    QSaveFile sf(m_workingDir.filePath(QStringLiteral("stores/custom_paths.json")));
    if (sf.open(QIODevice::WriteOnly)) {
        sf.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
        sf.commit();
    }
}

// ---------------------------------------------------------------------------
// English category names (TreeBrandCategories::ColEnglishName) — stored as
// {path:[...], name:"..."} entries rather than a flat map keyed by the
// '\x1f'-joined path, so the file stays readable/editable outside the app.
// ---------------------------------------------------------------------------

void PaneStore::_loadEnglishCategoryNames()
{
    QHash<QString, QString> names;
    QFile f(m_workingDir.filePath(QStringLiteral("stores/category_english_names.json")));
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
        for (const QJsonValue &v : arr) {
            const QJsonObject obj = v.toObject();
            QStringList path;
            for (const QJsonValue &s : obj.value(QStringLiteral("path")).toArray())
                path << s.toString();
            const QString name = obj.value(QStringLiteral("name")).toString();
            if (!path.isEmpty() && !name.isEmpty())
                names.insert(path.join(QLatin1Char('\x1f')), name);
        }
    }
    m_treeModel->setEnglishNames(names);
}

void PaneStore::_saveEnglishCategoryNames()
{
    QJsonArray arr;
    const QHash<QString, QString> names = m_treeModel->englishNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it) {
        if (it.value().isEmpty()) continue;
        QJsonObject obj;
        QJsonArray pathArr;
        for (const QString &part : it.key().split(QLatin1Char('\x1f')))
            pathArr.append(part);
        obj[QStringLiteral("path")] = pathArr;
        obj[QStringLiteral("name")] = it.value();
        arr.append(obj);
    }
    QSaveFile sf(m_workingDir.filePath(QStringLiteral("stores/category_english_names.json")));
    if (sf.open(QIODevice::WriteOnly)) {
        sf.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
        sf.commit();
    }
}

bool PaneStore::_isCurrentNodeCustom() const
{
    const QModelIndex idx = ui->treeViewBrandCategory->currentIndex();
    if (!idx.isValid()) return false;
    QStringList path;
    QModelIndex cur = idx;
    while (cur.isValid()) {
        path.prepend(m_treeModel->nodeNameForIndex(cur));
        cur = cur.parent();
    }
    return m_customPaths.contains(path);
}

// ---------------------------------------------------------------------------
// Add / Remove category
// ---------------------------------------------------------------------------

void PaneStore::_onAddCategory()
{
    const QModelIndex current = ui->treeViewBrandCategory->currentIndex();
    if (!current.isValid()) return;

    // New node is a child of the selected node.
    auto namePath = [&](QModelIndex idx) -> QStringList {
        QStringList parts;
        while (idx.isValid()) {
            parts.prepend(m_treeModel->nodeNameForIndex(idx));
            idx = idx.parent();
        }
        return parts;
    };

    const QStringList selectedPath = namePath(current);

    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("Add category"),
        tr("New node name (under \"%1\"):").arg(selectedPath.last()),
        QLineEdit::Normal, {}, &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    QStringList newPath = selectedPath;
    newPath << name.trimmed();

    if (m_customPaths.contains(newPath)) {
        QMessageBox::information(this, tr("Add category"),
                                 tr("A node with this name already exists at this level."));
        return;
    }

    m_customPaths.append(newPath);
    _saveCustomPaths();
    m_treeModel->setItems(m_items, m_customPaths);

    // Select the new node.
    std::function<QModelIndex(QModelIndex, const QStringList &, int)> findNode =
        [&](QModelIndex parent, const QStringList &path, int lvl) -> QModelIndex {
        if (lvl == static_cast<int>(path.size())) return parent;
        for (int i = 0; i < m_treeModel->rowCount(parent); ++i) {
            const QModelIndex child = m_treeModel->index(i, 0, parent);
            if (m_treeModel->nodeNameForIndex(child) == path[lvl])
                return findNode(child, path, lvl + 1);
        }
        return {};
    };
    const QModelIndex newNode = findNode({}, newPath, 0);
    if (newNode.isValid()) {
        ui->treeViewBrandCategory->expand(newNode.parent());
        ui->treeViewBrandCategory->setCurrentIndex(newNode);
    }
}

void PaneStore::_onRemoveCategory()
{
    if (!_isCurrentNodeCustom()) return;

    const QModelIndex current = ui->treeViewBrandCategory->currentIndex();
    auto namePath = [&](QModelIndex idx) -> QStringList {
        QStringList parts;
        while (idx.isValid()) {
            parts.prepend(m_treeModel->nodeNameForIndex(idx));
            idx = idx.parent();
        }
        return parts;
    };
    const QStringList path = namePath(current);
    const int depth = TreeBrandCategories::depthOfIndex(current);

    const QStringList asinsInNode = m_treeModel->asinsForIndex(current);

    QString msg = asinsInNode.isEmpty()
        ? tr("Remove custom category \"%1\"?").arg(path.last())
        : tr("Remove custom category \"%1\"?\n"
             "%2 product(s) currently in this node will be reassigned to \"(unknown)\".")
          .arg(path.last()).arg(asinsInNode.size());

    if (QMessageBox::question(this, tr("Remove category"), msg,
                              QMessageBox::Yes | QMessageBox::Cancel) != QMessageBox::Yes)
        return;

    // Reassign items: clear the field corresponding to this depth.
    if (!asinsInNode.isEmpty()) {
        const QSet<QString> asinSet(asinsInNode.begin(), asinsInNode.end());
        for (AmazonCatalogApi::StoreItem &item : m_items) {
            if (!asinSet.contains(item.asin)) continue;
            switch (depth) {
            case 0: item.brand    = {}; break;
            case 1: item.category = {}; break;
            case 2: item.gender   = {}; break;
            case 3: item.age      = {}; break;
            }
        }
        m_asinToItem.clear();
        for (const AmazonCatalogApi::StoreItem &item : m_items)
            m_asinToItem.insert(item.asin, item);
        _saveToDisk(_marketplaceId(), m_items);
    }

    // Remove the path and any child custom paths under it.
    m_customPaths.removeIf([&](const QStringList &p) {
        if (p.size() < path.size()) return false;
        return p.mid(0, path.size()) == path;
    });
    _saveCustomPaths();
    m_treeModel->setItems(m_items, m_customPaths);
    m_storeModel->clear();
}

QList<int> PaneStore::_selectedTableRows() const
{
    QSet<int> rows;
    for (const QModelIndex &idx : ui->tableViewAsins->selectionModel()->selectedIndexes())
        rows.insert(idx.row());
    QList<int> sorted(rows.cbegin(), rows.cend());
    std::sort(sorted.begin(), sorted.end());
    return sorted;
}

void PaneStore::_onCopyAsins()
{
    // One ASIN per selected row — each row already represents one (product,color)
    // group, so copying its representative ASIN is exactly "one per color/design".
    // Do NOT expand to the full group here (unlike Move/Remove): that would paste
    // every sibling size too, which is not what this button is for.
    QStringList asins;
    for (int row : _selectedTableRows())
        asins << m_storeModel->data(
            m_storeModel->index(row, TableStoreAsin::ColAsin)).toString();

    QGuiApplication::clipboard()->setText(asins.join(QLatin1Char(',')));
}

// ---------------------------------------------------------------------------
// _syncSavedOrderFromVisibleRows — after reordering rows in the currently
// selected node, update the GLOBAL m_savedOrder to match. m_savedOrder covers
// every category, not just the one on screen, so this must only replace the
// entries belonging to the visible node — wiping and rebuilding the whole
// list from m_storeModel (which only ever holds one node's rows) would erase
// every other category's saved order the moment you reorder anything.
// Entries are keyed by the stable (product,color) group key rather than by
// the representative ASIN shown in the table — which ASIN gets picked to
// represent a group depends on its category/gender (shoe-size / women's-size
// heuristics in _buildTable), so that identity is not stable across a
// category Move; the group key is, since it only depends on sku/color.
// ---------------------------------------------------------------------------

void PaneStore::_syncSavedOrderFromVisibleRows()
{
    QStringList visibleKeysInOrder;
    QSet<QString> visibleKeySet;
    for (int i = 0; i < m_storeModel->rowCount(); ++i) {
        const QString asin = m_storeModel->data(m_storeModel->index(i, TableStoreAsin::ColAsin)).toString();
        const QString key  = TreeBrandCategories::colorGroupKey(m_asinToItem.value(asin));
        visibleKeysInOrder << key;
        visibleKeySet.insert(key);
    }

    QStringList newOrder;
    for (const QString &key : std::as_const(m_savedOrder))
        if (!visibleKeySet.contains(key)) newOrder << key;
    newOrder << visibleKeysInOrder;

    m_savedOrder = newOrder;
    _saveOrder();
}

void PaneStore::_onMoveToTop()
{
    const QList<int> sel = _selectedTableRows();
    if (sel.isEmpty()) return;
    const int row = sel.first();
    if (!m_storeModel->moveRowToTop(row)) return;

    _syncSavedOrderFromVisibleRows();

    ui->tableViewAsins->selectionModel()->setCurrentIndex(
        m_storeModel->index(0, 0), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}

void PaneStore::_onMoveUp()
{
    const QList<int> sel = _selectedTableRows();
    if (sel.isEmpty()) return;
    const int row = sel.first();
    if (!m_storeModel->moveRowUp(row)) return;

    _syncSavedOrderFromVisibleRows();

    // Keep selection on the moved row
    ui->tableViewAsins->selectionModel()->setCurrentIndex(
        m_storeModel->index(row - 1, 0), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}

void PaneStore::_onMoveDown()
{
    const QList<int> sel = _selectedTableRows();
    if (sel.isEmpty()) return;
    const int row = sel.first();
    if (!m_storeModel->moveRowDown(row)) return;

    _syncSavedOrderFromVisibleRows();

    ui->tableViewAsins->selectionModel()->setCurrentIndex(
        m_storeModel->index(row + 1, 0), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}

void PaneStore::_onMoveToBottom()
{
    const QList<int> sel = _selectedTableRows();
    if (sel.isEmpty()) return;
    const int row = sel.first();
    if (!m_storeModel->moveRowToBottom(row)) return;

    _syncSavedOrderFromVisibleRows();

    ui->tableViewAsins->selectionModel()->setCurrentIndex(
        m_storeModel->index(m_storeModel->rowCount() - 1, 0),
        QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}

// ---------------------------------------------------------------------------
// _onRetrieve — coroutine: fetch all SKUs then per-SKU attributes
// ---------------------------------------------------------------------------

QCoro::Task<void> PaneStore::_onRetrieve()
{
    const QString marketplaceId = _marketplaceId();

    auto *progressDlg = new QDialog(parentWidget());
    progressDlg->setAttribute(Qt::WA_DeleteOnClose);
    progressDlg->setWindowTitle(tr("Retrieving store data… [%1]").arg(marketplaceId));
    progressDlg->resize(560, 400);
    auto *pLayout = new QVBoxLayout(progressDlg);

    auto *statusLabel = new QLabel(tr("Starting…"), progressDlg);
    QFont boldFont = statusLabel->font(); boldFont.setBold(true);
    statusLabel->setFont(boldFont);
    pLayout->addWidget(statusLabel);

    auto *progressBar = new QProgressBar(progressDlg);
    progressBar->setRange(0, 0);
    pLayout->addWidget(progressBar);

    auto *logEdit = new QTextEdit(progressDlg);
    logEdit->setReadOnly(true);
    logEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    pLayout->addWidget(logEdit);

    auto *btnLayout = new QHBoxLayout();
    auto *copyBtn   = new QPushButton(tr("Copy log"), progressDlg);
    btnLayout->addWidget(copyBtn);
    btnLayout->addStretch();
    auto *closeBtns = new QDialogButtonBox(QDialogButtonBox::Close, progressDlg);
    QPushButton *closeBtn = closeBtns->button(QDialogButtonBox::Close);
    if (closeBtn) closeBtn->setEnabled(false);
    btnLayout->addWidget(closeBtns);
    pLayout->addLayout(btnLayout);

    QPointer<QLabel>       statusLabelPtr(statusLabel);
    QPointer<QProgressBar> progressBarPtr(progressBar);
    QPointer<QTextEdit>    logEditPtr(logEdit);
    QPointer<QPushButton>  closeBtnPtr(closeBtn);

    auto appendLog = [logEditPtr](const QString &line) {
        if (!logEditPtr) return;
        const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
        logEditPtr->append(QStringLiteral("[%1] %2").arg(ts, line));
    };
    connect(copyBtn, &QPushButton::clicked, progressDlg, [logEditPtr]() {
        if (logEditPtr) QGuiApplication::clipboard()->setText(logEditPtr->toPlainText());
    });
    connect(closeBtns, &QDialogButtonBox::rejected, progressDlg, &QDialog::close);

    progressDlg->show();
    setEnabled(false);

    // Build the list of all configured marketplace IDs in the same region as the primary.
    // These are passed to fetchListingAttributes so a single call detects existence in all.
    QStringList sameRegionMpIds;
    if (const AmazonMarketplace *primaryMp = AmazonMarketplace::forMarketplaceId(marketplaceId)) {
        for (const AmazonMarketplace *rmp : AmazonMarketplace::forRegion(primaryMp->region())) {
            if (!_catalogApi()->sellerIdForMarketplace(rmp->marketplaceId()).isEmpty())
                sameRegionMpIds << rmp->marketplaceId();
        }
    }

    appendLog(tr("Fetching all SKUs via merchant listings report…"));
    if (statusLabelPtr) statusLabelPtr->setText(tr("Fetching SKU list…"));

    QHash<QString, QString> asinToSku;
    QHash<QString, int>    asinToInventory;
    co_await _catalogApi()->fetchAllSkusViaReport(marketplaceId, &asinToSku, &asinToInventory);

    if (asinToSku.isEmpty()) {
        appendLog(tr("⚠ No listings found. Check credentials and marketplace (current: %1).").arg(marketplaceId));
        if (statusLabelPtr) statusLabelPtr->setText(tr("Failed — no listings found."));
        if (closeBtnPtr)    closeBtnPtr->setEnabled(true);
        setEnabled(true);
        co_return;
    }

    const QList<QString> asins = asinToSku.keys();
    const int total = asins.size();
    appendLog(tr("Found %1 listing(s). Fetching attributes…").arg(total));
    if (progressBarPtr) { progressBarPtr->setRange(0, total); progressBarPtr->setValue(0); }

    QList<AmazonCatalogApi::StoreItem> items;
    items.reserve(total);
    int emptyBrandCount = 0;
    int parentCount = 0;

    for (int i = 0; i < asins.size(); ++i) {
        const QString &asin = asins.at(i);
        AmazonCatalogApi::StoreItem item;
        item.asin      = asin;
        item.sku       = asinToSku.value(asin);
        item.inventory = asinToInventory.value(asin, 0);

        co_await _catalogApi()->fetchListingAttributes(marketplaceId, item.sku, &item, sameRegionMpIds);

        // If the user manually moved this item, keep their placement regardless of what
        // the API returns — only update fields that don't affect tree position.
        const AmazonCatalogApi::StoreItem &prev = m_asinToItem.value(asin);
        if (prev.manuallyMoved) {
            item.brand         = prev.brand;
            item.category      = prev.category;
            item.gender        = prev.gender;
            item.age           = prev.age;
            item.manuallyMoved = true;
        }

        // If the whole call came back empty (API error / rate-limit), restore all
        // previously-cached fields so the item stays correctly categorised.
        if (!item.manuallyMoved
                && item.brand.isEmpty() && item.category.isEmpty() && item.title.isEmpty()) {
            if (!prev.brand.isEmpty()) {
                ++emptyBrandCount;
                item.brand    = prev.brand;
                item.category = prev.category;
                item.gender   = prev.gender;
                item.age      = prev.age;
                item.color    = prev.color;
                item.sizeValue = prev.sizeValue;
                item.title    = prev.title;
                item.mainImageUrl = prev.mainImageUrl;
                item.createdDate  = prev.createdDate;
                item.existsInMarketplaces = prev.existsInMarketplaces;
                if (emptyBrandCount <= 5)
                    appendLog(tr("  ⚠ [%1/%2] %3 — API returned nothing, kept cached data (brand=%4)")
                              .arg(i + 1).arg(total).arg(item.sku, item.brand));
            }
        } else if (!item.manuallyMoved && item.brand.isEmpty()) {
            // Brand alone missing — partial API response.
            ++emptyBrandCount;
            if (!prev.brand.isEmpty())
                item.brand = prev.brand;
            if (emptyBrandCount <= 5)
                appendLog(tr("  ⚠ [%1/%2] %3 — brand empty from API (title=%4, kept prev=%5)")
                          .arg(i + 1).arg(total).arg(item.sku, item.title, item.brand));
        }

        if (progressBarPtr) progressBarPtr->setValue(i + 1);
        if (statusLabelPtr)
            statusLabelPtr->setText(tr("[%1/%2] %3").arg(i + 1).arg(total).arg(item.sku));
        if ((i + 1) % 20 == 0 || (i + 1) == total)
            appendLog(tr("[%1/%2] %3 — brand=%4 category=%5 gender=%6 color=%7 parent=%8")
                      .arg(i + 1).arg(total)
                      .arg(item.sku, item.brand, item.category, item.gender, item.color)
                      .arg(item.isParent ? QStringLiteral("yes") : QStringLiteral("no")));
        if (item.isParent) {
            ++parentCount;
            continue; // variation parents have no color/size — skip them
        }
        if (item.color.isEmpty())
            appendLog(tr("  ⚠ [%1/%2] %3 (ASIN %4) — color empty, title: %5")
                      .arg(i + 1).arg(total).arg(item.sku, item.asin, item.title));

        items.append(item);

        if (i + 1 < total) {
            QTimer pause; pause.setSingleShot(true); pause.start(200);
            co_await qCoro(&pause).waitForTimeout();
        }
    }

    appendLog(tr("Skipped %1 variation parent(s), kept %2 child listing(s).")
              .arg(parentCount).arg(total - parentCount));
    if (emptyBrandCount > 0)
        appendLog(tr("⚠ %1/%2 item(s) had empty brand — possible API error or rate limit.")
                  .arg(emptyBrandCount).arg(total));

    _applyItems(items);
    // Invalidate stale disk thumbnails so _loadImages always re-downloads fresh images
    // after a retrieve (avoids serving a wrong cached thumb for a newly-fetched ASIN).
    for (const auto &item : std::as_const(items))
        m_asinToPixmap.remove(item.asin);
    _saveToDisk(marketplaceId, items);
    appendLog(tr("Saved %1 item(s) to stores/%2.json.").arg(items.size()).arg(marketplaceId));

    if (statusLabelPtr) statusLabelPtr->setText(tr("Done."));
    if (closeBtnPtr)    closeBtnPtr->setEnabled(true);
    setEnabled(true);
}

// ---------------------------------------------------------------------------
// _buildTable — build color-grouped rows from the given ASIN list, apply
// saved order (new items first), and populate the table synchronously.
// ---------------------------------------------------------------------------

void PaneStore::_buildTable(const QStringList &asins)
{
    // Build color groups (shoes → EU44, women → FR38, else first ASIN)
    auto extractSizeInt = [](const QString &raw) -> int {
        static const QRegularExpression re(QStringLiteral(R"(\b(\d+)\b)"));
        const auto m = re.match(raw);
        return m.hasMatch() ? m.captured(1).toInt() : 0;
    };

    // Grouping key: shared with TreeBrandCategories (whose tree counts must match
    // these table rows) and with _expandToGroup() (used by Move/Remove/Copy) —
    // see TreeBrandCategories::colorGroupKey for why color alone isn't enough.
    QHash<QString, QStringList> colorGroups;
    QStringList colorOrder;
    for (const QString &asin : std::as_const(asins)) {
        const AmazonCatalogApi::StoreItem &it = m_asinToItem.value(asin);
        const QString key = TreeBrandCategories::colorGroupKey(it);
        if (!colorGroups.contains(key)) colorOrder.append(key);
        colorGroups[key].append(asin);
    }

    // Build raw rows (one per color group)
    QList<TableStoreAsin::Row> unsortedRows;
    unsortedRows.reserve(colorGroups.size());

    for (const QString &colorKey : std::as_const(colorOrder)) {
        const QStringList &group = colorGroups.value(colorKey);
        if (group.isEmpty()) continue;

        const AmazonCatalogApi::StoreItem &first = m_asinToItem.value(group.first());
        const QString catUp = first.category.toUpper();
        const bool isShoes  = catUp.contains(QLatin1String("SHOE"))
                           || catUp.contains(QLatin1String("BOOT"))
                           || catUp.contains(QLatin1String("SANDAL"))
                           || catUp.contains(QLatin1String("FOOTWEAR"));
        const QString genLo = first.gender.toLower();
        const bool isWomen  = !isShoes
                           && (genLo.contains(QLatin1String("female"))
                            || genLo.contains(QLatin1String("women")));

        auto findSize = [&](int target) -> QString {
            for (const QString &a : group)
                if (extractSizeInt(m_asinToItem.value(a).sizeValue) == target)
                    return a;
            return {};
        };

        QString picked;
        if (isShoes) {
            picked = findSize(44);
            if (picked.isEmpty()) picked = findSize(39);
        } else if (isWomen) {
            picked = findSize(38);
            if (picked.isEmpty()) {
                int smallest = INT_MAX;
                for (const QString &a : group) {
                    const int n = extractSizeInt(m_asinToItem.value(a).sizeValue);
                    if (n > 0 && n < smallest) { smallest = n; picked = a; }
                }
            }
        }
        if (picked.isEmpty()) picked = group.first();

        const AmazonCatalogApi::StoreItem &si = m_asinToItem.value(picked);

        TableStoreAsin::Row row;
        row.asin                  = picked;
        row.title                 = si.title;
        row.image                 = m_asinToPixmap.value(picked);
        row.createdDate           = si.createdDate;
        row.existsInMarketplaces  = si.existsInMarketplaces;

        int available = -1, sales90 = -1, sales365 = -1;
        _aggregateStock(group, &available, &sales90, &sales365);
        row.salesYear      = sales365;
        row.stockDays      = estimatedDaysOfSupply(available, sales90, 90);
        row.recommended4mo = recommendedInventoryForDays(sales90, 90, kFourMonthCoverageDays);

        unsortedRows.append(row);
    }

    // Apply saved order: new rows (not in m_savedOrder) go first, then known order.
    // Matched by the stable (product,color) group key, not the representative
    // ASIN — see _syncSavedOrderFromVisibleRows for why ASIN identity isn't
    // stable across a category Move (shoe/gender size-picking can change which
    // ASIN represents a group), while the group key always is.
    const QSet<QString> savedSet(m_savedOrder.cbegin(), m_savedOrder.cend());
    QList<TableStoreAsin::Row> newRows, orderedRows;
    newRows.reserve(unsortedRows.size());
    orderedRows.resize(m_savedOrder.size()); // slots, some may stay default-constructed

    QHash<QString, int> savedPos;
    for (int i = 0; i < m_savedOrder.size(); ++i)
        savedPos.insert(m_savedOrder.at(i), i);

    for (const TableStoreAsin::Row &r : std::as_const(unsortedRows)) {
        const QString key = TreeBrandCategories::colorGroupKey(m_asinToItem.value(r.asin));
        if (savedSet.contains(key))
            orderedRows[savedPos.value(key)] = r;
        else
            newRows.append(r);
    }
    // Remove placeholder slots that have no matching row (ASIN was removed after last order save)
    QList<TableStoreAsin::Row> finalRows = newRows;
    for (const TableStoreAsin::Row &r : std::as_const(orderedRows))
        if (!r.asin.isEmpty())
            finalRows.append(r);

    m_storeModel->setRows(finalRows);

    ui->tableViewAsins->resizeColumnsToContents();
    ui->tableViewAsins->horizontalHeader()->setStretchLastSection(false);
    ui->tableViewAsins->setColumnWidth(TableStoreAsin::ColImage, 58);
    ui->tableViewAsins->horizontalHeader()->setSectionResizeMode(
        TableStoreAsin::ColTitle, QHeaderView::Stretch);

    // Kick off async image loading for ASINs without cached images
    QStringList needImages;
    for (const TableStoreAsin::Row &r : std::as_const(finalRows))
        if (r.image.isNull() && !m_asinToItem.value(r.asin).mainImageUrl.isEmpty())
            needImages.append(r.asin);
    if (!needImages.isEmpty())
        m_imageTask = _loadImages(needImages);
}

// ---------------------------------------------------------------------------
// Sales/stock cache — mirrors PaneMarketplaces' AmazonCache convention (24h
// TTL, partial refetch of whatever's missing/failed) under its own key prefix
// so the two caches never collide (PaneMarketplaces' universe is target-store
// SKUs; this one is the catalog SKUs currently visible in the Store tab).
// ---------------------------------------------------------------------------

void PaneStore::_aggregateStock(const QStringList &groupAsins,
                                 int *availableOut, int *sales90Out, int *sales365Out) const
{
    int available = -1, sales90 = -1, sales365 = -1;
    for (const QString &asin : groupAsins) {
        const QString sku = m_asinToItem.value(asin).sku.toLower();
        if (sku.isEmpty()) continue;

        const int a = m_stockAvailableBySkuLower.value(sku, -1);
        if (a >= 0) available = (available < 0 ? 0 : available) + a;
        const int s90 = m_stockSales90BySkuLower.value(sku, -1);
        if (s90 >= 0) sales90 = (sales90 < 0 ? 0 : sales90) + s90;
        const int s365 = m_stockSales365BySkuLower.value(sku, -1);
        if (s365 >= 0) sales365 = (sales365 < 0 ? 0 : sales365) + s365;
    }
    if (availableOut) *availableOut = available;
    if (sales90Out)   *sales90Out   = sales90;
    if (sales365Out)  *sales365Out  = sales365;
}

void PaneStore::_loadStockCache()
{
    auto s = WorkingDirectoryManager::instance()->settings();
    m_stockCacheTimestampUtc = s->value(QStringLiteral("PaneStoreStockCache/timestamp")).toLongLong();

    auto loadIntMap = [&](const QString &key, QHash<QString, int> *out) {
        out->clear();
        const QJsonObject obj = QJsonDocument::fromJson(
            s->value(key).toString().toUtf8()).object();
        for (auto it = obj.begin(); it != obj.end(); ++it)
            out->insert(it.key(), it.value().toInt(-1));
    };
    loadIntMap(QStringLiteral("PaneStoreStockCache/available"), &m_stockAvailableBySkuLower);
    loadIntMap(QStringLiteral("PaneStoreStockCache/sales90"),   &m_stockSales90BySkuLower);
    loadIntMap(QStringLiteral("PaneStoreStockCache/sales365"),  &m_stockSales365BySkuLower);
}

void PaneStore::_saveStockCache() const
{
    auto s = WorkingDirectoryManager::instance()->settings();
    s->setValue(QStringLiteral("PaneStoreStockCache/timestamp"), m_stockCacheTimestampUtc);

    auto saveIntMap = [&](const QString &key, const QHash<QString, int> &map) {
        QJsonObject obj;
        for (auto it = map.begin(); it != map.end(); ++it)
            obj.insert(it.key(), it.value());
        s->setValue(key, QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
    };
    saveIntMap(QStringLiteral("PaneStoreStockCache/available"), m_stockAvailableBySkuLower);
    saveIntMap(QStringLiteral("PaneStoreStockCache/sales90"),   m_stockSales90BySkuLower);
    saveIntMap(QStringLiteral("PaneStoreStockCache/sales365"),  m_stockSales365BySkuLower);
}

// ---------------------------------------------------------------------------
// _onRefreshSalesStock — fetches Amazon FBA qty + 90d/365d sales for every SKU
// in the currently selected tree node, via the same AbstractInventorySource
// abstraction PaneMarketplaces uses (so "days of supply" here can never
// silently disagree with the number driving the Temu sync there — both call
// estimatedDaysOfSupply() in MarketplaceTypes.h). Scoped to the current
// selection rather than the whole catalog: fetchSalesUnits is one Orders-API
// call per SKU, too slow/rate-limited to run against thousands of ASINs.
// ---------------------------------------------------------------------------

QCoro::Task<void> PaneStore::_onRefreshSalesStock()
{
    const QModelIndex treeIdx = ui->treeViewBrandCategory->currentIndex();
    const QStringList asins   = m_treeModel->asinsForIndex(treeIdx);
    if (asins.isEmpty()) {
        QMessageBox::information(this, tr("Refresh sales & stock"),
            tr("Select a node in the Brand/Category tree first — refresh is scoped "
               "to what's currently shown (fetching sales for the whole catalog would "
               "be far too slow)."));
        co_return;
    }

    QStringList skus;
    QSet<QString> seenLower;
    for (const QString &asin : asins) {
        const QString sku = m_asinToItem.value(asin).sku;
        if (sku.isEmpty() || seenLower.contains(sku.toLower())) continue;
        seenLower.insert(sku.toLower());
        skus << sku;
    }
    if (skus.isEmpty()) co_return;

    auto *progressDlg = new QDialog(parentWidget());
    progressDlg->setAttribute(Qt::WA_DeleteOnClose);
    progressDlg->setWindowTitle(tr("Refreshing sales & stock…"));
    progressDlg->resize(560, 400);
    auto *pLayout = new QVBoxLayout(progressDlg);

    auto *statusLabel = new QLabel(tr("Starting…"), progressDlg);
    QFont boldFont = statusLabel->font(); boldFont.setBold(true);
    statusLabel->setFont(boldFont);
    pLayout->addWidget(statusLabel);

    auto *progressBar = new QProgressBar(progressDlg);
    progressBar->setRange(0, skus.size());
    pLayout->addWidget(progressBar);

    auto *logEdit = new QTextEdit(progressDlg);
    logEdit->setReadOnly(true);
    logEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    pLayout->addWidget(logEdit);

    auto *closeBtns = new QDialogButtonBox(QDialogButtonBox::Close, progressDlg);
    QPushButton *closeBtn = closeBtns->button(QDialogButtonBox::Close);
    if (closeBtn) closeBtn->setEnabled(false);
    pLayout->addWidget(closeBtns);

    QPointer<QLabel>       statusLabelPtr(statusLabel);
    QPointer<QProgressBar> progressBarPtr(progressBar);
    QPointer<QTextEdit>    logEditPtr(logEdit);
    QPointer<QPushButton>  closeBtnPtr(closeBtn);
    QPointer<QDialog>      dlgPtr(progressDlg);

    auto appendLog = [logEditPtr](const QString &line) {
        if (!logEditPtr) return;
        const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
        logEditPtr->append(QStringLiteral("[%1] %2").arg(ts, line));
    };
    connect(closeBtns, &QDialogButtonBox::rejected, progressDlg, &QDialog::close);
    progressDlg->show();
    setEnabled(false);

    QList<AbstractInventorySource *> sources =
        AbstractInventorySourceFactory::buildAllInstances(
            WorkingDirectoryManager::instance()->settings().data());
    auto sourcesGuard = qScopeGuard([&sources]() { qDeleteAll(sources); });
    AbstractInventorySource *source = sources.isEmpty() ? nullptr : sources.first();

    if (!source) {
        appendLog(tr("✗ No fulfillment source configured — check API credentials in Settings."));
        if (statusLabelPtr) statusLabelPtr->setText(tr("Failed — no source configured."));
        if (closeBtnPtr)    closeBtnPtr->setEnabled(true);
        setEnabled(true);
        co_return;
    }

    // 24h TTL on the whole cache, like PaneMarketplaces' AmazonCache: once stale,
    // every SKU is treated as missing and re-fetched.
    const qint64 nowUtc = QDateTime::currentDateTimeUtc().toSecsSinceEpoch();
    const bool cacheStale = (nowUtc - m_stockCacheTimestampUtc) > 24 * 3600;

    appendLog(tr("→ %1: fetching inventory for %2 SKU(s)").arg(source->displayName()).arg(skus.size()));
    if (statusLabelPtr) statusLabelPtr->setText(tr("Fetching inventory…"));
    QList<StockRecord> records;
    co_await source->fetchAllInventory(skus, &records, appendLog);
    if (!dlgPtr) { setEnabled(true); co_return; }

    for (const StockRecord &r : std::as_const(records))
        m_stockAvailableBySkuLower.insert(r.sku.toLower(), r.available);

    for (int i = 0; i < skus.size(); ++i) {
        if (!dlgPtr) { setEnabled(true); co_return; }
        const QString sku      = skus.at(i);
        const QString skuLower = sku.toLower();
        if (progressBarPtr) progressBarPtr->setValue(i + 1);

        const bool needSales90  = cacheStale || m_stockSales90BySkuLower.value(skuLower, -1) < 0;
        const bool needSales365 = cacheStale || m_stockSales365BySkuLower.value(skuLower, -1) < 0;
        if (!needSales90 && !needSales365) continue;

        if (statusLabelPtr)
            statusLabelPtr->setText(tr("SKU %1/%2: %3 — fetching sales…").arg(i + 1).arg(skus.size()).arg(sku));

        if (needSales90) {
            int units = -1;
            co_await source->fetchSalesUnits(sku, 90, &units);
            if (!dlgPtr) { setEnabled(true); co_return; }
            m_stockSales90BySkuLower.insert(skuLower, units);
        }
        if (needSales365) {
            int units = -1;
            co_await source->fetchSalesUnits(sku, 365, &units);
            if (!dlgPtr) { setEnabled(true); co_return; }
            m_stockSales365BySkuLower.insert(skuLower, units);
        }
        appendLog(tr("  ✓ %1: 90d=%2 365d=%3").arg(sku)
                  .arg(m_stockSales90BySkuLower.value(skuLower, -1))
                  .arg(m_stockSales365BySkuLower.value(skuLower, -1)));
    }

    m_stockCacheTimestampUtc = nowUtc;
    _saveStockCache();

    _updateTableForCurrentSelection();

    appendLog(tr("Done."));
    if (statusLabelPtr) statusLabelPtr->setText(tr("Done."));
    if (closeBtnPtr)    closeBtnPtr->setEnabled(true);
    setEnabled(true);
}

// ---------------------------------------------------------------------------
// _onSortBySales — reorders the currently visible node's rows by Sales (12mo),
// highest first, and persists it as the manual display order (same mechanism
// as drag/move-up-down) so it sticks and feeds the storefront ordering.
// Rows with no fetched sales data (-1) sort last, not first.
// ---------------------------------------------------------------------------

void PaneStore::_onSortBySales()
{
    QList<TableStoreAsin::Row> rows = m_storeModel->rows();
    if (rows.isEmpty()) return;

    std::stable_sort(rows.begin(), rows.end(),
                     [](const TableStoreAsin::Row &a, const TableStoreAsin::Row &b) {
        return a.salesYear > b.salesYear;
    });

    m_storeModel->setRows(rows);
    _syncSavedOrderFromVisibleRows();
}

// ---------------------------------------------------------------------------
// _loadImages — async download of CDN thumbnails for ASINs lacking a cached image
// ---------------------------------------------------------------------------

QCoro::Task<void> PaneStore::_loadImages(QStringList asins)
{
    // Thumbnail URL transform: strip any existing qualifiers and add ._SL96_
    auto thumbUrl = [](const QString &url) -> QString {
        if (url.isEmpty()) return {};
        static const QRegularExpression re(
            QStringLiteral(R"(/images/I/([^./]+)(?:\.[^/]*)?\.(jpg|png|gif)$)"),
            QRegularExpression::CaseInsensitiveOption);
        const auto m = re.match(url);
        if (!m.hasMatch()) return url;
        const QString base = url.left(m.capturedStart(1));
        return base + m.captured(1) + QStringLiteral("._SL96_.") + m.captured(2);
    };

    QNetworkAccessManager nam;

    for (const QString &asin : std::as_const(asins)) {
        if (m_asinToPixmap.contains(asin)) continue; // already loaded

        const QString url = thumbUrl(m_asinToItem.value(asin).mainImageUrl);
        if (url.isEmpty()) continue;

        QNetworkReply *reply = nam.get(QNetworkRequest{QUrl(url)});
        co_await qCoro(reply).waitForFinished();
        const QByteArray data = reply->readAll();
        reply->deleteLater();

        if (!data.isEmpty()) {
            QPixmap px;
            if (px.loadFromData(data)) {
                const QPixmap thumb = px.scaled(
                    54, 54, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                m_asinToPixmap.insert(asin, thumb);
                m_storeModel->updateImage(asin, thumb);

                // Persist for future sessions so _applyItems can reload without network.
                if (!m_workingDir.path().isEmpty()) {
                    m_workingDir.mkpath(QStringLiteral("stores/thumbs"));
                    thumb.save(m_workingDir.filePath(
                        QStringLiteral("stores/thumbs/") + asin + QStringLiteral(".jpg")));
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Storefront image generation
// ---------------------------------------------------------------------------

void PaneStore::_onGenStorefrontImage()
{
    // Collect representative ASINs: selected rows, else all visible rows (up to 8).
    QStringList asins;
    const QList<int> sel = _selectedTableRows();
    if (!sel.isEmpty()) {
        for (int row : sel)
            asins << m_storeModel->data(
                m_storeModel->index(row, TableStoreAsin::ColAsin)).toString();
    } else {
        const int n = qMin(m_storeModel->rowCount(), 8);
        for (int i = 0; i < n; ++i)
            asins << m_storeModel->data(
                m_storeModel->index(i, TableStoreAsin::ColAsin)).toString();
    }

    if (asins.size() < 3) {
        QMessageBox::warning(this, tr("Gen image"),
                             tr("Select at least 3 products."));
        return;
    }
    if (asins.size() > 8) {
        QMessageBox::warning(this, tr("Gen image"),
                             tr("Select at most 8 products."));
        return;
    }

    QList<AmazonCatalogApi::StoreItem> items;
    items.reserve(asins.size());
    for (const QString &asin : std::as_const(asins))
        items.append(m_asinToItem.value(asin));

    AbstractCli *cli = ui->comboBoxGenCli->currentData().value<AbstractCli *>();
    if (!cli) {
        QMessageBox::warning(this, tr("Gen image"),
                             tr("No CLI with image generation support is available."));
        return;
    }

    auto *dlg = new DialogGenStorefrontImage(m_workingDir, items, m_asinToPixmap,
                                              cli, _currentNodePath(), this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &DialogGenStorefrontImage::imageGenerated,
            this, &PaneStore::_onStorefrontImageGenerated);
    dlg->show();
}

void PaneStore::_onStorefrontImageGenerated(const QString &desktopPath,
                                            const QString &mobilePath)
{
    Q_UNUSED(desktopPath);
    Q_UNUSED(mobilePath);
    _loadStorefrontVersions();
}

void PaneStore::_loadStorefrontVersions()
{
    ui->listVersionStrip->clear();
    ui->labelImage->setText(tr("(no image)"));
    ui->labelImage->setPixmap(QPixmap());

    if (m_workingDir.path().isEmpty()) return;

    const QString jsonPath =
        m_workingDir.filePath(QStringLiteral("stores/storefront/versions.json"));
    QFile f(jsonPath);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonArray rawArr = QJsonDocument::fromJson(f.readAll()).array();
    f.close();

    QList<QJsonObject> versions;
    versions.reserve(rawArr.size());
    for (const QJsonValue &v : rawArr)
        versions.append(v.toObject());
    std::sort(versions.begin(), versions.end(),
              [](const QJsonObject &a, const QJsonObject &b) {
                  return a.value(QStringLiteral("ts")).toVariant().toLongLong()
                       > b.value(QStringLiteral("ts")).toVariant().toLongLong();
              });

    const QDir sfDir(m_workingDir.filePath(QStringLiteral("stores/storefront")));
    const QStringList currentPath = _currentNodePath();

    // One strip item per image file — "Both" versions produce two items.
    auto addItem = [&](const QJsonObject &obj, const QString &fname, const QString &label) {
        const qint64 ts = obj.value(QStringLiteral("ts")).toVariant().toLongLong();
        const QString dateStr =
            QDateTime::fromSecsSinceEpoch(ts).toString(QStringLiteral("MM-dd HH:mm"));
        auto *li = new QListWidgetItem(ui->listVersionStrip);
        li->setText(QStringLiteral("%1\n%2").arg(dateStr, label));
        li->setData(Qt::UserRole,     sfDir.filePath(fname));  // abs path for display
        li->setData(Qt::UserRole + 1, static_cast<qlonglong>(ts)); // ts for delete
        QPixmap px(sfDir.filePath(fname));
        if (!px.isNull())
            li->setIcon(QIcon(px.scaled(100, 60, Qt::KeepAspectRatio,
                                        Qt::SmoothTransformation)));
    };

    // An image belongs here if its stored nodePath is a prefix of (or equal to) the
    // current selection — so images generated for "female" also show for "female > Adult".
    // Images with no nodePath (generated before this feature) are shown everywhere.
    auto nodePathVisible = [](const QStringList &stored, const QStringList &current) {
        if (stored.isEmpty()) return true;
        if (current.size() < stored.size()) return false;
        for (int i = 0; i < stored.size(); ++i)
            if (current[i] != stored[i]) return false;
        return true;
    };

    for (const QJsonObject &obj : std::as_const(versions)) {
        QStringList nodePath;
        for (const QJsonValue &v : obj.value(QStringLiteral("nodePath")).toArray())
            nodePath.append(v.toString());
        if (!nodePathVisible(nodePath, currentPath)) continue;
        const QString desktop = obj.value(QStringLiteral("desktop")).toString();
        const QString mobile  = obj.value(QStringLiteral("mobile")).toString();
        if (!desktop.isEmpty()) addItem(obj, desktop, tr("Desktop"));
        if (!mobile.isEmpty())  addItem(obj, mobile,  tr("Mobile"));
    }

    if (ui->listVersionStrip->count() > 0) {
        ui->listVersionStrip->setCurrentRow(0);
    }
}

void PaneStore::_showStorefrontImage(const QString &absPath)
{
    if (absPath.isEmpty()) {
        ui->labelImage->setPixmap(QPixmap());
        ui->labelImage->setText(tr("(no image)"));
        return;
    }
    QPixmap px(absPath);
    if (px.isNull()) {
        ui->labelImage->setPixmap(QPixmap());
        ui->labelImage->setText(tr("(no image)"));
        return;
    }
    const QSize target = ui->labelImage->size();
    ui->labelImage->setPixmap(px.scaled(target.isEmpty() ? px.size() : target,
                                        Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void PaneStore::_deleteSelectedVersion()
{
    const int row = ui->listVersionStrip->currentRow();
    if (row < 0) return;

    const qint64 victimTs =
        ui->listVersionStrip->item(row)->data(Qt::UserRole + 1).toLongLong();
    if (victimTs == 0) return;

    const QString jsonPath =
        m_workingDir.filePath(QStringLiteral("stores/storefront/versions.json"));
    QFile f(jsonPath);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonArray rawArr = QJsonDocument::fromJson(f.readAll()).array();
    f.close();

    // Find victim entry by ts.
    QJsonObject victim;
    for (const QJsonValue &v : rawArr) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("ts")).toVariant().toLongLong() == victimTs) {
            victim = o;
            break;
        }
    }
    if (victim.isEmpty()) return;

    const QDir sfDir(m_workingDir.filePath(QStringLiteral("stores/storefront")));
    for (const char *key : {"desktop", "mobile"}) {
        const QString fname = victim.value(QLatin1String(key)).toString();
        if (!fname.isEmpty())
            QFile::remove(sfDir.filePath(fname));
    }

    QJsonArray newArr;
    for (const QJsonValue &v : rawArr) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("ts")).toVariant().toLongLong() == victimTs)
            continue;
        newArr.append(o);
    }

    QSaveFile sf(jsonPath);
    if (sf.open(QIODevice::WriteOnly)) {
        sf.write(QJsonDocument(newArr).toJson(QJsonDocument::Compact));
        sf.commit();
    }

    _loadStorefrontVersions();
}

// ---------------------------------------------------------------------------
// _onExportProducts — copies the currently selected storefront desktop image
// and an ASINS.txt (selected ASINs, preceded by the category name translated
// per configured marketplace) into exportFolder/{brand}/{cat1_cat2_...}/.
// ---------------------------------------------------------------------------

void PaneStore::_onExportProducts()
{
    const QList<int> sel = _selectedTableRows();
    if (sel.isEmpty()) {
        QMessageBox::warning(this, tr("Export"), tr("Select at least one product first."));
        return;
    }
    // One ASIN per row — same semantics as Copy ASINs (not the full size group).
    QStringList asins;
    for (int row : sel)
        asins << m_storeModel->data(
            m_storeModel->index(row, TableStoreAsin::ColAsin)).toString();

    const QString exportRoot = ui->lineEditExportFolder->text();
    if (exportRoot.isEmpty()) {
        QMessageBox::warning(this, tr("Export"), tr("Pick an export folder first."));
        return;
    }

    const QModelIndex treeIdx = ui->treeViewBrandCategory->currentIndex();
    const QStringList nodePath = _currentNodePath();
    if (nodePath.isEmpty()) {
        QMessageBox::warning(this, tr("Export"), tr("Select a category in the tree first."));
        return;
    }
    const QString englishName = m_treeModel->englishNameForIndex(treeIdx).trimmed();
    if (englishName.isEmpty()) {
        QMessageBox::warning(this, tr("Export"),
            tr("Fill in the \"English name\" column for this category first "
               "(double-click its cell in the tree)."));
        return;
    }

    AbstractCli *cli = ui->comboBoxGenCli->currentData().value<AbstractCli *>();
    if (!cli) {
        QMessageBox::warning(this, tr("Export"),
            tr("No CLI with image generation support is available."));
        return;
    }

    const int verRow = ui->listVersionStrip->currentRow();
    if (verRow < 0) {
        QMessageBox::warning(this, tr("Export"),
            tr("No generated image selected — generate or select one first."));
        return;
    }
    const qint64 ts = ui->listVersionStrip->item(verRow)->data(Qt::UserRole + 1).toLongLong();
    const QString versionsJsonPath =
        m_workingDir.filePath(QStringLiteral("stores/storefront/versions.json"));
    QString desktopFile;
    {
        QFile f(versionsJsonPath);
        if (f.open(QIODevice::ReadOnly)) {
            for (const QJsonValue &v : QJsonDocument::fromJson(f.readAll()).array()) {
                const QJsonObject o = v.toObject();
                if (o.value(QStringLiteral("ts")).toVariant().toLongLong() == ts) {
                    desktopFile = o.value(QStringLiteral("desktop")).toString();
                    break;
                }
            }
        }
    }
    if (desktopFile.isEmpty()) {
        QMessageBox::warning(this, tr("Export"),
            tr("No desktop image available for the selected generated version "
               "(only a mobile image was generated)."));
        return;
    }
    const QString desktopAbs = m_workingDir.filePath(
        QStringLiteral("stores/storefront/%1").arg(desktopFile));
    if (!QFile::exists(desktopAbs)) {
        QMessageBox::warning(this, tr("Export"),
            tr("Desktop image file is missing on disk: %1").arg(desktopAbs));
        return;
    }

    // Destination: exportRoot/{brand}/{cat1_cat2_...} (skip the second level
    // when the tree selection is just the brand, with nothing underneath it).
    QDir dest(exportRoot);
    if (!dest.mkpath(nodePath.first()) || !dest.cd(nodePath.first())) {
        QMessageBox::warning(this, tr("Export"),
            tr("Could not create folder: %1").arg(dest.filePath(nodePath.first())));
        return;
    }
    if (nodePath.size() > 1) {
        const QString subName = nodePath.mid(1).join(QLatin1Char('_'));
        if (!dest.mkpath(subName) || !dest.cd(subName)) {
            QMessageBox::warning(this, tr("Export"),
                tr("Could not create folder: %1").arg(dest.filePath(subName)));
            return;
        }
    }

    const QString destImagePath = dest.filePath(QFileInfo(desktopAbs).fileName());
    QFile::remove(destImagePath); // QFile::copy refuses to overwrite an existing file
    if (!QFile::copy(desktopAbs, destImagePath)) {
        QMessageBox::warning(this, tr("Export"),
            tr("Failed to copy the image to %1").arg(destImagePath));
        return;
    }

    const QStringList languageLabels = _configuredLanguageLabels();
    const QString asinsPath = dest.filePath(QStringLiteral("ASINS.txt"));
    QFile::remove(asinsPath); // guarantee a clean write, same reasoning as the image above

    QString prompt = tr(
        "Write a UTF-8 text file at the exact absolute path below. Do not create "
        "any other files, and do not print anything else.\n\n"
        "Path: %1\n\n"
        "For each of the following language entries, in this exact order, translate "
        "the category name \"%2\" into that language, phrased as a PLURAL collection "
        "of several products, not a single item (for EN, keep the name unchanged but "
        "still plural). Each entry is labeled \"<language code> (<country codes that "
        "use it>)\" — use the language code/country codes to decide the language, "
        "but write ONLY the entry's exact label text as given (do not translate or "
        "alter the label itself). Then, in that SAME language, write a one-sentence "
        "meta-description, ALSO phrased for the plural collection (not one product): "
        "naturally work in 3-4 relevant SEO keywords for this category, and end the "
        "sentence with a plain, neutral invitation to browse/shop the collection "
        "(e.g. \"discover the full range\", \"available in several sizes and "
        "colors\", \"explore the collection\") — an invitation only, not a claim. "
        "Avoid ANY claim, promise, or subjective/performance adjective that Amazon's "
        "content policies could prohibit or flag: no quality, comfort, durability, "
        "performance, craftsmanship, or superiority adjectives; no price/discount/"
        "deal/value-for-money claims; no medical or health claims; no "
        "unsubstantiated superlatives (\"best\", \"#1\", \"guaranteed\"); no false "
        "urgency or scarcity claims; no comparisons to other sellers or brands; and "
        "no other trust- or desirability-building assertion of any kind — in any "
        "language. Stick to the category itself, its typical occasions/uses, and "
        "the neutral browse invitation. Write each entry as four lines: the label, "
        "the translated (plural) category name, the meta-description sentence, then "
        "a blank line. Entries, in order: %3. Write NOTHING else in the file — no "
        "ASINs, no extra commentary, nothing after the last entry's blank line.")
        .arg(asinsPath, englishName, languageLabels.join(QStringLiteral("; ")));

    ui->buttonExportProducts->setEnabled(false);
    const QString origText = ui->buttonExportProducts->text();
    ui->buttonExportProducts->setText(tr("Exporting…"));

    // The ASIN line is appended by US, never by the CLI: reproducing a long
    // exact token list verbatim is exactly the kind of task LLMs sometimes
    // corrupt (drop/reorder/substitute one ASIN), which is why the file's
    // last ASIN could end up not matching the actual selection. Writing it
    // ourselves from the `asins` QStringList we already built guarantees it's
    // always correct, regardless of what the CLI did with the translations.
    // Two trailing blank lines are added after the ASIN line, so it's easy to
    // select/copy without also grabbing the end of the file.
    const QString asinsLine = asins.join(QLatin1Char(','));
    auto appendAsinsLine = [asinsPath, asinsLine]() {
        QString existing;
        QFile f(asinsPath);
        if (f.exists() && f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            existing = QString::fromUtf8(f.readAll());
            f.close();
        }
        while (existing.endsWith(QLatin1Char('\n')) || existing.endsWith(QLatin1Char('\r')))
            existing.chop(1);
        if (!existing.isEmpty()) existing += QStringLiteral("\n\n");
        existing += asinsLine + QStringLiteral("\n\n\n");

        QSaveFile sf(asinsPath);
        if (sf.open(QIODevice::WriteOnly | QIODevice::Text)) {
            sf.write(existing.toUtf8());
            sf.commit();
        }
    };

    cli->runPromptAsync(prompt, dest.absolutePath(), this,
        [this, asinsPath, destImagePath, origText, cli, appendAsinsLine](CliRunResult result)
    {
        ui->buttonExportProducts->setText(origText);
        ui->buttonExportProducts->setEnabled(!_selectedTableRows().isEmpty());

        // ASINs are guaranteed regardless of translation success — a translation
        // failure shouldn't cost the user the (already-correct) ASIN export.
        appendAsinsLine();

        if (!result.processStarted) {
            QMessageBox::warning(this, tr("Export"),
                tr("Category translations failed — CLI not found: %1.\n"
                   "ASINs were still saved to %2.").arg(cli->getExecutable(), asinsPath));
            return;
        }
        if (result.exitCode != 0) {
            QMessageBox::warning(this, tr("Export"),
                tr("Category translations failed (ASINs were still saved).\n\n%1")
                    .arg(result.errorOutput.trimmed()));
            return;
        }
        QMessageBox::information(this, tr("Export"),
            tr("Exported image and ASINS.txt to:\n%1").arg(QFileInfo(destImagePath).absolutePath()));
    });
}
