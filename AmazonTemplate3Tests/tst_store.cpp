#include "gui/panes/PaneStore.h"
#include "TableStoreAsin.h"
#include "TreeBrandCategories.h"
#include "workingdirectory/WorkingDirectoryManager.h"
#include <QAbstractItemModelTester>
#include <QApplication>
#include <QBrush>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeView>
#include <QtTest>
#include <memory>

// Successful local no-op in place of an AI CLI. The app must append the exact
// selected ASINs itself, regardless of generated category text.
class StoreExportCli : public AbstractCli
{
public:
    mutable QString prompt;
    QString getName() const override { return "Offline export stub"; }
    QString getDescription() const override { return {}; }
    bool canGenSvg() const override { return false; }
    bool canGenImages() const override { return true; }
    bool canGenVideosFromText() const override { return false; }
    bool canGenVideoFromImages() const override { return false; }
    QString getExecutable() const override { return "/usr/bin/true"; }
    QStringList promptArgs() const override { return {}; }
    QString preparePrompt(const QString &value) const override { prompt = value; return value; }
};

class StoreTests : public QObject
{
    Q_OBJECT
    using Item = StorePlacements::Item;
    std::unique_ptr<QTemporaryDir> m_dir;

    static QList<Item> catalog()
    {
        QList<Item> items;
        for (int size = 38; size <= 40; ++size) {
            Item item;
            item.asin = QStringLiteral("A%1").arg(size);
            item.sku = QStringLiteral("PUMP-BLACK-%1").arg(size);
            item.title = "Black pump";
            item.brand = "Brand";
            item.category = "Pumps";
            item.gender = "female";
            item.age = "adult";
            item.color = "BLACK";
            item.sizeValue = QString::number(size);
            items.append(item);
        }
        auto other = items.first();
        other.asin = "B38";
        other.sku = "OTHER-BLACK-38";
        other.title = "Other shoe";
        items.append(other);
        return items;
    }
    static QSet<QString> pumpAsins() { return {"A38", "A39", "A40"}; }
    static QStringList category(const QString &name) { return {"Brand", name}; }

