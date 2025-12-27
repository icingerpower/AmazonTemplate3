#include <QSettings>
#include <QColor>

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
    
    // Build ordered IDs for model
    m_orderedFieldIds = m_curTemplateAllIds.keys();
    // Sort logic? Usually alpha.
    // Sort logic: Mandatory first, then alpha.
    std::sort(m_orderedFieldIds.begin(), m_orderedFieldIds.end(), [this](const QString &a, const QString &b){
        bool isInitialA = m_mandatoryIdsCurTemplates.contains(a);
        bool isRemovedA = m_idsRemovedManually.contains(a);
        bool isAddedA = m_idsAddedManually.contains(a);
        bool mandatoryA = (isInitialA && !isRemovedA) || isAddedA;

        bool isInitialB = m_mandatoryIdsCurTemplates.contains(b);
        bool isRemovedB = m_idsRemovedManually.contains(b);
        bool isAddedB = m_idsAddedManually.contains(b);
        bool mandatoryB = (isInitialB && !isRemovedB) || isAddedB;

        if (mandatoryA != mandatoryB)
        {
            return mandatoryA > mandatoryB; // True (mandatory) first
        }
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });

    // Notify model reset or reload?
    // Since load is async and might be called after construction, 
    // we should probably beginResetModel/endResetModel if we were already loaded?
    // But this is usually called once per template filling session.
    // Use layoutChanged?
    // Since this is a "load" operation, likely the view is set AFTER load or we want to refresh.
    // We are in a coroutine, so we can't emit from other thread if context issues.
    // But QCoro runs on main thread usually if started from main.
    // Let's assume safely we can emit signals.
    beginResetModel();
    // Re-assigned ordered ids above
    endResetModel();

    m_doneTemplatePaths = settings.value(SETTINGS_KEYS_TEMPLATES_DONE).value<QSet<QString>>();

    // Load reviewed attributes
    settings.beginGroup("reviewedAttributes");
    const QStringList products = settings.childKeys();
    for (const QString &pType : products)
    {
        const QStringList lst = settings.value(pType).toStringList();
        m_productType_reviewedAttributes[pType] = QSet<QString>(lst.begin(), lst.end());
    }
    settings.endGroup();

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

    // --- Persisted Reviewed Attributes Logic ---
    // m_productType_reviewedAttributes stores sets of attributes already reviewed for a product type.
    // It is a map: productType -> set of reviewed attributes?
    // Wait, the user requirement is: "for each product type for which attributes were assessed before, they won't be assessed again."
    // This implies we skip the AI assessment entirely if the ID is known for this product type.
    // However, knowing it was assessed doesn't tell us the RESULT (was it mandatory or not?).
    // Actually, AI results are stored in `m_idsNonMandatoryAddedByAi` or `m_mandatoryIdsFileRemovedAi`.
    // But those are likely cleared or specific to the current run?
    // `m_idsNonMandatoryAddedByAi` etc are persisted sets.
    // So if an ID is in those sets, we know its status.
    // BUT those sets might be template-specific or global?
    // In `_saveInSettings`, we save them under `productTypeLower` group.
    // So they are per product type.
    // So if we load them, we know the AI decision.
    // The issue is `toClassify` loop checks `!m_mandatoryIdsCurTemplates.contains(id)`.
    // `m_mandatoryIdsCurTemplates` is initialized from `curTemplateFieldIdsMandatory`.
    // If AI previously added it, it should be in `m_idsNonMandatoryAddedByAi`.
    // The `getMandatoryIds` unites them.
    // The previous logic was:
    // 1. `m_idsNonMandatoryAddedByAi` is loaded from settings.
    // 2. We check `toClassify` which are IDs in cur template but NOT in cur mandatory.
    // 3. IF `m_idsNonMandatoryAddedByAi` contains it, do we assume it's mandatory?
    //    Yes, `getMandatoryIds` includes it.
    //    But we shouldn't ask AI again.
    // 4. What about "No" answers? `m_mandatoryIdsFileRemovedAi`.
    //    If it's in `m_mandatoryIdsFileRemovedAi`, it was removed.
    //    So we shouldn't ask AI again.
    
    // So technically, if an ID is in `m_idsNonMandatoryAddedByAi` OR `m_mandatoryIdsFileRemovedAi`, it has been processed.
    // The user wants `m_productType_reviewedAttributes` explicitly.
    // "Update MandatoryAttributesManager to save, similarly to m_doneTemplatePaths, m_productType_reviewedAttributes."
    // "So for each product type for which attributes were assessed before, they won't be assessed again."
    // This suggests a specific "reviewed" set to track "we asked AI about this".
    // Even if AI returned "undecided" maybe? Or to explicitly track coverage.
    // I will implement `m_productType_reviewedAttributes` as requested.
    
    // In `load`:
    // After loading settings, we have `m_productType_reviewedAttributes`.
    // We should filter `toClassify`.
    
    QSet<QString> &reviewedForThisType = m_productType_reviewedAttributes[productTypeLower];

    QList<QString> finalToClassify;
    finalToClassify.reserve(toClassify.size());
    for (const QString& id : toClassify) {
        // If already reviewed, skip.
        if (reviewedForThisType.contains(id)) {
            continue;
        }
        // Also skip if we already have a decision in loaded sets (optimization, though user asked for specific set)
        if (m_idsNonMandatoryAddedByAi.contains(id) || m_mandatoryIdsFileRemovedAi.contains(id)) {
             reviewedForThisType.insert(id); // Ensure it's marked reviewed
             continue;
        }
        finalToClassify.push_back(id);
    }
    toClassify = finalToClassify;

    if (toClassify.isEmpty())
    {
        _saveInSettings();
        co_return;
    }

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
        step->apply = [this, attrId, undecided, normalizeYesNo, productTypeLower](const QString& bestReply) {
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
            // Mark as reviewed
            m_productType_reviewedAttributes[productTypeLower].insert(attrId);
        };

        phase1.push_back(step);
    }
