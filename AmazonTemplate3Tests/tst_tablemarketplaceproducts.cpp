#include <QtTest>
#include <QBrush>

#include "marketplaces/TableMarketplaceProducts.h"
#include "marketplaces/TableMarketplaceOrders.h"

class TableMarketplaceProductsTests : public QObject
{
    Q_OBJECT
private slots:
    void automaticQuantity_data()
    {
        QTest::addColumn<int>("available");
        QTest::addColumn<int>("sales");
        QTest::addColumn<int>("pct");
        QTest::addColumn<int>("cap");
        QTest::addColumn<int>("minDays");
        QTest::addColumn<int>("expected");
        QTest::newRow("screenshot-24-days") << 44 << 162 << 90 << 3 << 45 << 1;
        QTest::newRow("five-units") << 5 << 90 << 90 << 3 << 45 << 0;
        QTest::newRow("six-units") << 6 << 90 << 90 << 3 << 45 << 1;
        QTest::newRow("at-days-threshold") << 6 << 12 << 90 << 3 << 45 << 1;
        QTest::newRow("zero-percent") << 44 << 162 << 0 << 3 << 45 << 0;
        QTest::newRow("zero-stock") << 0 << 90 << 90 << 3 << 45 << 0;
        QTest::newRow("unknown-stock") << -1 << 90 << 90 << 3 << 45 << -1;
        QTest::newRow("above-threshold") << 10 << 5 << 90 << 0 << 60 << 6;
        QTest::newRow("normal-cap") << 10 << 5 << 90 << 3 << 60 << 3;
        QTest::newRow("no-sales") << 10 << 0 << 90 << 3 << 45 << 3;
        QTest::newRow("unknown-sales") << 10 << -1 << 90 << 3 << 45 << 3;
        QTest::newRow("disabled-threshold") << 44 << 162 << 90 << 3 << 0 << 3;
    }

    void automaticQuantity()
    {
        QFETCH(int, available);
        QFETCH(int, sales);
        QFETCH(int, pct);
        QFETCH(int, cap);
        QFETCH(int, minDays);
        QFETCH(int, expected);
        TableMarketplaceProducts model({"SKU"}, {{"fr", "Temu FR"}});
        model.setSyncParams(pct, cap, minDays);
        model.applyInventory({{"SKU", "ASIN", available, 0}});
        model.applySales("SKU", sales);
        QCOMPARE(model.targetQtyForSku("sku", "fr"), expected);
        QCOMPARE(model.data(model.index(0, 7)).toString(),
                 expected < 0 ? QString("-") : QString::number(expected));
    }

    void manualQuantityKeepsWarningAndIsStoreSpecific()
    {
        TableMarketplaceProducts model({"SKU"}, {{"fr", "Temu FR"}, {"de", "Temu DE"}});
        model.setSyncParams(90, 3, 45);
        model.applyInventory({{"SKU", "ASIN", 44, 0}});
        model.applySales("SKU", 162);
        model.applyStoreInventory("fr", {{"SKU", 3}});
        model.applyStoreInventory("de", {{"SKU", 3}});
        const QModelIndex cell = model.index(0, 7);
        const QBrush red(QColor(139, 0, 0));
        QCOMPARE(cell.data(Qt::BackgroundRole).value<QBrush>(), red);
        QVERIFY(model.flags(cell) & Qt::ItemIsEditable);
        QSignalSpy changes(&model, &QAbstractItemModel::dataChanged);
        QVERIFY(model.setData(cell, "4"));
        QCOMPARE(changes.count(), 1);
        QCOMPARE(cell.data().toString(), QString("4"));
        QCOMPARE(cell.data(Qt::EditRole).toString(), QString("4"));
        QCOMPARE(cell.data(Qt::BackgroundRole).value<QBrush>(), red);
        QCOMPARE(model.targetQtyForSku("sku", "fr"), 4);
        QCOMPARE(model.targetQtyForSku("SKU", "de"), 1);
        QCOMPARE(model.targetQtyForSku("SKU"), 1);

        // Sync reapplies parameters; edits must survive that and stock refreshes.
        model.setSyncParams(90, 3, 45);
        model.applyStoreInventory("fr", {{"SKU", 4}});
        QCOMPARE(model.targetQtyForSku("SKU", "fr"), 4);
        QCOMPARE(cell.data(Qt::BackgroundRole).value<QBrush>(), red);
        QVERIFY(model.setData(cell, 0));
        QCOMPARE(model.targetQtyForSku("SKU", "fr"), 0);
    }