    QModelIndex node(TreeBrandCategories *model, const QStringList &path)
    {
        QModelIndex result;
        for (const auto &part : path) {
            QModelIndex next;
            for (int row = 0; row < model->rowCount(result); ++row) {
                const auto child = model->index(row, 0, result);
                if (model->nodeNameForIndex(child) == part) { next = child; break; }
            }
            if (!next.isValid()) return {};
            result = next;
        }
        return result;
    }
    void setup(PaneStore &pane, const QList<Item> &items = catalog())
    {
        pane.setWorkingDir(QDir(m_dir->path()));
        pane.m_customPaths = {category("Pumps"), category("Low heels"), category("Square heels")};
        pane._saveCustomPaths();
        pane._applyItems(items);
        pane._saveToDisk(pane._marketplaceId(), items);
    }
    void selectNode(PaneStore &pane, const QStringList &path)
    {
        const auto index = node(pane.m_treeModel, path);
        QVERIFY2(index.isValid(), qPrintable(path.join('/')));
        pane.findChild<QTreeView *>("treeViewBrandCategory")->setCurrentIndex(index);
    }
    void selectPump(PaneStore &pane)
    {
        auto *table = pane.findChild<QTableView *>("tableViewAsins");
        for (int i = 0; i < pane.m_storeModel->rowCount(); ++i) {
            if (pane.m_storeModel->rows()[i].asin.startsWith('A')) {
                table->selectRow(i);
                return;
            }
        }
        QFAIL("Pump row missing");
    }
    void transfer(PaneStore &pane, bool duplicate, const QStringList &destination,
                  bool cancel = false, bool inspectDestination = true)
    {
        const auto source = pane._currentNodePath();
        bool interacted = false;
        QTimer timeout;
        timeout.setSingleShot(true);
        connect(&timeout, &QTimer::timeout, this, [] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject();
        });
        timeout.start(2000);
        QTimer::singleShot(0, this, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            if (cancel) { interacted = true; dialog->reject(); return; }
            auto *tree = dialog->findChild<QTreeView *>();
            const auto index = node(pane.m_treeModel, destination);
            if (!tree || !index.isValid()) { dialog->reject(); return; }
            tree->setCurrentIndex(index);
            interacted = true;
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        });
        auto *button = pane.findChild<QPushButton *>(duplicate ? "buttonDuplicateProducts" : "buttonMoveProducts");
        QVERIFY(button->isEnabled());
        button->click();
        QVERIFY(interacted);
        QCOMPARE(pane._currentNodePath(), source);
        // Most existing tests inspect the result in the destination category.
        // Navigation is explicit in the test; the action itself stays at source.
        if (!cancel && inspectDestination) selectNode(pane, destination);
    }
    void confirmButton(PaneStore &pane, const char *buttonName, bool yes = true)
    {
        bool interacted = false;
        QTimer timeout;
        timeout.setSingleShot(true);
        connect(&timeout, &QTimer::timeout, this, [] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject();
        });
        timeout.start(2000);
        QTimer::singleShot(0, this, [&] {
            auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!dialog) return;
            interacted = true;
            dialog->button(yes ? QMessageBox::Yes : QMessageBox::Cancel)->click();
        });
        auto *button = pane.findChild<QPushButton *>(buttonName);
        QVERIFY(button->isEnabled());
        button->click();
        QVERIFY(interacted);
    }
    int pumpRow(const PaneStore &pane) const
    {
        for (int i = 0; i < pane.m_storeModel->rowCount(); ++i)
            if (pane.m_storeModel->rows()[i].asin.startsWith('A')) return i;
        return -1;
    }
    void roundTrip(PaneStore &pane)
    {
        pane._saveToDisk(pane._marketplaceId(), pane.m_items);
        pane._loadFromDisk(pane._marketplaceId());
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName("AmazonTemplate3OfflineTests");
        QCoreApplication::setApplicationName("StoreTests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
    }
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_dir->isValid());
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_dir->path());
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, m_dir->path());
        WorkingDirectoryManager::instance()->setWorkingDir(QDir(m_dir->path()));
    }
    void legacyGroupingMoveRemoveAndCopy()
    {
        PaneStore pane;
        setup(pane);
        QAbstractItemModelTester treeTester(pane.m_treeModel, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QAbstractItemModelTester tableTester(pane.m_storeModel, QAbstractItemModelTester::FailureReportingMode::QtTest);
        selectNode(pane, category("Pumps"));
        QCOMPARE(pane.m_storeModel->rowCount(), 2); // two families sharing BLACK
        selectPump(pane);
        const auto representative = pane.m_storeModel->rows()[pumpRow(pane)].asin;
        pane.findChild<QPushButton *>("buttonCopyAsins")->click();
        QCOMPARE(QApplication::clipboard()->text(), representative);
        transfer(pane, false, category("Low heels"));
        QCOMPARE(pane.m_treeModel->asinsForIndex(node(pane.m_treeModel, category("Low heels"))).size(), 3);
        QCOMPARE(pane.m_treeModel->asinsForIndex(node(pane.m_treeModel, category("Pumps"))), QStringList{"B38"});
        for (const auto &item : pane.m_items)
            if (item.asin.startsWith('A')) { QCOMPARE(item.category, "Low heels"); QVERIFY(item.manuallyMoved); }
        selectPump(pane);
        confirmButton(pane, "buttonRemoveProducts");
        QCOMPARE(pane.m_items.size(), 1);
        roundTrip(pane);
        QCOMPARE(pane.m_items.first().asin, "B38");
    }
    void duplicateMoveDeleteAndColor()
    {
        PaneStore pane;
        setup(pane);
        QVERIFY(!pane.findChild<QPushButton *>("buttonDuplicateProducts")->isEnabled());
        selectNode(pane, category("Pumps"));
        selectPump(pane);
        transfer(pane, true, category("Low heels"));
        QCOMPARE(pane.m_items.size(), 4);
        QCOMPARE(pane.m_storeModel->rowCount(), 1);
        QVERIFY(pane.m_storeModel->rows().first().duplicate);
        QCOMPARE(pane.m_storeModel->data(pane.m_storeModel->index(0, TableStoreAsin::ColAsin), Qt::ForegroundRole)
                     .value<QBrush>().color(), QColor(135, 206, 250));
        QCOMPARE(pane.m_treeModel->asinsForIndex(node(pane.m_treeModel, category("Low heels"))).size(), 3);
        selectPump(pane);
        transfer(pane, true, category("Low heels")); // same destination is idempotent
        QCOMPARE(pane.m_storeModel->rowCount(), 1);
        for (const auto &item : pane.m_items)
            if (item.asin.startsWith('A')) QCOMPARE(pane.m_placements.placements(item).size(), 2);
        selectPump(pane);
        transfer(pane, false, category("Square heels"));
        QVERIFY(pane.m_treeModel->asinsForIndex(node(pane.m_treeModel, category("Low heels"))).isEmpty());
        QCOMPARE(pane.m_storeModel->rowCount(), 1);
        selectPump(pane);
        confirmButton(pane, "buttonRemoveProducts");
        selectNode(pane, category("Pumps"));
        QCOMPARE(pane.m_storeModel->rowCount(), 2);
        QVERIFY(!pane.m_storeModel->rows()[pumpRow(pane)].duplicate);
        QCOMPARE(pane.m_items.size(), 4);
    }
    void destinationsStayWithinProductBrand_data()
    {
        QTest::addColumn<bool>("duplicate");
        QTest::addColumn<bool>("unknownBrand");
        QTest::newRow("duplicate") << true << false;
        QTest::newRow("move") << false << false;
        QTest::newRow("duplicate-unknown-brand") << true << true;
        QTest::newRow("move-unknown-brand") << false << true;
    }
    void destinationsStayWithinProductBrand()
    {
        QFETCH(bool, duplicate);
        QFETCH(bool, unknownBrand);
        auto items = catalog();
        if (unknownBrand)
            for (auto &item : items) item.brand.clear();
        auto other = items.last();
        other.asin = "C38";
        other.brand = "Other brand";
        other.category = "Low heels";
        items.append(other);
        PaneStore pane;
        setup(pane, items);
        const QString sourceBrand = unknownBrand ? "(unknown brand)" : "Brand";
        const QStringList source{sourceBrand, "Pumps"};
        const QStringList target{sourceBrand, "Low heels"};
        pane.m_customPaths.append(target);
        pane._applyItems(items);
        selectNode(pane, source);
        selectPump(pane);

        bool inspected = false;
        QTimer timeout;
        timeout.setSingleShot(true);
        connect(&timeout, &QTimer::timeout, this, [] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject();
        });
        timeout.start(2000);
        QTimer::singleShot(0, this, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            auto *tree = dialog->findChild<QTreeView *>();
            QVERIFY(tree);
            QCOMPARE(tree->rootIndex(), node(pane.m_treeModel, {sourceBrand}));
            const auto allowed = node(pane.m_treeModel, target);
            QCOMPARE(allowed.parent(), tree->rootIndex());
            const auto otherBrand = node(pane.m_treeModel, {"Other brand", "Low heels"});
            QVERIFY(otherBrand.isValid());
            QVERIFY(otherBrand.parent() != tree->rootIndex());
            auto *ok = dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);

            // Even a programmatically selected index outside the visible subtree
            // must not bypass the brand restriction.
            tree->setCurrentIndex(otherBrand);
            ok->click();
            QVERIFY(dialog->isVisible());
            QCOMPARE(pane.m_asinToItem.value("A38").brand, unknownBrand ? QString() : QString("Brand"));
            tree->setCurrentIndex(allowed);
            inspected = true;
            ok->click();
        });
        pane.findChild<QPushButton *>(duplicate ? "buttonDuplicateProducts" : "buttonMoveProducts")->click();
        QVERIFY(inspected);
        QCOMPARE(pane._currentNodePath(), source);
        QCOMPARE(pane.m_treeModel->asinsForIndex(node(pane.m_treeModel, target)).size(), 3);
        QCOMPARE(pane.m_treeModel->asinsForIndex(node(pane.m_treeModel, {"Other brand", "Low heels"})), QStringList{"C38"});
        QCOMPARE(pane.m_treeModel->asinsForIndex(node(pane.m_treeModel, source)).size(), duplicate ? 4 : 1);
        QCOMPARE(pane.m_asinToItem.value("A38").brand, unknownBrand ? QString() : QString("Brand"));
    }
    void productActionsPreserveTree_data()
    {
        QTest::addColumn<QString>("action");
        QTest::addColumn<bool>("lastProduct");
        for (const auto &action : {"duplicate", "move", "remove"}) {
            QTest::newRow(action) << QString(action) << false;
            QTest::newRow(qPrintable(QString(action) + "-last-product")) << QString(action) << true;
        }
    }
    void productActionsPreserveTree()
    {
        QFETCH(QString, action);
        QFETCH(bool, lastProduct);
        auto items = catalog();
        if (lastProduct) items.removeLast();
        auto other = catalog().last();
        other.asin = "C38";
        other.brand = "Other brand";
        items.prepend(other);
        PaneStore pane;
        setup(pane, items);
        for (int i = 0; i < 30; ++i)
            pane.m_customPaths.append({"Other brand", QStringLiteral("Category %1").arg(i), "female"});
        pane._applyItems(items);
        QAbstractItemModelTester treeTester(pane.m_treeModel, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QSignalSpy resetSpy(pane.m_treeModel, &QAbstractItemModel::modelReset);
        pane.resize(1500, 650);
        pane.show();
        auto *tree = pane.findChild<QTreeView *>("treeViewBrandCategory");
        tree->expandAll();
        tree->collapse(node(pane.m_treeModel, {"Other brand", "Category 0"}));
        const QStringList source{"Brand", "Pumps", "female"};
        selectNode(pane, source);
        selectPump(pane);
        tree->selectionModel()->select(node(pane.m_treeModel, category("Square heels")),
                                       QItemSelectionModel::Select | QItemSelectionModel::Rows);
        tree->doItemsLayout();
        tree->scrollTo(tree->currentIndex(), QAbstractItemView::PositionAtBottom);
        QCoreApplication::processEvents();
        const QPersistentModelIndex current = tree->currentIndex();
        const int scrollPosition = tree->verticalScrollBar()->value();
        QVERIFY(scrollPosition > 0);
        QList<QPersistentModelIndex> oldNodes;
        QList<bool> oldExpansion;
        std::function<void(QModelIndex)> remember = [&](QModelIndex parent) {
            for (int row = 0; row < pane.m_treeModel->rowCount(parent); ++row) {
                const auto child = pane.m_treeModel->index(row, 0, parent);
                oldNodes.append(child);
                oldExpansion.append(tree->isExpanded(child));
                remember(child);
            }
        };
        remember({});
        const auto siblingNames = [&]() {
            QStringList names;
            const auto brand = node(pane.m_treeModel, {"Brand"});
            for (int row = 0; row < pane.m_treeModel->rowCount(brand); ++row)
                names.append(pane.m_treeModel->nodeNameForIndex(pane.m_treeModel->index(row, 0, brand)));
            return names;
        };
        const auto order = siblingNames();
        if (action == "remove") confirmButton(pane, "buttonRemoveProducts");
        else transfer(pane, action == "duplicate", category("Low heels"), false, false);
        QCoreApplication::processEvents();
        QCOMPARE(resetSpy.count(), 0);
        QVERIFY(current.isValid());
        QCOMPARE(tree->currentIndex(), QModelIndex(current));
        QCOMPARE(pane._currentNodePath(), source);
        QCOMPARE(siblingNames(), order);
        QCOMPARE(tree->verticalScrollBar()->value(), scrollPosition);
        QCOMPARE(tree->selectionModel()->selectedRows().size(), 2);
        for (int i = 0; i < oldNodes.size(); ++i) {
            QVERIFY(oldNodes[i].isValid());
            QCOMPARE(tree->isExpanded(oldNodes[i]), oldExpansion[i]);
        }
        const int expected = action == "duplicate" ? (lastProduct ? 1 : 2) : (lastProduct ? 0 : 1);
        QCOMPARE(pane.m_storeModel->rowCount(), expected);
        QCOMPARE(pane.m_treeModel->colorCountForIndex(current), expected);
        if (expected == 1 && action != "duplicate") QCOMPARE(pane.m_storeModel->rows().first().asin, "B38");
    }
    void aggregateCountsStockAndWarnings()
    {
        PaneStore pane;
        setup(pane);
        pane.m_placements.transfer(pane.m_items, pumpAsins(), category("Pumps"), category("Low heels"), true);
        pane.m_placements.transfer(pane.m_items, pumpAsins(), category("Pumps"), category("Square heels"), true);
        const auto items = pane.m_items;
        pane._applyItems(items);
        for (const auto &item : items) {
            pane.m_stockAvailableBySkuLower[item.sku.toLower()] = 10;
            pane.m_stockSales90BySkuLower[item.sku.toLower()] = 90;
            pane.m_stockSales365BySkuLower[item.sku.toLower()] = 365;
        }
        selectNode(pane, {"Brand"});
        QCOMPARE(pane.m_treeModel->asinsForIndex(node(pane.m_treeModel, {"Brand"})).size(), 4);
        QCOMPARE(pane.m_treeModel->colorCountForIndex(node(pane.m_treeModel, {"Brand"})), 2);
        QCOMPARE(pane.m_storeModel->rowCount(), 2);
        auto row = pane.m_storeModel->rows()[pumpRow(pane)];
        QCOMPARE(row.salesYear, 1095);
        QCOMPARE(row.stockDays, 10);
        QVERIFY(!row.duplicate);
        selectNode(pane, category("Low heels"));
        row = pane.m_storeModel->rows().first();
        QVERIFY(row.duplicate);
        QCOMPARE(row.salesYear, 1095);
        QCOMPARE(row.stockDays, 10);
        QCOMPARE(pane.m_storeModel->data(pane.m_storeModel->index(0, TableStoreAsin::ColStockDays), Qt::ForegroundRole)
                     .value<QBrush>().color(), QColor(0xd9, 0x3c, 0x3c));
        selectPump(pane);
        pane.findChild<QPushButton *>("buttonCopyAsins")->click();
        QCOMPARE(QApplication::clipboard()->text(), row.asin);
    }
    void persistenceRefreshAndOriginalDeletion()
    {
        PaneStore pane;
        setup(pane);
        selectNode(pane, category("Pumps"));
        selectPump(pane);
        transfer(pane, true, category("Low heels"));
        roundTrip(pane);
        selectNode(pane, category("Low heels"));
        QVERIFY(pane.m_storeModel->rows().first().duplicate);
        // This is the same apply/save boundary used by Retrieve, with fake fresh attributes.
        auto fresh = catalog();
        for (auto &item : fresh) item.title = "Refreshed title";
        pane._applyItems(fresh);
        roundTrip(pane);
        selectNode(pane, category("Low heels"));
        QCOMPARE(pane.m_storeModel->rows().first().title, "Refreshed title");
        selectNode(pane, category("Pumps"));
        selectPump(pane);
        confirmButton(pane, "buttonRemoveProducts");
        QCOMPARE(pane.m_items.size(), 4); // shared data retained for the remaining placement
        roundTrip(pane);
        pane._applyItems(fresh);
        selectNode(pane, category("Pumps"));
        QCOMPARE(pane.m_storeModel->rowCount(), 1);
        QCOMPARE(pane.m_storeModel->rows().first().asin, "B38");
        selectNode(pane, category("Low heels"));
        QCOMPARE(pane.m_storeModel->rowCount(), 1);
        QVERIFY(pane.m_storeModel->rows().first().duplicate);
        selectPump(pane);
        confirmButton(pane, "buttonRemoveProducts");
        QCOMPARE(pane.m_items.size(), 1);
    }
    void originalMoveAndDestinationCollision()
    {
        PaneStore pane;
        setup(pane);
        selectNode(pane, category("Pumps"));
        selectPump(pane);
        transfer(pane, true, category("Low heels"));
        selectNode(pane, category("Pumps"));
        selectPump(pane);
        transfer(pane, false, category("Square heels"));
        selectNode(pane, category("Low heels"));
        QCOMPARE(pane.m_storeModel->rowCount(), 1);
        QVERIFY(pane.m_storeModel->rows().first().duplicate);
        selectPump(pane);
        transfer(pane, false, category("Square heels"));
        QVERIFY(!pane.m_storeModel->rows().first().duplicate);
        for (const auto &item : pane.m_items)
            if (item.asin.startsWith('A')) QCOMPARE(pane.m_placements.placements(item).size(), 1);
    }
    void independentOrderingAndLegacyMigration()
    {
        PaneStore pane;
        setup(pane);
        // Legacy order files contained representative ASINs; preserve that initial order.
        QFile legacy(m_dir->filePath("stores/A1PA6795UKMFR9_order.json"));
        QVERIFY(legacy.open(QIODevice::WriteOnly));
        legacy.write("[\"B38\",\"A38\"]");
        legacy.close();
        pane._loadOrder();
        selectNode(pane, category("Pumps"));
        QCOMPARE(pane.m_storeModel->rows().first().asin, "B38");
        pane.findChild<QTableView *>("tableViewAsins")->selectAll();
        transfer(pane, true, category("Low heels"));
        QCOMPARE(pane.m_storeModel->rows().first().asin, "B38");
        selectPump(pane);
        pane.findChild<QPushButton *>("buttonMoveToTop")->click();
        QVERIFY(pane.m_storeModel->rows().first().asin.startsWith('A'));
        selectNode(pane, category("Pumps"));
        QCOMPARE(pane.m_storeModel->rows().first().asin, "B38");
        roundTrip(pane);
        selectNode(pane, category("Low heels"));
        QVERIFY(pane.m_storeModel->rows().first().asin.startsWith('A'));
        selectNode(pane, category("Pumps"));
        QCOMPARE(pane.m_storeModel->rows().first().asin, "B38");
    }
    void removeCategoryPreservesOtherPlacements()
    {
        PaneStore pane;
        setup(pane);
        selectNode(pane, category("Pumps"));
        selectPump(pane);
        transfer(pane, true, category("Low heels"));
        confirmButton(pane, "buttonRemoveCategory");
        QVERIFY(!node(pane.m_treeModel, category("Low heels")).isValid());
        selectNode(pane, category("Pumps"));
        QCOMPARE(pane.m_storeModel->rowCount(), 2);
        selectPump(pane);
        transfer(pane, true, category("Square heels"));
        selectNode(pane, category("Pumps"));
        confirmButton(pane, "buttonRemoveCategory");
        QVERIFY(!node(pane.m_treeModel, category("Pumps")).isValid());
        selectNode(pane, category("Square heels"));
        QCOMPARE(pane.m_storeModel->rowCount(), 1);
        selectNode(pane, category("(unknown category)"));
        QCOMPARE(pane.m_storeModel->rowCount(), 2);
        for (const auto &item : pane.m_items) QVERIFY(item.manuallyMoved);
        roundTrip(pane);
        QVERIFY(!node(pane.m_treeModel, category("Pumps")).isValid());
    }
    void mergeIsScopedAndDeduplicates()
    {
        PaneStore pane;
        auto items = catalog();
        auto otherBrand = items.first();
        otherBrand.asin = "C38";
        otherBrand.brand = "Other brand";
        otherBrand.category = "Low heels";
        items.append(otherBrand);
        setup(pane, items);
        selectNode(pane, category("Pumps"));
        selectPump(pane);
        transfer(pane, true, category("Low heels"));
        auto names = pane.m_treeModel->englishNames();
        names[QStringList{"Brand", "Low heels", "female"}.join('\x1f')] = "Women's heels";
        pane.m_treeModel->setEnglishNames(names);
        auto *tree = pane.findChild<QTreeView *>("treeViewBrandCategory");
        tree->selectionModel()->clearSelection();
        for (const auto &path : {category("Pumps"), category("Low heels")})
            tree->selectionModel()->select(node(pane.m_treeModel, path), QItemSelectionModel::Select | QItemSelectionModel::Rows);
        bool interacted = false;
        QTimer timeout;
        timeout.setSingleShot(true);
        connect(&timeout, &QTimer::timeout, this, [] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject();
        });
        timeout.start(2000);
        QTimer::singleShot(0, this, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            for (auto *radio : dialog->findChildren<QRadioButton *>())
                if (radio->text().startsWith("Pumps")) radio->setChecked(true);
            interacted = true;
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        });
        auto *button = pane.findChild<QPushButton *>("buttonMerge");
        QVERIFY(button->isEnabled());
        button->click();
        QVERIFY(interacted);
        QVERIFY(!node(pane.m_treeModel, category("Low heels")).isValid());
        QVERIFY(node(pane.m_treeModel, {"Other brand", "Low heels"}).isValid());
        QCOMPARE(pane.m_storeModel->rowCount(), 2);
        QCOMPARE(pane.m_treeModel->asinsForIndex(node(pane.m_treeModel, {"Brand"})).size(), 4);
        QCOMPARE(pane.m_treeModel->englishNameForIndex(node(pane.m_treeModel, {"Brand", "Pumps", "female"})), "Women's heels");
        roundTrip(pane);
        QVERIFY(!node(pane.m_treeModel, category("Low heels")).isValid());
    }
    void cancelAndWorkingDirectoryIsolation()
    {
        PaneStore pane;
        setup(pane);
        selectNode(pane, category("Pumps"));
        selectPump(pane);
        transfer(pane, true, category("Low heels"), true);
        QCOMPARE(pane.m_placements.placements(pane.m_items.first()).size(), 1);
        confirmButton(pane, "buttonRemoveProducts", false);
        QCOMPARE(pane.m_items.size(), 4);
        transfer(pane, true, category("Low heels"));
        QTemporaryDir empty;
        QVERIFY(empty.isValid());
        WorkingDirectoryManager::instance()->setWorkingDir(QDir(empty.path()));
        pane.setWorkingDir(QDir(empty.path()));
        QVERIFY(pane.m_items.isEmpty());
        QCOMPARE(pane.m_treeModel->rowCount({}), 0);
        QVERIFY(!pane.findChild<QPushButton *>("buttonDuplicateProducts")->isEnabled());
    }
    void mixedSelectionMovesOnlyCurrentPlacements()
    {
        PaneStore pane;
        setup(pane);
        pane.m_placements.transfer(pane.m_items, pumpAsins(), category("Pumps"), category("Low heels"), true);
        pane.m_placements.transfer(pane.m_items, {"B38"}, category("Pumps"), category("Low heels"), false);
        const auto items = pane.m_items;
        pane._applyItems(items);
        selectNode(pane, category("Low heels"));
        QCOMPARE(pane.m_storeModel->rowCount(), 2);
        QVERIFY(pane.m_storeModel->rows()[pumpRow(pane)].duplicate);
        pane.findChild<QTableView *>("tableViewAsins")->selectAll();
        transfer(pane, false, category("Square heels"));
        QCOMPARE(pane.m_storeModel->rowCount(), 2);
        QCOMPARE(pane.m_asinToItem.value("B38").category, "Square heels");
        selectNode(pane, category("Pumps"));
        QCOMPARE(pane.m_storeModel->rowCount(), 1);
        QVERIFY(!pane.m_storeModel->rows().first().duplicate);
        selectNode(pane, category("Low heels"));
        QCOMPARE(pane.m_storeModel->rowCount(), 0);
    }
    void aggregateRemovalPreservesOutsideBrand()
    {
        auto items = catalog();
        StorePlacements placements;
        placements.transfer(items, pumpAsins(), category("Pumps"), category("Low heels"), true);
        placements.transfer(items, pumpAsins(), category("Pumps"), {"Other brand", "Pumps"}, true);
        placements.remove(items, pumpAsins(), {"Brand"});
        QCOMPARE(items.size(), 4);
        for (const auto &item : items) {
            if (!item.asin.startsWith('A')) continue;
            const auto remaining = placements.placements(item);
            QCOMPARE(remaining.size(), 1);
            QCOMPARE(remaining.first().path[0], "Other brand");
            QVERIFY(remaining.first().duplicate);
        }
        placements.removeCategory(items, {"Other brand"});
        QCOMPARE(items.size(), 1);
        QCOMPARE(items.first().asin, "B38");
    }
    void duplicateExportUsesCategoryAndUniqueAsins()
    {
        StoreExportCli cli;
        PaneStore pane;
        setup(pane);
        pane.setAvailableClis({&cli});
        selectNode(pane, category("Pumps"));
        selectPump(pane);
        transfer(pane, true, category("Low heels"));
        auto names = pane.m_treeModel->englishNames();
        names[category("Low heels").join('\x1f')] = "Low heels";
        names[QStringLiteral("Brand")] = "All shoes";
        pane.m_treeModel->setEnglishNames(names);
        // Rebuild to display the English names, just as loading persisted names does.
        const auto items = pane.m_items;
        pane._applyItems(items);
        QDir dir(m_dir->path());
        QVERIFY(dir.mkpath("stores/storefront"));
        QPixmap image(4, 4);
        image.fill(Qt::white);
        QVERIFY(image.save(dir.filePath("stores/storefront/fixture.png")));
        QFile versions(dir.filePath("stores/storefront/versions.json"));
        QVERIFY(versions.open(QIODevice::WriteOnly));
        versions.write(QJsonDocument(QJsonArray{QJsonObject{{"ts", 1}, {"desktop", "fixture.png"}}}).toJson());
        versions.close();
        pane.findChild<QLineEdit *>("lineEditExportFolder")->setText(dir.filePath("exports"));

        for (const auto &path : {category("Low heels"), QStringList{"Brand"}}) {
            selectNode(pane, path);
            pane.findChild<QTableView *>("tableViewAsins")->selectAll();
            pane.findChild<QListWidget *>("listVersionStrip")->setCurrentRow(0);
            QStringList expected;
            for (const auto &row : pane.m_storeModel->rows()) expected.append(row.asin);
            QCOMPARE(expected.size(), path.size() == 1 ? 2 : 1);
            bool completed = false;
            bool success = false;
            QTimer closeDialog;
            connect(&closeDialog, &QTimer::timeout, this, [&] {
                if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                    success = box->icon() == QMessageBox::Information;
                    completed = true;
                    box->accept();
                }
            });
            closeDialog.start(10);
            pane.findChild<QPushButton *>("buttonExportProducts")->click();
            QTRY_VERIFY_WITH_TIMEOUT(completed, 3000);
            QVERIFY(success);
            const auto outputDir = dir.filePath("exports/" + path.join('/'));
            QFile asins(outputDir + "/ASINS.txt");
            QVERIFY(asins.open(QIODevice::ReadOnly));
            QCOMPARE(QString::fromUtf8(asins.readAll()).trimmed(), expected.join(','));
            QVERIFY(QFile::exists(outputDir + "/fixture.png"));
            QVERIFY(cli.prompt.contains(path.size() == 1 ? "All shoes" : "Low heels"));
        }
    }

    void placementValidationAndRefreshPaths()
    {
        StorePlacements placements;
        auto items = catalog();
        placements.load("A38", QJsonArray{QJsonObject{{"path", QJsonArray{"bad"}}}});
        QCOMPARE(placements.placements(items.first()).size(), 1);
        placements.transfer(items, pumpAsins(), category("Pumps"), category("Low heels"), true);
        items.first().category = "Updated Amazon category";
        const auto memberships = placements.placements(items.first());
        QCOMPARE(memberships.size(), 2);
        QCOMPARE(memberships.first().path[1], "Updated Amazon category");
        QCOMPARE(memberships.last().path[1], "Low heels");
        StorePlacements loaded;
        loaded.load(items.first().asin, placements.toJson(items.first()));
        QCOMPARE(loaded.placements(items.first()).size(), 2);
        QVERIFY(loaded.isDuplicate(items.first(), category("Low heels")));
    }
};

QTEST_MAIN(StoreTests)
#include "tst_store.moc"
