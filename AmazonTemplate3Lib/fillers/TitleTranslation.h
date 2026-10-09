#ifndef TITLETRANSLATION_H
#define TITLETRANSLATION_H

#include "../../common/openai/OpenAi2.h"

namespace TitleTranslation {

struct TitleParts {
    QString body;
    QString variation; // Contents of the final variation parentheses, without delimiters.
};

TitleParts splitTitle(const QString &title);
QSharedPointer<OpenAi2::StepMultipleAskAi> createStep(
        const QString &titleBody, const QString &langCodeTo);

}

#endif
