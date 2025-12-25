#include <QSettings>

#include "../../common/openai/OpenAi2.h"

#include "MandatoryAttributesManager.h"

MandatoryAttributesManager *MandatoryAttributesManager::instance()
{
    static MandatoryAttributesManager instance;
    return &instance;
}

void MandatoryAttributesManager::load(
        const QString &settingPath,
        const QString &productType,
        const QSet<QString> &curTemplateFieldIds,
        const QSet<QString> &curTemplateFieldIdsMandatory)
{
    _clear();

    m_settingsPath = settingPath;
    m_productType  = productType;

    QSettings settings{m_settingsPath, QSettings::IniFormat};
    const QString productTypeLower = m_productType.toLower();

    // Robust serialization: store sets as QStringList (QSettings round-trips this reliably).
    auto readSet = [&](const QString& key) -> QSet<QString> {
        const QStringList lst = settings.value(key).toStringList();
        return QSet<QString>(lst.begin(), lst.end());
    };

    settings.beginGroup(productTypeLower);
    {
        for (auto it = m_key_ids.begin(); it != m_key_ids.end(); ++it)
        {
            const QString key = it.key();
            if (settings.contains(key))
            {
                *it.value() = readSet(key);
            }
        }
    }
    settings.endGroup();

    // Current template state
    m_mandatoryIdsCurTemplates = curTemplateFieldIdsMandatory;
    m_curTemplateAllIds        = curTemplateFieldIds;

    // If we already have history, no AI classification needed here.
    if (!m_mandatoryIdsPreviousTemplates.isEmpty())
    {
        _saveInSettings();
        return;
    }

    // Classify only attributes that are NOT already mandatory in current template.
    QList<QString> toClassify;
    toClassify.reserve(m_curTemplateAllIds.size());
    for (const QString& id : m_curTemplateAllIds)
    {
        if (!m_mandatoryIdsCurTemplates.contains(id))
            toClassify.push_back(id);
    }

    if (toClassify.isEmpty())
    {
        _saveInSettings();
        return;
    }

    auto normalizeYesNo = [](const QString& s) -> QString {
        return s.trimmed().toLower();
    };

    auto buildPrompt = [&](const QString& attributeId) -> QString {
        return QString(
            "You are helping classify Amazon listing attributes.\n"
            "Product type: \"%1\".\n"
            "Attribute id: \"%2\".\n\n"
            "Question: Is this attribute mandatory to create a compliant product page, "
            "OR required to avoid a common quality-issue warning (i.e., it should be treated as mandatory)?\n\n"
            "Rules:\n"
            "- Reply with exactly one word: yes or no\n"
            "- No punctuation, no explanation.\n"
        ).arg(m_productType, attributeId);
    };

    // Track undecided IDs after phase 1.
    // Use shared_ptr to keep it alive until lambdas execute.
    auto undecided = QSharedPointer<QSet<QString>>::create();

    // ------------------------
    // Phase 1 (gpt-5-mini): 3x, accept only if unanimous; else return empty => phase 2
    // ------------------------
    QList<QSharedPointer<OpenAi2::StepMultipleAsk>> phase1;
    phase1.reserve(toClassify.size());

    for (const QString& attrId : toClassify)
    {
        auto step = QSharedPointer<OpenAi2::StepMultipleAsk>::create();
        step->id   = attrId;
        step->name = "Mandatory attribute classification (3x unanimous)";
        step->cachingKey = QString("mandatory_attr_v1__%1__%2").arg(productTypeLower, attrId);

        step->neededReplies = 3;
        step->maxRetries    = 3;

        step->getPrompt = [buildPrompt, attrId](int /*nAttempts*/) -> QString {
            return buildPrompt(attrId);
        };

        step->validate = [normalizeYesNo](const QString& gptReply, const QString& /*lastWhy*/) -> bool {
            const QString r = normalizeYesNo(gptReply);
            return (r == "yes" || r == "no");
        };

        step->chooseBest = OpenAi2::CHOOSE_ALL_SAME_OR_EMPTY;
        step->apply = [this, attrId, undecided, normalizeYesNo](const QString& bestReply) {
            const QString r = normalizeYesNo(bestReply);
            if (r == "yes")
            {
                m_idsNonMandatoryAddedByAi.insert(attrId);
                m_mandatoryIdsCurTemplates.insert(attrId);
            }
            else if (r == "no")
            {
                m_mandatoryIdsFileRemovedAi.insert(attrId);
            }
            else
            {
                // empty / invalid => undecided
                undecided->insert(attrId);
            }
        };

        phase1.push_back(step);
    }

    // You need OpenAi2 to provide a completion callback.
    // If you do NOT have this yet, implement an overload:
    // askGptMultipleTime(steps, model, onAllSuccess, onAllFailure)
    //
    // Assuming you have it (or you add it), use:
    auto onPhase1Done = [this, productTypeLower, buildPrompt, normalizeYesNo, undecided]() {
        if (undecided->isEmpty())
        {
            _saveInSettings();
            return;
        }

        // ------------------------
        // Phase 2 (gpt-5.2): 5x, majority vote
        // ------------------------
        QList<QSharedPointer<OpenAi2::StepMultipleAsk>> phase2;
        phase2.reserve(undecided->size());

        for (const QString& attrId : *undecided)
        {
            auto step = QSharedPointer<OpenAi2::StepMultipleAsk>::create();
            step->id   = attrId;
            step->name = "Mandatory attribute classification (5x majority)";
            step->cachingKey = QString("mandatory_attr_v2__%1__%2").arg(productTypeLower, attrId);

            step->neededReplies = 5;
            step->maxRetries    = 3;

            step->getPrompt = [buildPrompt, attrId](int /*nAttempts*/) -> QString {
                return buildPrompt(attrId);
            };

            step->validate = [normalizeYesNo](const QString& gptReply, const QString& /*lastWhy*/) -> bool {
                const QString r = normalizeYesNo(gptReply);
                return (r == "yes" || r == "no");
            };

            step->chooseBest = OpenAi2::CHOOSE_MOST_FREQUENT;
            step->apply = [this, attrId, normalizeYesNo](const QString& bestReply) {
                const QString r = normalizeYesNo(bestReply);
                if (r == "yes")
                {
                    m_idsNonMandatoryAddedByAi.insert(attrId);
                    m_mandatoryIdsCurTemplates.insert(attrId);
                }
                else if (r == "no")
                {
                    m_mandatoryIdsFileRemovedAi.insert(attrId);
                }
            };

            phase2.push_back(step);
        }

        // If you have completion callback for phase2, save after it finishes.
        auto onPhase2Done = [this]() {
            _saveInSettings();
        };

        // Replace this with your actual overload (see note above).
        // OpenAi2::instance()->askGptMultipleTime(phase2, "gpt-5.2", onPhase2Done, onPhase2Done);

        OpenAi2::instance()->askGptMultipleTime(phase2, "gpt-5.2");
        // If you don't have a completion hook yet, saving here will likely happen too early.
        // So: strongly prefer adding the callback and saving in onPhase2Done().
        _saveInSettings();
    };

    // Replace this with your actual overload (see note above).
    // OpenAi2::instance()->askGptMultipleTime(phase1, "gpt-5-mini", onPhase1Done, onPhase1Done);

    OpenAi2::instance()->askGptMultipleTime(phase1, "gpt-5-mini");
    // If you don't have completion hook yet, you cannot safely chain phase 2 here.
    // You MUST add a completion callback in OpenAi2 for this to be correct.
    _saveInSettings();

    Q_UNUSED(onPhase1Done);
}


