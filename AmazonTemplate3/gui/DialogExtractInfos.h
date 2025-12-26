#ifndef DIALOGEXTRACTINFOS_H
#define DIALOGEXTRACTINFOS_H

#include <QDialog>

namespace Ui {
class DialogExtractInfos;
}

class TableInfoExtractor;

class DialogExtractInfos : public QDialog
{
    Q_OBJECT

public:
    explicit DialogExtractInfos(const QString &workingDir, QWidget *parent = nullptr);
    ~DialogExtractInfos();

    TableInfoExtractor *getTableInfoExtractor() const;

public slots:
    void browseGtinTemplateFile();
    void fillGtinTemplateFile();
    void copyColumn();
    void pasteSkus();
    void pasteTitles();
    void readGtinCodes();
    void generateImageNames();
    void checkImageFileNames();

private:
    Ui::DialogExtractInfos *ui;
    QString m_workingDir;
    void _connectSlots();
};

#endif // DIALOGEXTRACTINFOS_H
