#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimer>

#include "fillers/TitleTranslation.h"
#include "fillers/FillerTitle.h"
#include "TemplateFiller.h"
#include "AttributeFlagsTable.h"

class FillerTitleTests : public QObject
{
    Q_OBJECT

private slots:
    void separatesVariation_data();
    void separatesVariation();
    void validatesMeasurements_data();
    void validatesMeasurements();
    void promptsUseOnlyBody();
    void rejectsMalformedFinalReplies();
    void fillsTitleAndPreservesVariation_data();
    void fillsTitleAndPreservesVariation();
    void cleanup() { OpenAi2::instance()->resetForTests(); }
};

void FillerTitleTests::separatesVariation_data()
{
    QTest::addColumn<QString>("title");
    QTest::addColumn<QString>("body");
    QTest::addColumn<QString>("variation");
    QTest::newRow("original")
            << "Escarpins À Talon Aiguille 7 cm - Bride Élastiquée (Guépard, 34)"
            << "Escarpins À Talon Aiguille 7 cm - Bride Élastiquée" << "Guépard, 34";
    QTest::newRow("reuse-generated-child")
            << "Pumps With 2.76 in (7 cm) Heels - Elastic Strap (Leopard, 34)"
            << "Pumps With 2.76 in (7 cm) Heels - Elastic Strap" << "Leopard, 34";
    QTest::newRow("measurement-at-end")
            << "Pumps With 2.76 in (7 cm)" << "Pumps With 2.76 in (7 cm)" << "";
    QTest::newRow("measurement-in-middle")
            << "Pumps With 2.76 in (7 cm) Heels" << "Pumps With 2.76 in (7 cm) Heels" << "";
    QTest::newRow("dimension-at-end")
            << "Mat 3.94 x 7.87 in (10 x 20 cm)" << "Mat 3.94 x 7.87 in (10 x 20 cm)" << "";
    QTest::newRow("no-space-before-suffix")
            << "Pumps 7 cm(Leopard, 34)" << "Pumps 7 cm" << "Leopard, 34";
    QTest::newRow("size-only") << "Pumps (34)" << "Pumps" << "34";
    QTest::newRow("cm-in-variation")
            << "Mat (Blue, 10 cm)" << "Mat" << "Blue, 10 cm";
    QTest::newRow("cm-size-only-variation")
            << "Mat (10 cm)" << "Mat" << "10 cm";
}

void FillerTitleTests::separatesVariation()
{
    QFETCH(QString, title);
    QFETCH(QString, body);
    QFETCH(QString, variation);
    const auto parts = TitleTranslation::splitTitle(title);
    QCOMPARE(parts.body, body);
    QCOMPARE(parts.variation, variation);
}

void FillerTitleTests::validatesMeasurements_data()
{
    QTest::addColumn<QString>("source");
    QTest::addColumn<QString>("language");
    QTest::addColumn<QString>("reply");
    QTest::addColumn<bool>("valid");
    QTest::newRow("heel") << "Escarpins Talon 7 cm" << "EN" << "Pumps With 2.76 in (7 cm) Heels" << true;
    QTest::newRow("cm-only") << "Escarpins Talon 7 cm" << "EN" << "Pumps With 7 cm Heels" << false;
    QTest::newRow("wrong-inches") << "Talon 7 cm" << "EN" << "Heels 2.75 in (7 cm)" << false;
    QTest::newRow("wrong-cm") << "Talon 7 cm" << "EN" << "Heels 3.15 in (8 cm)" << false;
    QTest::newRow("missing-measurement") << "Talon 7 cm" << "EN" << "Heels" << false;
    QTest::newRow("duplicate") << "Talon 7 cm" << "EN" << "Heels 2.76 in (7 cm) 2.76 in (7 cm)" << false;
    QTest::newRow("invented") << "Escarpins" << "EN" << "Pumps 2.76 in (7 cm)" << false;
    QTest::newRow("inches-only") << "Talon 7 cm" << "EN" << "Heels 2.76 in" << false;
    QTest::newRow("uppercase-units") << "Talon 7 cm" << "EN" << "Heels 2.76 In (7 Cm)" << false;
    QTest::newRow("decimal-comma") << "Talon 7,5 cm" << "EN" << "Heels 2.95 in (7.5 cm)" << true;
    QTest::newRow("no-space") << "Talon 7CM" << "EN" << "Heels 2.76 in (7 cm)" << true;
    QTest::newRow("whole-inch") << "Talon 7,62 cm" << "EN" << "Heels 3 in (7.62 cm)" << true;
    QTest::newRow("dimensions") << "Tapis 10×20 cm" << "EN" << "Mat 3.94 x 7.87 in (10 x 20 cm)" << true;
    QTest::newRow("lost-dimension") << "Tapis 10x20 cm" << "EN" << "Mat 7.87 in (20 cm)" << false;
    QTest::newRow("range") << "Hauteur 7–10 cm" << "EN" << "Height 2.76-3.94 in (7-10 cm)" << true;
    QTest::newRow("two-measurements") << "Talon 7 cm Plateforme 2 cm" << "EN"
            << "Heels 2.76 in (7 cm) Platform 0.79 in (2 cm)" << true;
    QTest::newRow("repeated-measurement") << "Largeur 7 cm Hauteur 7 cm" << "EN"
            << "Width 2.76 in (7 cm) Height 2.76 in (7 cm)" << true;
    QTest::newRow("already-converted") << "Heels 2.76 in (7 cm)" << "EN" << "Heels 2.76 in (7 cm)" << true;
    QTest::newRow("variation-leaked") << "Talon 7 cm" << "EN" << "Heels 2.76 in (7 cm) (Leopard, 34)" << false;
    QTest::newRow("commentary") << "Talon 7 cm" << "EN" << "Heels 2.76 in (7 cm) (Translated)" << false;
    QTest::newRow("non-english") << "Talon 7 cm" << "DE" << "Absatz 7 cm" << true;
    QTest::newRow("no-measurement") << "Escarpins EU 34" << "EN" << "Pumps EU 34" << true;
}