// TODO record those asked already to not ask again
    co_await OpenAi2::instance()->askGptMultipleTimeCoro(phase1, "gpt-5-mini"); // QDebug() << "Number of attributes that will be classified by AI as mandatory or not:" << phase1.size();


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
        step->apply = [this, attrId, normalizeYesNo, productTypeLower](const QString& bestReply) {
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
            // Mark as reviewed (redundant but safe)
            m_productType_reviewedAttributes[productTypeLower].insert(attrId);
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

MandatoryAttributesManager::MandatoryAttributesManager(QObject *parent)
    : QAbstractTableModel(parent)
{
    m_key_ids["m_mandatoryIdsCurTemplates"] = &m_mandatoryIdsCurTemplates;
    m_key_ids["m_mandatoryIdsFileRemovedAi"] = &m_mandatoryIdsFileRemovedAi;
    m_key_ids["m_mandatoryIdsPreviousTemplates"] = &m_mandatoryIdsPreviousTemplates;
    m_key_ids["m_idsNonMandatoryAddedByAi"] = &m_idsNonMandatoryAddedByAi;
    m_key_ids["m_idsAddedManually"] = &m_idsAddedManually;
    m_key_ids["m_idsRemovedManually"] = &m_idsRemovedManually;
}

// QAbstractTableModel implementation

int MandatoryAttributesManager::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_orderedFieldIds.size();
}

int MandatoryAttributesManager::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return 3;
}

QVariant MandatoryAttributesManager::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_orderedFieldIds.size())
        return QVariant();

    const QString fieldId = m_orderedFieldIds.at(index.row());

    if (role == Qt::DisplayRole)
    {
        if (index.column() == 0)
        {
            return fieldId;
        }
        return QVariant();
    }
    else if (role == Qt::CheckStateRole)
    {
        if (index.column() == 1)
        {
            // First load mandatory status
            return m_mandatoryIdsCurTemplates.contains(fieldId) ? Qt::Checked : Qt::Unchecked;
        }
        else if (index.column() == 2)
        {
            // Effective mandatory status
            // Logic: (In Initial AND NOT Removed) OR Added
            bool isInitial = m_mandatoryIdsCurTemplates.contains(fieldId);
            bool isRemoved = m_idsRemovedManually.contains(fieldId); // m_idsRemovedManually contains overrides
            // Wait, m_idsRemovedManually is strictly for MANUAL overrides?
            // "making attribute in m_idsAddedManually or m_idsRemovedManually when the mandatary state changes over curTemplateFieldIdsMandatory"
            // So Effective = (Initial AND !RemovedManually) OR AddedManually.
            // Note: m_mandatoryIdsCurTemplates INCLUDES AI additions?
            // In phase 1/2 apply: m_mandatoryIdsCurTemplates.insert(attrId);
            // So `m_mandatoryIdsCurTemplates` represents "Base + AI".
            // So Manual overrides act on top of that.
            
            bool effectivelyMandatory = (isInitial && !isRemoved) || m_idsAddedManually.contains(fieldId);
            return effectivelyMandatory ? Qt::Checked : Qt::Unchecked;
        }
    }
    else if (role == Qt::BackgroundRole)
    {
        // Optional: Highlight AI changes?
        // Column 1 is "First load" (Base + AI?).
        // If AI added it, it's in m_mandatoryIdsCurTemplates AND m_idsNonMandatoryAddedByAi.
        if (index.column() == 1)
        {
             if (m_idsNonMandatoryAddedByAi.contains(fieldId))
                 return QColor(Qt::cyan).lighter(180); // Visualize AI addition
             if (m_mandatoryIdsFileRemovedAi.contains(fieldId))
                 return QColor(Qt::red).lighter(180); // Visualize AI removal (though it won't be in CurTemplates if removed?)
                 // If removed by AI, it is inserted into m_mandatoryIdsFileRemovedAi.
                 // It is NOT removed from m_mandatoryIdsCurTemplates?
                 // Wait, Phase 1 apply: "m_mandatoryIdsFileRemovedAi.insert(attrId)".
                 // The starting set `toClassify` includes IDs NOT in CurTemplates.
                 // So if AI removes it, it just stays NOT in CurTemplates.
                 // But wait, `toClassify` are IDs that are NOT in `m_mandatoryIdsCurTemplates`.
                 // So AI only adds new ones or confirms removal (by doing nothing/adding to removed set).
                 // So `m_mandatoryIdsCurTemplates` grows with AI "yes".
        }
    }
    
    return QVariant();
}

