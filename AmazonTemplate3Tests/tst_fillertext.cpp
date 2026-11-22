#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimer>

#include "OpenAi2.h"
#include "xlsxdocument.h"
#include "fillers/FillerText.h"
#include "TemplateFiller.h"
#include "AttributeFlagsTable.h"
#include "AiFailureTable.h"

namespace {
QString createMinimalTemplate(const QTemporaryDir &temp, const QString &fileName)
{
    const QString path = temp.filePath(fileName);
    QXlsx::Document doc;
    doc.addSheet("Template");
    doc.selectSheet("Template");
    doc.write(1, 1, "Settings");
    doc.write(5, 1, "contribution_sku#1.value");
    doc.write(5, 2, "product_type#1.value");
    doc.write(5, 3, "color#1.value");
    doc.write(6, 1, "ABC123");
    doc.write(7, 1, "P-HEELS");
    doc.write(7, 2, "SHOES");
    doc.addSheet("Data Definitions");
    doc.addSheet("Valid Values");
    doc.selectSheet("Template");
    doc.saveAs(path);
    return path;
}
} // namespace

class FillerTextTests : public QObject
{
    Q_OBJECT

private slots:
    void cleanup() { OpenAi2::instance()->resetForTests(); }

    void parseAndValidateRules_data();
    void parseAndValidateRules();
    void translatesColorWithoutParenthesesAndRetriesWithFeedback();
    void invalidatesCachedFullWidthParenthesesColor();
    void isolatesTaskFailureAndSavesSiblingTranslations();
};

void FillerTextTests::parseAndValidateRules_data()
{
    QTest::addColumn<QString>("fieldId");
    QTest::addColumn<QString>("reply");
    QTest::addColumn<bool>("expectedValid");
    QTest::addColumn<QString>("expectedFormatted");

    QTest::newRow("polish-parentheses-rejected")
            << "color#1.value"
            << QStringLiteral(R"json({"value":"Złamana biel (kość słoniowa)"})json")
            << false << QString{};
    QTest::newRow("japanese-fullwidth-parentheses-rejected")
            << "color#1.value"
            << QStringLiteral(R"json({"value":"オフホワイト（アイボリー）"})json")
            << false << QString{};
    QTest::newRow("polish-without-parentheses-valid")
            << "color#1.value"
            << QStringLiteral(R"json({"value":"złamana biel kość słoniowa"})json")
            << true << QStringLiteral("Złamana biel kość słoniowa");
    QTest::newRow("color-angle-brackets-rejected")
            << "color#1.value"
            << QStringLiteral(R"json({"value":"<b>Blanc</b>"})json")
            << false << QString{};
    QTest::newRow("color-over-40-chars-rejected")
            << "color#1.value"
            << QStringLiteral(R"json({"value":"Złamana biel kość słoniowa bardzo jasny odcień kremowy"})json")
            << false << QString{};
    QTest::newRow("description-allowed-html-valid")
            << "product_description#1.value"
            << QStringLiteral(R"json({"value":"<p><b>Details</b><br>Nice shoes</p><ul><li>Soft</li></ul>"})json")
            << true << QStringLiteral("<p><b>Details</b><br>Nice shoes</p><ul><li>Soft</li></ul>");
    QTest::newRow("description-disallowed-html-attr-rejected")
            << "product_description#1.value"
            << QStringLiteral(R"json({"value":"<p class=\"x\">Details</p>"})json")
            << false << QString{};
    QTest::newRow("generic-text-parentheses-allowed-but-html-rejected")
            << "care_instructions#1.value"
            << QStringLiteral(R"json({"value":"hand wash (cold)"})json")
            << true << QStringLiteral("Hand wash (cold)");
}

void FillerTextTests::parseAndValidateRules()
{
    QFETCH(QString, fieldId);
    QFETCH(QString, reply);
    QFETCH(bool, expectedValid);
    QFETCH(QString, expectedFormatted);

    QString formatted;
    QCOMPARE(FillerText::parseAndValidate(fieldId, reply, formatted), expectedValid);
    if (expectedValid)
    {
        QCOMPARE(formatted, expectedFormatted);
    }
}

