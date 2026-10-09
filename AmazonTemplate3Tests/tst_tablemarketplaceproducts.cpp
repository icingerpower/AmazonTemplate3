#include <QtTest>
#include <QBrush>

#include "marketplaces/TableMarketplaceProducts.h"

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
};

QTEST_GUILESS_MAIN(TableMarketplaceProductsTests)
#include "tst_tablemarketplaceproducts.moc"
