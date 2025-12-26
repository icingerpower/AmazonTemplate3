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

public slots:
    void browseSourceMain();
    void baseControls();
    void extractProductInfos();

private:
    Ui::MainWindow *ui;
    void _connectSlots();
    TemplateFiller *m_templateFiller;
    QDir m_workingDir;
    QString m_settingsFilePath;
    QString m_settingsKeyExtraInfos;
    QString m_settingsKeyApi;
    void _clearTemplateFiller();
    void _setGenerateButtonsEnabled(bool enable);
    void _enableGenerateButtonIfValid();
};

#endif // MAINWINDOW_H
