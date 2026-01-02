#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QDir>
#include <QSettings>
#include <QSharedPointer>

class TemplateFiller;

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    QSharedPointer<QSettings> settingsFolder() const;
    QMap<QString, QString> get_skuPattern_customInstructions() const;

public slots:
    void browseSourceMain();
    void baseControls();
    bool baseControlsWithoutPopup();
    void findValidateMandatoryFieldIds();
    void viewFormatCustomInstructions();
    void viewAttributes();
    void extractProductInfos();
    void onApiKeyChanged(const QString &key);
    void generate();

private:
    Ui::MainWindow *ui;
    void _connectSlots();
    TemplateFiller *m_templateFiller;
    QDir m_workingDir;
    QString m_settingsFilePath;
    QString m_settingsKeyExtraInfos;
    QString m_settingsKeyApi;
    void _clearTemplateFiller();
    void _setControlButtonsEnabled(bool enable);
    void _setGenerateButtonsEnabled(bool enable);
    void _enableGenerateButtonIfValid();
};

#endif // MAINWINDOW_H
