#include "TitleTranslation.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace {

const QString numberPattern = R"(\d+(?:[.,]\d+)?)";
const QString chainPattern = numberPattern
        + R"((?:\s*(?:[xX×]|[-–])\s*)" + numberPattern + ")*";
const QRegularExpression cmMeasurement(
        "(?<![\\w.,])(" + chainPattern + R"()\s*cm\b)",
        QRegularExpression::CaseInsensitiveOption);
const QRegularExpression dualMeasurement(
        "(?<![\\w.,])(" + chainPattern + R"()\s+in\s+\(()"
        + chainPattern + R"()\s+cm\))");

// Normalize decimal commas, separators and insignificant zeros. Conversion is
// done locally so the AI's arithmetic is checked against the original title.
QString normalizedChain(QString chain, bool toInches = false)
{
    chain.replace(',', '.');
    chain.replace(QRegularExpression(R"(\s+)"), "");
    chain.replace('X', 'x');
    chain.replace(QChar(0x00d7), 'x');
    chain.replace(QChar(0x2013), '-');
    static const QRegularExpression number(R"(\d+(?:\.\d+)?)");
    auto matches = number.globalMatch(chain);
    QString result;
    qsizetype previousEnd = 0;
    while (matches.hasNext())
    {
        const auto match = matches.next();
        result += chain.mid(previousEnd, match.capturedStart() - previousEnd);
        double value = match.captured().toDouble();
        if (toInches)
            value = QString::number(value / 2.54, 'f', 2).toDouble();
        result += QString::number(value, 'g', 12);
        previousEnd = match.capturedEnd();
    }
    return result + chain.mid(previousEnd);
}

QStringList centimeterChains(const QString &title)
{
    QStringList chains;
    auto matches = cmMeasurement.globalMatch(title);
    while (matches.hasNext())
        chains.append(normalizedChain(matches.next().captured(1)));
    return chains;
}

bool validTitle(const QString &reply, const QString &source, bool english)
{
    QString remaining = reply.trimmed();
    if (remaining.isEmpty())
        return false;

    if (english)
    {
        auto expected = centimeterChains(source);
        auto pairs = dualMeasurement.globalMatch(remaining);
        while (pairs.hasNext())
        {
            const auto pair = pairs.next();
            const QString cm = normalizedChain(pair.captured(2));
            if (!expected.removeOne(cm)
                    || normalizedChain(pair.captured(1)) != normalizedChain(cm, true))
                return false;
        }
        remaining.remove(dualMeasurement);
        if (!expected.isEmpty() || cmMeasurement.match(remaining).hasMatch())
            return false;
    }
    else
    {
        // Measurement parentheses can occur in an already translated source.
        // Continue rejecting variation suffixes and explanatory AI commentary.
        static const QRegularExpression measurementParentheses(
                "\\(" + chainPattern + R"(\s*(?:cm|in|inch|inches)\))",
                QRegularExpression::CaseInsensitiveOption);
        remaining.remove(measurementParentheses);
    }
    return !remaining.contains('(') && !remaining.contains(')');
}

QString measurementInstruction(const QString &source, bool english)
{
    if (!english)
        return {};
    QString instruction =
            "For every physical measurement in cm in the title body, output inches first "
            "followed by the original centimeter value, exactly as XX in (YY cm). "
            "Divide cm by 2.54, round inches to at most two decimal places and omit trailing zeros. "
            "For example, 7 cm becomes 2.76 in (7 cm). "
            "Preserve every dimension and range endpoint, using x for dimensions and - for ranges. "
            "Do not convert shoe/clothing size labels or invent measurements. "
            "Do not duplicate existing inch/centimeter pairs. Keep in and cm lowercase.\n";
    const auto chains = centimeterChains(source);
    if (!chains.isEmpty())
    {
        instruction += "Required measurements (locally calculated; retain each occurrence):\n";
        for (const auto &cm : chains)
            instruction += normalizedChain(cm, true) + " in (" + cm + " cm)\n";
    }
    return instruction;
}

} // namespace

namespace TitleTranslation {

TitleParts splitTitle(const QString &title)
{
    const QString trimmed = title.trimmed();
    static const QRegularExpression trailingParentheses(R"(\s*\(([^()]*)\)$)");
    // Only an explicit inch/cm pair is a body measurement at the end. A lone
    // "(10 cm)" may itself be legal size-only variation information.
    static const QRegularExpression trailingDualMeasurement(
            dualMeasurement.pattern() + "$",
            QRegularExpression::CaseInsensitiveOption);
    const auto suffix = trailingParentheses.match(trimmed);
    if (suffix.hasMatch() && !trailingDualMeasurement.match(trimmed).hasMatch())
        return {trimmed.left(suffix.capturedStart()).trimmed(), suffix.captured(1)};
    return {trimmed, {}};
}

QSharedPointer<OpenAi2::StepMultipleAskAi> createStep(
        const QString &titleBody, const QString &langCodeTo)
{
    const bool english = langCodeTo.compare("EN", Qt::CaseInsensitive) == 0;
    const QString rules = measurementInstruction(titleBody, english);
    auto step = QSharedPointer<OpenAi2::StepMultipleAskAi>::create();
    // Both the persistent title cache and OpenAi2 cache must bypass old replies.
    step->id = "FillerTitle_translation_v2_" + titleBody + "_" + langCodeTo;
    step->cachingKey = step->id;
    step->name = "Translate product title";
    step->neededReplies = 2;
    step->gptModel = "gpt-5.2";
    step->getPrompt = [titleBody, langCodeTo, rules](int) {
        return QString("Translate the following product title body to language '%1'. "
                       "Each word must start with a capital letter, except unit abbreviations "
                       "such as in and cm, which must remain lowercase. "
                       "Output only the translated title body. Do not add variation information "
                       "or commentary; parentheses are only allowed for measurements.\n")
                .arg(langCodeTo) + rules + "Title: '" + titleBody + "'";
    };
    step->validate = [titleBody, english](const QString &reply, const QString &) {
        return validTitle(reply, titleBody, english);
    };
    step->getPromptGetBestReply = [titleBody, langCodeTo, rules](int, const QList<QString> &replies) {
        QString prompt = QString("Original title body: '%1'\nTarget language: '%2'\n")
                .arg(titleBody, langCodeTo) + rules + "Candidate translations:\n";
        for (const auto &reply : replies)
            prompt += "- " + reply + '\n';
        prompt += "Compare the candidates against the original title. Select the most faithful "
                  "translation and correct any measurement or formatting errors. Verify that "
                  "all measurements are preserved and conversions satisfy the instructions. "
                  "Do not add variation information or commentary. Keep unit abbreviations lowercase. "
                  "Output ONLY a valid JSON object with the key 'translation' containing the title body.\n"
                  "Example: {\"translation\": \"Selected Text\"}";
        return prompt;
    };
    step->validateBestReply = [titleBody, english](const QString &reply, const QString &) {
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(reply.toUtf8(), &error);
        return error.error == QJsonParseError::NoError && doc.isObject()
                && validTitle(doc.object().value("translation").toString(), titleBody, english);
    };
    return step;
}

} // namespace TitleTranslation
