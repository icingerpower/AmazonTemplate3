#include "BrokenChildTable.h"

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

QTEST_GUILESS_MAIN(BrokenChildTableTests)
#include "tst_brokenchildtable.moc"
