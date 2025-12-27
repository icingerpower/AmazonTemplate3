#include <QSettings>

#include "../../common/openai/OpenAi2.h"

#include "MandatoryAttributesManager.h"

const QString MandatoryAttributesManager::SETTINGS_KEYS_TEMPLATES_DONE{"templatesDone"};

QCoro::Task<void> MandatoryAttributesManager::load(
        const QString &templateFileNameFrom,
        const QString &settingPath,
        const QString &productType,
        const QHash<QString, int> &curTemplateFieldIds,
        const QSet<QString> &curTemplateFieldIdsMandatory,
        bool restart)
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
    m_curTemplateAllIds = curTemplateFieldIds;

    m_doneTemplatePaths = settings.value(SETTINGS_KEYS_TEMPLATES_DONE).value<QSet<QString>>();
    if (m_doneTemplatePaths.contains(templateFileNameFrom) && !restart)
    {
        co_return;
    }
    m_doneTemplatePaths.insert(templateFileNameFrom);

    // If we already have history, no AI classification needed here.
    if (!m_mandatoryIdsPreviousTemplates.isEmpty())
    {
        _saveInSettings();
        co_return;
    }

    // Classify only attributes that are NOT already mandatory in current template.
    QList<QString> toClassify;
    toClassify.reserve(m_curTemplateAllIds.size());
    const auto &curTemplateAllIds = m_curTemplateAllIds.keys();
    for (const QString& id : curTemplateAllIds)
    {
        if (!m_mandatoryIdsCurTemplates.contains(id))
        {
            toClassify.push_back(id);
        }
    }

    if (toClassify.isEmpty())
    {
        _saveInSettings();
        co_return;
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
    // NOTE: In coroutine, local stack variables are preserved across co_await if captured by value or reference in a way that respects lifetime?
    // Actually, in QCoro, the coroutine frame preserves locals. But we are passing lambdas to OpenAi2 which might execute asynchronously.
    // However, OpenAi2 is processing them. The callbacks (apply/validate) are stored in Step object.
    // Since Step object is kept alive by OpenAi2 until finished, and we await it, it's safe if we capture properly.
    // Using shared_ptr for undecided is safe and good practice here.
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

    co_await OpenAi2::instance()->askGptMultipleTimeCoro(phase1, "gpt-5-mini");


    if (undecided->isEmpty())
    {
        _saveInSettings();
        co_return;
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

    co_await OpenAi2::instance()->askGptMultipleTimeCoro(phase2, "gpt-5.2");

    _saveInSettings();
}

QSet<QString> MandatoryAttributesManager::getMandatoryIds() const
{
    auto ids = m_mandatoryIdsPreviousTemplates.isEmpty()
            ? m_mandatoryIdsCurTemplates : m_mandatoryIdsPreviousTemplates;
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
    settings.setValue(SETTINGS_KEYS_TEMPLATES_DONE, QVariant::fromValue(m_doneTemplatePaths));
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

void MandatoryAttributesManager::setIdsChangedManually(
        const QSet<QString> &newIdsAddedManually
        , const QSet<QString> &newIdsRemovedManually)
{
    m_idsRemovedManually = newIdsRemovedManually;
    m_idsAddedManually = newIdsAddedManually;
    _saveInSettings();
}

const QSet<QString> &MandatoryAttributesManager::idsNonMandatoryAddedByAi() const
{
    return m_idsNonMandatoryAddedByAi;
}

const QSet<QString> &MandatoryAttributesManager::mandatoryIdsFileRemovedAi() const
{
    return m_mandatoryIdsFileRemovedAi;
}

void MandatoryAttributesManager::setMandatoryIdsPreviousTemplates(
        const QSet<QString> &newMandatoryIdsPreviousTemplates)
{
    m_mandatoryIdsPreviousTemplates = newMandatoryIdsPreviousTemplates;
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
