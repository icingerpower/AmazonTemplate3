#include <QtTest>
#include <QCoreApplication>
#include <QPixmap>
#include "TableReviews.h"

class TableReviewsTests : public QObject
{
    Q_OBJECT

private slots:
    void testInitialState();
    void testAddAndRetrieve();
    void testUpdateTranslation();
    void testUpdateImage();
    void testRemoveAt();
    void testReviewItemJson();
    void testParsedDate();
    void testSortingAndInsertionByDate();
    void testPreserveTranslationOnUpdate();
    void testEditableFlags();
    void testEmptyTranslationOnNoTranslationNeeded();
};

void TableReviewsTests::testInitialState()
{
    TableReviews table;
    QCOMPARE(table.rowCount(), 0);
    QCOMPARE(table.columnCount(), TableReviews::ColumnCount);
    QCOMPARE(table.headerData(TableReviews::ColImage, Qt::Horizontal).toString(), QStringLiteral("Image"));
    QCOMPARE(table.headerData(TableReviews::ColStars, Qt::Horizontal).toString(), QStringLiteral("Stars"));
    QCOMPARE(table.headerData(TableReviews::ColLink, Qt::Horizontal).toString(), QStringLiteral("Link"));
    QCOMPARE(table.headerData(TableReviews::ColAsin, Qt::Horizontal).toString(), QStringLiteral("ASIN"));
    QCOMPARE(table.headerData(TableReviews::ColReview, Qt::Horizontal).toString(), QStringLiteral("Review (Title + Text)"));
    QCOMPARE(table.headerData(TableReviews::ColTranslation, Qt::Horizontal).toString(), QStringLiteral("Translation (EN)"));
}

void TableReviewsTests::testAddAndRetrieve()
{
    TableReviews table;
    ReviewItem r1;
    r1.id = QStringLiteral("rev_1");
    r1.country = QStringLiteral("DE");
    r1.asin = QStringLiteral("B001234567");
    r1.productTitle = QStringLiteral("Sample Product");
    r1.stars = 4;
    r1.date = QStringLiteral("2026-09-10");
    r1.link = QStringLiteral("https://www.amazon.de/gp/customer-reviews/rev_1");
    r1.title = QStringLiteral("Sehr gut");
    r1.text = QStringLiteral("Passt perfekt und sieht gut aus.");

    bool isNew = table.addOrUpdateReview(r1);
    QVERIFY(isNew);
    QCOMPARE(table.rowCount(), 1);

    QCOMPARE(table.data(table.index(0, TableReviews::ColCountry)).toString(), QStringLiteral("DE"));
    QVERIFY(table.data(table.index(0, TableReviews::ColStars)).toString().contains(QStringLiteral("(4)")));
    QCOMPARE(table.data(table.index(0, TableReviews::ColLink)).toString(), r1.link);
    QCOMPARE(table.data(table.index(0, TableReviews::ColAsin)).toString(), r1.asin);
    QCOMPARE(table.data(table.index(0, TableReviews::ColReview)).toString(), QStringLiteral("Sehr gut\n\nPasst perfekt und sieht gut aus."));
    QCOMPARE(table.data(table.index(0, TableReviews::ColDate)).toString(), QStringLiteral("2026-09-10"));

    // Size hint for image column
    QCOMPARE(table.data(table.index(0, TableReviews::ColImage), Qt::SizeHintRole).toSize(), QSize(58, 58));

    // Update existing review
    r1.title = QStringLiteral("Ausgezeichnet");
    bool isUpdated = table.addOrUpdateReview(r1);
    QVERIFY(!isUpdated);
    QCOMPARE(table.rowCount(), 1);
    QCOMPARE(table.data(table.index(0, TableReviews::ColReview)).toString(), QStringLiteral("Ausgezeichnet\n\nPasst perfekt und sieht gut aus."));
}

