#include "ClassificationTypeMap.h"
#include <QSettings>
#include <QDir>
#include <algorithm>

void ClassificationTypeMap::load(const QString &workingDir)
{
    m_filePath = QDir(workingDir).filePath(QStringLiteral("classification_types.ini"));
    m_map.clear();
    QSettings s(m_filePath, QSettings::IniFormat);
    s.beginGroup(QStringLiteral("types"));
    const QStringList keys = s.childKeys();
    for (const QString &key : keys)
        m_map.insert(key, s.value(key).toString());
    s.endGroup();
}

void ClassificationTypeMap::save() const
{
    if (m_filePath.isEmpty()) return;
    QSettings s(m_filePath, QSettings::IniFormat);
    s.beginGroup(QStringLiteral("types"));
    s.remove(QString{}); // clear existing keys in group
    for (auto it = m_map.constBegin(); it != m_map.constEnd(); ++it)
        s.setValue(it.key(), it.value());
    s.endGroup();
    s.sync();
}

QString ClassificationTypeMap::productType(const QString &classificationId) const
{
    return m_map.value(classificationId);
}

void ClassificationTypeMap::setProductType(const QString &classificationId, const QString &productType)
{
    if (!classificationId.isEmpty() && !productType.isEmpty())
        m_map.insert(classificationId, productType);
}

QStringList ClassificationTypeMap::knownProductTypes() const
{
    QStringList types;
    for (const QString &v : m_map)
        if (!types.contains(v)) types.append(v);
    std::sort(types.begin(), types.end());
    return types;
}
