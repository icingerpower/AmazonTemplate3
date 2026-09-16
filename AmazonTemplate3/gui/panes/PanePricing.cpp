#pragma GCC optimize("O1")
#include "PanePricing.h"
#include "PricingProgressDialog.h"
#include "SettingsTable.h"
#include "TableCurrencyRates.h"
#include "ui_PanePricing.h"
#include "workingdirectory/WorkingDirectoryManager.h"
#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QButtonGroup>
#include <QCoro/QCoroNetworkReply>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScopeGuard>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QTextDocument>
#include <QTextEdit>

namespace {
class PriceDelegate : public QStyledItemDelegate {
  public:
    explicit PriceDelegate(QObject *parent, bool live = false) : QStyledItemDelegate(parent), m_live(live) {}
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override {
        const auto html = index.data(TableAmazonPricing::RichTextRole);
        if (!html.isValid()) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        QStyleOptionViewItem cell(option);
        initStyleOption(&cell, index);
        cell.text.clear();
        const auto *style = cell.widget ? cell.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &cell, painter, cell.widget);
        QTextDocument document;
        document.setDefaultFont(cell.font);
        document.setDocumentMargin(3);
        document.setHtml(html.toString());
        document.setTextWidth(cell.rect.width());
        QAbstractTextDocumentLayout::PaintContext context;
        context.palette = cell.palette;
        if (cell.state & QStyle::State_Selected)
            context.palette.setColor(QPalette::Text, cell.palette.highlightedText().color());
        painter->save();
        painter->setClipRect(cell.rect);
        painter->translate(cell.rect.topLeft());
        document.documentLayout()->draw(painter, context);
        painter->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        const auto html = index.data(TableAmazonPricing::RichTextRole);
        if (!html.isValid())
            return QStyledItemDelegate::sizeHint(option, index);
        const auto *table = qobject_cast<const QTableView *>(parent());
        const int width = table ? table->columnWidth(index.column()) : 380;
        QTextDocument document;
        document.setDefaultFont(option.font);
        document.setDocumentMargin(3);
        document.setHtml(html.toString());
        document.setTextWidth(qMax(40, width));
        return QSize(width, qMax(58, int(std::ceil(document.size().height())) + 4));
    }
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &) const override {
        auto *edit = new QLineEdit(parent);
        auto *validator = new QDoubleValidator(0.000001, 100000000, 6, edit);
        validator->setNotation(QDoubleValidator::StandardNotation);
        validator->setLocale(QLocale::c());
        edit->setValidator(validator);
        if (m_live) {
            auto *delegate = const_cast<PriceDelegate *>(this);
            connect(edit, &QLineEdit::textEdited, delegate,
                    [delegate, edit] { emit delegate->commitData(edit); });
        }
        return edit;
    }
    void setEditorData(QWidget *editor, const QModelIndex &index) const override {
        if (m_live && editor->property("pricingOriginalSet").toBool())
            return; // Live commits must not reset the cursor or selection while typing.
        QStyledItemDelegate::setEditorData(editor, index);
        if (m_live) {
            editor->setProperty("pricingOriginal", index.data(Qt::EditRole));
            editor->setProperty("pricingOriginalSet", true);
        }
    }
    bool eventFilter(QObject *object, QEvent *event) override {
        if (m_live && event->type() == QEvent::KeyPress &&
            static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
            if (auto *edit = qobject_cast<QLineEdit *>(object)) {
                edit->setText(edit->property("pricingOriginal").toString());
                emit commitData(edit);
            }
        }
        return QStyledItemDelegate::eventFilter(object, event);
    }

  private:
    bool m_live;
};
} // namespace
PanePricing::PanePricing(QWidget *parent)
    : PanePricing(WorkingDirectoryManager::instance()->workingDir(), parent) {}