void TableReviewsTests::testUpdateTranslation()
{
    TableReviews table;
    ReviewItem r;
    r.id = QStringLiteral("rev_2");
    r.title = QStringLiteral("Bello");
    r.text = QStringLiteral("Ottimo prodotto");
    table.addOrUpdateReview(r);

    QCOMPARE(table.data(table.index(0, TableReviews::ColTranslation)).toString(), QString());

    table.updateTranslation(0, QStringLiteral("Title: Beautiful\nReview: Great product"));
    QCOMPARE(table.data(table.index(0, TableReviews::ColTranslation)).toString(), QStringLiteral("Title: Beautiful\nReview: Great product"));
}

void TableReviewsTests::testUpdateImage()
{
    TableReviews table;
    ReviewItem r;
    r.id = QStringLiteral("rev_img");
    r.asin = QStringLiteral("B00IMAGE01");
    table.addOrUpdateReview(r);

    QPixmap px(30, 30);
    px.fill(Qt::red);
    table.updateImage(QStringLiteral("B00IMAGE01"), px);

    QVariant imgData = table.data(table.index(0, TableReviews::ColImage), Qt::DecorationRole);
    QVERIFY(!imgData.isNull());
    QCOMPARE(imgData.value<QPixmap>().size(), QSize(30, 30));
}

void TableReviewsTests::testRemoveAt()
{
    TableReviews table;
    ReviewItem r1; r1.id = QStringLiteral("1");
    ReviewItem r2; r2.id = QStringLiteral("2");
    table.addOrUpdateReview(r1);
    table.addOrUpdateReview(r2);
    QCOMPARE(table.rowCount(), 2);

    table.removeAt(0);
    QCOMPARE(table.rowCount(), 1);
    QCOMPARE(table.reviewAt(0).id, QStringLiteral("2"));

    table.clear();
    QCOMPARE(table.rowCount(), 0);
}

void TableReviewsTests::testReviewItemJson()
{
    ReviewItem r;
    r.id = QStringLiteral("test_id");
    r.country = QStringLiteral("JP");
    r.asin = QStringLiteral("B00JP00001");
    r.productTitle = QStringLiteral("JP Product");
    r.imageUrl = QStringLiteral("https://m.media-amazon.com/images/I/abc.jpg");
    r.stars = 5;
    r.date = QStringLiteral("2026-08-15");
    r.link = QStringLiteral("https://www.amazon.co.jp/gp/customer-reviews/test_id");
    r.title = QStringLiteral("最高");
    r.text = QStringLiteral("とても良いです");
    r.translation = QStringLiteral("Best. Very good.");
    r.translationChecked = true;

    QJsonObject json = r.toJson();
    ReviewItem parsed = ReviewItem::parse(json);

    QCOMPARE(parsed.id, r.id);
    QCOMPARE(parsed.country, r.country);
    QCOMPARE(parsed.asin, r.asin);
    QCOMPARE(parsed.productTitle, r.productTitle);
    QCOMPARE(parsed.imageUrl, r.imageUrl);
    QCOMPARE(parsed.stars, r.stars);
    QCOMPARE(parsed.date, r.date);
    QCOMPARE(parsed.link, r.link);
    QCOMPARE(parsed.title, r.title);
    QCOMPARE(parsed.text, r.text);
    QCOMPARE(parsed.translation, r.translation);
    QCOMPARE(parsed.translationChecked, r.translationChecked);
}

