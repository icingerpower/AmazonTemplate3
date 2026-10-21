#include "BrokenChildTable.h"
#include "apis/AmazonCatalogApi.h"
#include <QCoro/QCoroTask>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

class BrokenChildTableTests : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void automaticRepairsKeepExistingRules();
    void forceHealthyRowWithoutTargetingSiblings();
    void forceZeroImagesAndRespectRequestedRepairs();
    void invalidRowsDoNotFallBackToFixAll();
    void forcedImagesUseAnotherSameColorChild();
    void brokenMarketplacesAndAllZeroNotWarning();
    void populatePrefersEuSizeOverUs();

private:
    BrokenChildTable m_table;
};

void BrokenChildTableTests::init()
{
    m_table.setMarketplaces({{"FR", "FR"}, {"DE", "DE"}, {"US", "US", false},
                             {"IT", "IT"}, {"ES", "ES"}});
    auto health = [](bool loaded, bool exists, bool parent, int images) {
        return QJsonObject{{"loaded", loaded}, {"exists", exists},
                           {"hasParent", parent}, {"imageCount", images}};
    };
    auto row = [](const QString &asin, const QString &color, const QJsonArray &cells) {
        return QJsonObject{{"asin", asin}, {"color", color}, {"health", cells}};
    };
    const QJsonArray healthy{health(true, true, true, 7), health(true, true, true, 7),
                             health(true, true, true, 7), health(false, true, true, 7),
                             health(true, false, true, 7)};
    const QJsonArray broken{health(true, true, false, 3), health(true, true, true, 0)};
    const QJsonArray rows{row("selected", "Burgundy", healthy),
                          row("sibling", "burgundy", healthy),
                          row("broken", "burgundy", broken),
                          row("other-color", "Black", QJsonArray{health(true, true, true, 9)})};
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile file(QDir(dir.path()).filePath("broken_child_health.json"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(QJsonObject{
        {"marketplaces", QJsonArray{"FR", "DE", "US", "IT", "ES"}},
        {"rows", rows}}).toJson());
    file.close();
    QVERIFY(m_table.loadFromDir(QDir(dir.path())));
}

void BrokenChildTableTests::automaticRepairsKeepExistingRules()
{
    const auto targets = m_table.getFixTargets(true, true);
    QCOMPARE(targets.size(), 1);
    QCOMPARE(targets.first().rowIdx, 2);
    QCOMPARE(targets.first().mktIdx, 0);
    QVERIFY(targets.first().needsParent);
    QVERIFY(targets.first().needsImages);
}

void BrokenChildTableTests::forceHealthyRowWithoutTargetingSiblings()
{
    const auto targets = m_table.getFixTargets(true, true, 0);
    QCOMPARE(targets.size(), 2); // inactive, unloaded and absent marketplaces excluded
    for (int i = 0; i < targets.size(); ++i) {
        QCOMPARE(targets[i].rowIdx, 0);
        QCOMPARE(targets[i].mktIdx, i);
        QVERIFY(targets[i].needsParent);
        QVERIFY(targets[i].needsImages);
    }
    // Forcing a repair must not alter cached health or later automatic targeting.
    QCOMPARE(m_table.rows().first().health.first().imageCount, 7);
    QVERIFY(m_table.rows().first().health.first().hasParent);
    automaticRepairsKeepExistingRules();
    m_table.setMarketplaceActive("FR", false);
    const auto remaining = m_table.getFixTargets(true, true, 0);
    QCOMPARE(remaining.size(), 1);
    QCOMPARE(remaining.first().mktIdx, 1);
}

void BrokenChildTableTests::forceZeroImagesAndRespectRequestedRepairs()
{
    const auto images = m_table.getFixTargets(false, true, 2);
    QCOMPARE(images.size(), 2);
    for (const auto &target : images) {
        QCOMPARE(target.rowIdx, 2);
        QVERIFY(!target.needsParent);
        QVERIFY(target.needsImages);
    }
    const auto parents = m_table.getFixTargets(true, false, 0);
    QCOMPARE(parents.size(), 2);
    for (const auto &target : parents) {
        QVERIFY(target.needsParent);
        QVERIFY(!target.needsImages);
    }
    QVERIFY(m_table.getFixTargets(false, false, 0).isEmpty());
}

void BrokenChildTableTests::invalidRowsDoNotFallBackToFixAll()
{
    QVERIFY(m_table.getFixTargets(true, true, -2).isEmpty());
    QVERIFY(m_table.getFixTargets(true, true, m_table.rowCount()).isEmpty());
}

void BrokenChildTableTests::forcedImagesUseAnotherSameColorChild()
{
    QCOMPARE(m_table.bestImageSourceAsin("burgundy", 0), QString("selected"));
    QCOMPARE(m_table.bestImageSourceAsin("burgundy", 0, "selected"), QString("sibling"));
    QVERIFY(m_table.bestImageSourceAsin("black", 0, "other-color").isEmpty());
    QVERIFY(m_table.bestImageSourceAsin("burgundy", 3, "selected").isEmpty());
    QVERIFY(m_table.bestImageSourceAsin("burgundy", 4, "selected").isEmpty());
    QVERIFY(m_table.bestImageSourceAsin("burgundy", -1, "selected").isEmpty());
    QVERIFY(m_table.bestImageSourceAsin("burgundy", 5, "selected").isEmpty());
}

void BrokenChildTableTests::brokenMarketplacesAndAllZeroNotWarning()
{
    BrokenChildTable table;
    table.setMarketplaces({{"UK", "UK"}, {"FR", "FR"}, {"PL", "PL"}, {"MX", "MX"}, {"JP", "JP"}});
    auto health = [](bool loaded, bool exists, bool parent, int images) {
        return QJsonObject{{"loaded", loaded}, {"exists", exists},
                           {"hasParent", parent}, {"imageCount", images}};
    };
    auto row = [](const QString &asin, const QString &color, const QJsonArray &cells) {
        return QJsonObject{{"asin", asin}, {"color", color}, {"health", cells}};
    };
    // UK: all healthy (✓ 6)
    // FR: all have images but missing parent (✗ 6) -> broken
    // PL: child1 has 0 images (✗ 0), child2 has 6 images (✓ 6) -> broken
    // MX & JP: all children have 0 images (✗ 0) -> page not created, must not warn
    const QJsonArray rows{
        row("c1", "Red", QJsonArray{
            health(true, true, true, 6),
            health(true, true, false, 6),
            health(true, true, false, 0),
            health(true, true, false, 0),
            health(true, true, false, 0)
        }),
        row("c2", "Red", QJsonArray{
            health(true, true, true, 6),
            health(true, true, false, 6),
            health(true, true, true, 6),
            health(true, true, false, 0),
            health(true, true, false, 0)
        })
    };
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile file(QDir(dir.path()).filePath("broken_child_health.json"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(QJsonObject{
        {"marketplaces", QJsonArray{"UK", "FR", "PL", "MX", "JP"}},
        {"rows", rows}}).toJson());
    file.close();
    QVERIFY(table.loadFromDir(QDir(dir.path())));

    QCOMPARE(table.brokenMarketplaceCodes(), (QStringList{"FR", "PL"}));

    // Column indices: 6 fixed cols + mktIdx (0=UK, 1=FR, 2=PL, 3=MX, 4=JP)
    QVERIFY(!table.data(table.index(0, 6 + 0), Qt::BackgroundRole).isValid()); // UK healthy
    QVERIFY(table.data(table.index(0, 6 + 1), Qt::BackgroundRole).isValid());  // FR broken
    QVERIFY(table.data(table.index(0, 6 + 2), Qt::BackgroundRole).isValid());  // PL c1 broken (0 imgs while c2 has 6)
    QVERIFY(!table.data(table.index(1, 6 + 2), Qt::BackgroundRole).isValid()); // PL c2 healthy
    QVERIFY(!table.data(table.index(0, 6 + 3), Qt::BackgroundRole).isValid()); // MX all 0 -> not warning
    QVERIFY(!table.data(table.index(1, 6 + 3), Qt::BackgroundRole).isValid()); // MX all 0 -> not warning
    QVERIFY(!table.data(table.index(0, 6 + 4), Qt::BackgroundRole).isValid()); // JP all 0 -> not warning
}

void BrokenChildTableTests::populatePrefersEuSizeOverUs()
{
#ifdef AMAZONCATALOGAPI_UNIT_TESTS
    AmazonCatalogApi api("id", "secret", "rt", "rt", "rt");
    api.setMockForTests([](const QString &/*path*/, const QMap<QString, QString> &qp) -> QByteArray {
        const QString mp = qp.value("marketplaceIds");
        QString sz;
        if (mp == QLatin1String("A1PA6795UKMFR9"))
            sz = QStringLiteral("35 EU Schmal"); // DE
        else if (mp == QLatin1String("ATVPDKIKX0DER"))
            sz = QStringLiteral("4 Narrow");     // US
        // FR has empty size (broken)
        QJsonObject attrs;
        if (!sz.isEmpty())
            attrs.insert("size", QJsonArray{QJsonObject{{"value", sz}}});
        return QJsonDocument(QJsonObject{
            {"asin", "B0FY6NWC8D"},
            {"attributes", attrs}
        }).toJson();
    });

    BrokenChildTable table;
    table.setMarketplaces({
        {"A1PA6795UKMFR9", "DE", true},
        {"A13V1IB3VIYZZH", "FR", true},
        {"ATVPDKIKX0DER",  "US", true},
    });
    BrokenChildTable::ChildEntry entry;
    entry.asin = QStringLiteral("B0FY6NWC8D");
    entry.color = QStringLiteral("Red and Black Cheetah");
    QCoro::waitFor(table.populate(&api, {entry}));

    QCOMPARE(table.rowCount(), 1);
    QCOMPARE(table.rows().first().size, QStringLiteral("35 EU Schmal"));
    QCOMPARE(table.rows().first().sizeSource, QStringLiteral("DE"));
#endif
}

QTEST_GUILESS_MAIN(BrokenChildTableTests)
#include "tst_brokenchildtable.moc"
