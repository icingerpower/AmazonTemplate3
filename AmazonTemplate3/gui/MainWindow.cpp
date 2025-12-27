#include <QFileDialog>
#include <QMessageBox>
#include <QProgressDialog>

#include <TemplateFiller.h>
#include <TemplateExceptions.h>
#include <FileModelToFill.h>
#include <FileModelSources.h>

#include "../../common/workingdirectory/WorkingDirectoryManager.h"
#include "OpenAi2.h"

#include "DialogExtractInfos.h"
#include "DialogValidateMandatory.h"
#include "MainWindow.h"
#include <QCoro/QCoroCore>
#include <ExceptionOpenAiNotInitialized.h>
#include "./ui_MainWindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    m_templateFiller = nullptr;
    ui->setupUi(this);
    _setGenerateButtonsEnabled(false);
    m_settingsKeyExtraInfos = "MainWindowExtraInfos";
    m_settingsKeyApi = "MainWindowKey";
    _setControlButtonsEnabled(false);
    auto settings = WorkingDirectoryManager::instance()->settings();
    if (settings->contains(m_settingsKeyApi))
    {
        auto key = settings->value(m_settingsKeyApi).toString();
        if (!key.isEmpty())
        {
            ui->lineEditOpenAiKey->setText(key);
            OpenAi2::instance()->init(key);
        }
    }
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
    connect(ui->buttonBasicControls,
            &QPushButton::clicked,
            this,
            &MainWindow::baseControls);
    connect(ui->buttonFindMandatoryFieldIds,
            &QPushButton::clicked,
            this,
            &MainWindow::findValidateMandatoryFieldIds);
    connect(ui->buttonExtractProductInfos,
            &QPushButton::clicked,
            this,
            &MainWindow::extractProductInfos);
    connect(ui->lineEditOpenAiKey,
            &QLineEdit::textChanged,
            this,
            &MainWindow::onApiKeyChanged);
}

void MainWindow::onApiKeyChanged(const QString &key)
{
    auto settings = WorkingDirectoryManager::instance()->settings();
    if (!key.isEmpty())
    {
        settings->setValue(m_settingsKeyApi, key);
        OpenAi2::instance()->init(key);
    }
    else if (settings->contains(m_settingsKeyApi))
    {
        settings->remove(m_settingsKeyApi);
    }
    _enableGenerateButtonIfValid();
}

void MainWindow::_clearTemplateFiller()
{
    if (m_templateFiller != nullptr)
    {
        delete m_templateFiller;
        m_templateFiller = nullptr;
    }
}

void MainWindow::_setControlButtonsEnabled(bool enable)
{
    ui->buttonBasicControls->setEnabled(enable);
    ui->buttonFindMandatoryFieldIds->setEnabled(enable);
    ui->buttonExtractProductInfos->setEnabled(enable);
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
        _setControlButtonsEnabled(true);
    }
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
    ui->buttonGenAiDesc->setEnabled(enable);
    ui->buttonReviewAiDesc->setEnabled(enable);
    ui->buttonDisplayPossibleValues->setEnabled(enable);
    ui->buttonRunPromptsManually->setEnabled(enable);
}

void MainWindow::baseControls()
{
    try {
        m_templateFiller->checkParentSkus();
        m_templateFiller->checkPreviewImages();
        m_templateFiller->checkKeywords();
        QMessageBox::warning(
                    this,
                    tr("Controls done"),
                    tr("Controls successfully done"));
    }
    catch (const TemplateExceptions &exception)
    {
        QMessageBox::warning(
                    this,
                    exception.title(),
                    exception.error());
    }
}

void MainWindow::findValidateMandatoryFieldIds()
{
    try
    {
        const auto &previousFilePath = m_templateFiller->findPreviousTemplatePath();

        auto progress = new QProgressDialog(
                    tr("Loading mandatory attributes…"),
                    QString{}, 0, 0, this);
        progress->setWindowModality(Qt::ApplicationModal);
        progress->setCancelButton(nullptr);
        progress->setMinimumDuration(0);
        progress->setAutoClose(false);
        progress->setAutoReset(false);
        progress->setValue(0);
        progress->show();

        QPointer<QProgressDialog> progressGuard{progress};

        QCoro::connect(m_templateFiller->findAttributesMandatoryToValidateManually(previousFilePath),
                       this, [this, progressGuard](TemplateFiller::AttributesToValidate attrToValidate)
        {
            if (progressGuard)
            {
                progressGuard->close();
                progressGuard->deleteLater();
            }

            DialogValidateMandatory dialog{attrToValidate};
            auto ret = dialog.exec();
            if (ret == QDialog::Accepted)
            {
                m_templateFiller->validateMandatory(dialog.getAttributeValidatedMandatory(),
                                                    dialog.getAttributeValidatedNotMandatory());
            }
        });
    }
    catch (const ExceptionOpenAiNotInitialized &exception)
    {
        QMessageBox::warning(
                    this,
                    exception.title(),
                    exception.error());
    }
}

void MainWindow::extractProductInfos()
{
    DialogExtractInfos dialog{m_workingDir.path()};
    dialog.exec();
}

