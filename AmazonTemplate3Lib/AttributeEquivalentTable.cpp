#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <algorithm>

#include "../../common/openai/OpenAi2.h"

#include "AttributeEquivalentTable.h"
#include "Attribute.h"
#include "ExceptionTemplate.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>


const QStringList AttributeEquivalentTable::HEADERS{
    QObject::tr("Attribute")
    , QObject::tr("Equivalent values")
};

AttributeEquivalentTable::AttributeEquivalentTable(
        const QString &workingDirectory, QObject *parent)
    : QAbstractTableModel(parent)
{
    m_filePath = QDir{workingDirectory}.absoluteFilePath("attributeEquivalent.csv");
    _loadFromFile();
    _buildHash();
}

void AttributeEquivalentTable::remove(const QModelIndex &index)
{
    beginRemoveRows(QModelIndex{}, index.row(), index.row());
    m_listOfStringList.removeAt(index.row());
    _saveInFile();
    endRemoveRows();
}

bool AttributeEquivalentTable::hasEquivalent(const QString &fieldIdAmzV02, const QString &value) const
{
    auto it = m_fieldIdAmzV02_listOfEquivalents.constFind(fieldIdAmzV02);
    if (it != m_fieldIdAmzV02_listOfEquivalents.constEnd())
    {
        for (const auto &equivalents : it.value())
        {
            if (equivalents.contains(value))
            {
                return true;
            }
        }
    }
    return false;
}

