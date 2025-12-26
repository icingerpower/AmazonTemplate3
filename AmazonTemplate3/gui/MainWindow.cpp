#include <QFileDialog>

#include <TemplateFiller.h>
#include <FileModelToFill.h>
#include <FileModelSources.h>
#include <TemplateFiller.h>

#include "MainWindow.h"
#include "./ui_MainWindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    m_templateFiller = nullptr;
    ui->setupUi(this);
    QString m_settingsKeyExtraInfos;
    QString m_settingsKeyApi;
    _connectSlots();
}

MainWindow::~MainWindow()
{
    delete ui;
    _clearTemplateFiller();
}

void MainWindow::_connectSlots()
{
    connect(ui->buttonBrowseSource,
            &QPushButton::clicked,
            this,
            &MainWindow::browseSourceMain);
}

void MainWindow::_clearTemplateFiller()
{
    if (m_templateFiller != nullptr)
    {
        delete m_templateFiller;
    }
}

void MainWindow::browseSourceMain()
{
    QSettings settings;
    const QString key{"MainWindow__browseSourceMain"};
    QDir lastDir{settings.value(key, QDir{}.path()).toString()};
    const QString &filePath = QFileDialog::getOpenFileName(
        this,
        tr("Template file pre-filled"),
        lastDir.path(),
        QString{"Xlsx (*TOFILL*.xlsx *TOFILL*.XLSX *TOFILL*.xlsm *TOFILL*.XLSM)"},
        nullptr,
        QFileDialog::DontUseNativeDialog);
    if (!filePath.isEmpty())
    {
        _clearTemplateFiller();
        m_workingDir = QFileInfo{filePath}.dir();
        m_settingsFilePath = m_workingDir.absoluteFilePath("settings.ini");
        auto settingsDir = settingsFolder();
        if (settingsDir->contains(m_settingsKeyExtraInfos))
        {
            ui->textEditExtraInfos->setText(
                        settingsDir->value(m_settingsKeyExtraInfos).toString());
        }
        else
        {
            ui->textEditExtraInfos->clear();
        }
        ui->buttonExtractProductInfos->setEnabled(true);
        const auto &workingDirPath = m_workingDir.path();
        settings.setValue(key, workingDirPath);
        ui->lineEditTo->setText(filePath);
        _enableGenerateButtonIfValid();
        auto *curModelSource = ui->treeViewSources->model();
        auto *fileModelSources
            = new FileModelSources{workingDirPath, ui->treeViewSources};
        ui->treeViewSources->setModel(fileModelSources);
        ui->treeViewSources->setRootIndex(
            fileModelSources->index(workingDirPath));
        ui->treeViewSources->header()->resizeSection(0, 300);
        auto *curModelToFill = ui->treeViewToFill->model();
        auto *fileModelToFill
            = new FileModelToFill{workingDirPath, ui->treeViewToFill};
        m_templateFiller = new TemplateFiller{filePath, fileModelToFill->getFilePaths()};
        ui->treeViewToFill->setModel(fileModelToFill);
        ui->treeViewToFill->setRootIndex(fileModelToFill->index(workingDirPath));
        ui->treeViewToFill->header()->resizeSection(0, 300);
        if (curModelToFill != nullptr)
        {
            curModelToFill->deleteLater();
            curModelSource->deleteLater();
        }
    }
    m_templateFiller = nullptr;
}

QSharedPointer<QSettings> MainWindow::settingsFolder() const
{
    return QSharedPointer<QSettings>{new QSettings{m_settingsFilePath, QSettings::IniFormat}};
}

void MainWindow::_enableGenerateButtonIfValid()
{
    if (!ui->lineEditOpenAiKey->text().isEmpty()
        && !ui->lineEditTo->text().isEmpty())
    {
        _setGenerateButtonsEnabled(true);
    }
    else
    {
        _setGenerateButtonsEnabled(false);
    }
}

void MainWindow::_setGenerateButtonsEnabled(bool enable)
{
    ui->buttonGenerate->setEnabled(enable);
    ui->buttonBasicControls->setEnabled(enable);
    ui->buttonGenAiDesc->setEnabled(enable);
    ui->buttonReviewAiDesc->setEnabled(enable);
    ui->buttonDisplayPossibleValues->setEnabled(enable);
    ui->buttonRunPromptsManually->setEnabled(enable);
}

void MainWindow::baseControls()
{

}

void MainWindow::extractProductInfos()
{

}

