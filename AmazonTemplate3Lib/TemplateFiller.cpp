#include <QFileInfo>
#include <QRegularExpression>
#include <QDirIterator>

#include "AttributesMandatoryAiTable.h"
#include "AttributesMandatoryTable.h"

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
        , const QStringList &templateToPaths)
{
    m_mandatoryAttributesTable = nullptr;
    m_mandatoryAttributesAiTable = nullptr;
    m_attributeEquivalentTable = nullptr;
    m_attributeFlagsTable = nullptr;
    m_attributePossibleMissingTable = nullptr;
    m_attributeValueReplacedTable = nullptr;
    setTemplates(workingDirCommon, templateFromPath, templateToPaths);
}

TemplateFiller::~TemplateFiller()
{
    delete m_mandatoryAttributesTable;
    delete m_mandatoryAttributesAiTable;
}

void TemplateFiller::setTemplates(
        const QString &commonSettingsDir
        , const QString &templateFromPath
        , const QStringList &templateToPaths)
{
    const auto &productType = _get_productType(templateFromPath); // TODO Exception empty file + ma
    if (productType.isEmpty())
    {
        ExceptionTemplate exception;
        exception.setInfos(QObject::tr("Empty template")
                           , QObject::tr("The template is not filled as no product type could be found."));
        exception.raise();
    }
    m_templateFromPath = templateFromPath;
    m_templateToPaths = templateToPaths;
    m_countryCodeFrom = _getCountryCode(templateFromPath);
    m_langCodeFrom = _getLangCode(templateFromPath);
    m_workingDirCommon = commonSettingsDir;
    m_workingDir = QFileInfo{m_templateFromPath}.dir();
    m_workingDirImage = m_workingDir.absoluteFilePath("images");
    _clearAttributeManagers();
    m_mandatoryAttributesAiTable = new AttributesMandatoryAiTable;
    QXlsx::Document doc(m_templateFromPath);
    const auto &fieldId_index = _get_fieldId_index(doc);
    const auto &fieldIdMandatory = _get_fieldIdMandatoryAll();
        // 2. Resolve "Previous" mandatory fields
    const auto &previousFieldIdMandatory = _get_fieldIdMandatoryPrevious();

    m_attributePossibleMissingTable = new AttributePossibleMissingTable{commonSettingsDir};
    m_attributeValueReplacedTable = new AttributeValueReplacedTable{commonSettingsDir};
    m_mandatoryAttributesTable = new AttributesMandatoryTable{
            commonSettingsDir, productType, fieldIdMandatory, previousFieldIdMandatory, fieldId_index};
    m_attributeEquivalentTable = new AttributeEquivalentTable{commonSettingsDir};
    m_attributeFlagsTable = new AttributeFlagsTable{commonSettingsDir};
    m_marketplaceFrom = _get_marketplaceFrom();
    m_attributeFlagsTable->recordAttributeNotRecordedYet(m_marketplaceFrom, fieldIdMandatory);
    m_attributeFlagsTable->recordAttributeNotRecordedYet(m_marketplaceFrom, m_mandatoryAttributesTable->getMandatoryIds());
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
                const auto &countryCodeTo = _getCountryCode(templateToPath);
                const auto &langCodeTo = _getLangCode(templateToPath);
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
            Q_ASSERT(!skuImageBaseName.startsWith("P-"));
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
        ExceptionTemplate exception;
        exception.setInfos(
                    QObject::tr("Preview images missing"),
                    QObject::tr("The following AI preview images are missing in the folder images") + ":\n" + missingImageFileNames.join("\n"));
        exception.raise();
    }
}

QHash<QString, QHash<QString, QHash<QString, QHash<QString, QSet<QString>>>>>
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

    QHash<QString, QHash<QString, QHash<QString, QHash<QString, QSet<QString>>>>>
            marketplace_countryCode_langCode_fieldId_possibleValues;
    allFieldIds.intersect(m_mandatoryAttributesTable->getMandatoryIds());
    bool addedMissing = false;
    for (const auto &filePath : filePaths)
    {
        QXlsx::Document doc(filePath);
        const auto &countryCode = _getCountryCode(filePath);
        const auto &langCode = _getLangCode(filePath);
        const auto &marketplace = _get_marketplace(doc);
        const auto &productType = _get_productType(filePath);
        const auto &fieldId_possibleValues = _get_fieldId_possibleValues(doc);
        for (auto it = fieldId_possibleValues.begin();
             it != fieldId_possibleValues.end(); ++it)
        {
            const auto &fieldId = it.key();
            auto possibleValues = it.value();
            m_attributeValueReplacedTable->replaceIfContains(
                            marketplace, countryCode, langCode, fieldId, possibleValues);
            marketplace_countryCode_langCode_fieldId_possibleValues
                    [marketplace][countryCode][langCode][fieldId] = possibleValues;
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
                m_attributeValueReplacedTable->replaceIfContains(
                            marketplace, countryCode, langCode, missingFieldId, possibleValues);
                marketplace_countryCode_langCode_fieldId_possibleValues
                        [marketplace][countryCode][langCode][missingFieldId]
                        = possibleValues;
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
    return marketplace_countryCode_langCode_fieldId_possibleValues;
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
        infos.productType = _get_productType(templatePath);
        QXlsx::Document doc{templatePath};
        infos.marketplace = _get_marketplace(doc);
        infos.countryCode = _getCountryCode(templatePath);
        infos.langCode = _getLangCode(templatePath);
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
            marketplaceId_attributeId_attributeInfos[it.key()][it.value()] = attribute;
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
                            [infos.marketplace][infos.countryCode][infos.langCode][mandatoryId]
                        );
            }
        }
    }
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

QString TemplateFiller::_get_productType(const QString &filePath) const
{
    QXlsx::Document doc(filePath);
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

QSharedPointer<QSettings> TemplateFiller::settingsWorkingDir() const
{
    const auto &settingsPath = m_workingDir.absoluteFilePath("settings.ini");
    return QSharedPointer<QSettings>::create(settingsPath, QSettings::IniFormat);
}

QCoro::Task<TemplateFiller::AttributesToValidate> TemplateFiller::findAttributesMandatoryToValidateManually() const
{
    TemplateFiller::AttributesToValidate attributesToValidateManually;
    const auto &productType = _get_productType(m_templateFromPath);
    Q_ASSERT(!productType.isEmpty());
    QXlsx::Document doc{m_templateFromPath};
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
    auto settings = settingsWorkingDir();
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
