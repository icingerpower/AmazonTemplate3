#include <QFileInfo>
#include <QRegularExpression>

#include "TemplateExceptions.h"
#include "TemplateFiller.h"

TemplateFiller::TemplateFiller(
        const QString &templateFromPath, const QStringList &templateToPaths)
{
    setTemplates(templateFromPath, templateToPaths);
}

void TemplateFiller::setTemplates(
        const QString &templateFromPath, const QStringList &templateToPaths)
{
    m_templateFromPath = templateFromPath;
    m_templateToPaths = templateToPaths;
    m_countryCodeFrom = _getCountryCode(templateFromPath);
    m_langCodeFrom = _getLangCode(templateFromPath);
    m_workingDir = QFileInfo{m_templateFromPath}.dir();
    m_workingDirImage = m_workingDir.absoluteFilePath("images");
}

void TemplateFiller::checkParentSkus()
{
    QXlsx::Document document(m_templateFromPath);
    _selectTemplateSheet(document);
    const auto &fieldId_index = _get_fieldId_index(document);
    int indColSku = _getIndColSku(fieldId_index);
    int indColSkuParent = _getIndColSkuParent(fieldId_index);
    QSet<QString> parentsDone;
    QSet<QString> skus;
    QString lastParent;
    const auto &dim = document.dimension();
    int lastRow = dim.lastRow();
    auto version = _getDocumentVersion(document);
    int row = _getRowFieldId(version) + 1;
    for (int i=row; i<lastRow; ++i)
    {
        auto cellSku = document.cellAt(i+1, indColSku + 1);
        if (!cellSku)
        {
            break;
        }
        QString sku{cellSku->value().toString()};
        if (sku.startsWith("ABC"))
        {
            continue;
        }
        if (!sku.isEmpty())
        {
            if (!skus.contains(sku))
            {
                skus.insert(sku);
            }
            else
            {
                TemplateExceptions exception;
                exception.setInfos(
                            QObject::tr("Double skus"),
                            QObject::tr("The following skus appears twice:") + " " + sku);
                exception.raise();
            }
        }
        auto cellSkuParent = document.cellAt(i+1, indColSkuParent + 1);
        if (cellSkuParent)
        {
            QString skuParent{cellSkuParent->value().toString()};
            if (!lastParent.isEmpty()
                    && skuParent != lastParent
                    && parentsDone.contains(skuParent))
            {
                TemplateExceptions exception;
                exception.setInfos(
                            QObject::tr("Uncoherent parent"),
                            QObject::tr("The following parent appears twice:") + " " + skuParent);
                exception.raise();
            }
            if (!skuParent.isEmpty())
            {
                lastParent = skuParent;
                parentsDone.insert(lastParent);
            }
        }
    }
}

void TemplateFiller::checkKeywords()
{
    const auto &keywordsFileInfos = m_workingDir.entryInfoList(
                QStringList{"keywor*.txt", "Keywor*.txt"}, QDir::Files, QDir::Name);
    if (keywordsFileInfos.size() == 0)
    {
        TemplateExceptions exception;
        exception.setInfos(
                    QObject::tr("Keywords file missing"),
                    QObject::tr("The keywords.txt file is missing"));
        exception.raise();
    }
    else
    {
        QStringList keywordFilePaths;
        for (const auto &fileInfo : keywordsFileInfos)
        {
            keywordFilePaths << fileInfo.absoluteFilePath();
        }
        const auto &countryCode_langCodes = _readKeywords(keywordFilePaths);
        for (const auto &templateToPath : m_templateToPaths)
        {
            if (templateToPath.contains("amazon", Qt::CaseInsensitive))
            {
                const auto &countryCodeTo = _getCountryCode(templateToPath);
                const auto &langCodeTo = _getLangCode(templateToPath);
                if (!countryCode_langCodes.contains(countryCodeTo))
                {
                    TemplateExceptions exception;
                    exception.setInfos(
                                QObject::tr("Keywords file issue"),
                                QObject::tr("The keywords.txt file doesn't contains the country code") + ": " + countryCodeTo);
                }
                else if (countryCode_langCodes[countryCodeTo].contains(langCodeTo))
                {
                    TemplateExceptions exception;
                    exception.setInfos(
                                QObject::tr("Keywords file issue"),
                                QObject::tr("The keywords.txt file doesn't contains") + ": " + countryCodeTo + "/" + langCodeTo);
                }
            }

        }
    }
}

