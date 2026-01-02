#include <QFileInfo>
#include <QRegularExpression>
#include <QDirIterator>

#include "AttributesMandatoryAiTable.h"
#include "AttributesMandatoryTable.h"

#include "AiFailureTable.h"
#include "AttributeEquivalentTable.h"
#include "AttributeFlagsTable.h"
#include "AttributePossibleMissingTable.h"
#include "AttributeValueReplacedTable.h"
#include "ExceptionTemplate.h"
#include "TemplateFiller.h"

const QHash<QString, QSet<QString>> TemplateFiller::SHEETS_MANDATORY{
    {"Définitions des données", {"Obligatoire"}}
    , {"Data Definitions", {"Required"}}
    , {"Datendefinitionen", {"Erforderlich", "Pflichtfeld"}}
    , {"Definizioni dati", {"Obbligatorio"}}
    , {"Gegevensdefinities", {"Verplicht"}}
    , {"Definitioner av data", {"Krävs"}}
    , {"Veri Tanımları", {"Zorunlu"}}
    , {"Definicje danych", {"Wymagane"}}
    , {"Definiciones de datos", {"Obligatorio"}}
    , {"データ定義", {"必須"}}
};

const QSet<QString> TemplateFiller::VALUES_MANDATORY
    = []() -> QSet<QString>
{
    QSet<QString> values;
    for (auto it = SHEETS_MANDATORY.begin();
         it != SHEETS_MANDATORY.end(); ++it)
    {
        for (auto &valMandatory : it.value())
        {
            values.insert(valMandatory);
        }
    }
    return values;
}();

TemplateFiller::TemplateFiller(
        const QString &workingDirCommon
        , const QString &templateFromPath
        , const QStringList &templateToPaths
        , const QStringList &templateSourcePaths
        , const QMap<QString, QString> &skuPattern_customInstruction)
{
    m_age = AbstractFiller::Adult;
    m_gender = AbstractFiller::UndefinedGender;
    m_mandatoryAttributesTable = nullptr;
    m_mandatoryAttributesAiTable = nullptr;
    m_attributeEquivalentTable = nullptr;
    m_attributeFlagsTable = nullptr;
    m_attributePossibleMissingTable = nullptr;
    m_attributeValueReplacedTable = nullptr;
    m_aiFailureTable = nullptr;
    setTemplates(workingDirCommon
                 , templateFromPath
                 , templateToPaths
                 , templateSourcePaths
                 , skuPattern_customInstruction);
}

TemplateFiller::~TemplateFiller()
{
    delete m_mandatoryAttributesTable;
    delete m_mandatoryAttributesAiTable;
}

void TemplateFiller::setTemplates(
        const QString &commonSettingsDir
        , const QString &templateFromPath
        , const QStringList &templateToPaths
        , const QStringList &templateSourcePaths
        , const QMap<QString, QString> &skuPattern_customInstructions)
{
    m_skuPattern_customInstructions = skuPattern_customInstructions;
    QXlsx::Document doc(templateFromPath);
    const auto &productType = _get_productType(doc); // TODO Exception empty file + ma
    if (productType.isEmpty())
    {
        ExceptionTemplate exception;
        exception.setInfos(QObject::tr("Empty template")
                           , QObject::tr("The template is not filled as no product type could be found."));
        exception.raise();
    }
    m_templateFromPath = templateFromPath;
    m_templateToPaths = templateToPaths;
    if (m_templateToPaths.contains(m_templateFromPath))
    {
        m_templateToPaths.removeAll(m_templateFromPath);
        m_templateToPaths.insert(0, m_templateFromPath);
    }
    m_templateSourcePaths = templateSourcePaths;
    m_countryCodeFrom = _get_countryCode(templateFromPath);
    m_langCodeFrom = _get_langCode(templateFromPath);
    m_workingDirCommon = commonSettingsDir;
    m_workingDir = QFileInfo{m_templateFromPath}.dir();
    m_workingDirImage = m_workingDir.absoluteFilePath("images");
    _clearAttributeManagers();
    m_mandatoryAttributesAiTable = new AttributesMandatoryAiTable;
    auto all_fieldId_index = _get_fieldId_index(doc);
    for (const auto &templateToPath : m_templateToPaths)
    {
        if (templateToPath != m_templateFromPath)
        {
            QXlsx::Document docTo(templateToPath);
            const auto &fieldId_index_to = _get_fieldId_index(docTo);
            for (auto it = fieldId_index_to.cbegin(); it != fieldId_index_to.cend(); ++it)
            {
                if (!all_fieldId_index.contains(it.key()))
                {
                    all_fieldId_index[it.key()] = -1;
                }
            }
        }
    }
    const auto &fieldIdMandatory = _get_fieldIdMandatoryAll();
        // 2. Resolve "Previous" mandatory fields
    const auto &previousFieldIdMandatory = _get_fieldIdMandatoryPrevious();

    m_attributePossibleMissingTable = new AttributePossibleMissingTable{commonSettingsDir};
    m_attributeValueReplacedTable = new AttributeValueReplacedTable{commonSettingsDir};
    m_mandatoryAttributesTable = new AttributesMandatoryTable{
            commonSettingsDir, productType, fieldIdMandatory, previousFieldIdMandatory, all_fieldId_index};
    m_attributeEquivalentTable = new AttributeEquivalentTable{commonSettingsDir};
    m_attributeFlagsTable = new AttributeFlagsTable{commonSettingsDir};
    m_aiFailureTable = new AiFailureTable{};
    m_marketplaceFrom = _get_marketplaceFrom();
    m_attributeFlagsTable->recordAttributeNotRecordedYet(m_marketplaceFrom, fieldIdMandatory);
    m_attributeFlagsTable->recordAttributeNotRecordedYet(m_marketplaceFrom, m_mandatoryAttributesTable->getMandatoryIds());
    m_connectionFlagsTable = m_mandatoryAttributesTable->connect(m_mandatoryAttributesTable,
                              &AttributesMandatoryTable::dataChanged,
                              m_mandatoryAttributesTable,
                              [this](){
    const auto &newMandatoryIds = m_mandatoryAttributesTable->getMandatoryIds();
    m_attributeFlagsTable->recordAttributeNotRecordedYet(m_marketplaceFrom, newMandatoryIds);
    });
}