void FillerTextTests::translatesColorWithoutParenthesesAndRetriesWithFeedback()
{
    auto *ai = OpenAi2::instance();
    ai->resetForTests();
    ai->init("offline-test-placeholder");

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = createMinimalTemplate(temp, "PUMPS-092-TOFILLNEW-FR.xlsx");

    TemplateFiller templateFiller(temp.path(), path, {}, {}, {});
    auto *flags = templateFiller.attributeFlagsTable();
    flags->recordAttribute({{Attribute::AMAZON_V02, "color#1.value"}}, Attribute::ChildOnly);

    QStringList seenPrompts;
    int calls = 0;
    ai->setTransportForTests([&](const QString &, const QString &prompt, const QList<QString> &,
                                 std::function<void(QString)> ok,
                                 std::function<void(OpenAi2::TransportError)>) {
        ++calls;
        seenPrompts.append(prompt);
        // First attempt returns parenthetical Polish translation; second obeys retry feedback.
        const QString reply = (calls == 1)
                ? QStringLiteral(R"json({"value":"Złamana biel (kość słoniowa)"})json")
                : QStringLiteral(R"json({"value":"Złamana biel kość słoniowa"})json");
        QTimer::singleShot(0, [ok, reply] { ok(reply); });
    });

    const QHash<QString, QHash<QString, QSet<QString>>> parents{
        {"P-HEELS", {{"Blanc cassé ivoire", {"SKU-IVORY-36"}}}}
    };
    using Values = QHash<QString, QHash<QString, QString>>;
    const Values fromValues{
        {"P-HEELS", {}},
        {"SKU-IVORY-36", {{"color#1.value", "Blanc cassé ivoire"}}}
    };
    const QHash<QString, QMap<QString, QString>> valuesForAi{
        {"SKU-IVORY-36", {{"0_ai_description", "Off-white women's pumps"}}}
    };
    Values common;
    Values output;

    FillerText filler;
    auto runFill = [&]() -> QCoro::Task<void> {
        co_await filler.fill(&templateFiller, parents,
                             Attribute::AMAZON_V02, Attribute::AMAZON_V02,
                             "color#1.value", "color#1.value", nullptr,
                             "SHOES", "SHOES", "FR", "FR", "PL", "PL",
                             {}, {}, AbstractFiller::Female, AbstractFiller::Adult,
                             fromValues, valuesForAi, {}, common, output);
    };

    bool done = false;
    auto task = runFill().then([&] { done = true; });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);

    QCOMPARE(calls, 2);
    // Initial translation prompt must already instruct no parentheses and max length.
    QVERIFY(seenPrompts[0].contains("Blanc cassé ivoire"));
    QVERIFY(seenPrompts[0].contains("parenthes", Qt::CaseInsensitive));
    QVERIFY(seenPrompts[0].contains("40"));
    // Retry prompt must warn that previous reply failed validation.
    QVERIFY(seenPrompts[1].contains("failed validation", Qt::CaseInsensitive));

    QCOMPARE(output["SKU-IVORY-36"]["color#1.value"], QStringLiteral("Złamana biel kość słoniowa"));
    QCOMPARE(templateFiller.aiFailureTable()->rowCount(), 0);
}

void FillerTextTests::invalidatesCachedFullWidthParenthesesColor()
{
    auto *ai = OpenAi2::instance();
    ai->resetForTests();
    ai->init("offline-test-placeholder");

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = createMinimalTemplate(temp, "PUMPS-092-TOFILLNEW-FR.xlsx");

    TemplateFiller templateFiller(temp.path(), path, {}, {}, {});
    auto *flags = templateFiller.attributeFlagsTable();
    flags->recordAttribute({{Attribute::AMAZON_V02, "color#1.value"}}, Attribute::ChildOnly);

    const QString valueId = QStringLiteral("all_Amazon V02_JP_JP_P-HEELS_Blanc cassé ivoire_color#1.value");
    templateFiller.saveAiValue("filledTexts.ini", valueId,
                               QStringLiteral(R"({"value":"オフホワイト（アイボリー）"})"));

    int calls = 0;
    ai->setTransportForTests([&](const QString &, const QString &, const QList<QString> &,
                                 std::function<void(QString)> ok,
                                 std::function<void(OpenAi2::TransportError)>) {
        ++calls;
        QTimer::singleShot(0, [ok] {
            ok(QStringLiteral(R"({"value":"オフホワイト アイボリー"})"));
        });
    });

    const QHash<QString, QHash<QString, QSet<QString>>> parents{
        {"P-HEELS", {{"Blanc cassé ivoire", {"SKU-IVORY-36"}}}}
    };
    using Values = QHash<QString, QHash<QString, QString>>;
    const Values fromValues{{"SKU-IVORY-36", {{"color#1.value", "Blanc cassé ivoire"}}}};
    const QHash<QString, QMap<QString, QString>> valuesForAi{
        {"SKU-IVORY-36", {{"0_ai_description", "Off-white women's pumps"}}}
    };
    Values common;
    Values output;

    FillerText filler;
    auto runFill = [&]() -> QCoro::Task<void> {
        co_await filler.fill(&templateFiller, parents,
                             Attribute::AMAZON_V02, Attribute::AMAZON_V02,
                             "color#1.value", "color#1.value", nullptr,
                             "SHOES", "SHOES", "FR", "FR", "JP", "JP",
                             {}, {}, AbstractFiller::Female, AbstractFiller::Adult,
                             fromValues, valuesForAi, {}, common, output);
    };

    bool done = false;
    auto task = runFill().then([&] { done = true; });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);

    QCOMPARE(calls, 1);
    QCOMPARE(output["SKU-IVORY-36"]["color#1.value"], QStringLiteral("オフホワイト アイボリー"));
}