PanePricing::PanePricing(const QDir &workingDir, QWidget *parent)
    : QWidget(parent), ui(new Ui::PanePricing), m_working(workingDir),
      m_settings(workingDir.filePath("settings.ini"), QSettings::IniFormat),
      m_model(new TableAmazonPricing(this)), m_proxy(new PricingFilterProxy(this)),
      m_prices(new QStandardItemModel(this)), m_status(new QLabel(this)), m_modes(new QButtonGroup(this)) {
    ui->setupUi(this);
    setObjectName("PanePricing");
    ui->comboBoxRegion->addItem(tr("All regions"), QString());
    ui->comboBoxRegion->addItem(tr("Europe"), "Europe");
    ui->comboBoxRegion->addItem(tr("Americas"), "Americas");
    ui->comboBoxRegion->addItem(tr("Asia (cache only)"), "Asia");
    ui->comboBoxRegion->setToolTip(tr("Retrieve, preview and update only checked countries in this region. "
                                      "All regions restores your country selections."));
    TableCurrencyRates rates;
    QSettings legacyRates;
    for (const auto &entry : rates.entries()) {
        Pricing::Market m{entry.marketplaceId, entry.country, entry.currency,
                          entry.country == "US" || entry.country == "CA" || entry.country == "MX" ||
                                  entry.country == "BR"
                              ? "Americas"
                              : "Europe"};
        const QString rateKey = "pricingEditor/rates/" + m.id;
        m.rate = m.currency == "EUR"
                     ? 1
                     : m_settings.value(rateKey, legacyRates.value("pricing/rates/" + m.id + "/rate", -1))
                           .toDouble();
        const bool configured = !m_settings
                                     .value(m.continent == "Europe" ? SettingsTable::KEY_EU_SELLER_ID
                                                                    : SettingsTable::KEY_NA_SELLER_ID)
                                     .toString()
                                     .isEmpty();
        m.enabled = m_settings.value("pricingEditor/markets/" + m.id, configured).toBool();
        m_markets << m;
    }
    // JP is a read-only reference until the pricing workflow supports Far East credentials.
    m_markets << Pricing::Market{"A1VC38T7YXB528",
                                 "JP",
                                 "JPY",
                                 "Asia",
                                 m_settings.value("pricingEditor/rates/A1VC38T7YXB528", -1).toDouble(),
                                 false};
    m_prices->setRowCount(3);
    m_prices->setColumnCount(m_markets.size() + 1);
    m_prices->setVerticalHeaderLabels({tr("New price"), tr("EUR → currency"), tr("Use country")});
    m_prices->setHorizontalHeaderItem(0, new QStandardItem(tr("Default (EUR, required)")));
    for (int col = 0; col < m_prices->columnCount(); ++col) {
        for (int r = 0; r < 3; ++r)
            m_prices->setItem(r, col, new QStandardItem);
        if (col == 0) {
            m_prices->item(0, col)->setToolTip(
                tr("Required EUR default, converted to each country's currency unless overridden."));
            m_prices->item(1, col)->setText("1");
            m_prices->item(1, col)->setEditable(false);
            m_prices->item(2, col)->setEditable(false);
        } else {
            const auto &m = m_markets[col - 1];
            m_prices->setHorizontalHeaderItem(col, new QStandardItem(m.country + " (" + m.currency + ")"));
            m_prices->item(0, col)->setToolTip(
                tr("Optional price in %1. Blank uses the converted default.").arg(m.currency));
            if (m.rate > 0)
                m_prices->item(1, col)->setText(QString::number(m.rate, 'g', 8));
            m_prices->item(1, col)->setToolTip(
                tr("Units of %1 for EUR 1. Missing rates must be filled before updating.").arg(m.currency));
            if (m.currency == "EUR")
                m_prices->item(1, col)->setEditable(false);
            m_prices->item(2, col)->setCheckable(true);
            m_prices->item(2, col)->setEditable(false);
            m_prices->item(2, col)->setCheckState(m.enabled ? Qt::Checked : Qt::Unchecked);
            if (m.country == "JP") {
                m_prices->item(0, col)->setFlags(Qt::ItemIsEnabled);
                m_prices->item(2, col)->setFlags(Qt::ItemIsEnabled);
                m_prices->item(2, col)->setCheckable(false);
                m_prices->item(2, col)->setText(tr("Cache only"));
                m_prices->item(2, col)->setToolTip(
                    tr("JP can supply Main price from cache; live JP pricing is not supported here."));
            }
        }
    }
    ui->tableNewPrices->setModel(m_prices);
    ui->tableNewPrices->setItemDelegate(new PriceDelegate(ui->tableNewPrices, true));
    ui->tableNewPrices->setMaximumHeight(155);
    ui->tableNewPrices->setMinimumHeight(135);
    ui->tableNewPrices->resizeColumnsToContents();
    ui->tableNewPrices->horizontalHeader()->setMinimumSectionSize(110);
    auto *refresh = new QPushButton(tr("Refresh exchange rates"), this);
    refresh->setObjectName("buttonRefreshExchangeRates");
    refresh->setToolTip(tr("Load ECB EUR reference rates. Rates remain editable in the table."));
    ui->horizontalLayout->insertWidget(ui->horizontalLayout->count() - 1, refresh);
    connect(refresh, &QPushButton::clicked, this, [this] {
        if (!m_busy)
            m_task = refreshRates();
    });
    auto *one = new QRadioButton(tr("One price"), this), *continent = new QRadioButton(tr("Continent"), this),
         *all = new QRadioButton(tr("All country"), this);
    one->setObjectName("radioOnePrice");
    continent->setObjectName("radioContinent");
    all->setObjectName("radioAllCountry");
    m_modes->addButton(one, TableAmazonPricing::OnePrice);
    m_modes->addButton(continent, TableAmazonPricing::Continent);
    m_modes->addButton(all, TableAmazonPricing::AllCountry);
    ui->horizontalLayout_5->insertWidget(2, one);
    ui->horizontalLayout_5->insertWidget(3, continent);
    ui->horizontalLayout_5->insertWidget(4, all);
    auto *view = new QPushButton(tr("Preview all prices"), this);
    view->setObjectName("buttonViewAllPrices");
    view->setToolTip(
        tr("Review current and proposed prices per country. This opens a preview; nothing is submitted."));
    ui->pushButtonUpdatePrices->setToolTip(tr("Submit the eligible changes after confirmation. Review them "
                                              "first in Changes to apply or Preview all prices."));
    ui->horizontalLayout_5->insertWidget(5, view);
    connect(view, &QPushButton::clicked, this, &PanePricing::viewAllPrices);
    int mode = qBound(0, m_settings.value("pricingEditor/mode", 0).toInt(), 2);
    m_modes->button(mode)->setChecked(true);
    m_model->setMode(TableAmazonPricing::Mode(mode));
    connect(m_modes, &QButtonGroup::idClicked, this, [this](int mode) {
        m_loading = true;
        m_model->setMode(TableAmazonPricing::Mode(mode));
        restoreSort();
        m_loading = false;
        m_settings.setValue("pricingEditor/mode", mode);
    });
    for (auto *spin : {ui->spinBoxPriceMin, ui->spinBoxPriceMax}) {
        spin->setRange(0, 100000000);
        spin->setDecimals(2);
        spin->setSpecialValueText(tr("Any"));
        spin->setSuffix(" EUR");
    }
    ui->spinBoxInvLeftDays->setRange(0, 1000000);
    ui->spinBoxInvLeftDays->setSpecialValueText(tr("Any"));
    ui->spinBoxInvLeftDays->setToolTip(
        tr("Include at least this many estimated inventory days. Zero sales means zero days."));
    ui->lineEditSizeFrom->setPlaceholderText(tr("FR size / XS"));
    ui->lineEditSizeTo->setPlaceholderText(tr("FR size / XL"));
    m_model->setMarkets(m_markets);
    m_proxy->setSourceModel(m_model);
    ui->tableViewPricesToUpdate->setModel(m_proxy);
    ui->tableViewPricesToUpdate->setSortingEnabled(true);
    ui->tableViewPricesToUpdate->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableViewPricesToUpdate->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ui->tableViewPricesToUpdate->setItemDelegate(new PriceDelegate(ui->tableViewPricesToUpdate));
    ui->tableViewPricesToUpdate->verticalHeader()->setMinimumSectionSize(58);
    ui->tableViewPricesToUpdate->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    connect(ui->tableViewPricesToUpdate->horizontalHeader(), &QHeaderView::sectionResized,
            ui->tableViewPricesToUpdate, &QTableView::resizeRowsToContents);
    ui->tableViewPricesToUpdate->setColumnWidth(TableAmazonPricing::Title, 300);
    ui->tableViewPricesToUpdate->setColumnWidth(TableAmazonPricing::Sku, 180);
    ui->verticalLayout_2->addWidget(m_status);
    m_status->setWordWrap(true);
    auto *legend =
        new QLabel(tr("Preview updates automatically when you edit prices or select a direction. Changes to "
                      "apply: ↑ green = increase · ↓ red = decrease. "
                      "Hover for current → new local prices; blue New Price cells are manual edits."),
                   this);
    legend->setWordWrap(true);
    ui->verticalLayout_2->insertWidget(ui->verticalLayout_2->indexOf(ui->tableViewPricesToUpdate), legend);
    connect(ui->tableViewPricesToUpdate->horizontalHeader(), &QHeaderView::sortIndicatorChanged, this,
            [this](int column, Qt::SortOrder order) {
                if (m_loading)
                    return;
                const QString prefix = "pricingEditor/sort/" + QString::number(m_model->mode()) + '/';
                m_settings.setValue(prefix + "column", m_model->columnKey(column));
                m_settings.setValue(prefix + "order", int(order));
            });
    m_filters = QJsonDocument::fromJson(m_settings.value("pricingEditor/filters").toByteArray()).array();
    if (m_filters.isEmpty())
        m_filters.append(QJsonObject{{"name", "Default"}});
    // Default is identified by position, not a user-editable translated label.
    m_filters[0] = [&] {
        auto f = m_filters[0].toObject();
        f["name"] = "Default";
        return f;
    }();
    for (const auto &f : m_filters)
        ui->comboBoxFilters->addItem(f.toObject().value("name").toString());
    int selected = qBound(0, m_settings.value("pricingEditor/filterIndex", 0).toInt(), m_filters.size() - 1);
    ui->comboBoxFilters->setCurrentIndex(selected);
    m_repository = std::make_unique<AmazonPricingRepository>(m_working, credentials(false));
    m_model->setRows(m_repository->cachedRows(m_markets));
    rebuildChoices();
    loadFilter(selected);
    connect(ui->comboBoxFilters, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &PanePricing::loadFilter);
    connect(m_prices, &QStandardItemModel::itemChanged, this, [this] { changed(); });
    connect(ui->comboBoxProductType, &ProductTypeSelector::selectionChanged, this, &PanePricing::changed);
    for (auto *edit : {ui->lineEditSkuToContain, ui->lineEditTitle, ui->lineEditSizeFrom, ui->lineEditSizeTo})
        connect(edit, &QLineEdit::textChanged, this, [this] { changed(); });
    for (auto *combo : {ui->comboBoxBrand, ui->comboBoxRegion})
        connect(combo, &QComboBox::currentTextChanged, this, [this] { changed(); });
    for (auto *spin : {ui->spinBoxPriceMin, ui->spinBoxPriceMax})
        connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this] { changed(); });
    connect(ui->spinBoxInvLeftDays, qOverload<int>(&QSpinBox::valueChanged), this, [this] { changed(); });
    for (auto *radio : {ui->radioIncreaseOnly, ui->radioDecreaseOnly, ui->radioIncreaseDecrease})
        connect(radio, &QRadioButton::toggled, this, [this] { changed(); });
    connect(ui->buttonAddNewFilter, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const QString name =
            QInputDialog::getText(this, tr("New filter"), tr("Name"), QLineEdit::Normal, {}, &ok).trimmed();
        if (!ok || name.isEmpty())
            return;
        if (ui->comboBoxFilters->findText(name) >= 0) {
            QMessageBox::information(this, tr("Filter"), tr("That name already exists."));
            return;
        }
        saveFilters();
        auto f = m_filters[ui->comboBoxFilters->currentIndex()].toObject();
        f["name"] = name;
        m_filters.append(f);
        ui->comboBoxFilters->addItem(name);
        ui->comboBoxFilters->setCurrentIndex(m_filters.size() - 1);
        saveFilters();
    });
    connect(ui->buttonEditFilters, &QPushButton::clicked, this, [this] {
        QMenu menu;
        auto *rename = menu.addAction(tr("Rename selected filter"));
        auto *remove = menu.addAction(tr("Delete selected filter"));
        const int selected = ui->comboBoxFilters->currentIndex();
        rename->setEnabled(selected > 0);
        remove->setEnabled(selected > 0);
        auto *action =
            menu.exec(ui->buttonEditFilters->mapToGlobal(QPoint(0, ui->buttonEditFilters->height())));
        if (!action)
            return;
        if (action == rename) {
            bool ok = false;
            QString name = QInputDialog::getText(this, tr("Rename filter"), tr("Name"), QLineEdit::Normal,
                                                 ui->comboBoxFilters->currentText(), &ok)
                               .trimmed();
            if (!ok || name.isEmpty() || ui->comboBoxFilters->findText(name) >= 0)
                return;
            auto f = m_filters[selected].toObject();
            f["name"] = name;
            m_filters[selected] = f;
            ui->comboBoxFilters->setItemText(selected, name);
        } else if (action == remove) {
            const QSignalBlocker selectionSignals(ui->comboBoxFilters);
            m_loading = true;
            m_filters.removeAt(selected);
            ui->comboBoxFilters->removeItem(selected);
            ui->comboBoxFilters->setCurrentIndex(0);
            m_loading = false;
            loadFilter(0);
        }
        saveFilters();
    });
    connect(ui->buttonPriceUpdateReset, &QPushButton::clicked, this, [this] {
        QSet<QString> skus;
        for (const auto &i : ui->tableViewPricesToUpdate->selectionModel()->selectedRows())
            skus.insert(i.data(TableAmazonPricing::RowKeyRole).toString());
        m_model->resetRows(skus);
    });
    connect(ui->buttonRetrieveData, &QPushButton::clicked, this, [this] {
        if (!m_busy)
            m_task = retrieve();
    });
    connect(ui->pushButtonUpdatePrices, &QPushButton::clicked, this, [this] {
        if (!m_busy)
            m_task = update();
    });
    const auto splitter = m_settings.value("pricingEditor/splitter").toByteArray();
    if (!splitter.isEmpty())
        ui->splitter->restoreState(splitter);
    else
        ui->splitter->setSizes({210, 500});
    connect(ui->splitter, &QSplitter::splitterMoved, this,
            [this] { m_settings.setValue("pricingEditor/splitter", ui->splitter->saveState()); });
    m_loading = false;
    changed();
    restoreSort();
}
PanePricing::~PanePricing() {
    if (m_cancelled)
        *m_cancelled = true;
    m_task = {};
    delete ui;
}
AmazonPricingRepository::Credentials PanePricing::credentials(bool secrets) const {
    AmazonPricingRepository::Credentials c;
    c.sellerEu = m_settings.value(SettingsTable::KEY_EU_SELLER_ID).toString();
    c.sellerNa = m_settings.value(SettingsTable::KEY_NA_SELLER_ID).toString();
    if (secrets) {
        const auto *s = SettingsTable::instance();
        c.client = s->value(SettingsTable::KEY_LWA_CLIENT_ID);
        c.secret = s->value(SettingsTable::KEY_LWA_CLIENT_SECRET);
        c.tokenEu = s->value(SettingsTable::KEY_EU_LWA_REFRESH_TOKEN);
        c.tokenNa = s->value(SettingsTable::KEY_NA_LWA_REFRESH_TOKEN);
    }
    return c;
}
PricingFilterProxy::Filter PanePricing::filter() const {
    PricingFilterProxy::Filter f;
    f.region = ui->comboBoxRegion->currentData().toString();
    f.sku = ui->lineEditSkuToContain->text().trimmed();
    f.title = ui->lineEditTitle->text().trimmed();
    f.brand = ui->comboBoxBrand->currentData().toString();
    f.productTypes = ui->comboBoxProductType->selectedValues();
    f.sizeFrom = ui->lineEditSizeFrom->text().trimmed();
    f.sizeTo = ui->lineEditSizeTo->text().trimmed();
    f.minPrice = ui->spinBoxPriceMin->value();
    f.maxPrice = ui->spinBoxPriceMax->value();
    f.minDays = ui->spinBoxInvLeftDays->value();
    return f;
}
QList<Pricing::Market> PanePricing::marketsForRegion(bool keepMetadata) const {
    QList<Pricing::Market> result;
    const QString region = ui->comboBoxRegion->currentData().toString();
    for (auto m : m_markets) {
        if (!region.isEmpty() && m.continent != region) {
            if (!keepMetadata)
                continue;
            m.enabled = false;
        }
        result << m;
    }
    return result;
}
bool PanePricing::validRules() const {
    if (!Pricing::validPrice(m_default))
        return false;
    const auto f = filter();
    if (f.maxPrice > 0 && f.minPrice > f.maxPrice)
        return false;
    for (const auto &bound : {f.sizeFrom, f.sizeTo})
        if (!bound.isEmpty() && !std::isfinite(Pricing::sizeOrder(bound)))
            return false;
    bool enabled = false;
    for (const auto &m : marketsForRegion())
        if (m.enabled) {
            enabled = true;
            if (!Pricing::validPrice(m.rate))
                return false;
        }
    for (int col = 1; col < m_prices->columnCount(); ++col) {
        if (!m_region.isEmpty() && m_markets[col - 1].continent != m_region)
            continue;
        bool ok = false;
        auto text = m_prices->item(0, col)->text().trimmed();
        double n = text.toDouble(&ok);
        if (!text.isEmpty() && (!ok || !Pricing::validPrice(n)))
            return false;
    }
    return enabled;
}
void PanePricing::changed() {
    if (m_loading)
        return;
    const QSignalBlocker priceSignals(m_prices);
    bool ok = false;
    m_default = m_prices->item(0, 0)->text().toDouble(&ok);
    if (!ok)
        m_default = -1;
    m_overrides.clear();
    const QString region = ui->comboBoxRegion->currentData().toString();
    bool marketsChanged = region != m_region, selectionChanged = region != m_region;
    m_region = region;
    for (int i = 0; i < m_markets.size(); ++i) {
        auto &m = m_markets[i];
        ui->tableNewPrices->setColumnHidden(i + 1, !m_region.isEmpty() && m.continent != m_region);
        double rate = m_prices->item(1, i + 1)->text().toDouble(&ok);
        if (!ok || !Pricing::validPrice(rate))
            rate = -1;
        bool enabled = m_prices->item(2, i + 1)->checkState() == Qt::Checked;
        selectionChanged |= m.enabled != enabled;
        marketsChanged |= m.rate != rate || m.enabled != enabled;
        m.rate = rate;
        m.enabled = enabled;
        const double price = m_prices->item(0, i + 1)->text().toDouble(&ok);
        if (ok && Pricing::validPrice(price))
            m_overrides[m.id] = price;
        m_settings.setValue("pricingEditor/rates/" + m.id, m.rate);
        m_settings.setValue("pricingEditor/markets/" + m.id, m.enabled);
    }
    if (marketsChanged) {
        m_loading = true;
        m_model->setMarkets(marketsForRegion());
        if (selectionChanged && m_repository)
            m_model->setRows(m_repository->cachedRows(marketsForRegion(true)));
        restoreSort();
        m_loading = false;
    }
    m_direction = ui->radioIncreaseOnly->isChecked()   ? Pricing::Direction::Increase
                  : ui->radioDecreaseOnly->isChecked() ? Pricing::Direction::Decrease
                                                       : Pricing::Direction::Both;
    m_model->setRules(m_default, m_overrides, m_direction);
    m_proxy->setFilter(filter());
    saveFilters();
    const bool valid = validRules();
    ui->pushButtonUpdatePrices->setEnabled(valid && !m_busy);
    m_prices->item(0, 0)->setBackground(Pricing::validPrice(m_default) ? QBrush()
                                                                       : QBrush(QColor("#6b2828")));
    m_status->setText(
        valid ? tr("%1 / %2 SKUs · Cached observations; Retrieve data fills missing or expired values. "
                   "Grouped prices are in EUR; country prices use local currency.")
                    .arg(m_proxy->rowCount())
                    .arg(m_model->rowCount())
              : tr("Enter a positive default price and exchange rates for enabled countries. Check price and "
                   "size bounds. Retrieve data is available without a default price."));
    emit previewChanged();
}
void PanePricing::restoreSort() {
    for (int c = 0; c < m_model->columnCount(); ++c) {
        const int width = c == TableAmazonPricing::Image                                          ? 58
                          : c == TableAmazonPricing::MainPrice                                    ? 235
                          : c == TableAmazonPricing::AllPrice                                     ? 180
                          : c == TableAmazonPricing::Changes                                      ? 380
                          : c == TableAmazonPricing::Title                                        ? 240
                          : c == TableAmazonPricing::Sku                                          ? 150
                          : c == TableAmazonPricing::Sales90                                      ? 165
                          : c >= TableAmazonPricing::FixedCount && c < m_model->columnCount() - 2 ? 180
                                                                                                  : 130;
        ui->tableViewPricesToUpdate->setColumnWidth(c, width);
    }
    const QString prefix = "pricingEditor/sort/" + QString::number(m_model->mode()) + '/';
    const bool was = m_loading;
    m_loading = true;
    ui->tableViewPricesToUpdate->sortByColumn(
        m_model->columnForKey(m_settings.value(prefix + "column", "sku").toString()),
        m_settings.value(prefix + "order", 0).toInt() == 1 ? Qt::DescendingOrder : Qt::AscendingOrder);
    m_loading = was;
}
void PanePricing::saveFilters() {
    if (m_loading)
        return;
    const int i = ui->comboBoxFilters->currentIndex();
    if (i < 0 || i >= m_filters.size())
        return;
    const auto f = filter();
    auto o = m_filters[i].toObject();
    o["region"] = f.region;
    o["sku"] = f.sku;
    o["title"] = f.title;
    o["brand"] = f.brand;
    o.remove("type"); // migrate legacy single-choice presets on save
    o["types"] = QJsonArray::fromStringList(f.productTypes);
    o["from"] = f.sizeFrom;
    o["to"] = f.sizeTo;
    o["min"] = f.minPrice;
    o["max"] = f.maxPrice;
    o["days"] = f.minDays;
    o["direction"] = int(m_direction);
    o["default"] = m_prices->item(0, 0)->text();
    QJsonObject overrides;
    for (int j = 0; j < m_markets.size(); ++j)
        overrides[m_markets[j].id] = m_prices->item(0, j + 1)->text();
    o["overrides"] = overrides;
    m_filters[i] = o;
    m_settings.setValue("pricingEditor/filters", QJsonDocument(m_filters).toJson(QJsonDocument::Compact));
    m_settings.setValue("pricingEditor/filterIndex", i);
}
void PanePricing::loadFilter(int i) {
    if (i < 0 || i >= m_filters.size())
        return;
    m_loading = true;
    const auto o = m_filters[i].toObject();
    ui->comboBoxRegion->setCurrentIndex(qMax(0, ui->comboBoxRegion->findData(o.value("region").toString())));
    ui->lineEditSkuToContain->setText(o.value("sku").toString());
    ui->lineEditTitle->setText(o.value("title").toString());
    ui->lineEditSizeFrom->setText(o.value("from").toString());
    ui->lineEditSizeTo->setText(o.value("to").toString());
    auto select = [](QComboBox *combo, const QString &text) {
        int idx = combo->findData(text);
        if (idx < 0 && !text.isEmpty()) {
            combo->addItem(text, text);
            idx = combo->count() - 1;
        }
        combo->setCurrentIndex(qMax(0, idx));
    };
    select(ui->comboBoxBrand, o.value("brand").toString());
    QStringList types;
    if (o.contains("types")) {
        for (const auto &value : o.value("types").toArray())
            if (value.isString() && !value.toString().isEmpty())
                types << value.toString();
    } else if (!o.value("type").toString().isEmpty()) {
        types << o.value("type").toString();
    }
    ui->comboBoxProductType->setSelectedValues(types);
    ui->spinBoxPriceMin->setValue(o.value("min").toDouble());
    ui->spinBoxPriceMax->setValue(o.value("max").toDouble());
    ui->spinBoxInvLeftDays->setValue(o.value("days").toInt());
    int direction = o.value("direction").toInt();
    ui->radioIncreaseOnly->setChecked(direction == 0);
    ui->radioDecreaseOnly->setChecked(direction == 1);
    ui->radioIncreaseDecrease->setChecked(direction == 2);
    m_prices->item(0, 0)->setText(o.value("default").toString());
    for (int j = 0; j < m_markets.size(); ++j)
        m_prices->item(0, j + 1)->setText(o.value("overrides").toObject().value(m_markets[j].id).toString());
    m_loading = false;
    changed();
}
void PanePricing::rebuildChoices() {
    QSet<QString> brands, types;
    for (const auto &r : m_model->rows()) {
        if (!r.brand.isEmpty())
            brands.insert(r.brand);
        if (!r.productType.isEmpty())
            types.insert(r.productType);
    }
    auto fill = [](QComboBox *combo, const QSet<QString> &choices) {
        QSignalBlocker b(combo);
        const QString selected = combo->currentData().toString();
        combo->clear();
        combo->addItem(QObject::tr("All"), QString());
        auto names = choices.values();
        names.sort();
        for (const auto &name : names)
            combo->addItem(name, name);
        if (!selected.isEmpty() && combo->findData(selected) < 0)
            combo->addItem(selected, selected);
        combo->setCurrentIndex(qMax(0, combo->findData(selected)));
    };
    fill(ui->comboBoxBrand, brands);
    ui->comboBoxProductType->setChoices(types);
}
void PanePricing::viewAllPrices() {
    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("Preview current and new prices — all countries"));
    dialog->resize(1250, 650);
    auto *layout = new QVBoxLayout(dialog);
    auto *table = new QTableView(dialog);
    table->setObjectName("tablePreviewAllPrices");
    auto *model = new TableAmazonPricing(dialog);
    model->setMarkets(marketsForRegion());
    model->setMode(TableAmazonPricing::AllCountry);
    model->setRows(m_model->rows());
    model->setRules(m_default, m_overrides, m_direction);
    auto *proxy = new PricingFilterProxy(dialog);
    proxy->setSourceModel(model);
    proxy->setFilter(filter());
    auto refreshPreview = [this, model, proxy] {
        model->setMarkets(marketsForRegion());
        model->setRows(m_model->rows(), false);
        model->setRules(m_default, m_overrides, m_direction);
        proxy->setFilter(filter());
    };
    connect(this, &PanePricing::previewChanged, dialog, refreshPreview);
    // Manual edits, resets and fetched rows can change independently of the top controls.
    connect(m_model, &QAbstractItemModel::dataChanged, dialog, refreshPreview);
    connect(m_model, &QAbstractItemModel::modelReset, dialog, refreshPreview);
    connect(m_model, &QAbstractItemModel::rowsInserted, dialog, refreshPreview);
    table->setModel(proxy);
    table->setSortingEnabled(true);
    table->sortByColumn(TableAmazonPricing::Sku, Qt::AscendingOrder);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setColumnWidth(TableAmazonPricing::Title, 240);
    table->setColumnWidth(TableAmazonPricing::MainPrice, 235);
    table->setColumnWidth(TableAmazonPricing::AllPrice, 180);
    table->setColumnWidth(TableAmazonPricing::Changes, 380);
    table->setItemDelegate(new PriceDelegate(table));
    table->verticalHeader()->setMinimumSectionSize(58);
    table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    connect(table->horizontalHeader(), &QHeaderView::sectionResized, table,
            &QTableView::resizeRowsToContents);
    layout->addWidget(table);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    layout->addWidget(buttons);
    dialog->show();
}
QCoro::Task<void> PanePricing::retrieve() {
    m_busy = true;
    setEnabled(false);
    m_cancelled = std::make_shared<bool>(false);
    auto guard = qScopeGuard([this] {
        m_busy = false;
        setEnabled(true);
        rebuildChoices();
        changed();
    });
    m_repository = std::make_unique<AmazonPricingRepository>(m_working, credentials(true));
    QPointer<PricingProgressDialog> dialog = new PricingProgressDialog(this, m_cancelled);
    dialog->setEnabled(true);
    dialog->show();
    auto log = [dialog](const QString &text) {
        if (dialog)
            dialog->appendLog(text);
    };
    QString failure;
    try {
        co_await m_repository->retrieve(
            marketsForRegion(), filter(), m_cancelled, log,
            [this](const auto &row) { m_model->updateRow(row); },
            [dialog](const QString &phase, int done, int total, const QString &detail) {
                if (dialog)
                    dialog->progress(phase, done, total, detail);
            });
    } catch (const std::exception &e) {
        failure = QString::fromUtf8(e.what());
    } catch (...) {
        failure = tr("Unexpected retrieval error");
    }
    if (dialog) {
        const QString result =
            *m_cancelled         ? tr("Retrieval cancelled. Completed cache records were kept.")
            : !failure.isEmpty() ? tr("Retrieval failed: %1. Completed cache records were kept.").arg(failure)
                                 : tr("Retrieval finished. See the log for any unavailable data.");
        dialog->finish(result, !*m_cancelled && failure.isEmpty());
    }
}