    void rejectsInvalidEdits()
    {
        TableMarketplaceProducts model({"SKU"}, {{"fr", "Temu FR"}});
        const QModelIndex cell = model.index(0, 7);
        QVERIFY(!(model.flags(cell) & Qt::ItemIsEditable));
        QVERIFY(!model.setData(cell, 1));
        model.applyInventory({{"SKU", "ASIN", 6, 0}});
        for (const QVariant &value : {QVariant(-1), QVariant("abc"), QVariant("1.5"),
                                      QVariant("2147483648"), QVariant(1.5)})
            QVERIFY(!model.setData(cell, value));
        QVERIFY(!model.setData(cell, 1, Qt::DisplayRole));
        QVERIFY(!model.setData({}, 1));
        for (int col = 0; col < model.columnCount(); ++col) {
            if (col == 7) continue;
            QCOMPARE(bool(model.flags(model.index(0, col)) & Qt::ItemIsEditable),
                     col == TableMarketplaceProducts::ColSku);
            QVERIFY(!model.setData(model.index(0, col), 1));
        }
        QCOMPARE(model.index(0, TableMarketplaceProducts::ColSku).data(Qt::EditRole).toString(),
                 QString("SKU"));
        QCOMPARE(model.targetQtyForSku("SKU", "fr"), 6);
    }

    void columnHeaderToolTips()
    {
        TableMarketplaceProducts model({"SKU1"}, {{"fr", "Temu FR"}, {"de", "Temu DE"}});
        for (int col = 0; col < model.columnCount(); ++col) {
            const QString display = model.headerData(col, Qt::Horizontal, Qt::DisplayRole).toString();
            const QString toolTip = model.headerData(col, Qt::Horizontal, Qt::ToolTipRole).toString();
            QVERIFY2(!display.isEmpty(), qPrintable(QString("Empty display header for col %1").arg(col)));
            QVERIFY2(!toolTip.isEmpty(), qPrintable(QString("Empty tooltip header for col %1").arg(col)));
        }
        QVERIFY(model.headerData(6, Qt::Horizontal, Qt::ToolTipRole).toString().contains("Temu FR"));
        QVERIFY(model.headerData(7, Qt::Horizontal, Qt::ToolTipRole).toString().contains("Temu FR"));
        QVERIFY(model.headerData(8, Qt::Horizontal, Qt::ToolTipRole).toString().contains("Temu FR"));
        QVERIFY(model.headerData(9, Qt::Horizontal, Qt::ToolTipRole).toString().contains("Temu DE"));

        TableMarketplaceOrders ordersModel;
        for (int col = 0; col < ordersModel.columnCount(); ++col) {
            const QString toolTip = ordersModel.headerData(col, Qt::Horizontal, Qt::ToolTipRole).toString();
            QVERIFY2(!toolTip.isEmpty(), qPrintable(QString("Empty orders tooltip for col %1").arg(col)));
        }
    }

