#include <QFileInfo>

#include "TemplateFiller.h"

void TemplateFiller::setTemplates(
        const QString &templateFromPath, const QStringList &templateToPaths)
{
    m_templateFromPath = templateFromPath;
    m_templateToPaths = templateToPaths;
    m_countryCodeFrom = _getCountryCode(templateFromPath);
    m_langCodeFrom = _getLangCode(templateFromPath);
}

TemplateFiller::TemplateFiller()
{

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