void FillerTitleTests::validatesMeasurements()
{
    QFETCH(QString, source);
    QFETCH(QString, language);
    QFETCH(QString, reply);
    QFETCH(bool, valid);
    const auto step = TitleTranslation::createStep(source, language);
    QCOMPARE(step->validate(reply, {}), valid);
    const QString json = QString::fromUtf8(QJsonDocument(QJsonObject{{"translation", reply}})
            .toJson(QJsonDocument::Compact));
    // The same checks protect selection results and persisted cache reads.
    QCOMPARE(step->validateBestReply(json, {}), valid);
}

void FillerTitleTests::promptsUseOnlyBody()
{
    const QString source = "Escarpins Slingback Femme Imprimé Léopard À Talon Aiguille 7 cm - "
                           "Bride Arrière Élastiquée Pour Un Maintien Ajusté (Guépard, 34)";
    const auto parts = TitleTranslation::splitTitle(source);
    const auto step = TitleTranslation::createStep(parts.body, "EN");
    const QString translationPrompt = step->getPrompt(0);
    const QString selectionPrompt = step->getPromptGetBestReply(0, {"Pumps 2.76 in (7 cm)"});
    for (const auto &prompt : {translationPrompt, selectionPrompt})
    {
        QVERIFY(prompt.contains(parts.body));
        QVERIFY(prompt.contains("2.76 in (7 cm)"));
        QVERIFY(!prompt.contains("Guépard, 34"));
    }
    QCOMPARE(step->neededReplies, 2);
    QVERIFY(step->id != "FillerTitle_translation_" + parts.body + "_EN");
    QCOMPARE(step->cachingKey, step->id);
    const auto german = TitleTranslation::createStep(parts.body, "DE");
    QVERIFY(!german->getPrompt(0).contains("2.76 in (7 cm)"));
}

void FillerTitleTests::rejectsMalformedFinalReplies()
{
    const auto step = TitleTranslation::createStep("Talon 7 cm", "EN");
    for (const QString &reply : {QString{}, QString("not json"), QString("[]"), QString("{}"),
                                QString("{\"translation\":42}"), QString("{\"translation\":\"\"}")})
        QVERIFY(!step->validateBestReply(reply, {}));
}

