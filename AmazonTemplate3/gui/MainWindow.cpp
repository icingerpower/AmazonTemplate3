#include <QFileDialog>
#include <QMessageBox>
#include <QProgressDialog>

#include <TemplateFiller.h>
#include <ExceptionTemplate.h>
#include <AttributesMandatoryAiTable.h>
#include <FileModelToFill.h>
#include <FileModelSources.h>

#include "../../common/workingdirectory/WorkingDirectoryManager.h"
#include "OpenAi2.h"

#include "DialogExtractInfos.h"
#include "DialogAttributes.h"
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
    ui->progressBar->hide();
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
            OpenAi2::instance()->setMaxQueriesSameTime(10);
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
    connect(ui->buttonViewAttributes,
            &QPushButton::clicked,
            this,
            &MainWindow::viewAttributes);
    connect(ui->lineEditOpenAiKey,
            &QLineEdit::textChanged,
            this,
            &MainWindow::onApiKeyChanged);
    connect(ui->buttonGenerate,
            &QPushButton::clicked,
            this,
            &MainWindow::generate);
    connect(ui->buttonViewFormatExtraInfos,
            &QPushButton::clicked,
            this,
            &MainWindow::viewFormatCustomInstructions);
    connect(ui->textEditExtraInfos,
            &QTextEdit::textChanged,
            this,
            [this](){
        if (!ui->lineEditTo->text().isEmpty())
        {
            static QDateTime nextDateTime = QDateTime::currentDateTime().addSecs(-1);
            const QDateTime &currentDateTime = QDateTime::currentDateTime();
            if (nextDateTime.secsTo(currentDateTime) > 0)
            {
                int nSecs = 3;
                nextDateTime = currentDateTime.addSecs(nSecs);
                QTimer::singleShot(nSecs * 1000 + 20, this, [this]{
                    auto settingsDir = settingsFolder();
                    settingsDir->setValue(m_settingsKeyExtraInfos,
                                          ui->textEditExtraInfos->toPlainText());
                });
            }
        }
    });
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

void MainWindow::generate()
{
    if (baseControlsWithoutPopup())
    {

        auto progress = new QProgressDialog(
                    tr("Filling template..."),
                    QString{}, 0, 0, this);
        progress->setWindowModality(Qt::ApplicationModal);
        progress->setCancelButton(nullptr);
        progress->setMinimumDuration(0);
        progress->setAutoClose(false);
        progress->setAutoReset(false);
        progress->setValue(0);
        progress->show();

        QPointer<QProgressDialog> progressGuard{progress};

        qDebug() << "Filling templates...";
        QCoro::connect(m_templateFiller->fillValues(),
                       this, [this, progressGuard]()
        {
            if (progressGuard)
            {
                progressGuard->close();
                progressGuard->deleteLater();
            }

            QMessageBox::information(
                        this,
                        tr("Generation done"),
                        tr("Template filled successfully"));
        });
    }
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
    ui->buttonViewAttributes->setEnabled(enable);
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
        try {
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
            m_templateFiller = new TemplateFiller{
                    WorkingDirectoryManager::instance()->workingDir().path()
                    , filePath
                    , fileModelToFill->getFilePaths()
                    , fileModelSources->getFilePaths()
                    , get_skuPattern_customInstructions()
            };
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
        catch (const ExceptionTemplate &exception)
        {
            QMessageBox::warning(
                        this,
                        exception.title(),
                        exception.error());
        }
    }
}

QSharedPointer<QSettings> MainWindow::settingsFolder() const
{
    return QSharedPointer<QSettings>{new QSettings{m_settingsFilePath, QSettings::IniFormat}};
}

QMap<QString, QString> MainWindow::get_skuPattern_customInstructions() const
{
    QMap<QString, QString> skuPattern_customInstructions;
    auto customInstructions = ui->textEditExtraInfos->toPlainText().trimmed();
    if (!customInstructions.isEmpty())
    {
        const QStringList &lines = customInstructions.split("\n");
        skuPattern_customInstructions[QString{}] = lines[0].trimmed();
        if (!skuPattern_customInstructions[QString{}].endsWith("."))
        {
            skuPattern_customInstructions[QString{}] += ".";
        }
        QStringList lastSkus;
        QStringList lastInstructions;
        for (int i=1; i<lines.size(); ++i)
        {
            const auto &line = lines[i];
            if (line.startsWith("["))
            {
                if (!lastSkus.isEmpty() && !lastInstructions.isEmpty())
                {
                    for (const auto &sku : lastSkus)
                    {
                        skuPattern_customInstructions[sku] = lastInstructions.join(" ");
                    }
                }
                lastSkus.clear();
                lastInstructions.clear();
                const auto &lineSkus = lines[i].mid(1, line.size()-2);
                const auto &skus = lineSkus.split(",");
                for (const auto &sku : skus)
                {
                    lastSkus << sku.trimmed();
                }
            }
            else if (!lastSkus.isEmpty())
            {
                lastInstructions << line.trimmed();
                if (!lastInstructions.last().endsWith("."))
                {
                    lastInstructions.last() += ".";
                }
            }
        }
        if (!lastSkus.isEmpty() && !lastInstructions.isEmpty())
        {
            for (const auto &sku : lastSkus)
            {
                skuPattern_customInstructions[sku] = lastInstructions.join(" ");
            }
        }
    }
    return skuPattern_customInstructions;
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
    if (baseControlsWithoutPopup())
    {
        QMessageBox::information(
                    this,
                    tr("Controls done"),
                    tr("Controls successfully done"));
    }
}

bool MainWindow::baseControlsWithoutPopup()
{
    try
    {
        qDebug() << "m_templateFiller->checkParentSkus()...";
        m_templateFiller->checkParentSkus();
        qDebug() << "m_templateFiller->checkPossibleValues()...";
        m_templateFiller->checkPossibleValues();
        qDebug() << "m_templateFiller->checkColumnsFilled()...";
        m_templateFiller->checkColumnsFilled();
        //qDebug() << "m_templateFiller->buildAttributes()...";
        //m_templateFiller->buildAttributes();
        qDebug() << "m_templateFiller->checkPreviewImages()...";
        m_templateFiller->checkPreviewImages();
        qDebug() << "m_templateFiller->checkKeywords()...";
        m_templateFiller->checkKeywords();
        qDebug() << "m_templateFiller->checks...DONE SUCCESSFULLY";
        return true;
    }
    catch (const ExceptionTemplate &exception)
    {
        QMessageBox::warning(
                    this,
                    exception.title(),
                    exception.error());
    }
    return false;
}

void MainWindow::findValidateMandatoryFieldIds()
{
    try
    {
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

        QCoro::connect(m_templateFiller->findAttributesMandatoryToValidateManually(),
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
                m_templateFiller->mandatoryAttributesAiTable()->save();
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

void MainWindow::viewFormatCustomInstructions()
{
    QMessageBox::information(
        this,
        tr("Format"),
        tr("Custom instructions for every products\n"
           "[SKUSTART1,SKUSTART2]\nCustom instructions for products that include the skus\n"
           "[SKUSTART3,SKUSTART4]\nCustom instructions for products that include the skus"));
}

void MainWindow::viewAttributes()
{
    DialogAttributes dialog{m_templateFiller};
    dialog.exec();
}

void MainWindow::extractProductInfos()
{
    DialogExtractInfos dialog{m_workingDir.path()};
    dialog.exec();
}

