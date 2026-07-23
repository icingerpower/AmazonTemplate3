#ifndef BULLETFIXPROMPT_H
#define BULLETFIXPROMPT_H

#include <QString>
#include <QStringList>

// Shared bullet-point rewrite helpers, factored out of PaneWarnings so that
// PaneSizing ("Fix bullet points") and PaneWarnings use the exact same prompt
// and the exact same reply parser.

// Builds the AI prompt that rewrites a listing's 5 bullet points to be
// Amazon-compliant (dropping prohibited claims such as money-back / refund /
// "remboursement" / guarantees / medical claims).
//
// violationMessage: Amazon's verbatim violation text. When empty, the prompt is
//   phrased as a PROACTIVE compliance pass instead of a reaction to a reported
//   violation.
// currentBullets: the listing's current bullet points, already formatted for
//   display (e.g. one numbered bullet per line). May be empty.
QString buildBulletFixPrompt(QString title, QString asin,
                             QString violationMessage, QString currentBullets);

// Shared bullet extractor — strips markdown fences, handles an object or array
// JSON wrapper, falls back to line splitting. Returns up to 5 bullet strings.
QStringList extractBullets(const QString &raw);

#endif // BULLETFIXPROMPT_H