void TableReviewsTests::testParsedDate()
{
    ReviewItem r;

    // ISO
    r.date = QStringLiteral("2026-09-06");
    QCOMPARE(r.parsedDate(), QDate(2026, 9, 6));

    // English "6 September 2026"
    r.date = QStringLiteral("6 September 2026");
    QCOMPARE(r.parsedDate(), QDate(2026, 9, 6));

    // English "September 6, 2026"
    r.date = QStringLiteral("September 6, 2026");
    QCOMPARE(r.parsedDate(), QDate(2026, 9, 6));

    // French "15 août 2026"
    r.date = QStringLiteral("15 août 2026");
    QCOMPARE(r.parsedDate(), QDate(2026, 8, 15));

    // German "6. September 2026"
    r.date = QStringLiteral("6. September 2026");
    QCOMPARE(r.parsedDate(), QDate(2026, 9, 6));

    // Spanish "6 de septiembre de 2026"
    r.date = QStringLiteral("6 de septiembre de 2026");
    QCOMPARE(r.parsedDate(), QDate(2026, 9, 6));

    // Italian "20 agosto 2026"
    r.date = QStringLiteral("20 agosto 2026");
    QCOMPARE(r.parsedDate(), QDate(2026, 8, 20));

    // Japanese "2026年9月6日"
    r.date = QStringLiteral("2026年9月6日");
    QCOMPARE(r.parsedDate(), QDate(2026, 9, 6));

    // Full Amazon strings with "Review by ... on ..."
    r.date = QStringLiteral("Review by Amazon Customer on 28 July 2026");
    QCOMPARE(r.parsedDate(), QDate(2026, 7, 28));

    r.date = QStringLiteral("Review by Inspekteur on 6 September 2026");
    QCOMPARE(r.parsedDate(), QDate(2026, 9, 6));

    r.date = QStringLiteral("Review by Dawn on July 9, 2026");
    QCOMPARE(r.parsedDate(), QDate(2026, 7, 9));

    r.date = QStringLiteral("Review by Mario Adrián Moya Acosta on August 27, 2026");
    QCOMPARE(r.parsedDate(), QDate(2026, 8, 27));

    // Invalid / empty
    r.date = QStringLiteral("");
    QVERIFY(!r.parsedDate().isValid());
    r.date = QStringLiteral("Unknown date format");
    QVERIFY(!r.parsedDate().isValid());
}

void TableReviewsTests::testSortingAndInsertionByDate()
{
    TableReviews table;

    ReviewItem rOld;
    rOld.id = QStringLiteral("old");
    rOld.date = QStringLiteral("1 January 2024");

    ReviewItem rMid;
    rMid.id = QStringLiteral("mid");
    rMid.date = QStringLiteral("15 June 2025");

    ReviewItem rNew;
    rNew.id = QStringLiteral("new");
    rNew.date = QStringLiteral("10 September 2026");

    // Insert in non-chronological order: mid, old, new
    table.addOrUpdateReview(rMid);
    table.addOrUpdateReview(rOld);
    table.addOrUpdateReview(rNew);

    QCOMPARE(table.rowCount(), 3);
    // Descending order: new first, then mid, then old
    QCOMPARE(table.reviewAt(0).id, QStringLiteral("new"));
    QCOMPARE(table.reviewAt(1).id, QStringLiteral("mid"));
    QCOMPARE(table.reviewAt(2).id, QStringLiteral("old"));

    // Test sort ascending
    table.sort(TableReviews::ColDate, Qt::AscendingOrder);
    QCOMPARE(table.reviewAt(0).id, QStringLiteral("old"));
    QCOMPARE(table.reviewAt(1).id, QStringLiteral("mid"));
    QCOMPARE(table.reviewAt(2).id, QStringLiteral("new"));

    // Test sort descending
    table.sort(TableReviews::ColDate, Qt::DescendingOrder);
    QCOMPARE(table.reviewAt(0).id, QStringLiteral("new"));
    QCOMPARE(table.reviewAt(1).id, QStringLiteral("mid"));
    QCOMPARE(table.reviewAt(2).id, QStringLiteral("old"));

    // Test setReviews also sorts descending
    QList<ReviewItem> list = {rOld, rNew, rMid};
    table.setReviews(list);
    QCOMPARE(table.reviewAt(0).id, QStringLiteral("new"));
    QCOMPARE(table.reviewAt(1).id, QStringLiteral("mid"));
    QCOMPARE(table.reviewAt(2).id, QStringLiteral("old"));
}