    void skuHoverDisplaysProductNameWithLanguagePriority()
    {
        TableMarketplaceProducts model({"SKU-ALL", "SKU-FR-DE", "SKU-DE-ONLY", "SKU-STOCK", "SKU-NONE"},
                                       {{"fr", "Temu FR"}});

        // Initially no tooltips on SKU cells
        for (int r = 0; r < model.rowCount(); ++r)
            QVERIFY(!model.index(r, TableMarketplaceProducts::ColSku).data(Qt::ToolTipRole).isValid());

        // Apply StockRecord fallback name
        model.applyInventory({
            {"SKU-ALL",     "B0001", 10, 0, "Stock Fallback Title"},
            {"SKU-FR-DE",   "B0002", 10, 0, "Stock Fallback 2"},
            {"SKU-DE-ONLY", "B0003", 10, 0, ""},
            {"SKU-STOCK",   "B0004", 10, 0, "Only Stock Title"},
        });
        QCOMPARE(model.index(3, TableMarketplaceProducts::ColSku).data(Qt::ToolTipRole).toString(),
                 QString("Only Stock Title"));

        // Apply German and Spanish names (other languages)
        model.applyProductNames({
            {"SKU-ALL",     "Deutscher Titel"},
            {"SKU-FR-DE",   "Deutscher Titel 2"},
            {"SKU-DE-ONLY", "Nur Deutsch Titel"},
        }, "DE");
        QCOMPARE(model.index(2, TableMarketplaceProducts::ColSku).data(Qt::ToolTipRole).toString(),
                 QString("Nur Deutsch Titel"));

        // Apply French names — must beat German and Stock fallback
        model.applyProductNames({
            {"SKU-ALL",   "Titre Français"},
            {"SKU-FR-DE", "Titre Français Prioritaire"},
        }, "FR");
        QCOMPARE(model.index(0, TableMarketplaceProducts::ColSku).data(Qt::ToolTipRole).toString(),
                 QString("Titre Français"));
        QCOMPARE(model.index(1, TableMarketplaceProducts::ColSku).data(Qt::ToolTipRole).toString(),
                 QString("Titre Français Prioritaire"));

        // Apply English name on SKU-ALL — must beat French and German
        model.applyProductNames({
            {"sku-all", "English Priority Title"},
        }, "en_GB");
        QCOMPARE(model.index(0, TableMarketplaceProducts::ColSku).data(Qt::ToolTipRole).toString(),
                 QString("English Priority Title"));

        // Subsequent German/Spanish updates must not overwrite English or French priority
        model.applyProductNames({
            {"SKU-ALL",   "Título Español"},
            {"SKU-FR-DE", "Título Español 2"},
        }, "ES");
        QCOMPARE(model.index(0, TableMarketplaceProducts::ColSku).data(Qt::ToolTipRole).toString(),
                 QString("English Priority Title"));
        QCOMPARE(model.index(1, TableMarketplaceProducts::ColSku).data(Qt::ToolTipRole).toString(),
                 QString("Titre Français Prioritaire"));

        // ASIN-based lookup also respects language priority
        model.applyProductNamesByAsin({{"B0003", "English By ASIN"}}, "US");
        QCOMPARE(model.index(2, TableMarketplaceProducts::ColSku).data(Qt::ToolTipRole).toString(),
                 QString("English By ASIN"));
    }

    void redRowAndStoreQueriesForViewRedOnly()
    {
        TableMarketplaceProducts model(
            {"SKU-RED-FR", "SKU-OK", "SKU-MANUAL-RED"},
            {{"fr", "Temu FR"}, {"de", "Temu DE"}, {"es", "Temu ES"}});
        model.setSyncParams(100, 5, 0);
        model.applyInventory({
            {"SKU-RED-FR",     "A1", 2, 0}, // target = 2
            {"SKU-OK",         "A2", 5, 0}, // target = 5
            {"SKU-MANUAL-RED", "A3", 5, 0}, // target = 5
        });

        // FR has 4 units of SKU-RED-FR (target 2 < 4 -> RED), 3 units of SKU-OK (target 5 >= 3 -> OK)
        model.applyStoreInventory("fr", {{"SKU-RED-FR", 4}, {"SKU-OK", 3}, {"SKU-MANUAL-RED", 3}});
        // DE has 2 units of everything (target >= 2 -> no red)
        model.applyStoreInventory("de", {{"SKU-RED-FR", 2}, {"SKU-OK", 2}, {"SKU-MANUAL-RED", 3}});
        // ES has 0 units of everything (no red)
        model.applyStoreInventory("es", {{"SKU-RED-FR", 0}, {"SKU-OK", 0}, {"SKU-MANUAL-RED", 0}});

        QCOMPARE(model.storeCount(), 3);
        QVERIFY(model.hasRedInRow(0));   // SKU-RED-FR is red in FR
        QVERIFY(!model.hasRedInRow(1));  // SKU-OK has no red
        QVERIFY(!model.hasRedInRow(2));  // SKU-MANUAL-RED has no red yet

        QVERIFY(model.hasRedInStore(0));  // FR has red
        QVERIFY(!model.hasRedInStore(1)); // DE has no red
        QVERIFY(!model.hasRedInStore(2)); // ES has no red

        // Manual override on SKU-MANUAL-RED in DE to 1 (< storeQty 3) makes row 2 and store DE red
        const QModelIndex manualCellDe = model.index(2, TableMarketplaceProducts::k_fixedCols + 1 * 3 + 1);
        QVERIFY(model.setData(manualCellDe, 1));
        QVERIFY(model.hasRedInRow(2));
        QVERIFY(model.hasRedInStore(1));
        QVERIFY(!model.hasRedInStore(2)); // ES still has no red
    }
};

QTEST_GUILESS_MAIN(TableMarketplaceProductsTests)
#include "tst_tablemarketplaceproducts.moc"