QSet<QString> MandatoryAttributesManager::getMandatoryIds() const
{
    auto ids = m_mandatoryIdsPreviousTemplates.isEmpty() ? m_mandatoryIdsCurTemplates : m_mandatoryIdsPreviousTemplates;
    if (m_mandatoryIdsPreviousTemplates.isEmpty())
    {
        ids.subtract(m_mandatoryIdsFileRemovedAi);
    }
    ids.unite(m_idsNonMandatoryAddedByAi);
    ids.subtract(m_idsRemovedManually);
    ids.unite(m_idsAddedManually);
    return ids;
}

bool MandatoryAttributesManager::hasIdsFromPreviousTemplates() const
{
    return !m_mandatoryIdsPreviousTemplates.isEmpty();
}

MandatoryAttributesManager::MandatoryAttributesManager()
{
    m_key_ids["m_mandatoryIdsCurTemplates"] = &m_mandatoryIdsCurTemplates;
    m_key_ids["m_mandatoryIdsFileRemovedAi"] = &m_mandatoryIdsFileRemovedAi;
    m_key_ids["m_mandatoryIdsPreviousTemplates"] = &m_mandatoryIdsPreviousTemplates;
    m_key_ids["m_idsNonMandatoryAddedByAi"] = &m_idsNonMandatoryAddedByAi;
    m_key_ids["m_idsAddedManually"] = &m_idsAddedManually;
    m_key_ids["m_idsRemovedManually"] = &m_idsRemovedManually;
}