void TemplateFiller::_clearAttributeManagers()
{
    if (m_mandatoryAttributesAiTable != nullptr)
    {
        delete m_mandatoryAttributesAiTable;
    }
    m_mandatoryAttributesAiTable = nullptr;
    if (m_mandatoryAttributesTable != nullptr)
    {
        delete m_mandatoryAttributesTable;
    }
    m_mandatoryAttributesTable = nullptr;
    if (m_attributeEquivalentTable != nullptr)
    {
        m_attributeEquivalentTable->disconnect(m_connectionFlagsTable);
        m_attributeEquivalentTable->deleteLater();
    }
    m_attributeEquivalentTable = nullptr;
    if (m_attributeFlagsTable != nullptr)
    {
        m_attributeFlagsTable->deleteLater();
    }
    m_attributeFlagsTable = nullptr;
    if (m_attributePossibleMissingTable != nullptr)
    {
        m_attributePossibleMissingTable->deleteLater();
    }
    m_attributePossibleMissingTable = nullptr;
    if (m_attributeValueReplacedTable != nullptr)
    {
        m_attributeValueReplacedTable->deleteLater();
    }
    m_attributeValueReplacedTable = nullptr;
    if (m_aiFailureTable != nullptr)
    {
        m_aiFailureTable->deleteLater();
    }
    m_aiFailureTable = nullptr;
}

const QString &TemplateFiller::marketplaceFrom() const
{
    return m_marketplaceFrom;
}

QStringList TemplateFiller::_get_allTemplatePaths() const
{
    return QStringList{m_templateFromPath} << m_templateToPaths;
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
                ExceptionTemplate exception;
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
                ExceptionTemplate exception;
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
    if (skus.size() == 0)
    {
        ExceptionTemplate exception;
        exception.setInfos(
                    QObject::tr("No SKUs"),
                    QObject::tr("No skus found. Please check your file."));
        exception.raise();
    }
}

void TemplateFiller::checkKeywords()
{
    const auto &keywordsFileInfos = m_workingDir.entryInfoList(
                QStringList{"keywor*.txt", "Keywor*.txt"}, QDir::Files, QDir::Name);
    if (keywordsFileInfos.size() == 0)
    {
        ExceptionTemplate exception;
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
                const auto &countryCodeTo = _get_countryCode(templateToPath);
                const auto &langCodeTo = _get_langCode(templateToPath);
                if (!countryCode_langCodes.contains(countryCodeTo))
                {
                    ExceptionTemplate exception;
                    exception.setInfos(
                                QObject::tr("Keywords file issue"),
                                QObject::tr("The keywords.txt file doesn't contains the country code") + ": " + countryCodeTo);
                }
                else if (countryCode_langCodes[countryCodeTo].contains(langCodeTo))
                {
                    ExceptionTemplate exception;
                    exception.setInfos(
                                QObject::tr("Keywords file issue"),
                                QObject::tr("The keywords.txt file doesn't contains") + ": " + countryCodeTo + "/" + langCodeTo);
                }
            }

        }
    }
}

QHash<QString, QSet<QString>> TemplateFiller::_readKeywords()
{
    const auto &keywordsFileInfos = m_workingDir.entryInfoList(
                QStringList{"keywor*.txt", "Keywor*.txt"}, QDir::Files, QDir::Name);
    QStringList keywordFilePaths;
    for (const auto &fileInfo : keywordsFileInfos)
    {
        keywordFilePaths << fileInfo.absoluteFilePath();
    }
    return _readKeywords(keywordFilePaths);
}

QHash<QString, QSet<QString>> TemplateFiller::_readKeywords(
        const QStringList &filePaths)
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

QHash<QString, QString> TemplateFiller::checkPreviewImages()
{
    QHash<QString, QString> sku_imagePath;
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
            Q_ASSERT(!skuImageBaseName.startsWith("P-"));
            missingImageBaseNames.insert(skuImageBaseName);
        }
        if (!skuImageBaseName.isEmpty())
        {
            sku_imagePath[sku] = m_workingDirImage.absoluteFilePath(skuImageBaseName + ".jpg");
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
        ExceptionTemplate exception;
        exception.setInfos(
                    QObject::tr("Preview images missing"),
                    QObject::tr("The following AI preview images are missing in the folder images") + ":\n" + missingImageFileNames.join("\n"));
        exception.raise();
    }
    return sku_imagePath;
}

QHash<QString, QHash<QString, QHash<QString, QHash<QString, QHash<QString, QSet<QString>>>>>>
TemplateFiller::checkPossibleValues()
{
    // Will check if some possible values need to be added for some lang code
    QStringList filePaths{m_templateFromPath};
    filePaths << m_templateToPaths;
    QSet<QString> allFieldIds;
    for (const auto &filePath : filePaths)
    {
        QXlsx::Document doc(filePath);
        const auto &fieldId_possibleValues = _get_fieldId_possibleValues(doc);
        const auto &curFieldIds = fieldId_possibleValues.keys();
        for (const auto &fieldId : curFieldIds)
        {
            allFieldIds.insert(fieldId);
        }
    }

    QHash<QString, QHash<QString, QHash<QString, QHash<QString, QHash<QString, QSet<QString>>>>>>
            marketplace_countryCode_langCode_productType_fieldId_possibleValues;
    allFieldIds.intersect(m_mandatoryAttributesTable->getMandatoryIds());
    bool addedMissing = false;
    for (const auto &filePath : filePaths)
    {
        QXlsx::Document doc(filePath);
        const auto &countryCode = _get_countryCode(filePath);
        const auto &langCode = _get_langCode(filePath);
        const auto &marketplace = _get_marketplace(doc);
        const auto &productType = _get_productType(doc);
        const auto &fieldId_possibleValues = _get_fieldId_possibleValues(doc);
        for (auto it = fieldId_possibleValues.begin();
             it != fieldId_possibleValues.end(); ++it)
        {
            const auto &fieldId = it.key();
            auto possibleValues = it.value();
            if (possibleValues.size() > 0)
            {
                m_attributeValueReplacedTable->replaceIfContains(
                            marketplace, countryCode, langCode, productType, fieldId, possibleValues);
                marketplace_countryCode_langCode_productType_fieldId_possibleValues
                        [marketplace][countryCode][langCode][productType][fieldId] = possibleValues;
            }
        }
        const auto &curFieldIdsPossibleList = fieldId_possibleValues.keys();
        QSet<QString> curFieldIdsPossible{curFieldIdsPossibleList.begin(), curFieldIdsPossibleList.end()};
        const auto &fieldId_index = _get_fieldId_index(doc);
        const auto &curFieldIdsList = fieldId_index.keys();
        QSet<QString> curFieldIds{curFieldIdsList.begin(), curFieldIdsList.end()};
        QSet<QString> missingFieldIds = allFieldIds;
        missingFieldIds.intersect(curFieldIds);
        missingFieldIds.subtract(curFieldIdsPossible);
        for (const auto &missingFieldId : missingFieldIds)
        {
            if (!m_attributePossibleMissingTable->contains(
                        marketplace, countryCode, langCode, productType, missingFieldId))
            {
                m_attributePossibleMissingTable->recordAttribute(
                            marketplace
                            , countryCode
                            , langCode
                            , productType
                            , missingFieldId
                            , {"TODO"}
                            );
                addedMissing = true;
            }
            else
            {
                auto possibleValues = m_attributePossibleMissingTable->possibleValues(
                            marketplace, countryCode, langCode, productType, missingFieldId);
                if (possibleValues.size() > 0)
                {
                    m_attributeValueReplacedTable->replaceIfContains(
                                marketplace, countryCode, langCode, productType, missingFieldId, possibleValues);
                    marketplace_countryCode_langCode_productType_fieldId_possibleValues
                            [marketplace][countryCode][langCode][productType][missingFieldId]
                            = possibleValues;
                }
            }
        }
    }
    if (addedMissing)
    {
        ExceptionTemplate exception;
        exception.setInfos(QObject::tr("Possible attributes missing"),
                           QObject::tr("Some field has their possible attributes missing. Complete them in the attributes view."));
        exception.raise();
    }
    return marketplace_countryCode_langCode_productType_fieldId_possibleValues;
}