void FillerTextTests::isolatesTaskFailureAndSavesSiblingTranslations()
{
    auto *ai = OpenAi2::instance();
    ai->resetForTests();
    ai->init("offline-test-placeholder");

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = createMinimalTemplate(temp, "PUMPS-092-TOFILLNEW-FR.xlsx");

    TemplateFiller templateFiller(temp.path(), path, {}, {}, {});
    auto *flags = templateFiller.attributeFlagsTable();
    flags->recordAttribute({{Attribute::AMAZON_V02, "color#1.value"}}, Attribute::ChildOnly);

    ai->setTransportForTests([&](const QString &, const QString &prompt, const QList<QString> &,
                                 std::function<void(QString)> ok,
                                 std::function<void(OpenAi2::TransportError)>) {
        const QString reply = prompt.contains("Blanc cassé ivoire")
                ? QStringLiteral(R"json({"value":"Złamana biel (kość słoniowa)"})json")
                : QStringLiteral(R"json({"value":"Czarny"})json");
        QTimer::singleShot(0, [ok, reply] { ok(reply); });
    });

    const QHash<QString, QHash<QString, QSet<QString>>> parents{
        {"P-HEELS", {
            {"Blanc cassé ivoire", {"SKU-IVORY-36"}},
            {"Noir", {"SKU-BLACK-36"}}
        }}
    };
    using Values = QHash<QString, QHash<QString, QString>>;
    const Values fromValues{
        {"SKU-IVORY-36", {{"color#1.value", "Blanc cassé ivoire"}}},
        {"SKU-BLACK-36", {{"color#1.value", "Noir"}}}
    };
    const QHash<QString, QMap<QString, QString>> valuesForAi{
        {"SKU-IVORY-36", {{"0_ai_description", "Off-white women's pumps"}}},
        {"SKU-BLACK-36", {{"0_ai_description", "Black women's pumps"}}}
    };
    Values common;
    Values output;

    FillerText filler;
    bool threw = false;
    auto runFill = [&]() -> QCoro::Task<void> {
        try {
            co_await filler.fill(&templateFiller, parents,
                                 Attribute::AMAZON_V02, Attribute::AMAZON_V02,
                                 "color#1.value", "color#1.value", nullptr,
                                 "SHOES", "SHOES", "FR", "FR", "PL", "PL",
                                 {}, {}, AbstractFiller::Female, AbstractFiller::Adult,
                                 fromValues, valuesForAi, {}, common, output);
        } catch (...) {
            threw = true;
        }
    };

    bool done = false;
    auto task = runFill().then([&] { done = true; });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);

    // One invalid translation must be recorded in AiFailureTable without aborting sibling SKUs.
    QVERIFY(!threw);
    QCOMPARE(templateFiller.aiFailureTable()->rowCount(), 1);
    QCOMPARE(output["SKU-BLACK-36"]["color#1.value"], QStringLiteral("Czarny"));
    QVERIFY(templateFiller.hasAiValue("filledTexts.ini",
                                      "all_Amazon V02_PL_PL_P-HEELS_Noir_color#1.value"));
    QVERIFY(!output["SKU-IVORY-36"].contains("color#1.value")
            || output["SKU-IVORY-36"]["color#1.value"].isEmpty());
}

QTEST_GUILESS_MAIN(FillerTextTests)
#include "tst_fillertext.moc"
