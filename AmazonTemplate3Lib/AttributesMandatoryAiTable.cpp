#include "AttributesMandatoryAiTable.h"
#include <QSettings>
#include "../../common/openai/OpenAi2.h"

const QString AttributesMandatoryAiTable::SETTINGS_KEYS_GROUP{"attributesReviewed"};

AttributesMandatoryAiTable::AttributesMandatoryAiTable(QObject *parent)
    : QObject(parent)
{
}

bool AttributesMandatoryAiTable::isAttributeReviewed(const QString &attrId) const
{
    if (m_productType_reviewedAttributes.contains(m_productType.toLower()))
    {
        return m_productType_reviewedAttributes[m_productType.toLower()].contains(attrId);
    }
    return false;
}

void AttributesMandatoryAiTable::save()
{
    _saveInSettings();
}

void AttributesMandatoryAiTable::_clear()
{
    m_fieldIdsAiAdded.clear();
    m_fieldIdsAiRemoved.clear();
    // Start fresh effectively for the product type load
}

QCoro::Task<void> AttributesMandatoryAiTable::load(
        const QString &productType
        , const QSet<QString> &curTemplateFieldIdsMandatory
        , const QHash<QString, int> &curTemplateFieldIds)
{
    _clear();

    m_curTemplateFieldIdsMandatory = curTemplateFieldIdsMandatory;
    m_productType = productType;
    const QString productTypeLower = m_productType.toLower();

    QSettings settings{m_settingsPath, QSettings::IniFormat};

    // Load reviewed attributes
    settings.beginGroup(SETTINGS_KEYS_GROUP);
    const QStringList products = settings.childKeys();
    for (const QString &pType : products)
    {
        m_productType_reviewedAttributes[pType]
                = settings.value(pType).value<QSet<QString>>();
    }
    settings.endGroup();

    // Identify attributes to classify
    // We only care about attributes that are in current template but NOT reviewed.
    QList<QString> toClassify;
    QSet<QString> reviewedForThisType;
    if (m_productType_reviewedAttributes.contains(productTypeLower))
    {
        reviewedForThisType = m_productType_reviewedAttributes[productTypeLower];
    }

    for (auto it = curTemplateFieldIds.begin();
         it != curTemplateFieldIds.end(); ++it)
    {
        const auto &fieldId = it.key();
        if (!reviewedForThisType.contains(fieldId))
        {
            toClassify.push_back(fieldId);
        }
    }

    if (toClassify.isEmpty())
    {
        co_return;
    }

    // Prepare AI prompts
    auto buildPrompt = [&](const QString& attributeId) -> QString {
        return QString(
            "You are helping classify Amazon listing attributes.\n"
            "Product type: \"%1\".\n"
            "Attribute id: \"%2\".\n\n"
            "Question: Is this attribute mandatory to create a compliant product page, "
            "OR required to avoid convertiation rate penalty or a common page quality-issue warning (i.e., it should be treated as mandatory)?\n\n"
            "Rules:\n"
            "- Reply with exactly one word: yes or no\n"
            "- No punctuation, no explanation.\n"
        ).arg(m_productType, attributeId);
    };

    auto normalizeYesNo = [](const QString& s) -> QString {
        return s.trimmed().toLower();
    };

    auto undecided = QSharedPointer<QSet<QString>>::create();

    // Phase 1
    QList<QSharedPointer<OpenAi2::StepMultipleAsk>> phase1;
    phase1.reserve(toClassify.size());

    for (const QString& attrId : toClassify)
    {
        auto step = QSharedPointer<OpenAi2::StepMultipleAsk>::create();
        step->id = attrId;
        step->name = "Mandatory attribute classification (3x unanimous)";
        step->neededReplies = 2;
        step->cachingKey = attrId + "Mandatory";
        step->maxRetries = 3;
        step->gptModel = "gpt-5.2";
        step->getPrompt = [buildPrompt, attrId](int) { return buildPrompt(attrId); };
        step->validate = [normalizeYesNo](const QString& r, const QString&) {
             const QString n = normalizeYesNo(r);
             return (n == "yes" || n == "no");
        };
        step->chooseBest = OpenAi2::CHOOSE_ALL_SAME_OR_EMPTY;
        step->apply = [this, attrId, undecided, normalizeYesNo, productTypeLower](const QString& bestReply) {
            const QString r = normalizeYesNo(bestReply);
            if (r == "yes") {
                m_fieldIdsAiAdded.insert(attrId);
            } else if (r == "no") {
                m_fieldIdsAiRemoved.insert(attrId);
            } else {
                undecided->insert(attrId);
            }
            m_productType_reviewedAttributes[productTypeLower].insert(attrId);
        };
        phase1.push_back(step);
    }

    co_await OpenAi2::instance()->askGptMultipleTimeCoro(phase1, "removeparam");

    if (undecided->isEmpty())
    {
        co_return;
    }

    // Phase 2
    QList<QSharedPointer<OpenAi2::StepMultipleAsk>> phase2;
    phase2.reserve(undecided->size());

    for (const QString& attrId : std::as_const(*undecided))
    {
        auto step = QSharedPointer<OpenAi2::StepMultipleAsk>::create();
        step->id = attrId;
        step->name = "Mandatory attribute classification (3x majority)";
        step->neededReplies = 3;
        step->maxRetries = 3;
        step->cachingKey = attrId + "Mandatory";
        step->gptModel = "gpt-5.2";
        step->getPrompt = [buildPrompt, attrId](int) { return buildPrompt(attrId); };
        step->validate = [normalizeYesNo](const QString& r, const QString&) {
             const QString n = normalizeYesNo(r);
             return (n == "yes" || n == "no");
        };
        step->chooseBest = OpenAi2::CHOOSE_MOST_FREQUENT;
        step->apply = [this, attrId, normalizeYesNo, productTypeLower](const QString& bestReply) {
            const QString r = normalizeYesNo(bestReply);
            if (r == "yes") {
                m_fieldIdsAiRemoved.insert(attrId);
            } else if (r == "no") {
                m_fieldIdsAiAdded.insert(attrId);
            }
            m_productType_reviewedAttributes[productTypeLower].insert(attrId);
        };
        phase2.push_back(step);
    }

    co_await OpenAi2::instance()->askGptMultipleTimeCoro(phase2, "gpt-5.2");

}

QSet<QString> AttributesMandatoryAiTable::fieldIdsAiAdded() const
{
    auto ids = m_fieldIdsAiAdded;
    ids.subtract(m_curTemplateFieldIdsMandatory);
    return ids;
}

QSet<QString> AttributesMandatoryAiTable::fieldIdsAiRemoved() const
{
    auto ids = m_fieldIdsAiRemoved;
    ids.intersect(m_curTemplateFieldIdsMandatory);
    return ids;
}

void AttributesMandatoryAiTable::_saveInSettings()
{
    QSettings settings{m_settingsPath, QSettings::IniFormat};
    const QString productTypeLower = m_productType.toLower();

    // Save reviewed attributes
    settings.beginGroup(SETTINGS_KEYS_GROUP);
    for (auto it = m_productType_reviewedAttributes.begin(); it != m_productType_reviewedAttributes.end(); ++it)
    {
         settings.setValue(it.key(), QVariant::fromValue(it.value()));
    }
    settings.endGroup();
    settings.sync();
}