void TemplateFiller::buildAttributes()
{
    const auto &marketplace = _get_marketplaceFrom();
    const auto &mandatoryIds = m_mandatoryAttributesTable->getMandatoryIds();
    const auto &marketplace_countryCode_langCode_fieldId_possibleValues = checkPossibleValues();
    struct TemplateInfo{
        QString marketplace;
        QString countryCode;
        QString langCode;
        QString productType;
    };
    QHash<QString, TemplateInfo> templatePath_infos;
    const QStringList &templatePaths = _get_allTemplatePaths();
    for (const auto &templatePath : templatePaths)
    {
        TemplateInfo infos;
        QXlsx::Document doc{templatePath};
        infos.productType = _get_productType(doc);
        infos.marketplace = _get_marketplace(doc);
        infos.countryCode = _get_countryCode(templatePath);
        infos.langCode = _get_langCode(templatePath);
        templatePath_infos[templatePath] = infos;
    }

    for (const auto &mandatoryId : mandatoryIds)
    {
        const auto &marketplace_fieldId
                = m_attributeFlagsTable->get_marketplace_id(marketplace, mandatoryId);
        auto attribute = QSharedPointer<Attribute>::create();
        for (auto it = marketplace_fieldId.begin();
             it != marketplace_fieldId.end(); ++it)
        {
            m_marketplace_attributeId_attributeInfos[it.key()][it.value()] = attribute;
        }
        attribute->setFlag(m_attributeFlagsTable->getFlags(marketplace, mandatoryId));
        for (const auto &templatePath : templatePaths)
        {
            const auto &infos = templatePath_infos[templatePath];
            if (marketplace_countryCode_langCode_fieldId_possibleValues.contains(infos.marketplace)
                    && marketplace_countryCode_langCode_fieldId_possibleValues[infos.marketplace].contains(
                        infos.countryCode)
                    && marketplace_countryCode_langCode_fieldId_possibleValues[infos.marketplace][infos.countryCode].contains(
                        infos.langCode)
                    && marketplace_countryCode_langCode_fieldId_possibleValues[infos.marketplace][infos.countryCode][infos.langCode].contains(
                        infos.productType)
                    && marketplace_countryCode_langCode_fieldId_possibleValues[infos.marketplace][infos.countryCode][infos.langCode][infos.productType].contains(
                        mandatoryId)
                    )
            {
                attribute->setPossibleValues(
                            infos.marketplace,
                            infos.countryCode,
                            infos.langCode,
                            infos.productType,
                            marketplace_countryCode_langCode_fieldId_possibleValues
                            [infos.marketplace][infos.countryCode][infos.langCode][infos.productType][mandatoryId]
                        );
            }
        }
    }
}

void TemplateFiller::checkColumnsFilled()
{
    //Q_ASSERT(m_marketplace_attributeId_attributeInfos.size() > 0); // Build attribute should have been called
    QXlsx::Document doc{m_templateFromPath};
    const auto &marketplace = _get_marketplace(doc);
    const auto &fieldIds = m_mandatoryAttributesTable->getMandatoryIds();
    QSet<QString> fieldIdsNeededAll;
    QSet<QString> fieldIdsNeededChildren;
    QSet<QString> fieldIdsNeededNoParent;

    for (const auto &fieldId : fieldIds)
    {
        if (m_attributeFlagsTable->hasFlag(
                    marketplace, fieldId, Attribute::NoAI))
        {
            fieldIdsNeededChildren.insert(fieldId);
            if (!m_attributeFlagsTable->hasFlag(
                        marketplace, fieldId, Attribute::ChildOnly))
            {
                fieldIdsNeededAll.insert(fieldId);
            }
        }
        if (m_attributeFlagsTable->hasFlag(
                    marketplace, fieldId, Attribute::ChildOnly))
        {
            fieldIdsNeededNoParent.insert(fieldId);
        }
    }
    const auto &dim = doc.dimension();
    int lastRow = dim.lastRow();
    auto version = _getDocumentVersion(doc);
    int row = _getRowFieldId(version) + 1;
    const auto &fieldId_index = _get_fieldId_index(doc);
    const auto &parentSku_skus = _get_parentSku_skus(doc);
    int indColSku = _getIndColSku(fieldId_index);
    QSet<QString> fieldIdsWithMissingValue;
    QSet<QString> fieldIdsShouldNotHaveValue;
    for (int i=row; i<lastRow; ++i)
    {
        const auto &sku = _get_cellVal(doc, i, indColSku);
        if (sku.startsWith("ABC"))
        {
            continue;
        }
        else if (sku.isEmpty())
        {
            break;
        }
        bool isParent = parentSku_skus.contains(sku);
        const auto &fieldIdsToCheck = isParent ? fieldIdsNeededAll : fieldIdsNeededChildren;
        for (const auto &fieldId : fieldIdsToCheck)
        {
            if (fieldId_index.contains(fieldId))
            {
                int colIndex = fieldId_index[fieldId];
                const auto &celVal = _get_cellVal(doc, i, colIndex);
                if (celVal.isEmpty())
                {
                    fieldIdsWithMissingValue.insert(fieldId);
                }
            }
        }
        if (isParent)
        {
            for (const auto &fieldId : fieldIdsNeededNoParent)
            {
                if (fieldId_index.contains(fieldId))
                {
                    int colIndex = fieldId_index[fieldId];
                    const auto &celVal = _get_cellVal(doc, i, colIndex);
                    if (!celVal.isEmpty())
                    {
                        fieldIdsShouldNotHaveValue.insert(fieldId);
                    }
                }
            }
        }
    }
    if (fieldIdsWithMissingValue.size() > 0)
    {
        QStringList fieldIdsWithMissingValueList{fieldIdsWithMissingValue.begin(), fieldIdsWithMissingValue.end()};
        fieldIdsWithMissingValueList.sort();
        ExceptionTemplate exception;
        exception.setInfos(QObject::tr("Values missing")
                           , QObject::tr("The following field ids doesn't have a required value") + ":\n" + fieldIdsWithMissingValueList.join("\n"));
        exception.raise();
    }
    if (fieldIdsShouldNotHaveValue.size() > 0)
    {
        QStringList fieldIdsShouldNotHaveValueList{fieldIdsShouldNotHaveValue.begin(), fieldIdsShouldNotHaveValue.end()};
        fieldIdsShouldNotHaveValueList.sort();
        ExceptionTemplate exception;
        exception.setInfos(QObject::tr("Wrong values for parent")
                           , QObject::tr("The following field ids have values for parent while no value is required") + ":\n" + fieldIdsShouldNotHaveValueList.join("\n"));
        exception.raise();
    }
}