bool AttributeEquivalentTable::hasEquivalent(
        const QString &fieldIdAmzV02
        , const QString &value
        , const QSet<QString> &possibleValues) const
{
    auto it = m_fieldIdAmzV02_listOfEquivalents.constFind(fieldIdAmzV02);
    if (it != m_fieldIdAmzV02_listOfEquivalents.constEnd())
    {
        for (const auto &equivalents : it.value())
        {
            if (equivalents.contains(value))
            {
                for (const auto &equivalent : equivalents)
                {
                    if (possibleValues.contains(equivalent))
                    {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

int AttributeEquivalentTable::getPosAttr(
        const QString &fieldIdAmzV02, const QString &value) const
{
    int pos = 0;
    for (const auto &stringList : m_listOfStringList)
    {
        if (stringList[0] == fieldIdAmzV02)
        {
            const auto &values = stringList.last().split(CELL_SEP);
            if (values.contains(value))
            {
                return pos;
            }
        }
        ++pos;
    }
    return -1;
}

int AttributeEquivalentTable::getPosAttr(
        const QString &fieldIdAmzV02, const QSet<QString> &equivalentValues) const
{
    int pos = 0;
    for (const auto &stringList : m_listOfStringList)
    {
        if (stringList[0] == fieldIdAmzV02)
        {
            const auto &cellValues = stringList.last().split(CELL_SEP);
            for (const auto &cellValue : cellValues)
            {
                if (equivalentValues.contains(cellValue))
                {
                    return pos;
                }
            }
        }
        ++pos;
    }
    return -1;
}

void AttributeEquivalentTable::recordAttribute(
        const QString &fieldIdAmzV02,
        const QSet<QString> &equivalentValues)
{
    int posAttr = -1;
    if (m_fieldIdAmzV02_listOfEquivalents.contains(fieldIdAmzV02))
    {
        posAttr = getPosAttr(fieldIdAmzV02, equivalentValues);
    }
    if (!m_fieldIdAmzV02_listOfEquivalents.contains(fieldIdAmzV02) || posAttr == -1)
    {
        QStringList newRow{fieldIdAmzV02};
        QStringList equivalentsList;
        for (const auto &equivalentValue : equivalentValues)
        {
            equivalentsList << equivalentValue;
        }
        equivalentsList.sort();
        newRow << equivalentsList.join(CELL_SEP);
        Q_ASSERT(!newRow.last().contains("Amazon V02"));
        // Find insertion point to keep alphabetical order
        int insertPos = 0;
        for (int i = 0; i < m_listOfStringList.size(); ++i) {
            if (m_listOfStringList[i][0].compare(fieldIdAmzV02, Qt::CaseInsensitive) > 0) {
                insertPos = i;
                break;
            }
            insertPos = i + 1;
        }

        beginInsertRows(QModelIndex{}, insertPos, insertPos);
        m_listOfStringList.insert(insertPos, newRow);
        _saveInFile();
        _buildHash();
        endInsertRows();
    }
    else
    {
        const auto &currentEquivalent = m_listOfStringList[posAttr].last().split(CELL_SEP);
        QSet<QString> equivalentValues{currentEquivalent.begin(), currentEquivalent.end()};
        int sizeBefore = equivalentValues.size();
        for (const auto &newVal : equivalentValues)
        {
            equivalentValues.insert(newVal);
        }
        if (sizeBefore != equivalentValues.size())
        {
            QStringList listEquivalentValues{equivalentValues.begin(), equivalentValues.end()};
            std::sort(listEquivalentValues.begin(), listEquivalentValues.end());
            int indColLast = m_listOfStringList[posAttr].size() - 1;
            m_listOfStringList[posAttr][indColLast] = listEquivalentValues.join(CELL_SEP);
            _buildHash();
            emit dataChanged(index(posAttr, indColLast)
                             , index(posAttr, indColLast));
        }
    }
}

const QSet<QString> &AttributeEquivalentTable::getEquivalentValues(
        const QString &fieldIdAmzV02, const QString &value) const
{
    if (m_fieldIdAmzV02_listOfEquivalents.contains(fieldIdAmzV02))
    {
        for (const auto &listOfEquivalent : m_fieldIdAmzV02_listOfEquivalents[fieldIdAmzV02])
        {
            if (listOfEquivalent.contains(value))
            {
                return listOfEquivalent;
            }
        }
    }
    static QSet<QString> empty;
    return empty;
}

const QString &AttributeEquivalentTable::getEquivalentValue(
        const QString &fieldIdAmzV02
        , const QString &value
        , const QSet<QString> &possibleValues) const
{
    auto it = m_fieldIdAmzV02_listOfEquivalents.constFind(fieldIdAmzV02);
    if (it != m_fieldIdAmzV02_listOfEquivalents.constEnd())
    {
        for (const auto &equivalents : it.value())
        {
            if (equivalents.contains(value))
            {
                for (const auto &equivalent : equivalents)
                {
                    if (possibleValues.contains(equivalent))
                    {
                        return equivalent;
                    }
                }
            }
        }
    }
    static QString empty;
    return empty;
}

QCoro::Task<void> AttributeEquivalentTable::askAiEquivalentValues(
        const QString &fieldIdAmzV02, const QString &value, const Attribute *attribute)
{
    qDebug() << "parseAndValidated::askAiEquivalentValues..." << fieldIdAmzV02 << value;
    const auto &marketplace_countryCode_langCode_category_possibleValues
            = attribute->marketplace_countryCode_langCode_category_possibleValues();
   if (marketplace_countryCode_langCode_category_possibleValues.size() == 0)
   {
       qDebug() << "AttributeEquivalentTable::askAiEquivalentValues FAILED as " + fieldIdAmzV02 + " doesn't have possible values";
       co_return;
   }
    
    struct ValidationGroup {
        QString label;
        QSet<QString> allowedValues;
    };
    QList<ValidationGroup> choiceGroups;

    QStringList marketplaces = marketplace_countryCode_langCode_category_possibleValues.keys();
    std::sort(marketplaces.begin(), marketplaces.end());
    for (const auto &marketplace : marketplaces)
    {
        const auto &countryMap = marketplace_countryCode_langCode_category_possibleValues[marketplace];
        QStringList countries = countryMap.keys();
        std::sort(countries.begin(), countries.end());
        for (const auto &country : countries)
        {
             const auto &langMap = countryMap[country];
             QStringList langs = langMap.keys();
             std::sort(langs.begin(), langs.end());
             for (const auto &lang : langs)
             {
                  const auto &catMap = langMap[lang];
                  QSet<QString> allValues;
                  const auto &categories = catMap.keys();
                  for(const auto &cat : categories)
                  {
                      allValues.unite(catMap[cat]);
                  }
                  
                  QString label = QString(" - %1-%2-%3: ").arg(marketplace, country, lang);
                  choiceGroups.append({label, allValues});
             }
        }
    }

    // Prepare AI prompts
    auto buildPrompt = [fieldIdAmzV02, value, choiceGroups](int) -> QString {
        QStringList lines;
        lines << "You are helping translate/map Amazon attribute values."
              << "- For fieldId \"" + fieldIdAmzV02 + "\" with value \"" + value + "\", tell me, for each marketplace-country-lang below, the exact value to select."
              << "- Use ONLY one of the allowed values listed for that country-lang (case-insensitive match, but return the exact string)."
              << "- If no good match exists, suggest the value the closest and add a comment with probability of being wrong next to the selected word."
              << ""
              << "Allowed values per country-lang:";

        for (const auto &group : choiceGroups)
        {
             QStringList valuesList = group.allowedValues.values();
             std::sort(valuesList.begin(), valuesList.end());
             
             QString line = group.label;
             line += valuesList.isEmpty() ? "(none)" : valuesList.join(", ");
             lines << line;
        }

        lines << ""
              << "Exemple of reply format (don't add anything else):"
              << "{"
              << "                    \"Parent\""
              << "                    , \"Superior\""
              << "                    , \"Üst Ürün\""
              << "                    , \"Principal\""
              << "                    , \"Ouder\""
              << "                    , \"Ana\" // Comment if needed"
              << "                    , \"Bovenliggend\""
              << "                    , \"Förälder\""
              << "                    , \"Lverordnad\""
              << "}";
        return lines.join("\n");
    };

    auto parseReply = [](const QString &reply) -> QSet<QString> {
        QSet<QString> result;
        QString clean = reply;
        clean = clean.replace("{", "").replace("}", "");
        // Remove comments // ...
        // Simple regex to remove comments
        static QRegularExpression commentRegex("//.*");
        clean = clean.replace(commentRegex, "");
        
        const auto &parts = clean.split(",");
        for (const QString &part : parts)
        {
            QString v = part.trimmed();
            if (v.startsWith("\"") && v.endsWith("\"")) {
                v = v.mid(1, v.length() - 2);
            } else if (v.startsWith("'") && v.endsWith("'")) {
                v = v.mid(1, v.length() - 2);
            }
            v.replace("\"", "").replace("'", "");
            v = v.trimmed();
            if (!v.isEmpty()) result.insert(v);
        }
        return result;
    };
    
    auto validateReply = [parseReply, choiceGroups](const QString &r, const QString &) -> bool {
        // Must check that in the list, there is at least one value of each QStringList valuesList = allValues.values(); that is created
        if (r.contains("Amazon V")) return false;
        if (r.contains(": ")) return false;
        if (!r.contains("{") || !r.contains("}")) return false;
        
        QSet<QString> repliedValues = parseReply(r);
        
        // We need to check case-insensitive? The prompt assumes exact string return but mentions "case-insensitive match".
        // The user said: "This will allow to avoid the AI that forget to pick one value in the list or don't reply exactly with one of the choices, may be changing the case"
        // So we should probably check if we can match one value for EACH group.
        
        for (const auto &group : choiceGroups)
        {
            if (group.allowedValues.isEmpty()) continue; // If no allowed values, we can't really enforce selection? Or we expect (none)?
            
            bool found = false;
            for (const auto &val : repliedValues)
            {
                 // Check exact match first
                 if (group.allowedValues.contains(val))
                 {
                     found = true;
                     break;
                 }
                 // Check case insensitive
                 for (const auto &allowed : group.allowedValues)
                 {
                     if (allowed.compare(val, Qt::CaseInsensitive) == 0)
                     {
                         found = true;
                         break;
                     }
                 }
                 if (found) break;
            }
            
            if (!found) {
                // qWarning() << "Validation failed: No valid value found for group" << group.label;
                return false;
            }
        }
        
        return true;
    };
    
    auto success = QSharedPointer<bool>::create(false);
    auto resultingValues = QSharedPointer<QSet<QString>>::create();
    
    // Phase 1
    {
        qDebug() << "AttributeEquivalentTable::askAiEquivalentValues starting phase 1 for " + fieldIdAmzV02;
        auto step = QSharedPointer<OpenAi2::StepMultipleAsk>::create();
        step->id = fieldIdAmzV02 + "_" + value + "_p1";
        step->name = "Attribute equivalence Phase 1 (2x unanimous)";
        step->neededReplies = 2;
        step->cachingKey = step->id + "_v1";
        step->maxRetries = 10;
        step->gptModel = "gpt-5.2";
        step->getPrompt = buildPrompt;
        step->validate = validateReply;
        step->chooseBest = OpenAi2::CHOOSE_ALL_SAME_OR_EMPTY;
        step->apply = [this, fieldIdAmzV02, success, resultingValues, parseReply](const QString &best) {
            if (!best.isEmpty()) {
                *success = true;
                *resultingValues = parseReply(best);
                recordAttribute(fieldIdAmzV02, *resultingValues);
            }
        };
        
        QList<QSharedPointer<OpenAi2::StepMultipleAsk>> phase1;
        phase1 << step;
        co_await OpenAi2::instance()->askGptMultipleTimeCoro(phase1, "gpt-5.2");
        
        if (*success) co_return;
    }
    
    // Phase 2
    {
        qDebug() << "AttributeEquivalentTable::askAiEquivalentValues starting phase 2 for " + fieldIdAmzV02;
        auto step = QSharedPointer<OpenAi2::StepMultipleAskAi>::create();
        step->id = fieldIdAmzV02 + "_" + value + "_p2";
        step->name = "Attribute equivalence Phase 2 (3x AI choose)";
        step->neededReplies = 3;
        step->cachingKey = step->id + "_v1";
        step->maxRetries = 10;
        step->gptModel = "gpt-5.2";
        step->getPrompt = buildPrompt;
        step->validate = validateReply;
        
        step->getPromptGetBestReply = [](int, const QList<QString> &gptValidReplies) -> QString {
            QStringList lines;
            lines << "I asked 3 experts to map attribute values and they gave different answers.";
            lines << "Here are the answers:";
            for(int i=0; i<gptValidReplies.size(); ++i) {
                lines << QString("Expert %1:").arg(i+1) << gptValidReplies[i] << "";
            }
            lines << "Task: Analyze these answers and return the single best one (fix errors if needed).";
            lines << "Return ONLY the format requested initially (JSON-like list of strings), nothing else.";
            return lines.join("\n");
        };
        
        step->validateBestReply = [validateReply](const QString &r, const QString &why) {
            return validateReply(r, why);
        };
        
        step->apply = [this, fieldIdAmzV02, resultingValues, parseReply](const QString &best) {
             if (!best.isEmpty()) {
                *resultingValues = parseReply(best);
                recordAttribute(fieldIdAmzV02, *resultingValues);
             }
        };
        step->onLastError = [fieldIdAmzV02](const QString &reply, QNetworkReply::NetworkError networkError, const QString &err) -> bool {
             QString errorMsg = QString("NetworkError: %1 | Reply: %2 | Error: %3")
                     .arg(QString::number(networkError), reply, err);
             ExceptionTemplate exception;
             exception.setInfos("Attribute Equivalence Error", "Failed to get equivalent values for " + fieldIdAmzV02 + ": " + errorMsg);
             exception.raise();
             return true;
        };
        
        QList<QSharedPointer<OpenAi2::StepMultipleAskAi>> phase2;
        phase2 << step;
        co_await OpenAi2::instance()->askGptMultipleTimeAiCoro(phase2, "gpt-5.2");
    }
}

QCoro::Task<void> AttributeEquivalentTable::askAiEquivalentValues(
        const QString &fieldIdAmzV02
        , const QString &value
        , const QString &langCodeFrom
        , const QString &langCodeTo
        , const QSet<QString> &possibleValues)
{
    qDebug() << "parseAndValidated::askAiEquivalentValues..." << fieldIdAmzV02 << langCodeFrom << langCodeTo << value << possibleValues;
    if (possibleValues.isEmpty())
    {
        qDebug() << "AttributeEquivalentTable::askAiEquivalentValues FAILED as possibleValues is empty for " << fieldIdAmzV02;
        co_return;
    }

    auto buildPrompt = [fieldIdAmzV02, value, langCodeFrom, langCodeTo, possibleValues](int) -> QString {
        QStringList lines;
        lines << "You are an expert in Amazon attribute translation."
              << QString("I have a value \"%1\" in language \"%2\" for the attribute \"%3\".")
                     .arg(value, langCodeFrom, fieldIdAmzV02)
              << QString("Please select the best equivalent value from the following list of allowed values in language \"%1\":")
                     .arg(langCodeTo);
        
        QStringList sortedPossible = possibleValues.values();
        std::sort(sortedPossible.begin(), sortedPossible.end());
        
        if (sortedPossible.size() > 500) {
             // Safety cap, though unlikely to be hit for typical strict attributes
             lines << "(List truncated to first 500)";
             for(int i=0; i<500; ++i) lines << "- " + sortedPossible[i];
        } else {
             for(const auto &v : sortedPossible) lines << "- " + v;
        }

        lines << ""
              << "Reply ONLY with a valid JSON object containing the exact selected value in the key 'value'."
              << "Example: {\"value\": \"SelectedValue\"}";
        return lines.join("\n");
    };

    auto validateReply = [possibleValues](const QString &reply, const QString &) -> bool {
        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(reply.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) return false;
        
        QString v = doc.object().value("value").toString();
        // Check if the value is in possibleValues (case insensitive? usually strict)
        // Attribute values are usually case sensitive or strictly defined. 
        // We can do a check.
        if (possibleValues.contains(v)) return true;
        
        // Try case insensitive match?
        // For now, strict match as we requested exact value.
        return false;
    };

    auto parseReply = [](const QString &reply) -> QString {
        QJsonDocument doc = QJsonDocument::fromJson(reply.toUtf8());
        return doc.object().value("value").toString();
    };

    auto step = QSharedPointer<OpenAi2::StepMultipleAsk>::create();
    step->id = QString("AttEquiv_v2_%1_%2_%3_%4").arg(fieldIdAmzV02, value, langCodeFrom, langCodeTo);
    step->name = "Attribute equivalence selection";
    step->cachingKey = step->id;
    step->neededReplies = 3;
    step->maxRetries = 12;
    step->gptModel = "gpt-5.2";
    step->getPrompt = buildPrompt;
    step->validate = validateReply;
    step->chooseBest = OpenAi2::CHOOSE_MOST_FREQUENT;
    
    // Application
    // We need to capture 'this' safely? Coroutine should be fine if 'this' is alive. 
    // Usually 'templateFiller' or 'parent' keeps 'this' alive.
    step->apply = [this, fieldIdAmzV02, value, parseReply](const QString &best) {
        if (!best.isEmpty()) {
            QString chosen = parseReply(best);
            if (!chosen.isEmpty()) {
                // We add BOTH the original value and the chosen equivalent value to the record
                // so they become linked in the same set.
                recordAttribute(fieldIdAmzV02, {value, chosen});
            }
        }
    };
    step->onLastError = [fieldIdAmzV02](const QString &reply, QNetworkReply::NetworkError networkError, const QString &err) -> bool {
         QString errorMsg = QString("NetworkError: %1 | Reply: %2 | Error: %3")
                 .arg(QString::number(networkError), reply, err);
         ExceptionTemplate exception;
         exception.setInfos("Attribute Equivalence Error", "Failed to get equivalent values for " + fieldIdAmzV02 + ": " + errorMsg);
         exception.raise();
         return true;
    };

    QList<QSharedPointer<OpenAi2::StepMultipleAsk>> steps;
    steps << step;
    co_await OpenAi2::instance()->askGptMultipleTimeCoro(steps, "gpt-5.2");
}


QSet<QString> AttributeEquivalentTable::getEquivalentGenderWomen() const
{
    return getEquivalentValues("target_gender#1.value", "Female");
}

QSet<QString> AttributeEquivalentTable::getEquivalentGenderMen() const
{
    return getEquivalentValues("target_gender#1.value", "Male");
}

QSet<QString> AttributeEquivalentTable::getEquivalentGenderUnisex() const
{
    return getEquivalentValues("target_gender#1.value", "Unisex");
}

QSet<QString> AttributeEquivalentTable::getEquivalentAgeAdult() const
{
    return getEquivalentValues("age_range_description#1.value", "Adult");
}

QSet<QString> AttributeEquivalentTable::getEquivalentAgeKid() const
{
    return getEquivalentValues("age_range_description#1.value", "Little Kid");
}

QSet<QString> AttributeEquivalentTable::getEquivalentAgeBaby() const
{
    return getEquivalentValues("age_range_description#1.value", "Infant");
}

QVariant AttributeEquivalentTable::headerData(
        int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
    {
        if (section >= 0 && section < HEADERS.size())
        {
            return HEADERS[section];
        }
    }
    return QVariant{};
}

int AttributeEquivalentTable::rowCount(const QModelIndex &) const
{
    return m_listOfStringList.size();
}

int AttributeEquivalentTable::columnCount(const QModelIndex &) const
{
    return HEADERS.size();
}

QVariant AttributeEquivalentTable::data(const QModelIndex &index, int role) const
{
    if (role == Qt::EditRole || role == Qt::DisplayRole)
    {
        if (index.row() >= 0 && index.row() < m_listOfStringList.size() &&
            index.column() >= 0 && index.column() < HEADERS.size())
        {
            return m_listOfStringList[index.row()][index.column()];
        }
    }
    return QVariant{};
}

bool AttributeEquivalentTable::setData(
        const QModelIndex &index, const QVariant &value, int role)
{
    if (data(index, role) != value)
    {
        if (index.row() >= 0 && index.row() < m_listOfStringList.size() &&
            index.column() >= 0 && index.column() < HEADERS.size())
        {
            m_listOfStringList[index.row()][index.column()] = value.toString();
            _saveInFile();
            _buildHash();
            emit dataChanged(index, index, {role});
            return true;
        }
    }
    return false;
}

Qt::ItemFlags AttributeEquivalentTable::flags(const QModelIndex &) const
{
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

void AttributeEquivalentTable::_buildHash()
{
    for (const auto &stringList : m_listOfStringList)
    {
        const auto &cellValues = stringList.last().split(CELL_SEP);
        m_fieldIdAmzV02_listOfEquivalents[stringList[0]]
                << QSet<QString>{cellValues.begin(), cellValues.end()};
    }
}

void AttributeEquivalentTable::_loadFromFile()
{
    QFile file{m_filePath};
    if (file.open(QFile::ReadOnly))
    {
        QTextStream stream{&file};
        const auto &lines = stream.readAll().split("\n");
        
        if (!lines.isEmpty())
        {
            // Skip first line (header)
            for (int i=1; i<lines.size(); ++i)
            {
                if (lines[i].trimmed().isEmpty())
                {
                    continue;
                }
                
                // Assuming standard comma separation for the CSV columns themselves
                // We need a definition for COL_SEP or use "," directly. 
                // Previous files use COL_SEP which is defined in CMakeLists.txt via add_definitions(-DCOL_SEP=",")
                // So I can use COL_SEP here too.
                
                const auto &elements = lines[i].split(COL_SEP);
                if (elements.size() == HEADERS.size())
                {
                    m_listOfStringList << elements;
                }
            }
            std::sort(m_listOfStringList.begin(), m_listOfStringList.end());
        }
        file.close();
    }
}

void AttributeEquivalentTable::_saveInFile()
{
    QFile file{m_filePath};
    if (file.open(QFile::WriteOnly))
    {
        QTextStream stream{&file};
        stream << HEADERS.join(COL_SEP);
        for (const auto &row : m_listOfStringList)
        {
            stream << "\n" + row.join(COL_SEP);
        }
        file.close();
    }
}