void FillerTitleTests::fillsTitleAndPreservesVariation_data()
{
    QTest::addColumn<QString>("source");
    QTest::addColumn<QString>("translatedBody");
    QTest::addColumn<QString>("expectedTitle");
    QTest::newRow("user-example")
            << "Escarpins Slingback Femme Imprimé Léopard À Talon Aiguille 7 cm - Bride Arrière Élastiquée Pour Un Maintien Ajusté (Guépard, 34)"
            << "Women's Leopard Print Slingback Pumps With 2.76 in (7 cm) Stiletto Heels - Elasticated Back Strap For An Adjustable Fit"
            << "Women's Leopard Print Slingback Pumps With 2.76 in (7 cm) Stiletto Heels - Elasticated Back Strap For An Adjustable Fit (Leopard, 34)";
    QTest::newRow("reuse-generated-child")
            << "Pumps With 2.76 in (7 cm) Heels (Leopard, 34)"
            << "Pumps With 2.76 in (7 cm) Heels"
            << "Pumps With 2.76 in (7 cm) Heels (Leopard, 34)";
    QTest::newRow("reuse-generated-parent")
            << "Pumps With Heels 2.76 in (7 cm)"
            << "Pumps With Heels 2.76 in (7 cm)"
            << "Pumps With Heels 2.76 in (7 cm)";
}

void FillerTitleTests::fillsTitleAndPreservesVariation()
{
    QFETCH(QString, source);
    QFETCH(QString, translatedBody);
    QFETCH(QString, expectedTitle);
    auto ai = OpenAi2::instance();
    ai->resetForTests();
    ai->init("offline-test-placeholder");
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath("shoes-FR.xlsx");
    QXlsx::Document doc;
    doc.addSheet("Template");
    doc.selectSheet("Template");
    doc.write(1, 1, "TemplateType=fptcustom");
    doc.write(3, 1, "item_sku");
    doc.write(3, 2, "feed_product_type");
    doc.write(3, 3, "item_name");
    doc.write(5, 2, "SHOES");
    doc.addSheet("Data Definitions");
    doc.addSheet("Valid Values");
    QVERIFY(doc.saveAs(path));
    TemplateFiller templateFiller(temp.path(), path, {}, {}, {});
    auto flags = templateFiller.attributeFlagsTable();
    flags->recordAttribute({{Attribute::AMAZON_V01, "item_name"}});
    flags->recordAttribute({{Attribute::AMAZON_V01, "size_name"}}, Attribute::Size);

    // Legacy cached cm-only translations must no longer suppress regeneration.
    const auto sourceBody = TitleTranslation::splitTitle(source).body;
    templateFiller.saveAiValue("aiTitleTranslations.ini",
                              "FillerTitle_translation_" + sourceBody + "_EN",
                              "{\"translation\":\"Pumps 7 cm\"}");
    int calls = 0;
    QStringList prompts;
    const QString selected = QString::fromUtf8(QJsonDocument(QJsonObject{{"translation", translatedBody}})
            .toJson(QJsonDocument::Compact));
    ai->setTransportForTests([&](const QString &, const QString &prompt, const QList<QString> &,
                                std::function<void(QString)> ok,
                                std::function<void(OpenAi2::TransportError)>) {
        ++calls;
        prompts.append(prompt);
        const auto reply = prompt.contains("Candidate translations:") ? selected : translatedBody;
        QTimer::singleShot(0, [ok, reply] { ok(reply); });
    });
    using Values = QHash<QString, QHash<QString, QString>>;
    const Values input{{"sku", {{"item_name", source}, {"size_name", "34"}}}};
    Values common{{"sku", {{"color_name", "Leopard"}}}};
    Values output{{"sku", {{"size_name", "34"}}}};
    FillerTitle filler;
    auto runFill = [&]() -> QCoro::Task<void> {
        // Await here to retain temporary reference arguments until fill completes.
        co_await filler.fill(&templateFiller, {}, Attribute::AMAZON_V01, Attribute::AMAZON_V01,
                             "item_name", "item_name", nullptr, "SHOES", "SHOES", "FR", "FR", "UK", "EN",
                             {}, {}, AbstractFiller::Female, AbstractFiller::Adult,
                             input, {}, {}, common, output);
    };
    bool done = false;
    auto task = runFill().then([&] { done = true; });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
    QCOMPARE(output["sku"]["item_name"], expectedTitle);
    QVERIFY(calls > 0);
    for (const auto &prompt : prompts)
    {
        QVERIFY(prompt.contains(sourceBody));
        QVERIFY(!prompt.contains("Guépard, 34"));
        QVERIFY(!prompt.contains("Leopard, 34"));
    }

    // A fresh fill reuses the new persistent cache without another AI request.
    const int callsBeforeCache = calls;
    common["sku"].remove("item_name");
    done = false;
    auto cachedTask = runFill().then([&] { done = true; });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
    QCOMPARE(calls, callsBeforeCache);
    QCOMPARE(output["sku"]["item_name"], expectedTitle);
}

QTEST_GUILESS_MAIN(FillerTitleTests)
#include "tst_fillertitle.moc"