QCoro::Task<void> TemplateFiller::fillValues()
{
    buildAttributes();
    m_sku_imagePreviewFilePath = checkPreviewImages();
    _fillValuesSources();
    co_await _readAgeGender();
    m_sku_fieldId_fromValues = _get_sku_fieldId_fromValues(m_templateFromPath);
    const auto &mandatoryFieldIds = m_mandatoryAttributesTable->getMandatoryIds();
    QStringList sortedFieldIds{mandatoryFieldIds.begin(), mandatoryFieldIds.end()};
    sortedFieldIds.sort();

    QXlsx::Document document(m_templateFromPath);
    const auto &parentSku_variation_skus = _get_parentSku_variation_skus(document);
    const auto &marketplaceFrom = _get_marketplace(document);
    const auto &productTypeFrom = _get_productType(document);
    const auto &langCodeFrom = _get_langCode(m_templateFromPath);
    const auto &countryCodeFrom = _get_countryCode(m_templateFromPath);

    co_await AbstractFiller::fillValuesForAi(this,
                                             productTypeFrom,
                                             countryCodeFrom,
                                             langCodeFrom,
                                             m_gender,
                                             m_age,
                                             m_skuPattern_customInstructions,
                                             m_sku_fieldId_fromValues,
                                             m_sku_attribute_valuesForAi);

    for (const auto &targetPath : m_templateToPaths) // TODO check order and make sure from is made first for possible values
    {
        QXlsx::Document docTo{targetPath};
        const auto &countryCodeTo = _get_countryCode(targetPath);
        const auto &langCodeTo = _get_langCode(targetPath);
        const auto &marketplaceTo = _get_marketplace(docTo);
        const auto &productTypeTo = _get_productType(document);


        for (const auto &filler : AbstractFiller::ALL_FILLERS_SORTED)
        {
            for (const auto &fieldIdFrom : sortedFieldIds)
            {
                const auto &attribute =  m_marketplace_attributeId_attributeInfos[marketplaceFrom][fieldIdFrom].data();
                if (filler->canFill(this, attribute, marketplaceFrom, fieldIdFrom))
                {
                    const auto &fieldIdTo = m_attributeFlagsTable->getFieldId(
                                marketplaceFrom, fieldIdFrom, marketplaceTo);
                    co_await filler->fill(
                                this
                                , parentSku_variation_skus
                                , marketplaceFrom
                                , marketplaceTo
                                , fieldIdFrom
                                , fieldIdTo
                                , attribute
                                , productTypeFrom
                                , productTypeTo
                                , countryCodeFrom
                                , langCodeFrom
                                , countryCodeTo
                                , langCodeTo
                                , m_countryCode_langCode_keywords
                                , m_skuPattern_countryCode_langCode_keywords
                                , m_gender
                                , m_age
                                , m_sku_fieldId_fromValues
                                , m_sku_attribute_valuesForAi
                                , m_countryCode_langCode_sku_fieldId_toValues[countryCodeFrom][langCodeFrom]
                                , m_langCode_sku_fieldId_toValues[langCodeTo]
                                , m_countryCode_langCode_sku_fieldId_toValues[countryCodeTo][langCodeTo]
                                );
                }
            }
        }
    }
    _saveTemplates();
    co_return;
}

void TemplateFiller::_fillValuesSources()
{
    const auto &mandatoryFieldIds = m_mandatoryAttributesTable->getMandatoryIds();
    const auto &marketplaceFrom = _get_marketplaceFrom();
    if (m_templateSourcePaths.size() > 0)
    {
        for (const auto &templateSourcePath : m_templateSourcePaths)
        {
            const auto &countryCode = _get_countryCode(templateSourcePath);
            const auto &langCode = _get_langCode(templateSourcePath);
            QSet<QString> whiteListSourceFieldIds;
            QXlsx::Document document(templateSourcePath);
            const auto &marketplaceSource = _get_marketplace(document);
            for (const auto &mandatoryFieldId : mandatoryFieldIds)
            {
                if (m_attributeFlagsTable->hasFlag(marketplaceFrom, mandatoryFieldId, Attribute::ReadablePreviousTemplates))
                {
                    const auto &mandatoryFieldIdSource = m_attributeFlagsTable->getFieldId(
                            marketplaceFrom, mandatoryFieldId, marketplaceSource);
                    whiteListSourceFieldIds.insert(mandatoryFieldIdSource);
                }
            }
            m_countryCode_langCode_sku_fieldId_sourceValues[countryCode][langCode]
                    = _get_sku_fieldId_fromValues(templateSourcePath, whiteListSourceFieldIds);
        }
    }
}