QHash<QString, QSet<QString>> TemplateFiller::_readKeywords(const QStringList &filePaths)
{
    QHash<QString, QSet<QString>> countryCode_langCodes;
    for (const auto &filePath : filePaths)
    {
        auto patterns = QFileInfo{filePath}.baseName().split("__");
        patterns.takeFirst();
        QFile file{filePath};
        if (file.open(QFile::ReadOnly))
        {
            QTextStream stream{&file};
            auto lines = stream.readAll().split("\n");
            file.close();
            for (int i=0; i<lines.size(); ++i)
            {
                if (lines[i].startsWith("[") && lines[i].endsWith("]") && i+1<lines.size())
                {
                    const auto &countryLangCode = lines[i].mid(1, lines[i].size()-2);
                    QString langCode = _getLangCodeFromText(countryLangCode);
                    QString countryCode;
                    if (countryLangCode.contains("_"))
                    {
                        const auto &elements = countryLangCode.split("_");
                        countryCode = elements[1].toUpper();
                    }
                    else
                    {
                        countryCode = countryLangCode;
                    }
                    countryCode_langCodes[countryCode].insert(langCode);
                    if (patterns.isEmpty())
                    {
                        m_countryCode_langCode_keywords[countryCode][langCode] = lines[i+1];
                    }
                    else
                    {
                        for (const auto &pattern : patterns)
                        {
                            m_skuPattern_countryCode_langCode_keywords[pattern][countryCode][langCode] = lines[i+1];
                        }
                    }
                }
            }
        }
    }
    return countryCode_langCodes;
}

void TemplateFiller::checkPreviewImages()
{
    const auto &imageFileInfos = m_workingDirImage.entryInfoList(
                QStringList{"*.jpg"}, QDir::Files);
    QSet<QString> existingImageBaseNames;
    for (const auto &imageFileInfo : imageFileInfos)
    {
        const auto &baseName = imageFileInfo.baseName();
        existingImageBaseNames.insert(baseName);
    }
    QXlsx::Document document(m_templateFromPath);
    _selectTemplateSheet(document);
    const auto &fieldId_index = _get_fieldId_index(document);
    const auto &parentSku_skus = _get_parentSku_skus(document);
    int indColSku = _getIndColSku(fieldId_index);
    int indColSkuParent = _getIndColSkuParent(fieldId_index);
    int indColColor = _getIndColColorName(fieldId_index);
    QHash<QString, QSet<QString>> parent_color;
    const auto &dim = document.dimension();
    int lastRow = dim.lastRow();
    auto version = _getDocumentVersion(document);
    int row = _getRowFieldId(version) + 1;
    QSet<QString> missingImageBaseNames;
    for (int i=row; i<lastRow; ++i)
    {
        auto cellSku = document.cellAt(i+1, indColSku + 1);
        if (!cellSku)
        {
            break;
        }
        QString sku{cellSku->value().toString()};
        if (sku.startsWith("ABC") || sku.isEmpty())
        {
            continue;
        }
        bool isParent = parentSku_skus.contains(sku);
        if (isParent)
        {
            continue;
        }
        auto cellSkuParent = document.cellAt(i+1, indColSkuParent + 1);
        QString color;
        QString skuParent;
        if (cellSkuParent)
        {
            skuParent = cellSkuParent->value().toString();
        }
        auto cellColor = document.cellAt(i+1, indColColor + 1);
        if (cellColor)
        {
            color = cellColor->value().toString();
        }
        QString skuImageBaseName;
        if (skuParent.isEmpty())
        {
            skuImageBaseName = sku;
        }
        else
        {
            if (!parent_color.contains(skuParent)
                    || !parent_color[skuParent].contains(color))
            {
                skuImageBaseName = sku;
                parent_color[skuParent].insert(color);
            }
        }
        if (!skuImageBaseName.isEmpty() && !existingImageBaseNames.contains(skuImageBaseName))
        {
            missingImageBaseNames.insert(skuImageBaseName);
        }
    }
    QStringList missingImageFileNames{missingImageBaseNames.begin(), missingImageBaseNames.end()};
    std::sort(missingImageFileNames.begin(), missingImageFileNames.end());
    for (auto &missingImageFileName : missingImageFileNames)
    {
        missingImageFileName += ".jpg";
    }
    if (missingImageFileNames.size() > 0)
    {
        TemplateExceptions exception;
        exception.setInfos(
                    QObject::tr("Preview images missing"),
                    QObject::tr("The following AI preview images are missing in the folder images") + ":\n" + missingImageFileNames.join("\n"));
        exception.raise();
    }
}

