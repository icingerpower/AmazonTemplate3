#ifndef FILEMODELTOFILL_H
#define FILEMODELTOFILL_H

#include <QFileSystemModel>

class FileModelToFill : public QFileSystemModel
{
public:
    FileModelToFill(const QString &dirPath, QObject *parent = nullptr);
    QList<QFileInfo> getFileInfos() const;
    QStringList getFilePaths() const;
};

#endif // FILEMODELTOFILL_H