void TemplateFiller::_saveTemplates()
{
    // 1. Get ordered SKUs from source template
    QXlsx::Document docFrom(m_templateFromPath);
    _selectTemplateSheet(docFrom);
    const auto &fieldId_index_from = _get_fieldId_index(docFrom);
    int indColSkuFrom = _getIndColSku(fieldId_index_from);
    auto versionFrom = _getDocumentVersion(docFrom);
    int rowDataFrom = _getRowFieldId(versionFrom) + 1;
    const auto &dimFrom = docFrom.dimension();
    int lastRowFrom = dimFrom.lastRow();
    
    QStringList orderedSkus;
    for (int i=rowDataFrom; i<lastRowFrom; ++i)
    {
        auto cellSku = docFrom.cellAt(i+1, indColSkuFrom + 1);
        if (cellSku)
        {
            QString sku = cellSku->value().toString();
            if (!sku.isEmpty() && !sku.startsWith("ABC"))
            {
                orderedSkus << sku;
            }
        }
    }

    // 2. Fill target templates
    for (const auto &targetPath : m_templateToPaths)
    {
        const auto &countryCode = _get_countryCode(targetPath);
        const auto &langCode = _get_langCode(targetPath);
        QXlsx::Document docTo{targetPath};
        _selectTemplateSheet(docTo);
        
        const auto &fieldId_index = _get_fieldId_index(docTo);
        int indColSku = _getIndColSku(fieldId_index);
        
        // Find where to start writing (after existing data)
        int writeRow = docTo.dimension().lastRow();
        auto versionTo = _getDocumentVersion(docTo);
        int rowHeader = _getRowFieldId(versionTo) + 1;
        docTo.setRowHidden(rowHeader, false);
        
        for (const auto &sku : orderedSkus)
        {
            docTo.write(writeRow + 1, indColSku + 1, sku); // Write SKU
            if (m_countryCode_langCode_sku_fieldId_toValues.contains(countryCode)
                    && m_countryCode_langCode_sku_fieldId_toValues[countryCode].contains(langCode)
                    && m_countryCode_langCode_sku_fieldId_toValues[countryCode][langCode].contains(sku))
            {

                const auto &fieldId_value = m_countryCode_langCode_sku_fieldId_toValues[countryCode][langCode][sku];
                for (auto it = fieldId_value.begin(); it != fieldId_value.end(); ++it)
                {
                    const auto &fieldId = it.key();
                    if (fieldId_index.contains(fieldId))
                    {
                        int col = fieldId_index[fieldId];
                        docTo.write(writeRow + 1, col+1, it.value());
                    }
                }
            }
            ++writeRow;
        }

        QString toFillFilePathNew{targetPath};
        toFillFilePathNew.replace("TOFILL", "FILLED");
        Q_ASSERT(toFillFilePathNew != targetPath);
        if (toFillFilePathNew != targetPath)
        {
            docTo.saveAs(toFillFilePathNew);
        }
    }
}

const QHash<QString, QString> &TemplateFiller::sku_imagePreviewFilePath() const
{
    return m_sku_imagePreviewFilePath;
}

void TemplateFiller::saveAiValue(
        const QString &settingsFileName, const QString &id, const QString &value) const
{
    Q_ASSERT(settingsFileName.endsWith(".ini"));
    const QString &settingsFilePath = m_workingDir.absoluteFilePath(settingsFileName);
    QSettings settings{settingsFilePath, QSettings::IniFormat};
    settings.setValue(id, value);
}

bool TemplateFiller::hasAiValue(const QString &settingsFileName, const QString &id) const
{
    Q_ASSERT(settingsFileName.endsWith(".ini"));
    const QString &settingsFilePath = m_workingDir.absoluteFilePath(settingsFileName);
    QSettings settings{settingsFilePath, QSettings::IniFormat};
    return settings.contains(id);
}

QString TemplateFiller::getAiReply(const QString &settingsFileName, const QString &id) const
{
    Q_ASSERT(settingsFileName.endsWith(".ini"));
    const QString &settingsFilePath = m_workingDir.absoluteFilePath(settingsFileName);
    QSettings settings{settingsFilePath, QSettings::IniFormat};
    return settings.value(id).toString();
}

QString TemplateFiller::_get_cellVal(QXlsx::Document &doc, int row, int col) const
{
    auto cell = doc.cellAt(row+1, col + 1);
    if (cell)
    {
         return cell->value().toString();
    }
    return QString{};
}

QStringList TemplateFiller::findPreviousTemplatePath() const
{
    qDebug() << "TemplateFiller::findPreviousTemplatePath...";
    const QString &type = _get_productType(m_templateFromPath);
    QStringList templatePaths;
    QDirIterator it(m_workingDir.absolutePath() + "/..", QStringList() << "*FILLED*.xlsm", QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        const QString &filePath = it.next();
        qDebug() << "TemplateFiller::findPreviousTemplatePath...reading: " << filePath;
        QXlsx::Document doc{filePath};
        _selectTemplateSheet(doc);

        const auto &fieldId_index = _get_fieldId_index(doc);
        int indColProductType = _getIndColProductType(fieldId_index);
        
        // Read header? No, read first row after header.
        auto version = _getDocumentVersion(doc);
        int rowData = _getRowFieldId(version) + 1;
        
        auto cellProductType = doc.cellAt(rowData + 2, indColProductType + 1); // +2 in case exemple row
        if (cellProductType)
        {
            QString productType = cellProductType->value().toString();
            if (productType.compare(type, Qt::CaseInsensitive) == 0)
            {
                templatePaths << filePath;
            }
        }
    }
    qDebug() << "TemplateFiller::findPreviousTemplatePath...DONE";
    return templatePaths;
}

QString TemplateFiller::_get_productType(QXlsx::Document &doc) const
{
    _selectTemplateSheet(doc);

    const auto &fieldId_index = _get_fieldId_index(doc);
    int indColProductType = _getIndColProductType(fieldId_index);

    // Read header? No, read first row after header.
    auto version = _getDocumentVersion(doc);
    int rowData = _getRowFieldId(version) + 1;

    auto cellProductType = doc.cellAt(rowData + 2, indColProductType + 1); // +2 in case exemple row
    if (cellProductType)
    {
        return cellProductType->value().toString();
    }
    else
    {
        const auto &fieldId_possibleValues = _get_fieldId_possibleValues(doc);
        QString fieldId;
        if (version == V01)
        {
            fieldId = "feed_product_type";
        }
        else if (version == V02)
        {
            fieldId = "product_type#1.value";
        }
        else
        {
            Q_ASSERT(false);
        }
        return *fieldId_possibleValues[fieldId].begin();
    }
    return QString{};
}