QList<AmazonPricingRepository::Update> PanePricing::pendingUpdates() const {
    if (!validRules())
        return {};
    QList<AmazonPricingRepository::Update> updates;
    const auto &activeMarkets = m_model->markets();
    for (int r = 0; r < m_proxy->rowCount(); ++r) {
        const int source = m_proxy->mapToSource(m_proxy->index(r, 0)).row();
        const auto &row = m_model->rows()[source];
        for (int j = 0; j < activeMarkets.size(); ++j) {
            double proposed = m_model->proposed(row, j);
            if (proposed <= 0)
                continue;
            const auto &m = activeMarkets[j];
            const auto l = row.listings.value(m.id);
            updates << AmazonPricingRepository::Update{row.sku,       m.id,    m.currency,
                                                       l.productType, l.price, proposed};
        }
    }
    return updates;
}
QCoro::Task<void> PanePricing::update() {
    if (!validRules())
        co_return;
    const auto updates = pendingUpdates();
    if (updates.isEmpty()) {
        QMessageBox::information(this, tr("Update prices"), tr("No visible prices qualify for an update."));
        co_return;
    }
    QMessageBox confirmation(
        QMessageBox::Question, tr("Update Amazon prices"),
        tr("Submit %1 price changes for the currently visible rows? Region: %2. Each current price "
           "will be checked again before submitting. Show Details lists every SKU, country and old → new "
           "price. "
           "Cancel during the run stops further work but cannot undo changes already sent.")
            .arg(updates.size())
            .arg(ui->comboBoxRegion->currentText()),
        QMessageBox::Yes | QMessageBox::No, this);
    QStringList recap;
    for (const auto &u : updates)
        recap << AmazonPricingRepository::describeUpdate(u);
    confirmation.setDetailedText(recap.join('\n'));
    confirmation.setDefaultButton(QMessageBox::No);
    if (confirmation.exec() != QMessageBox::Yes)
        co_return;
    m_busy = true;
    setEnabled(false);
    m_cancelled = std::make_shared<bool>(false);
    auto guard = qScopeGuard([this] {
        m_busy = false;
        setEnabled(true);
        changed();
    });
    m_repository = std::make_unique<AmazonPricingRepository>(m_working, credentials(true));
    QPointer<PricingProgressDialog> dialog = new PricingProgressDialog(this, m_cancelled, true);
    dialog->setEnabled(true);
    dialog->show();
    auto log = [dialog](const QString &s) {
        if (dialog)
            dialog->appendLog(s);
    };
    int completed = 0;
    bool failed = false;
    try {
        co_await m_repository->update(updates, m_cancelled, log,
                                      [this, dialog, &completed, &updates](const auto &u, bool success) {
                                          if (success)
                                              m_model->resetMarket(u.sku, u.marketplace);
                                          ++completed;
                                          if (dialog)
                                              dialog->progress(tr("Price updates"), completed, updates.size(),
                                                               AmazonPricingRepository::describeUpdate(u));
                                      });
    } catch (...) {
        failed = true;
        log(tr("Update stopped unexpectedly. Check the per-row submission log and live prices before "
               "retrying."));
    }
    if (dialog)
        dialog->finish(
            failed ? tr("Update interrupted. Review the log.")
            : *m_cancelled
                ? tr("Update cancelled. Review the log for changes already sent.")
                : tr("Update run finished. Review submitted, skipped and failed entries in the log."),
            !failed && !*m_cancelled);
}
QCoro::Task<void> PanePricing::refreshRates() {
    m_busy = true;
    setEnabled(false);
    auto guard = qScopeGuard([this] {
        m_busy = false;
        setEnabled(true);
        changed();
    });
    QNetworkAccessManager network;
    QNetworkRequest request(QUrl("https://www.ecb.europa.eu/stats/eurofxref/eurofxref-daily.xml"));
    request.setTransferTimeout(30000);
    auto *reply = network.get(request);
    co_await qCoro(reply).waitForFinished();
    const auto bytes = reply->readAll();
    const bool success = reply->error() == QNetworkReply::NoError;
    reply->deleteLater();
    QDate date;
    const auto rates = Pricing::parseEuroRates(bytes, &date);
    if (!success || rates.isEmpty()) {
        QMessageBox::warning(this, tr("Exchange rates"),
                             tr("Could not retrieve valid exchange rates. Existing values were kept."));
        co_return;
    }
    m_loading = true;
    for (int i = 0; i < m_markets.size(); ++i)
        if (rates.contains(m_markets[i].currency))
            m_prices->item(1, i + 1)->setText(QString::number(rates.value(m_markets[i].currency), 'g', 8));
    m_settings.setValue("pricingEditor/ratesDate", date.toString(Qt::ISODate));
    m_loading = false;
    ui->tableNewPrices->setToolTip(tr("ECB EUR reference rates dated %1. Country overrides and All country "
                                      "prices are in local currency.")
                                       .arg(date.toString(Qt::ISODate)));
}
