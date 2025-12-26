#ifndef FILEMODELSOURCES_H
#define FILEMODELSOURCES_H

#include <QFileSystemModel>
#include <QObject>

class FileModelSources : public QFileSystemModel
{
public:
    FileModelSources(const QString &dirPath, QObject *parent = nullptr);
    QList<QFileInfo> getFileInfos() const;
    QStringList getFilePaths() const;
};

#endif // FILEMODELSOURCES_H
