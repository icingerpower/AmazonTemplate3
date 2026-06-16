#pragma GCC optimize("O1")
#include "PaneStore.h"
#include "ui_PaneStore.h"
#include "SettingsTable.h"
#include "TableStoreAsin.h"
#include "TreeBrandCategories.h"
#include "AmazonMarketplace.h"

#include <climits>

#include <QClipboard>
#include <QMessageBox>
#include <QRadioButton>
#include <QRegularExpression>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
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
#include <QTimer>
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
    ui->tableViewAsins->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->tableViewAsins->setAlternatingRowColors(true);
    ui->tableViewAsins->setSortingEnabled(false);
    ui->tableViewAsins->verticalHeader()->setDefaultSectionSize(58);

    // Countries chip-bar depends on m_storeModel (sets existence columns).
    _populateCountriesList();

    connect(ui->buttonRetrieve, &QPushButton::clicked, this, [this]() {
        m_retrieveTask = _onRetrieve();
    });
    connect(ui->buttonMerge, &QPushButton::clicked, this, &PaneStore::_onMerge);
    connect(ui->pushButton,      &QPushButton::clicked, this, &PaneStore::_onCopyAsins);
    connect(ui->buttonMoveUp,    &QPushButton::clicked, this, &PaneStore::_onMoveUp);
    connect(ui->buttonMoveDown,  &QPushButton::clicked, this, &PaneStore::_onMoveDown);

    // Enable Up/Down only when a table row is selected
    connect(ui->tableViewAsins->selectionModel(),
            &QItemSelectionModel::selectionChanged,
            this, [this]() {
                const QModelIndexList sel =
                    ui->tableViewAsins->selectionModel()->selectedRows();
                ui->buttonMoveUp->setEnabled(!sel.isEmpty());
                ui->buttonMoveDown->setEnabled(!sel.isEmpty());
            });

    connect(ui->treeViewBrandCategory->selectionModel(),
            &QItemSelectionModel::currentChanged,
            this, [this](const QModelIndex &, const QModelIndex &) {
                _onTreeSelectionChanged();
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
    _loadFromDisk(_marketplaceId());
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
    for (const QJsonValue &v : QJsonDocument::fromJson(f.readAll()).array())
        m_savedOrder.append(v.toString());
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

    m_treeModel->setItems(items);
    m_storeModel->clear();
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

    // Remap: change loser value → winner for the relevant field.
    for (AmazonCatalogApi::StoreItem &item : m_items) {
        QString *field = nullptr;
        switch (depth) {
        case 0: field = &item.brand;    break;
        case 1: field = &item.category; break;
        case 2: field = &item.gender;   break;
        case 3: field = &item.age;      break;
        }
        if (field && *field == rawLoser)
            *field = rawWinner;
    }

    // Rebuild (sales cache preserved intentionally).
    m_asinToItem.clear();
    for (const AmazonCatalogApi::StoreItem &item : m_items)
        m_asinToItem.insert(item.asin, item);
    m_treeModel->setItems(m_items);
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

void PaneStore::_onCopyAsins()
{
    const QModelIndex treeIdx = ui->treeViewBrandCategory->currentIndex();
    const QStringList asins   = m_treeModel->asinsForIndex(treeIdx);
    QGuiApplication::clipboard()->setText(asins.join(QLatin1Char(',')));
}

void PaneStore::_onMoveUp()
{
    const QModelIndexList sel = ui->tableViewAsins->selectionModel()->selectedRows();
    if (sel.isEmpty()) return;
    const int row = sel.first().row();
    if (!m_storeModel->moveRowUp(row)) return;

    // Sync m_savedOrder to the new table order
    m_savedOrder.clear();
    for (int i = 0; i < m_storeModel->rowCount(); ++i)
        m_savedOrder.append(m_storeModel->data(m_storeModel->index(i, TableStoreAsin::ColAsin)).toString());
    _saveOrder();

    // Keep selection on the moved row
    ui->tableViewAsins->selectionModel()->setCurrentIndex(
        m_storeModel->index(row - 1, 0), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}

void PaneStore::_onMoveDown()
{
    const QModelIndexList sel = ui->tableViewAsins->selectionModel()->selectedRows();
    if (sel.isEmpty()) return;
    const int row = sel.first().row();
    if (!m_storeModel->moveRowDown(row)) return;

    m_savedOrder.clear();
    for (int i = 0; i < m_storeModel->rowCount(); ++i)
        m_savedOrder.append(m_storeModel->data(m_storeModel->index(i, TableStoreAsin::ColAsin)).toString());
    _saveOrder();

    ui->tableViewAsins->selectionModel()->setCurrentIndex(
        m_storeModel->index(row + 1, 0), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
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

    for (int i = 0; i < asins.size(); ++i) {
        const QString &asin = asins.at(i);
        AmazonCatalogApi::StoreItem item;
        item.asin      = asin;
        item.sku       = asinToSku.value(asin);
        item.inventory = asinToInventory.value(asin, 0);

        co_await _catalogApi()->fetchListingAttributes(marketplaceId, item.sku, &item, sameRegionMpIds);

        // If brand came back empty, preserve whatever was stored on disk so a transient
        // API failure doesn't erase brand data we previously fetched successfully.
        if (item.brand.isEmpty()) {
            ++emptyBrandCount;
            const AmazonCatalogApi::StoreItem &prev = m_asinToItem.value(asin);
            if (!prev.brand.isEmpty())
                item.brand = prev.brand;
            // Log first few empty-brand items so the user can diagnose root cause.
            if (emptyBrandCount <= 5)
                appendLog(tr("  ⚠ [%1/%2] %3 — brand empty from API (title=%4, kept prev=%5)")
                          .arg(i + 1).arg(total).arg(item.sku, item.title, item.brand));
        }

        items.append(item);

        if (progressBarPtr) progressBarPtr->setValue(i + 1);
        if (statusLabelPtr)
            statusLabelPtr->setText(tr("[%1/%2] %3").arg(i + 1).arg(total).arg(item.sku));
        if ((i + 1) % 20 == 0 || (i + 1) == total)
            appendLog(tr("[%1/%2] %3 — brand=%4 category=%5 gender=%6")
                      .arg(i + 1).arg(total)
                      .arg(item.sku, item.brand, item.category, item.gender));

        if (i + 1 < total) {
            QTimer pause; pause.setSingleShot(true); pause.start(200);
            co_await qCoro(&pause).waitForTimeout();
        }
    }

    if (emptyBrandCount > 0)
        appendLog(tr("⚠ %1/%2 item(s) had empty brand — possible API error or rate limit.")
                  .arg(emptyBrandCount).arg(total));

    _applyItems(items);
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

    QHash<QString, QStringList> colorGroups;
    QStringList colorOrder;
    for (const QString &asin : std::as_const(asins)) {
        const AmazonCatalogApi::StoreItem &it = m_asinToItem.value(asin);
        const QString key = it.color.isEmpty() ? asin : it.color;
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
        unsortedRows.append(row);
    }

    // Apply saved order: new rows (not in m_savedOrder) go first, then known order.
    const QSet<QString> savedSet(m_savedOrder.cbegin(), m_savedOrder.cend());
    QList<TableStoreAsin::Row> newRows, orderedRows;
    newRows.reserve(unsortedRows.size());
    orderedRows.resize(m_savedOrder.size()); // slots, some may stay default-constructed

    QHash<QString, int> savedPos;
    for (int i = 0; i < m_savedOrder.size(); ++i)
        savedPos.insert(m_savedOrder.at(i), i);

    for (const TableStoreAsin::Row &r : std::as_const(unsortedRows)) {
        if (savedSet.contains(r.asin))
            orderedRows[savedPos.value(r.asin)] = r;
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
