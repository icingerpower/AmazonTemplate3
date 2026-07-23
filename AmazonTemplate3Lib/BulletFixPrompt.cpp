#include "BulletFixPrompt.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>

QString buildBulletFixPrompt(QString title, QString asin,
                             QString violationMessage, QString currentBullets)
{
    // When Amazon gave us a concrete violation we quote it; otherwise we phrase
    // the task as a proactive compliance rewrite (same output contract either
    // way, so the reply parser is shared).
    const QString violationBlock = violationMessage.trimmed().isEmpty()
        ? QStringLiteral(
            "There is no specific reported violation. Proactively rewrite the "
            "bullet points to be fully Amazon-compliant: remove any prohibited "
            "claims such as money-back / refund / \"remboursement\" promises, "
            "guarantees, warranties, medical or health claims, and any other "
            "content Amazon disallows.\n\n")
        : QStringLiteral(
            "Amazon violation message: \"%1\"\n\n").arg(violationMessage);

    return QStringLiteral(
        "You are an expert in writing Amazon product pages. The following product's "
        "bullet points must be made fully Amazon-compliant.\n\n"
        "Product title: %1\nASIN: %2\n\n"
        "%3"
        "Current bullet points:\n%4\n"
        "Write 5 improved, Amazon-compliant bullet points that increase perceived value "
        "while respecting Amazon policies. Remove every prohibited claim (money-back / "
        "refund / \"remboursement\" / guarantee / warranty / medical or health claims). "
        "Always add exactly one emoji at the beginning of each bullet point. Keep the "
        "SAME language as the current bullet points. Do not invent information; only "
        "state verifiable facts.\n\n"
        "Output a valid JSON object with a single key \"bullet_points\" containing an "
        "array of exactly 5 strings.\n"
        "Example: {\"bullet_points\": [\"\xe2\x9c\xa8 Point 1\", \"\xf0\x9f\x8e\xaf Point 2\", \"\xf0\x9f\x92\xaa Point 3\", "
        "\"\xf0\x9f\x8c\x9f Point 4\", \"\xe2\x9c\x85 Point 5\"]}")
        .arg(title, asin, violationBlock, currentBullets);
}

QStringList extractBullets(const QString &raw)
{
    QString jsonText = raw.trimmed();
    static const QRegularExpression kFence(
        QStringLiteral("```(?:json)?\\s*\\n?([\\s\\S]*?)\\n?```"),
        QRegularExpression::MultilineOption);
    const auto fm = kFence.match(jsonText);
    if (fm.hasMatch()) jsonText = fm.captured(1).trimmed();

    QStringList bullets;
    auto fromObj = [&](const QJsonObject &obj) {
        const QJsonArray arr = obj.value(QStringLiteral("bullet_points")).toArray();
        for (const QJsonValue &v : arr) {
            if (bullets.size() >= 5) break;
            const QString s = v.toString().trimmed();
            if (!s.isEmpty()) bullets.append(s);
        }
    };
    const QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8());
    if (doc.isObject())
        fromObj(doc.object());
    else if (doc.isArray() && !doc.array().isEmpty())
        fromObj(doc.array().first().toObject());

    if (bullets.size() < 5) {
        // Line-split fallback — skip JSON syntax / fence lines
        bullets.clear();
        for (const QString &line : raw.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            if (bullets.size() >= 5) break;
            const QString t = line.trimmed();
            if (t.isEmpty() || t.startsWith(QLatin1String("```"))
                || t == QLatin1String("{") || t == QLatin1String("}")
                || t == QLatin1String("[") || t == QLatin1String("]"))
                continue;
            bullets.append(t);
        }
    }
    return bullets;
}
