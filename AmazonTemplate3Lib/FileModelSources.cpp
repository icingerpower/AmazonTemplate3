#include "FileModelSources.h"

FileModelSources::FileModelSources(const QString &dirPath, QObject *parent)
    : QFileSystemModel(parent)
{
    setRootPath(dirPath);
    setNameFilters(QStringList{"*SOURCE*.xlsm", "*SOURCE.xlsm", "*SOURCE*.xlsx", "*SOURCE.xlsx"});
    setNameFilterDisables(false);
}

QList<QFileInfo> FileModelSources::getFileInfos() const
{
    return rootDirectory().entryInfoList(nameFilters(), QDir::Files);
}

QStringList FileModelSources::getFilePaths() const
{
    QStringList filePaths;
    const auto &fileInfos = getFileInfos();
    for (const auto &fileInfo : fileInfos)
    {
        filePaths << fileInfo.absoluteFilePath();
    }
    return filePaths;
}