int TemplateFiller::_getIndCol(
        const QHash<QString, int> &fieldId_index
        , const QStringList &possibleValues) const
{
    for (const auto &possibleSkuId : possibleValues)
    {
        auto it = fieldId_index.find(possibleSkuId);
        if (it != fieldId_index.end())
        {
            return it.value();
        }
    }
    Q_ASSERT(false);
    return -1;
}

int TemplateFiller::_getIndColSku(
    const QHash<QString, int> &fieldId_index) const
{
    const QStringList possibleValues{
        "item_sku", "contribution_sku#1.value"};
    return _getIndCol(fieldId_index, possibleValues);
}

int TemplateFiller::_getIndColSkuParent(
    const QHash<QString, int> &fieldId_index) const
{
    const QStringList possibleValues{
        "parent_sku", "child_parent_sku_relationship#1.parent_sku"};
    return _getIndCol(fieldId_index, possibleValues);
}

int TemplateFiller::_getIndColColorName(const QHash<QString, int> &fieldId_index) const
{
    static const QStringList FIELD_IDS_COLOR_NAME{
        "color_name"
        , "color#1.value"
    };
    return _getIndCol(fieldId_index, FIELD_IDS_COLOR_NAME);
}

QString TemplateFiller::_getCountryCode(const QString &templateFilePath) const
{
    QStringList elements = QFileInfo{templateFilePath}.baseName().split("-");
    bool isNum = false;
    while (true)
    {
        elements.last().toInt(&isNum);
        if (isNum)
        {
            elements.takeLast();
        }
        else
        {
            break;
        }
    }
    if (elements.last().contains("_"))
    {
        return elements.last().split("_").last();

    }
    return elements.last();
}

void TemplateFiller::_selectTemplateSheet(QXlsx::Document &doc) const
{
    const QStringList SHEETS_TEMPLATE{
        "Template"
        , "Vorlage"
        , "Modèle"
        , "Sjabloon"
        , "Mall"
        , "Szablon"
        , "Plantilla"
        , "Modello"
        , "Şablon"
        , "Gabarit"
        , "テンプレート"
    };
    bool sheetSelected = false;
    for (const QString &sheetName : SHEETS_TEMPLATE)
    {
        if (doc.selectSheet(sheetName))
        {
            sheetSelected = true;
            break;
        }
    }

    Q_ASSERT(sheetSelected);

    if (!sheetSelected)
    {
        QStringList sheets = doc.sheetNames();
        if (sheets.size() >= 5)
        {
            doc.selectSheet(sheets.at(4)); // 5th sheet
        }
        else
        {
            Q_ASSERT(false);
            doc.selectSheet(sheets.first());
        }
    }
}

TemplateFiller::VersionAmz TemplateFiller::_getDocumentVersion(
    QXlsx::Document &document) const
{
    _selectTemplateSheet(document);
    auto firstCell = document.cellAt(1, 1);
    if (firstCell)
    {
        const QString version{firstCell->value().toString()};
        if (version.startsWith("settings", Qt::CaseInsensitive))
        {
            return V02;
        }
        else
        {
            return V01;
        }
    }
    Q_ASSERT(false);
    return V01;
}