QString TemplateFiller::_get_productType(const QString &filePath) const
{
    QXlsx::Document doc{filePath};
    return _get_productType(doc);
}

QSharedPointer<QSettings> TemplateFiller::settingsProducts() const
{
    const auto &settingsPath = m_workingDir.absoluteFilePath("settings.ini");
    return QSharedPointer<QSettings>::create(settingsPath, QSettings::IniFormat);
}

AiFailureTable *TemplateFiller::aiFailureTable() const
{
    return m_aiFailureTable;
}

QSharedPointer<QSettings> TemplateFiller::settingsCommon() const
{
    const auto &settingsPath = m_workingDirCommon.absoluteFilePath("settings.ini");
    return QSharedPointer<QSettings>::create(settingsPath, QSettings::IniFormat);
}

QHash<QString, QHash<QString, QString>> TemplateFiller::_get_sku_fieldId_fromValues(
        const QString &templatePath, const QSet<QString> &fieldIdsWhiteList) const
{
    QHash<QString, QHash<QString, QString>> sku_fieldId_values;
    QXlsx::Document document(templatePath);
    _selectTemplateSheet(document);
    const auto &fieldId_index = _get_fieldId_index(document);
    int indColSku = _getIndColSku(fieldId_index);
    const auto &dim = document.dimension();
    int lastRow = dim.lastRow();
    auto version = _getDocumentVersion(document);
    int row = _getRowFieldId(version) + 1;
    for (int i=row; i<lastRow; ++i)
    {
        const auto &sku = _get_cellVal(document, i, indColSku);
        if (sku.startsWith("ABC"))
        {
            continue;
        }
        if (!sku.isEmpty())
        {
            for (auto it = fieldId_index.cbegin();
                 it != fieldId_index.cend(); ++it)
            {
                const auto &fieldId = it.key();
                int colIndex = it.value();
                const auto &value = _get_cellVal(document, i, colIndex);
                if (!value.isEmpty())
                {
                    if (fieldIdsWhiteList.isEmpty() || fieldIdsWhiteList.contains(fieldId))
                    {
                        sku_fieldId_values[sku][fieldId] = value;
                    }
                }
            }
        }
    }
    return sku_fieldId_values;
}

QCoro::Task<TemplateFiller::AttributesToValidate> TemplateFiller::findAttributesMandatoryToValidateManually() const
{
    TemplateFiller::AttributesToValidate attributesToValidateManually;
    QXlsx::Document doc{m_templateFromPath};
    const auto &productType = _get_productType(doc);
    Q_ASSERT(!productType.isEmpty());
    _selectTemplateSheet(doc);
    const auto &fieldId_index = _get_fieldId_index(doc);
    const auto &fieldIdMandatory = _get_fieldIdMandatoryAll();
    // 1. AI Load
    co_await m_mandatoryAttributesAiTable->load(
                productType,
                fieldIdMandatory,
                fieldId_index);


    attributesToValidateManually.addedAi
            = m_mandatoryAttributesAiTable->fieldIdsAiAdded();
    attributesToValidateManually.removedAi
            = m_mandatoryAttributesAiTable->fieldIdsAiRemoved();

    co_return attributesToValidateManually;
}

void TemplateFiller::validateMandatory(
        const QSet<QString> &attributesMandatory, const QSet<QString> &attributesNotMandatory)
{
    m_mandatoryAttributesTable->update(attributesMandatory, attributesNotMandatory);
    m_attributeFlagsTable->recordAttributeNotRecordedYet(m_marketplaceFrom, attributesMandatory);
}

AttributesMandatoryTable *TemplateFiller::mandatoryAttributesTable() const
{
    return m_mandatoryAttributesTable;
}

AttributesMandatoryAiTable *TemplateFiller::mandatoryAttributesAiTable() const
{
    return m_mandatoryAttributesAiTable;
}

AttributeEquivalentTable *TemplateFiller::attributeEquivalentTable() const
{
    return m_attributeEquivalentTable;
}

AttributeFlagsTable *TemplateFiller::attributeFlagsTable() const
{
    return m_attributeFlagsTable;
}

AttributePossibleMissingTable *TemplateFiller::attributePossibleMissingTable() const
{
    return m_attributePossibleMissingTable;
}