void MandatoryAttributesManager::_saveInSettings()
{
    QSettings settings{m_settingsPath, QSettings::IniFormat};
    const QString productTypeLower = m_productType.toLower();
    settings.beginGroup(productTypeLower);

    auto writeSet = [&](const QString& key, const QSet<QString>& ids) {
        // Prefer a stable serialization (QSettings reliably stores QStringList).
        const QStringList lst = QStringList(ids.begin(), ids.end());

        if (!lst.isEmpty())
        {
            settings.setValue(key, lst);
        }
        else if (settings.contains(key))
        {
            settings.remove(key);
        }
    };

    for (auto it = m_key_ids.begin(); it != m_key_ids.end(); ++it)
    {
        const QString key = it.key();
        const QSet<QString>& ids = *it.value();
        writeSet(key, ids);
    }

    settings.endGroup();
    settings.sync();
}

void MandatoryAttributesManager::setIdsRemovedManually(
        const QSet<QString> &newIdsRemovedManually)
{
    m_idsRemovedManually = newIdsRemovedManually;
    _saveInSettings();
}

void MandatoryAttributesManager::setIdsAddedManually(
        const QSet<QString> &newIdsAddedManually)
{
    m_idsAddedManually = newIdsAddedManually;
    _saveInSettings();
}

void MandatoryAttributesManager::setIdsNonMandatoryAddedByAi(
        const QSet<QString> &newIdsNonMandatoryAddedByAi)
{
    m_idsNonMandatoryAddedByAi = newIdsNonMandatoryAddedByAi;
    _saveInSettings();
}

void MandatoryAttributesManager::setMandatoryIdsPreviousTemplates(
        const QSet<QString> &newMandatoryIdsPreviousTemplates)
{
    m_mandatoryIdsPreviousTemplates = newMandatoryIdsPreviousTemplates;
    _saveInSettings();
}

void MandatoryAttributesManager::setMandatoryIdsFileRemovedAi(
        const QSet<QString> &newMandatoryIdsFileRemovedAi)
{
    m_mandatoryIdsFileRemovedAi = newMandatoryIdsFileRemovedAi;
    _saveInSettings();
}

void MandatoryAttributesManager::setMandatoryIdsCurTemplates(
        const QSet<QString> &newMandatoryIdsCurTemplates)
{
    m_mandatoryIdsCurTemplates = newMandatoryIdsCurTemplates;
    _saveInSettings();
}

void MandatoryAttributesManager::_clear()
{
    for (auto &pkeys : m_key_ids)
    {
        pkeys->clear();
    }
}