int TemplateFiller::_getRowFieldId(VersionAmz version) const
{
    if (version == V01)
    {
        return 2;
    }
    else if (version == V02)
    {
        return 4;
    }
    else
    {
        Q_ASSERT(false);
        return -1;
    }
}

QHash<QString, int> TemplateFiller::_get_fieldId_index(
    QXlsx::Document &doc) const
{
    auto version = _getDocumentVersion(doc);
    int rowCell = _getRowFieldId(version);
    const auto &dim = doc.dimension();
    QHash<QString, int> colId_index;
    int lastColumn = dim.lastColumn();
    for (int i=0; i<lastColumn; ++i)
    {
        auto cell = doc.cellAt(rowCell + 1, i + 1);
        if (cell)
        {
            QString fieldId{cell->value().toString()};
            if (!fieldId.isEmpty())
            {
                _formatFieldId(fieldId);
                if (!colId_index.contains(fieldId))
                {
                    colId_index[fieldId] = i;
                }
            }
        }
    }
    return colId_index;
}

QHash<QString, QSet<QString>> TemplateFiller::_get_parentSku_skus(
        QXlsx::Document &doc) const
{
    QHash<QString, QSet<QString>> parentSku_skus;
    const auto &fieldId_index = _get_fieldId_index(doc);
    int indColSku = _getIndColSku(fieldId_index);
    int indColSkuParent = _getIndColSkuParent(fieldId_index);

    const auto &dim = doc.dimension();
    int lastRow = dim.lastRow();
    auto version = _getDocumentVersion(doc);
    int row = _getRowFieldId(version) + 1;

    for (int i=row; i<lastRow; ++i)
    {
        auto cellSku = doc.cellAt(i+1, indColSku + 1);
        if (!cellSku)
        {
            break;
        }

        QString sku{cellSku->value().toString()};
        if (sku.startsWith("ABC") || sku.isEmpty())
        {
            continue;
        }

        auto cellSkuParent = doc.cellAt(i+1, indColSkuParent + 1);
        if (!cellSkuParent)
        {
            continue;
        }

        QString skuParent{cellSkuParent->value().toString()};
        if (skuParent.isEmpty())
        {
            continue;
        }

        parentSku_skus[skuParent].insert(sku);
    }
    return parentSku_skus;
}

void TemplateFiller::_formatFieldId(QString &fieldId) const
{
    static const QRegularExpression brackets(R"(\[[^\]]*\])");
    fieldId.remove(brackets);
    if (fieldId.contains(" "))
    {
        fieldId = fieldId.split(" ")[0];
    }
    Q_ASSERT(!fieldId.contains("[") && !fieldId.contains("]"));
}

QString TemplateFiller::_getLangCode(const QString &templateFilePath) const
{
    QStringList elements = QFileInfo{templateFilePath}.baseName().split("-");
    bool isNum = false;
    while (true)
    {
        elements.last().toInt(&isNum);
        if (isNum)
        {
            elements.takeLast();
        }
        else
        {
            break;
        }
    }
    const auto &langInfos = elements.last();
    return _getLangCodeFromText(langInfos);
}

QString TemplateFiller::_getLangCodeFromText(const QString &langInfos) const
{
    if (langInfos.contains("_"))
    {
        return langInfos.split("_")[0].toUpper();
    }
    if (langInfos.contains("UK", Qt::CaseInsensitive)
        || langInfos.contains("COM", Qt::CaseInsensitive)
        || langInfos.contains("AU", Qt::CaseInsensitive)
        || langInfos.contains("CA", Qt::CaseInsensitive)
        || langInfos.contains("SG", Qt::CaseInsensitive)
        || langInfos.contains("SA", Qt::CaseInsensitive)
        || langInfos.contains("IE", Qt::CaseInsensitive)
        || langInfos.contains("AE", Qt::CaseInsensitive)
        )
    {
        return "EN";
    }
    if (langInfos.contains("BE", Qt::CaseInsensitive))
    {
        return "FR";
    }
    if (langInfos.contains("MX", Qt::CaseInsensitive))
    {
        return "ES";
    }
    return langInfos;
}