AttributeValueReplacedTable *TemplateFiller::attributeValueReplacedTable() const
{
    return m_attributeValueReplacedTable;
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

int TemplateFiller::_getIndColGender(
        const QHash<QString, int> &fieldId_index) const
{
    const QStringList possibleValues{
        "target_gender", "target_gender#1.value"};
    return _getIndCol(fieldId_index, possibleValues);
}

int TemplateFiller::_getIndColAge(
        const QHash<QString, int> &fieldId_index) const
{
    const QStringList possibleValues{
        "age_range_description", "age_range_description#1.value"};
    return _getIndCol(fieldId_index, possibleValues);
}

int TemplateFiller::_getIndColProductType(
        const QHash<QString, int> &fieldId_index) const
{
    const QStringList possibleValues{
        "feed_product_type", "product_type#1.value"};
    return _getIndCol(fieldId_index, possibleValues);
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

QString TemplateFiller::_get_countryCode(const QString &templateFilePath) const
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

void TemplateFiller::_selectMandatorySheet(QXlsx::Document &doc) const
{
    bool sheetSelected = false;
    for (auto it = SHEETS_MANDATORY.begin();
         it != SHEETS_MANDATORY.end(); ++it)
    {
        if (doc.selectSheet(it.key()))
        {
            sheetSelected = true;
            break;
        }
    }
    Q_ASSERT(sheetSelected);

    if (!sheetSelected)
    {
        QStringList sheets = doc.sheetNames();
        if (sheets.size() >= 7)
        {
            doc.selectSheet(sheets.at(6)); // 7th sheet
        }
        else
        {
            Q_ASSERT(false);
            // Fallback: select the first sheet available.
        }
    }
}

void TemplateFiller::_selectValidValuesSheet(QXlsx::Document &doc) const
{
    const QStringList SHEETS_VALID_VALUES{
        "Valeurs valides"
        , "Valid Values"
        , "Valori validi"
        , "Geldige waarden"
        , "Gültige Werte"
        , "Giltiga värden"
        , "Valores válidos"
        , "Poprawne wartości"
        , "Geçerli Değerler"
        , "推奨値"
    };

    bool sheetSelected = false;
    for (const QString &sheetName : SHEETS_VALID_VALUES)
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
            doc.selectSheet(sheets.at(3)); // 4th sheet
        }
        else
        {
            Q_ASSERT(false);
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

QString TemplateFiller::_get_marketplaceFrom() const
{
    QXlsx::Document doc{m_templateFromPath};
    return _get_marketplace(doc);
}

QString TemplateFiller::_get_marketplace(QXlsx::Document &doc) const
{
     _selectTemplateSheet(doc);
    auto version = _getDocumentVersion(doc);
    if (version == V01)
    {
        return Attribute::AMAZON_V01;
    }
    else if (version == V02)
    {
        return Attribute::AMAZON_V02;
    }
    else
    {
        Q_ASSERT(false);
    }
    return QString{};
}

QSet<QString> TemplateFiller::_get_fieldIdMandatory(QXlsx::Document &doc) const
{
    QSet<QString> fieldIds;
    _selectMandatorySheet(doc);
    auto dimMandatory = doc.dimension();
    const int colIndFieldId = 1;
    const int colIndFieldName = 2;
    const int colIndHint = 3;
    const int colIndHintSecond = 4;
    int colIndMandatory = 0;
    for (int j=0; j<10; ++j)
    {
        auto cell = doc.cellAt(2, j + 1);
        if (cell)
        {
            colIndMandatory = j;
        }
        else
        {
            break;
        }
    }
    int lastRow = dimMandatory.lastRow();
    for (int i = 3; i<lastRow; ++i)
    {
        auto cellFieldId = doc.cellAt(i+1, colIndFieldId + 1);
        auto cellFieldName = doc.cellAt(i+1, colIndFieldName + 1);
        auto cellMandatory = doc.cellAt(i+1, colIndMandatory + 1);
        if (cellFieldId && cellFieldName && cellMandatory)
        {
            QString fieldId{cellFieldId->value().toString()};
            _formatFieldId(fieldId);
            QString mandatory{cellMandatory->value().toString()};
            if (cellFieldName && cellMandatory && fieldId != mandatory)
            {
                if (VALUES_MANDATORY.contains(mandatory))
                {
                    fieldIds.insert(fieldId);
                }
            }
        }
    }
    Q_ASSERT(!fieldIds.isEmpty()); // Usually it means SHEETS_MANDATORY needs to be added a new value
    return fieldIds;
}

QSet<QString> TemplateFiller::_get_fieldIdMandatoryAll() const
{
    QXlsx::Document doc{m_templateFromPath};
    auto fieldIdMandatory = _get_fieldIdMandatory(doc);
    for (const auto &targetPath : m_templateToPaths)
    {
        QXlsx::Document docTo{targetPath};
        const auto &fieldIdMandatoryTo = _get_fieldIdMandatory(doc);
        fieldIdMandatory.unite(fieldIdMandatoryTo);
    }
    return fieldIdMandatory;
}

QSet<QString> TemplateFiller::_get_fieldIdMandatoryPrevious() const
{
    QSet<QString> previousFieldIdMandatory;
    auto settings = settingsProducts();
    const QString key{"fieldIdMandatoryPrevious"};
    if (settings->contains(key))
    {
        qDebug() << "Loading _get_fieldIdMandatoryPrevious from settings";
        return settings->value(key).value<QSet<QString>>();
    }
    qDebug() << "Loading _get_fieldIdMandatoryPrevious couldn't be done from settings";
    const auto &previousTemplatePaths = findPreviousTemplatePath();

    if (!previousTemplatePaths.isEmpty())
    {
        // Naïve approach: read the first one.
        // Ideally we might want to merge or pick best, but "previous" usually implies "last used".
        // The list might be sorted or not. existing logic 'findPreviousTemplatePath' returns list.
        // We'll proceed with the first one for now.
        QXlsx::Document docPrev{previousTemplatePaths.first()};
        if (docPrev.load())
        {
             // We reuse _get_fieldIdMandatory logic
             previousFieldIdMandatory = _get_fieldIdMandatory(docPrev);
        }
    }
    settings->setValue(key, QVariant::fromValue(previousFieldIdMandatory));
    return previousFieldIdMandatory;
}

QHash<QString, QSet<QString>> TemplateFiller::_get_fieldId_possibleValues(
        QXlsx::Document &doc) const
{
    QHash<QString, QSet<QString>> fieldId_possibleValues;
    QHash<QString, QString> fieldName_fieldId;
    _selectTemplateSheet(doc);
    auto version = _getDocumentVersion(doc);
    int rowFieldId = _getRowFieldId(version);
    int rowFieldName = rowFieldId - 1;
    auto dimTemplate = doc.dimension();
    for (int i = 0; i < dimTemplate.lastColumn(); ++i)
    {
        auto cellFieldName = doc.cellAt(rowFieldName + 1, i + 1);
        auto cellFieldId = doc.cellAt(rowFieldId + 1, i + 1);
        if (cellFieldId && cellFieldName)
        {
            QString fieldId{cellFieldId->value().toString()};
            _formatFieldId(fieldId);
            QString fieldName{cellFieldName->value().toString()};
            if (!fieldName.isEmpty() && !fieldId.isEmpty())
            {
                fieldName_fieldId[fieldName] = fieldId;
            }
        }
    }
    _selectValidValuesSheet(doc);
    const auto &dimValidValues = doc.dimension();
    for (int i=1; i<dimValidValues.lastRow(); ++i)
    {
        auto cellFieldName = doc.cellAt(i+1, 2);
        if (cellFieldName)
        {
            QString fieldName{cellFieldName->value().toString()};
            if (fieldName.contains(" - ["))
            {
                fieldName = fieldName.split(" - [")[0];
            }
            if (!fieldName.isEmpty())
            {
                Q_ASSERT(fieldName_fieldId.contains(fieldName));
                if (fieldName_fieldId.contains(fieldName))
                {
                    const auto &fieldId = fieldName_fieldId[fieldName];
                    for (int j=2; i<dimValidValues.lastColumn(); ++j)
                    {
                        auto cellValue = doc.cellAt(i+1, j+1);
                        QString value;
                        if (cellValue)
                        {
                            value = cellValue->value().toString();
                            if (!value.isEmpty())
                            {
                                fieldId_possibleValues[fieldId] << value;
                            }
                        }
                        if (value.isEmpty())
                        {
                            break;
                        }
                    }
                }
            }
        }
    }
    return fieldId_possibleValues;
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

QHash<QString, QHash<QString, QSet<QString>>> TemplateFiller::_get_parentSku_variation_skus(QXlsx::Document &doc) const
{
    QHash<QString, QHash<QString, QSet<QString>>> parentSku_variation_skus;
    const auto &fieldId_index = _get_fieldId_index(doc);
    int indColSku = _getIndColSku(fieldId_index);
    int indColSkuParent = _getIndColSkuParent(fieldId_index);
    int indColColor = _getIndColColorName(fieldId_index);

    const auto &dim = doc.dimension();
    int lastRow = dim.lastRow();
    auto version = _getDocumentVersion(doc);
    int row = _getRowFieldId(version) + 1;

    for (int i=row; i<lastRow; ++i)
    {
        const auto &sku = _get_cellVal(doc, i, indColSku);
        if (sku.isEmpty())
        {
            break;
        }
        if (sku.startsWith("ABC"))
        {
            continue;
        }

        const auto &skuParent = _get_cellVal(doc, i, indColSkuParent);
        if (skuParent.isEmpty())
        {
            continue;
        }

        const auto &color = _get_cellVal(doc, i, indColColor);

        parentSku_variation_skus[skuParent][color].insert(sku);
    }
    return parentSku_variation_skus;
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

QString TemplateFiller::_get_langCode(const QString &templateFilePath) const
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


QCoro::Task<void> TemplateFiller::_readAgeGender()
{
    QXlsx::Document doc(m_templateFromPath);
    _selectTemplateSheet(doc);
    const auto &fieldId_index = _get_fieldId_index(doc);
    int indColAge = _getIndColAge(fieldId_index);
    int indColGender = _getIndColGender(fieldId_index);

    QSharedPointer<Attribute> ageAttribute;
    QSharedPointer<Attribute> genderAttribute;
    
    const QSet<QString> ageFieldIds {"target_audience_keyword", "target_audience_base", "age_range_description"};

    const QSet<QString> genderFieldIds{"target_gender", "department_name"};
    
    const auto &marketplace = _get_marketplaceFrom();

    // Find attributes
    QString ageFieldIdFound;
    QString genderFieldIdFound;
    
    for (const auto &id : ageFieldIds)
    {
        bool found = false;
        for (auto it = fieldId_index.begin(); it != fieldId_index.end(); ++it)
        {
            if (it.key().startsWith(id))
            {
                QString realId = it.key();
                // Find attribute in our map
                if (m_marketplace_attributeId_attributeInfos.contains(marketplace) && 
                    m_marketplace_attributeId_attributeInfos[marketplace].contains(realId))
                {
                    ageAttribute = m_marketplace_attributeId_attributeInfos[marketplace][realId];
                    ageFieldIdFound = realId;
                    found = true;
                    break;
                }
            }
        }
        if (found) break;
    }
    
    for (const auto &id : genderFieldIds)
    {
        bool found = false;
        for (auto it = fieldId_index.begin(); it != fieldId_index.end(); ++it)
        {
            if (it.key().startsWith(id))
            {
                QString realId = it.key();
                if (m_marketplace_attributeId_attributeInfos.contains(marketplace) && 
                    m_marketplace_attributeId_attributeInfos[marketplace].contains(realId))
                {
                    genderAttribute = m_marketplace_attributeId_attributeInfos[marketplace][realId];
                    genderFieldIdFound = realId;
                    found = true;
                    break;
                }
            }
        }
        if (found) break;
    }

    auto version = _getDocumentVersion(doc);
    int rowData = _getRowFieldId(version) + 1;


    // Check Age - 3 conditions
    if (ageAttribute)
    {
        const QString &age = _get_cellVal(doc, rowData + 2, indColAge); // +2 in case exemple row and Parent in first row
        if (m_attributeEquivalentTable->getEquivalentAgeAdult().isEmpty())
        {
            co_await m_attributeEquivalentTable->askAiEquivalentValues(
                 ageFieldIdFound, "Adult", ageAttribute.data());
        }
        if (m_attributeEquivalentTable->getEquivalentAgeKid().isEmpty())
        {
            co_await m_attributeEquivalentTable->askAiEquivalentValues(
                 ageFieldIdFound, "Little Kid", ageAttribute.data());
        }
        if (m_attributeEquivalentTable->getEquivalentAgeBaby().isEmpty())
        {
             co_await m_attributeEquivalentTable->askAiEquivalentValues(
                 ageFieldIdFound, "Infant", ageAttribute.data());
        }
        if (m_attributeEquivalentTable->getEquivalentAgeAdult().contains(age))
        {
            m_age = AbstractFiller::Adult;
        }
        else if (m_attributeEquivalentTable->getEquivalentAgeKid().contains(age))
        {
            m_age = AbstractFiller::Kid;
        }
        else if (m_attributeEquivalentTable->getEquivalentAgeBaby().contains(age))
        {
            m_age = AbstractFiller::Baby;
        }
    }

    // Check Gender - 3 conditions
    if (genderAttribute)
    {
        const QString &gender = _get_cellVal(doc, rowData + 2, indColGender); // +2 in case exemple row and Parent in first row
        if (m_attributeEquivalentTable->getEquivalentGenderMen().isEmpty()) {
             co_await m_attributeEquivalentTable->askAiEquivalentValues(
                 genderFieldIdFound, "Male", genderAttribute.data());
        }
        if (m_attributeEquivalentTable->getEquivalentGenderWomen().isEmpty()) {
             co_await m_attributeEquivalentTable->askAiEquivalentValues(
                 genderFieldIdFound, "Female", genderAttribute.data());
        }
        if (m_attributeEquivalentTable->getEquivalentGenderUnisex().isEmpty()) {
             co_await m_attributeEquivalentTable->askAiEquivalentValues(
                 genderFieldIdFound, "Unisex", genderAttribute.data());
        }
        if (m_attributeEquivalentTable->getEquivalentGenderMen().contains(gender))
        {
            m_gender = AbstractFiller::Male;
        }
        else if (m_attributeEquivalentTable->getEquivalentGenderWomen().contains(gender))
        {
            m_gender = AbstractFiller::Female;
        }
        else if (m_attributeEquivalentTable->getEquivalentGenderUnisex().contains(gender))
        {
            m_gender = AbstractFiller::Unisex;
        }
    }
    co_return;
}
