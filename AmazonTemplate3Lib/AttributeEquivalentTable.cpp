#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <algorithm>

#include "../../common/openai/OpenAi2.h"

#include "AttributeEquivalentTable.h"
#include "Attribute.h"

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

bool AttributeEquivalentTable::hasEquivalent(
        const QString &fieldIdAmzV02, const QString &value) const
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
    if (!m_fieldIdAmzV02_listOfEquivalents.contains(fieldIdAmzV02))
    {
        posAttr = getPosAttr(fieldIdAmzV02, equivalentValues);
    }
    if (!m_fieldIdAmzV02_listOfEquivalents.contains(fieldIdAmzV02) || posAttr == -1)
    {
        QStringList newRow;
        newRow << fieldIdAmzV02;
        for (const auto &equivalentValue : equivalentValues)
        {
            newRow << equivalentValue;
        }
        beginInsertRows(QModelIndex{}, 0, 0);
        m_listOfStringList.insert(0, newRow);
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

QCoro::Task<void> AttributeEquivalentTable::askAiEquivalentValues(
        const QString &fieldIdAmzV02, const QString &value, const Attribute *attribute)
{
    const auto &marketplace_countryCode_langCode_category_possibleValues
            = attribute->marketplace_countryCode_langCode_category_possibleValues();
    
    // Prepare AI prompts
    auto buildPrompt = [fieldIdAmzV02, value, marketplace_countryCode_langCode_category_possibleValues](int) -> QString {
        QStringList lines;
        lines << "You are helping translate/map Amazon attribute values."
              << "- For fieldId \"" + fieldIdAmzV02 + "\" with value \"" + value + "\", tell me, for each marketplace-country-lang below, the exact value to select."
              << "- Use ONLY one of the allowed values listed for that country-lang (case-insensitive match, but return the exact string)."
              << "- If no good match exists, suggest the value the closest and add a comment with probability of being wrong next to the selected word."
              << ""
              << "Allowed values per country-lang:";

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
                      // Assuming we merge all categories or just take first? 
                      // The previous code example iterated over "countryLang".
                      // Here we iterate all.
                      QSet<QString> allValues;
                      for(const auto &cat : catMap.keys()) {
                          allValues.unite(catMap[cat]);
                      }
                      
                      QStringList valuesList = allValues.values();
                      std::sort(valuesList.begin(), valuesList.end());
                      
                      QString line = QString(" - %1-%2-%3: ").arg(marketplace, country, lang);
                      line += valuesList.isEmpty() ? "(none)" : valuesList.join(", ");
                      lines << line;
                 }
            }
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
            if (!v.isEmpty()) result.insert(v);
        }
        return result;
    };
    
    auto validateReply = [parseReply](const QString &r, const QString &) -> bool {
        // Must contain at least one value if input value is not empty?
        // Or at least look like the format
        return r.contains("{") && r.contains("}");
    };
    
    auto success = QSharedPointer<bool>::create(false);
    auto resultingValues = QSharedPointer<QSet<QString>>::create();
    
    // Phase 1
    {
        auto step = QSharedPointer<OpenAi2::StepMultipleAsk>::create();
        step->id = fieldIdAmzV02 + "_" + value + "_p1";
        step->name = "Attribute equivalence Phase 1 (2x unanimous)";
        step->neededReplies = 2;
        step->cachingKey = step->id + "_v1";
        step->maxRetries = 3;
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
        auto step = QSharedPointer<OpenAi2::StepMultipleAskAi>::create();
        step->id = fieldIdAmzV02 + "_" + value + "_p2";
        step->name = "Attribute equivalence Phase 2 (3x AI choose)";
        step->neededReplies = 3;
        step->cachingKey = step->id + "_v1";
        step->maxRetries = 3;
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
        
        QList<QSharedPointer<OpenAi2::StepMultipleAskAi>> phase2;
        phase2 << step;
        co_await OpenAi2::instance()->askGptMultipleTimeAiCoro(phase2, "gpt-5.2");
    }
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
    return getEquivalentValues("age_range_description#1.value", "Child");
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
                if (lines[i].trimmed().isEmpty()) continue;
                
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