void TableReviewsTests::testPreserveTranslationOnUpdate()
{
    TableReviews table;

    ReviewItem r;
    r.id = QStringLiteral("rev_tr");
    r.date = QStringLiteral("2026-09-01");
    r.title = QStringLiteral("Sehr gut");
    r.text = QStringLiteral("Gefällt mir");
    table.addOrUpdateReview(r);

    table.updateTranslation(0, QStringLiteral("Very good\nI like it"));
    QCOMPARE(table.data(table.index(0, TableReviews::ColTranslation)).toString(), QStringLiteral("Very good\nI like it"));

    // Now pretend scraper runs again and fetches the same review without translation
    ReviewItem scrapedAgain = r;
    scrapedAgain.translation.clear();
    scrapedAgain.text = QStringLiteral("Gefällt mir sehr");
    table.addOrUpdateReview(scrapedAgain);

    // Translation should be preserved!
    QCOMPARE(table.data(table.index(0, TableReviews::ColTranslation)).toString(), QStringLiteral("Very good\nI like it"));
    QCOMPARE(table.reviewAt(0).translation, QStringLiteral("Very good\nI like it"));
}

void TableReviewsTests::testEditableFlags()
{
    TableReviews table;
    ReviewItem r;
    r.id = QStringLiteral("rev_flag");
    r.date = QStringLiteral("2026-09-01");
    table.addOrUpdateReview(r);

    // ColReview, ColTranslation, and ColAsin must have Qt::ItemIsEditable
    Qt::ItemFlags reviewFlags = table.flags(table.index(0, TableReviews::ColReview));
    QVERIFY(reviewFlags & Qt::ItemIsEditable);

    Qt::ItemFlags trFlags = table.flags(table.index(0, TableReviews::ColTranslation));
    QVERIFY(trFlags & Qt::ItemIsEditable);

    Qt::ItemFlags asinFlags = table.flags(table.index(0, TableReviews::ColAsin));
    QVERIFY(asinFlags & Qt::ItemIsEditable);

    // ColLink, ColStars, ColDate must NOT have Qt::ItemIsEditable
    Qt::ItemFlags linkFlags = table.flags(table.index(0, TableReviews::ColLink));
    QVERIFY(!(linkFlags & Qt::ItemIsEditable));

    Qt::ItemFlags starsFlags = table.flags(table.index(0, TableReviews::ColStars));
    QVERIFY(!(starsFlags & Qt::ItemIsEditable));

    Qt::ItemFlags dateFlags = table.flags(table.index(0, TableReviews::ColDate));
    QVERIFY(!(dateFlags & Qt::ItemIsEditable));
}

void TableReviewsTests::testEmptyTranslationOnNoTranslationNeeded()
{
    TableReviews table;
    ReviewItem r;
    r.id = QStringLiteral("rev_en");
    r.title = QStringLiteral("Good shoes");
    r.text = QStringLiteral("Very comfortable");
    table.addOrUpdateReview(r);

    QVERIFY(!table.reviewAt(0).translationChecked);

    // Leave empty when no translation needed
    table.updateTranslation(0, QString());
    QVERIFY(table.reviewAt(0).translationChecked);
    QCOMPARE(table.data(table.index(0, TableReviews::ColTranslation)).toString(), QString());

    // Legacy migration test: (Original is EN/FR) is cleared and marked checked
    ReviewItem legacy;
    legacy.id = QStringLiteral("leg_1");
    legacy.translation = QStringLiteral("(Original is EN/FR)");
    QJsonObject json = legacy.toJson();
    ReviewItem migrated = ReviewItem::parse(json);
    QCOMPARE(migrated.translation, QString());
    QVERIFY(migrated.translationChecked);
}

QTEST_MAIN(TableReviewsTests)
#include "tst_tablereviews.moc"
