#include "apis/AmazonInventoryApi.h"
#include "gui/panes/PanePricing.h"
#include "gui/panes/ProductTypeSelector.h"
#include "pricing/AmazonDataCache.h"
#include "pricing/TableAmazonPricing.h"
#include <QBrush>
#include <QComboBox>
#include <QFile>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QStandardItemModel>
#include <QTableView>
#include <QTemporaryDir>
#include <QtTest>

class PricingEditorTests : public QObject {
    Q_OBJECT
  private:
    QList<Pricing::Market> markets() const {
        return {{"FR", "FR", "EUR", "Europe", 1, true},
                {"DE", "DE", "EUR", "Europe", 1, true},
                {"US", "US", "USD", "Americas", 2, true}};
    }
    Pricing::Row row(QString sku = "sku-a") const {
        Pricing::Row r;
        r.sku = sku;
        r.asin = "B000000001";
        r.titleFr = "Titre français";
        r.titleEn = "English title";
        r.sizeFr = "M";
        r.brand = "Brand";
        r.productType = "DRESS";
        for (const auto &m : markets()) {
            Pricing::Listing l;
            l.exists = true;
            l.price = m.country == "US" ? 60 : 20;
            l.productType = "DRESS";
            r.listings[m.id] = l;
        }
        r.available = 10;
        r.sales90 = 5;
        return r;
    }
    void write(const QString &path, const QJsonArray &array) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(array).toJson());
    }
  private slots:
    void priceEditorCommitsAndRefreshes();
    void modesAndConversion();
    void multipleProductTypesMatchAny();
    void productTypePopupAndPresetMigration();
    void mainPricePriority();
    void regionScopesPricesAndPersists();
    void reviewOnlyEligibleChanges();
    void directionAndManualColors();
    void mandatoryDefaultAndReset();
    void anyMarketplaceFilterAndZeroSales();
    void sortingUsesNumbersAndStableSku();
    void frenchAndLetterSizes();
    void cacheIdentityAndFreshness();
    void cacheMergeAndCorruption();
    void legacyImportPreservesEverySku();
    void cachePriceAndStatsScope();
    void widgetPersistsSortingAndPresets();
    void exchangeRateParsing();
    void salesMissingIsNotZero();
};
void PricingEditorTests::priceEditorCommitsAndRefreshes() {
    QTemporaryDir dir;
    QSettings settings(dir.path() + "/settings.ini", QSettings::IniFormat);
    settings.setValue("pricingEditor/markets/A13V1IB3VIYZZH", true);
    settings.sync();
    PanePricing pane{QDir(dir.path())};
    pane.resize(1500, 850);
    pane.show();
    auto *region = pane.findChild<QComboBox *>("comboBoxRegion");
    region->setCurrentIndex(region->findData("Europe"));
    auto *model = pane.findChild<TableAmazonPricing *>();
    auto sample = row();
    sample.listings.clear();
    for (const auto &market : model->markets()) {
        auto &listing = sample.listings[market.id];
        listing.exists = true;
        listing.price = 20;
        listing.productType = "SHOES";
    }
    model->setRows({sample});
    auto *prices = pane.findChild<QTableView *>("tableNewPrices");
    auto *table = pane.findChild<QTableView *>("tableViewPricesToUpdate");
    pane.findChild<QPushButton *>("buttonViewAllPrices")->click();
    auto *preview = pane.findChild<QTableView *>("tablePreviewAllPrices");
    QVERIFY(preview);
    pane.activateWindow();
    prices->setFocus();
    QCoreApplication::processEvents();
    for (const QString value : {QString("25.99"), QString("35.99")}) {
        prices->setCurrentIndex(prices->model()->index(0, 0));
        prices->edit(prices->currentIndex());
        auto *editor = prices->findChild<QLineEdit *>();
        QVERIFY(editor);
        editor->selectAll();
        QTest::keyClicks(editor, value);
        // Both views must refresh before Enter or focus loss.
        QTRY_COMPARE(prices->currentIndex().data().toString(), value);
        QTRY_VERIFY_WITH_TIMEOUT(
            table->model()->index(0, TableAmazonPricing::Changes).data().toString().contains(value), 500);
        QTRY_VERIFY_WITH_TIMEOUT(
            preview->model()->index(0, TableAmazonPricing::Changes).data().toString().contains(value), 500);
        QTest::keyClick(editor, Qt::Key_Return);
        QCOMPARE(model->index(0, model->columnForKey("all:new")).data().toString(), value);
        QTRY_VERIFY_WITH_TIMEOUT(prices->findChild<QLineEdit *>() == nullptr, 500);
    }
    // Escape restores the original price and both previews after a live edit.
    prices->edit(prices->model()->index(0, 0));
    auto *editor = prices->findChild<QLineEdit *>();
    QVERIFY(editor);
    editor->selectAll();
    QTest::keyClicks(editor, "45.99");
    QTRY_VERIFY(preview->model()->index(0, TableAmazonPricing::Changes).data().toString().contains("45.99"));
    QTest::keyClick(editor, Qt::Key_Escape);
    QTRY_COMPARE(prices->model()->index(0, 0).data().toString(), QString("35.99"));
    QTRY_VERIFY(preview->model()->index(0, TableAmazonPricing::Changes).data().toString().contains("35.99"));
    // Snapshot refreshes must replace old manual edits and resets, not preserve them.
    QVERIFY(model->setData(model->index(0, model->columnForKey("all:new")), 48.99));
    QVERIFY(preview->model()->index(0, TableAmazonPricing::Changes).data().toString().contains("48.99"));
    QVERIFY(model->setData(model->index(0, model->columnForKey("all:new")), 58.99));
    QVERIFY(preview->model()->index(0, TableAmazonPricing::Changes).data().toString().contains("58.99"));
    model->resetRows({sample.sku});
    QCOMPARE(preview->model()->index(0, TableAmazonPricing::Changes).data().toString(),
             QString("No changes"));
    QVERIFY(model->setData(model->index(0, model->columnForKey("all:new")), ""));
    QVERIFY(preview->model()->index(0, TableAmazonPricing::Changes).data().toString().contains("35.99"));
    pane.findChild<QLineEdit *>("lineEditSkuToContain")->setText("no-such-sku");
    QCOMPARE(preview->model()->rowCount(), 0);
}
void PricingEditorTests::multipleProductTypesMatchAny() {
    PricingFilterProxy proxy;
    PricingFilterProxy::Filter filter;
    filter.productTypes = {"SHOES", "SANDAL", "BOOT"};
    filter.brand = "Brand";
    proxy.setFilter(filter);
    auto sample = row();
    for (const auto &type : filter.productTypes) {
        sample.productType = type;
        QVERIFY(proxy.accepts(sample, markets()));
        QVERIFY(proxy.mayMatchMetadata(sample));
    }
    sample.brand = "Other";
    QVERIFY(!proxy.accepts(sample, markets()));
    sample.brand = "Brand";
    sample.productType = "DRESS";
    QVERIFY(!proxy.accepts(sample, markets()));
    QVERIFY(!proxy.mayMatchMetadata(sample));
    sample.productType.clear();
    QVERIFY(!proxy.accepts(sample, markets()));
    QVERIFY(proxy.mayMatchMetadata(sample)); // missing metadata still gets hydrated
    filter.productTypes.clear();
    proxy.setFilter(filter);
    QVERIFY(proxy.accepts(sample, markets()));
}
void PricingEditorTests::productTypePopupAndPresetMigration() {
    QTemporaryDir dir;
    QSettings settings(dir.path() + "/settings.ini", QSettings::IniFormat);
    settings.setValue(
        "pricingEditor/filters",
        QJsonDocument(QJsonArray{QJsonObject{{"name", "Default"}, {"type", "SHOES"}}}).toJson());
    settings.sync();
    QJsonArray listings;
    for (const QString type : {QString("SHOES"), QString("SANDAL"), QString("BOOT"), QString("DRESS")})
        listings.append(QJsonObject{{"sku", type}, {"asin", type}, {"category", type}});
    write(dir.path() + "/stores/A13V1IB3VIYZZH.json", listings);
    {
        PanePricing pane{QDir(dir.path())};
        pane.resize(1500, 850);
        pane.show();
        auto *selector = pane.findChild<ProductTypeSelector *>("comboBoxProductType");
        auto *table = pane.findChild<QTableView *>("tableViewPricesToUpdate");
        QVERIFY(selector);
        QCOMPARE(selector->selectedValues(), QStringList{"SHOES"});
        QCOMPARE(table->model()->rowCount(), 1);
        selector->showPopup();
        QCoreApplication::processEvents();
        auto clickType = [&](const QString &type) {
            const auto index = selector->model()->index(selector->findText(type), 0);
            QTest::mouseClick(selector->view()->viewport(), Qt::LeftButton, Qt::NoModifier,
                              selector->view()->visualRect(index).center());
        };
        clickType("BOOT");
        QVERIFY(selector->view()->isVisible());
        clickType("SANDAL");
        QVERIFY(selector->view()->isVisible());
        QCOMPARE(selector->selectedValues(), (QStringList{"BOOT", "SANDAL", "SHOES"}));
        QCOMPARE(table->model()->rowCount(), 3);
        selector->hidePopup();
    }
    {
        PanePricing pane{QDir(dir.path())};
        auto *selector = pane.findChild<ProductTypeSelector *>("comboBoxProductType");
        auto *table = pane.findChild<QTableView *>("tableViewPricesToUpdate");
        QCOMPARE(selector->selectedValues(), (QStringList{"BOOT", "SANDAL", "SHOES"}));
        QCOMPARE(table->model()->rowCount(), 3);
        // A catalog refresh must preserve selections even when a type is absent.
        selector->setChoices({"DRESS"});
        QCOMPARE(selector->selectedValues(), (QStringList{"BOOT", "SANDAL", "SHOES"}));
        QVERIFY(selector->findText("BOOT") > 0);
        selector->model()->setData(selector->model()->index(0, 0), Qt::Checked, Qt::CheckStateRole);
        QVERIFY(selector->selectedValues().isEmpty());
        QCOMPARE(table->model()->rowCount(), 4);
    }
    settings.sync();
    const auto saved =
        QJsonDocument::fromJson(settings.value("pricingEditor/filters").toByteArray()).array()[0].toObject();
    QVERIFY(saved.contains("types"));
    QVERIFY(saved.value("types").toArray().isEmpty());
    QVERIFY(!saved.contains("type"));
}
void PricingEditorTests::regionScopesPricesAndPersists() {
    QTemporaryDir dir;
    const QString fr = "A13V1IB3VIYZZH", us = "ATVPDKIKX0DER";
    QSettings settings(dir.path() + "/settings.ini", QSettings::IniFormat);
    settings.setValue("pricingEditor/markets/" + fr, true);
    settings.setValue("pricingEditor/markets/" + us, true);
    settings.sync();
    AmazonDataCache cache(QDir(dir.path()), "|");
    const auto now = QDateTime::currentSecsSinceEpoch();
    for (const QString sku : {QString("A"), QString("B")}) {
        for (const QString mp : {fr, us}) {
            if (sku == "B" && mp == fr)
                continue;
            const QJsonObject body{
                {"summaries", QJsonArray{QJsonObject{{"marketplaceId", mp}, {"itemName", "Title"}}}},
                {"offers", QJsonArray{QJsonObject{{"marketplaceId", mp},
                                                  {"price", QJsonObject{{"amount", mp == fr ? 20 : 60}}}}}}};
            QVERIFY(cache.write("listing", AmazonDataCache::listingKey(mp, sku),
                                {{"sku", sku}, {"marketplace", mp}, {"exists", true}, {"body", body}}, now));
        }
    }
    QVERIFY(cache.write("rowStats", "A", {{"scope", QJsonArray{fr}}, {"available", 7}, {"sales90", 3}}, now));
    {
        PanePricing pane{QDir(dir.path())};
        auto *regions = pane.findChild<QComboBox *>("comboBoxRegion");
        auto *table = pane.findChild<QTableView *>("tableViewPricesToUpdate");
        auto *prices = pane.findChild<QTableView *>("tableNewPrices");
        auto *model = pane.findChild<TableAmazonPricing *>();
        auto *update = pane.findChild<QPushButton *>("pushButtonUpdatePrices");
        QVERIFY(regions && table && prices && model && update);
        QCOMPARE(regions->count(), 4);
        prices->model()->setData(prices->model()->index(0, 0), "25");
        pane.findChild<QRadioButton *>("radioIncreaseDecrease")->click();
        QVERIFY(!update->isEnabled()); // selected US has no exchange rate yet
        regions->setCurrentIndex(regions->findData("Europe"));
        QVERIFY(update->isEnabled());
        QCOMPARE(pane.pendingUpdates().size(), 1);
        QCOMPARE(pane.pendingUpdates().first().marketplace, fr);
        QCOMPARE(pane.pendingUpdates().first().currency, QString("EUR"));
        QCOMPARE(table->model()->rowCount(), 1); // US-only B is filtered out
        QCOMPARE(table->model()->index(0, TableAmazonPricing::Changes).data().toString(),
                 QString("↑ 25.00 € (FR)"));
        QCOMPARE(table->model()->index(0, TableAmazonPricing::Sales90).data().toInt(), 3);
        for (const auto &m : model->markets())
            QCOMPARE(m.continent, QString("Europe"));
        // Manual edits remain restricted to Europe and survive a region round trip.
        const int source = qobject_cast<QSortFilterProxyModel *>(table->model())
                               ->mapToSource(table->model()->index(0, 0))
                               .row();
        QVERIFY(model->setData(model->index(source, model->columnForKey("all:new")), 27));
        regions->setCurrentIndex(regions->findData("Americas"));
        QCOMPARE(table->model()->rowCount(), 2);
        QVERIFY(!update->isEnabled());
        for (const auto &m : model->markets())
            QCOMPARE(m.continent, QString("Americas"));
        int usColumn = -1;
        for (int c = 1; c < prices->model()->columnCount(); ++c)
            if (prices->model()->headerData(c, Qt::Horizontal).toString() == "US (USD)")
                usColumn = c;
        QVERIFY(usColumn > 0);
        prices->model()->setData(prices->model()->index(1, usColumn), "2");
        QVERIFY(update->isEnabled());
        QCOMPARE(pane.pendingUpdates().size(), 2);
        for (const auto &u : pane.pendingUpdates()) {
            QCOMPARE(u.marketplace, us);
            QCOMPARE(u.currency, QString("USD"));
            QCOMPARE(u.after, 50.0);
        }
        QCOMPARE(table->model()->index(0, TableAmazonPricing::Changes).data().toString(),
                 QString("↓ 25.00 € (US)"));
        QCOMPARE(table->model()->index(0, TableAmazonPricing::MainPrice).data().toString(),
                 QString("US · 60.00 USD (conv 30.00 EUR)"));
        regions->setCurrentIndex(0);
        QCOMPARE(table->model()->index(0, TableAmazonPricing::Changes).data().toString(),
                 QString("↑ 27.00 € (FR)\n↓ 25.00 € (US)"));
        regions->setCurrentIndex(regions->findData("Asia"));
        QVERIFY(!update->isEnabled()); // JP remains cache-only
        regions->setCurrentIndex(regions->findData("Europe"));
        QVERIFY(prices->isColumnHidden(usColumn));
        QCOMPARE(prices->model()->index(2, usColumn).data(Qt::CheckStateRole).toInt(), int(Qt::Checked));
    }
    PanePricing restored{QDir(dir.path())};
    QCOMPARE(restored.findChild<QComboBox *>("comboBoxRegion")->currentData().toString(), QString("Europe"));
    auto *table = restored.findChild<QTableView *>("tableViewPricesToUpdate");
    QCOMPARE(table->model()->rowCount(), 1);
    QCOMPARE(table->model()->index(0, TableAmazonPricing::Changes).data().toString(),
             QString("↑ 25.00 € (FR)"));
}
void PricingEditorTests::mainPricePriority() {
    TableAmazonPricing model;
    auto ms = markets();
    ms << Pricing::Market{"CA", "CA", "CAD", "Americas", 3, true}
       << Pricing::Market{"JP", "JP", "JPY", "Asia", 160, false};
    model.setMarkets(ms);
    auto r = row();
    r.listings["CA"] = r.listings["US"];
    r.listings["JP"] = r.listings["US"];
    r.listings["JP"].price = 3200;
    const QStringList countries{"FR", "DE", "US", "CA", "JP"};
    const QStringList expected{"FR · 20.00 €", "DE · 20.00 €", "US · 60.00 USD (conv 30.00 EUR)",
                               "CA · 60.00 CAD (conv 20.00 EUR)", "JP · 3200.00 JPY (conv 20.00 EUR)"};
    for (int i = 0; i < countries.size(); ++i) {
        model.setRows({r});
        QCOMPARE(model.index(0, TableAmazonPricing::MainPrice).data().toString(), expected[i]);
        QVERIFY(!(model.flags(model.index(0, TableAmazonPricing::MainPrice)) & Qt::ItemIsEditable));
        r.listings.remove(countries[i]);
    }
    model.setRows({r});
    QVERIFY(!model.index(0, TableAmazonPricing::MainPrice).data().isValid());
    r = row();
    r.listings.remove("FR");
    r.listings.remove("DE");
    model.setRows({r});
    ms[2].rate = -1;
    model.setMarkets(ms);
    QCOMPARE(model.index(0, TableAmazonPricing::MainPrice).data().toString(),
             QString("US · 60.00 USD (conv — EUR)"));
    QVERIFY(!model.index(0, TableAmazonPricing::MainPrice).data(TableAmazonPricing::SortRole).isValid());
}
void PricingEditorTests::reviewOnlyEligibleChanges() {
    TableAmazonPricing model;
    model.setMarkets(markets());
    model.setRows({row()});
    model.setRules(25, {}, Pricing::Direction::Both);
    auto summary = [&] { return model.index(0, TableAmazonPricing::Changes); };
    QCOMPARE(summary().data().toString(), QString("↑ 25.00 € (DE / FR)\n↓ 25.00 € (US)"));
    QVERIFY(summary().data(TableAmazonPricing::RichTextRole).toString().contains("#14532d"));
    QVERIFY(summary().data(TableAmazonPricing::RichTextRole).toString().contains("#991b1b"));
    QVERIFY(summary().data(Qt::ToolTipRole).toString().contains("US: 60.00 → 50.00 USD"));
    QCOMPARE(summary().data(TableAmazonPricing::SortRole).toInt(), 3);
    QVERIFY(!(model.flags(summary()) & Qt::ItemIsEditable));
    for (auto mode :
         {TableAmazonPricing::OnePrice, TableAmazonPricing::Continent, TableAmazonPricing::AllCountry}) {
        model.setMode(mode);
        QCOMPARE(summary().data().toString(), QString("↑ 25.00 € (DE / FR)\n↓ 25.00 € (US)"));
    }
    model.setRules(25, {}, Pricing::Direction::Increase);
    QVERIFY(model.setData(model.index(0, TableAmazonPricing::FixedCount + 1), 15));
    QCOMPARE(summary().data().toString(), QString("↑ 25.00 € (DE)")); // blocked blue manual edit absent
    model.setRules(25, {}, Pricing::Direction::Both);
    QCOMPARE(summary().data().toString(), QString("↑ 25.00 € (DE)\n↓ 15.00 € (FR)\n↓ 25.00 € (US)"));
    model.resetMarket(row().sku, "FR");
    auto ms = markets();
    ms[1].enabled = false;
    model.setMarkets(ms);
    QCOMPARE(summary().data().toString(), QString("↓ 25.00 € (US)"));
    model.setRules(30, {}, Pricing::Direction::Both); // unchanged US price
    QCOMPARE(summary().data().toString(), QString("No changes"));
    model.setRules(-1, {}, Pricing::Direction::Both);
    QCOMPARE(summary().data(TableAmazonPricing::SortRole).toInt(), 0);
}
void PricingEditorTests::modesAndConversion() {
    TableAmazonPricing model;
    model.setMarkets(markets());
    model.setRows({row()});
    model.setRules(25, {}, Pricing::Direction::Both);
    QCOMPARE(model.columnCount(), 13);
    QCOMPARE(model.data(model.index(0, model.columnForKey("all:new"))).toString(), QString("25.00"));
    model.setMode(TableAmazonPricing::Continent);
    QCOMPARE(model.columnCount(), 16);
    model.setMode(TableAmazonPricing::AllCountry);
    QCOMPARE(model.columnCount(), 18);
    QCOMPARE(model.proposed(model.rows()[0], 2), 50.0);
    model.setRules(25, {{"US", 55}}, Pricing::Direction::Both);
    QCOMPARE(model.proposed(model.rows()[0], 2), 55.0);
    QCOMPARE(model.data(model.index(0, TableAmazonPricing::Title)).toString(), QString("Titre français"));
}
void PricingEditorTests::directionAndManualColors() {
    TableAmazonPricing m;
    m.setMarkets(markets());
    m.setMode(TableAmazonPricing::AllCountry);
    m.setRows({row()});
    m.setRules(25, {}, Pricing::Direction::Increase);
    QCOMPARE(m.proposed(m.rows()[0], 0), 25.0);
    QCOMPARE(m.proposed(m.rows()[0], 2), -1.0);
    QCOMPARE(
        m.data(m.index(0, TableAmazonPricing::FixedCount + 1), Qt::BackgroundRole).value<QBrush>().color(),
        QColor("#14532d"));
    QVERIFY(m.setData(m.index(0, TableAmazonPricing::FixedCount + 1), 15.0));
    QCOMPARE(m.proposed(m.rows()[0], 0), -1.0);
    QCOMPARE(m.data(m.index(0, TableAmazonPricing::FixedCount + 1)).toString(), QString("15.00"));
    QCOMPARE(
        m.data(m.index(0, TableAmazonPricing::FixedCount + 1), Qt::BackgroundRole).value<QBrush>().color(),
        QColor("#173f73"));
    m.setRules(25, {}, Pricing::Direction::Both);
    QCOMPARE(m.proposed(m.rows()[0], 0), 15.0);
    QCOMPARE(
        m.data(m.index(0, TableAmazonPricing::FixedCount + 1), Qt::BackgroundRole).value<QBrush>().color(),
        QColor("#173f73"));
    QCOMPARE(
        m.data(m.index(0, TableAmazonPricing::FixedCount + 5), Qt::BackgroundRole).value<QBrush>().color(),
        QColor("#9a450b"));
    m.setRules(25, {}, Pricing::Direction::Decrease);
    QCOMPARE(m.proposed(m.rows()[0], 1), -1.0);
    QVERIFY(!m.setData(m.index(0, TableAmazonPricing::FixedCount + 1), -20));
    QVERIFY(
        !m.setData(m.index(0, TableAmazonPricing::FixedCount + 1), std::numeric_limits<double>::infinity()));
}
void PricingEditorTests::exchangeRateParsing() {
    QDate date;
    const auto rates = Pricing::parseEuroRates(
        R"(<Envelope><Cube><Cube time="2026-09-15"><Cube currency="USD" rate="1.15"/><Cube currency="GBP" rate="0.85"/></Cube></Cube></Envelope>)",
        &date);
    QCOMPARE(date, QDate(2026, 9, 15));
    QCOMPARE(rates.value("EUR"), 1.0);
    QCOMPARE(rates.value("USD"), 1.15);
    QVERIFY(Pricing::parseEuroRates("<broken>", &date).isEmpty());
    QVERIFY(
        Pricing::parseEuroRates(R"(<Cube time="2026-09-15"><Cube currency="USD" rate="0"/></Cube>)", &date)
            .isEmpty());
    QVERIFY(Pricing::parseEuroRates(R"(<Cube><Cube currency="USD" rate="1.2"/></Cube>)", &date).isEmpty());
}
void PricingEditorTests::mandatoryDefaultAndReset() {
    TableAmazonPricing m;
    m.setMarkets(markets());
    m.setRows({row("b"), row("a")});
    m.setRules(-1, {{"FR", 25}}, Pricing::Direction::Both);
    QCOMPARE(m.proposed(m.rows()[0], 0), -1.0);
    QVERIFY(!(m.flags(m.index(0, m.columnForKey("all:new"))) & Qt::ItemIsEditable));
    m.setRules(25, {}, Pricing::Direction::Both);
    QVERIFY(m.setData(m.index(0, m.columnForKey("all:new")), 30.0));
    QCOMPARE(m.proposed(m.rows()[0], 2), -1.0); // equal after conversion
    m.resetRows({"a"});
    QCOMPARE(m.proposed(m.rows()[1], 0), -1.0);
    QCOMPARE(m.proposed(m.rows()[0], 0), 30.0);
    auto changed = m.rows()[0];
    changed.titleFr = "New";
    m.updateRow(changed);
    QCOMPARE(m.proposed(m.rows()[0], 0), 30.0);
}
void PricingEditorTests::anyMarketplaceFilterAndZeroSales() {
    PricingFilterProxy p;
    PricingFilterProxy::Filter f;
    f.minPrice = 19;
    f.maxPrice = 21;
    p.setFilter(f);
    QVERIFY(p.accepts(row(), markets()));
    f.minPrice = 21;
    f.maxPrice = 29;
    p.setFilter(f);
    QVERIFY(!p.accepts(row(), markets()));
    f.minPrice = 0;
    f.maxPrice = 0;
    f.minDays = 1;
    p.setFilter(f);
    auto r = row();
    r.sales90 = 0;
    QCOMPARE(r.inventoryDays(), 0);
    QVERIFY(!p.accepts(r, markets()));
    r.sales90 = -1;
    QCOMPARE(r.inventoryDays(), -1);
    QVERIFY(!p.accepts(r, markets()));
    f.minDays = 0;
    f.sku = "SKU-";
    f.title = "FRANÇAIS";
    f.sizeFrom = "S";
    f.sizeTo = "L";
    p.setFilter(f);
    QVERIFY(p.accepts(row(), markets()));
}
void PricingEditorTests::sortingUsesNumbersAndStableSku() {
    TableAmazonPricing m;
    m.setMarkets(markets());
    auto a = row("a"), b = row("b"), c = row("c");
    a.sales90 = 10;
    b.sales90 = 2;
    c.sales90 = -1;
    m.setRows({a, c, b});
    PricingFilterProxy p;
    p.setSourceModel(&m);
    p.sort(TableAmazonPricing::Sales90, Qt::AscendingOrder);
    QCOMPARE(p.index(0, 1).data().toString(), QString("b"));
    QCOMPARE(p.index(2, 1).data().toString(), QString("c"));
    p.sort(TableAmazonPricing::Sales90, Qt::DescendingOrder);
    QCOMPARE(p.index(0, 1).data().toString(), QString("a"));
    QCOMPARE(p.index(2, 1).data().toString(), QString("c"));
    p.sort(TableAmazonPricing::Sku, Qt::AscendingOrder);
    m.resetRows({p.index(0, 0).data(TableAmazonPricing::RowKeyRole).toString()});
    QVERIFY(m.rows()[0].resetMarkets.contains("FR"));
    QVERIFY(m.rows()[2].resetMarkets.isEmpty());
}
void PricingEditorTests::frenchAndLetterSizes() {
    QVERIFY(Pricing::convertSize("999", "US", "FR", "female", "adult", "DRESS").isEmpty());
    QVERIFY(Pricing::convertSize("38", "FR", "BR", "female", "adult", "DRESS").isEmpty());
    QVERIFY(Pricing::sizeInRange("M", "S", "L"));
    QVERIFY(!Pricing::sizeInRange("XS", "S", "L"));
    QVERIFY(Pricing::sizeInRange("38/40", "38", "40"));
    QVERIFY(!Pricing::sizeInRange("38/40", "38", "39"));
    QVERIFY(!Pricing::sizeInRange("3–6 months", "38", "40"));
    QVERIFY(!Pricing::sizeInRange("38", "S", "L"));
    QCOMPARE(Pricing::convertSize("38", "FR", "DE", "female", "adult", "DRESS"), QString("36"));
    QCOMPARE(Pricing::convertSize("M", "FR", "US", "female", "adult", "DRESS"), QString("M"));
    QVERIFY(Pricing::convertSize("38", "FR", "US", {}, "child", "DRESS").isEmpty());
}
void PricingEditorTests::cacheIdentityAndFreshness() {
    QTemporaryDir dir;
    AmazonDataCache a(QDir(dir.path()), "sellerA"), b(QDir(dir.path()), "sellerB");
    auto key = AmazonDataCache::listingKey("FR", "../a/SKU");
    QVERIFY(a.write("listing", key, {{"price", 20}}, 100));
    QVERIFY(b.read("listing", key).isEmpty());
    QVERIFY(a.read("listing", AmazonDataCache::listingKey("US", "../a/SKU")).isEmpty());
    auto r = a.read("listing", key);
    QVERIFY(AmazonDataCache::fresh(r, 60, 159));
    QVERIFY(!AmazonDataCache::fresh(r, 60, 160));
    QVERIFY(!AmazonDataCache::fresh(r, 60, 99));
    QVERIFY(a.read("listing", AmazonDataCache::listingKey("FR", "../a/sku")).isEmpty());
}
void PricingEditorTests::cacheMergeAndCorruption() {
    QTemporaryDir dir;
    AmazonDataCache a(QDir(dir.path()), "seller");
    QVERIFY(a.write("sales", "a", {{"units", 3}}, 100));
    QVERIFY(a.write("sales", "b", {{"units", 2}}, 200));
    QCOMPARE(a.read("sales", "a").value("fetchedUtc").toInteger(), 100);
    QVERIFY(a.write("sales", "a", {{"units", 1}}, 90));
    QCOMPARE(a.read("sales", "a").value("payload").toObject().value("units").toInt(), 3);
    QVERIFY(a.invalidate("sales", "a"));
    QVERIFY(a.read("sales", "a").isEmpty());
    QVERIFY(!a.read("sales", "b").isEmpty());
    auto dirs = a.root().entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    QVERIFY(!dirs.isEmpty());
    QDir sub(a.root().filePath(dirs.first()));
    auto files = sub.entryList({"*.json"}, QDir::Files);
    QVERIFY(!files.isEmpty());
    QFile f(sub.filePath(files.first()));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("broken");
    f.close();
    QVERIFY(a.all("sales").isEmpty());
}
void PricingEditorTests::legacyImportPreservesEverySku() {
    QTemporaryDir dir;
    const QString path = dir.path() + "/stores/FR.json";
    write(path, {{QJsonObject{{"sku", "A"}, {"asin", "B000000001"}, {"title", "Titre"}}},
                 {QJsonObject{{"sku", "B"}, {"asin", "B000000001"}, {"title", "Titre"}}}});
    QFile before(path);
    QVERIFY(before.open(QIODevice::ReadOnly));
    auto bytes = before.readAll();
    before.close();
    AmazonDataCache cache(QDir(dir.path()), "seller");
    auto rows = cache.seedRows(markets());
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows[0].asin, rows[1].asin);
    QCOMPARE(rows[0].listings.value("FR").price, -1.0);
    QVERIFY(before.open(QIODevice::ReadOnly));
    QCOMPARE(before.readAll(), bytes);
}
void PricingEditorTests::cachePriceAndStatsScope() {
    QTemporaryDir dir;
    QSettings settings(dir.path() + "/settings.ini", QSettings::IniFormat);
    settings.setValue("PaneStoreStockCache/available", "{\"a\":10}");
    settings.setValue("PaneStoreStockCache/sales90", "{\"a\":30}");
    settings.sync();
    write(dir.path() + "/stores/FR.json", {QJsonObject{{"sku", "A"}, {"asin", "B000000001"}}});
    AmazonDataCache cache(QDir(dir.path()), "seller");
    const auto body = QJsonDocument::fromJson(R"({"attributes":{"list_price":[{"value":99}]}})").object();
    QVERIFY(
        cache.write("listing", AmazonDataCache::listingKey("FR", "A"),
                    {{"sku", "A"}, {"marketplace", "FR"}, {"price", 99}, {"exists", true}, {"body", body}},
                    QDateTime::currentSecsSinceEpoch()));
    auto rows = cache.seedRows(markets());
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].listings.value("FR").price, -1.0);
    QCOMPARE(rows[0].sales90, -1);
    QCOMPARE(rows[0].available, -1);
    auto european = markets();
    european.removeLast();
    rows = cache.seedRows(european);
    QCOMPARE(rows[0].available, 10);
    QCOMPARE(rows[0].sales90, -1); // EU8 sales cannot describe just FR and DE.
    QVERIFY(cache.write("rowStats", "A",
                        {{"available", 7}, {"sales90", 4}, {"scope", QJsonArray{"FR", "DE"}}},
                        QDateTime::currentSecsSinceEpoch()));
    rows = cache.seedRows(european);
    QCOMPARE(rows[0].available, 7);
    QCOMPARE(rows[0].sales90, 4);
    rows = cache.seedRows(markets());
    QCOMPARE(rows[0].available, -1);
    QCOMPARE(rows[0].sales90, -1);
}
void PricingEditorTests::widgetPersistsSortingAndPresets() {
    QTemporaryDir dir;
    QSettings settings(dir.path() + "/settings.ini", QSettings::IniFormat);
    settings.setValue("AmazonApi/eu/sellerId", "test-seller");
    settings.sync();
    const QString fr = "A13V1IB3VIYZZH";
    write(dir.path() + "/stores/" + fr + ".json", {QJsonObject{{"sku", "SKU-02"},
                                                               {"asin", "B000000001"},
                                                               {"title", "Robe française"},
                                                               {"sizeValue", "M"},
                                                               {"color", "Bleu"}},
                                                   QJsonObject{{"sku", "SKU-01"},
                                                               {"asin", "B000000002"},
                                                               {"title", "Chemise"},
                                                               {"sizeValue", "S"},
                                                               {"color", "Blanc"}}});
    {
        PanePricing pane{QDir(dir.path())};
        pane.resize(1500, 850);
        pane.show();
        auto *table = pane.findChild<QTableView *>("tableViewPricesToUpdate");
        QVERIFY(table);
        QCOMPARE(table->model()->rowCount(), 2);
        table->sortByColumn(TableAmazonPricing::Sku, Qt::DescendingOrder);
        QCOMPARE(table->model()->index(0, 1).data().toString(), QString("SKU-02"));
        auto *model = pane.findChild<TableAmazonPricing *>();
        QVERIFY(model);
        auto samples = model->rows();
        for (auto &r : samples) {
            for (const auto &m : model->markets()) {
                if (m.currency != "EUR")
                    continue;
                auto &listing = r.listings[m.id];
                listing.exists = true;
                listing.price = m.country == "FR" ? 20 : 40;
            }
        }
        model->setRows(samples);
        pane.findChild<QRadioButton *>("radioIncreaseDecrease")->click();
        auto *prices = pane.findChild<QTableView *>("tableNewPrices");
        QVERIFY(prices);
        QVERIFY(prices->model()->setData(prices->model()->index(0, 0), "25.00"));
        auto *radio = pane.findChild<QRadioButton *>("radioAllCountry");
        QVERIFY(radio);
        radio->click();
        table->sortByColumn(TableAmazonPricing::Title, Qt::AscendingOrder);
        pane.findChild<QRadioButton *>("radioOnePrice")->click();
        QCOMPARE(table->horizontalHeader()->sortIndicatorOrder(), Qt::DescendingOrder);
        QCoreApplication::processEvents();
        QVERIFY(pane.grab().save("/tmp/pricing-pane-smoke.png"));
    }
    {
        PanePricing pane{QDir(dir.path())};
        auto *table = pane.findChild<QTableView *>("tableViewPricesToUpdate");
        QCOMPARE(table->horizontalHeader()->sortIndicatorOrder(), Qt::DescendingOrder);
        auto *prices = pane.findChild<QTableView *>("tableNewPrices");
        QCOMPARE(prices->model()->index(0, 0).data().toString(), QString("25.00"));
    }
    QTemporaryDir other;
    PanePricing pane{QDir(other.path())};
    auto *table = pane.findChild<QTableView *>("tableViewPricesToUpdate");
    QCOMPARE(table->horizontalHeader()->sortIndicatorOrder(), Qt::AscendingOrder);
}
void PricingEditorTests::salesMissingIsNotZero() {
    QCOMPARE(AmazonInventoryApi::parseSalesUnits(R"({"payload":[{"unitCount":3}]})"), 3);
    QCOMPARE(AmazonInventoryApi::parseSalesUnits(R"({"payload":[]})"), 0);
    QCOMPARE(AmazonInventoryApi::parseSalesUnits(R"({"payload":[{"unitCount":0}]})"), 0);
    QCOMPARE(AmazonInventoryApi::parseSalesUnits("{}"), -1);
    QCOMPARE(AmazonInventoryApi::parseSalesUnits(R"({"payload":[{}]})"), -1);
    QCOMPARE(AmazonInventoryApi::parseSalesUnits(R"({"payload":[{"unitCount":-1}]})"), -1);
    QCOMPARE(AmazonInventoryApi::parseSalesUnits("malformed"), -1);
}
QTEST_MAIN(PricingEditorTests)
#include "tst_pricingeditor.moc"