QVariant MandatoryAttributesManager::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
    {
        switch (section) {
        case 0: return tr("Attribute ID");
        case 1: return tr("Initially Mandatory");
        case 2: return tr("Mandatory (Editable)");
        default: return QVariant();
        }
    }
    return QVariant();
}

Qt::ItemFlags MandatoryAttributesManager::flags(const QModelIndex &index) const
{
    Qt::ItemFlags f = QAbstractTableModel::flags(index);
    if (index.column() == 2)
    {
        f |= Qt::ItemIsUserCheckable | Qt::ItemIsEditable;
    }
    return f;
}

bool MandatoryAttributesManager::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (index.isValid() && index.column() == 2 && role == Qt::CheckStateRole)
    {
        const QString fieldId = m_orderedFieldIds.at(index.row());
        bool newChecked = (value.toInt() == Qt::Checked);
        bool isInitial = m_mandatoryIdsCurTemplates.contains(fieldId);
        
        if (newChecked)
        {
            // User wants it mandatory.
            m_idsRemovedManually.remove(fieldId); // Un-remove if previously removed
            if (!isInitial)
            {
                m_idsAddedManually.insert(fieldId);
            }
        }
        else
        {
            // User wants it NOT mandatory.
            m_idsAddedManually.remove(fieldId); // Un-add if previously added
            if (isInitial)
            {
                m_idsRemovedManually.insert(fieldId);
            }
        }
        emit dataChanged(index, index);
        _saveInSettings();
        return true;
    }
    return false;
}

void MandatoryAttributesManager::_saveInSettings()
{
    QSettings settings{m_settingsPath, QSettings::IniFormat};
    const QString productTypeLower = m_productType.toLower();
    
    // Save m_productType_reviewedAttributes
    // Since we only want to save for CURRENT product type to avoid reading everything,
    // we assume the map only contains loaded data? 
    // Wait, load logic reads ALL product type groups?
    // No, load logic: settings.beginGroup(productTypeLower)... reads keys for THAT product type.
    // m_productType_reviewedAttributes stores map<ProductType, Set>.
    // We should save the set for current product type.
    
    // Load logic (lines 32-43) reads m_key_ids. m_key_ids does NOT include reviewedAttributes.
    // We need to add saving/loading for reviewedAttributes.
    
    settings.setValue(SETTINGS_KEYS_TEMPLATES_DONE, QVariant::fromValue(m_doneTemplatePaths));
    
    // Save reviewed attributes global map?
    // Or per product type group?
    // The user said "save, similarly to m_doneTemplatePaths, m_productType_reviewedAttributes".
    // m_doneTemplatePaths is global (not under product type group).
    // m_productType_reviewedAttributes is a map. QSettings can't save map directly cleanly structure-wise unless we flatten keys.
    // "m_productType_reviewedAttributes" key?
    // QSettings can save QVariant::Map if registered, but typically we sub-key.
    // "reviewedAttributes/ProductType" -> List.
    
    settings.beginGroup("reviewedAttributes");
    for (auto it = m_productType_reviewedAttributes.begin(); it != m_productType_reviewedAttributes.end(); ++it)
    {
         const QStringList lst = QStringList(it.value().begin(), it.value().end());
         settings.setValue(it.key(), lst);
    }
    settings.endGroup();

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
