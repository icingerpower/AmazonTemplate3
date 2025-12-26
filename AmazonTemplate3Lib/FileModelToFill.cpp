#include "FileModelToFill.h"

FileModelToFill::FileModelToFill(const QString &dirPath, QObject *parent)
    : QFileSystemModel(parent)
{
    setRootPath(dirPath);
    setNameFilters(QStringList{"*TOFILL*.xlsm", "*TOFILL.xlsm", "*TOFILL*.xlsx", "*TOFILL.xlsx"});
    setNameFilterDisables(false);
}

QList<QFileInfo> FileModelToFill::getFileInfos() const
{
    return rootDirectory().entryInfoList(nameFilters(), QDir::Files);
}

QStringList FileModelToFill::getFilePaths() const
{
    QStringList filePaths;
    const auto &fileInfos = getFileInfos();
    for (const auto &fileInfo : fileInfos)
    {
        filePaths << fileInfo.absoluteFilePath();
    }
    return filePaths;
}

