#include <QFileDialog>
#include <QMessageBox>
#include <QProgressDialog>
#include <QTableView>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <typeinfo>
#include <cxxabi.h>

#include <TemplateFiller.h>
#include <ExceptionTemplate.h>
#include <AttributesMandatoryAiTable.h>
#include <FileModelToFill.h>
#include <FileModelSources.h>
#include <AiFailureTable.h>

#include "../../common/workingdirectory/WorkingDirectoryManager.h"
#include "SettingsTable.h"

#include "../DialogExtractInfos.h"
#include "../DialogAttributes.h"
#include "../DialogValidateMandatory.h"
#include "PaneGenTemplate.h"
#include <QCoro/QCoroCore>
#include <ExceptionOpenAiNotInitialized.h>
#include <ExceptionOpenAiError.h>
#include "ui_PaneGenTemplate.h"

PaneGenTemplate::PaneGenTemplate(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PaneGenTemplate)
{
    m_templateFiller = nullptr;
    ui->setupUi(this);
    ui->progressBar->hide();
    _setGenerateButtonsEnabled(false);
    m_settingsKeyExtraInfos = "MainWindowExtraInfos";
    _setControlButtonsEnabled(false);
    _connectSlots();
}

PaneGenTemplate::~PaneGenTemplate()
{
    delete ui;
    _clearTemplateFiller();
}

void PaneGenTemplate::_connectSlots()
{
    connect(ui->buttonBrowseSource,
            &QPushButton::clicked,
            this,
            &PaneGenTemplate::browseSourceMain);
    connect(ui->buttonBasicControls,
            &QPushButton::clicked,
            this,
            &PaneGenTemplate::baseControls);
    connect(ui->buttonFindMandatoryFieldIds,
            &QPushButton::clicked,
            this,
            &PaneGenTemplate::findValidateMandatoryFieldIds);
    connect(ui->buttonExtractProductInfos,
            &QPushButton::clicked,
            this,
            &PaneGenTemplate::extractProductInfos);
    connect(ui->buttonViewAttributes,
            &QPushButton::clicked,
            this,
            &PaneGenTemplate::viewAttributes);
    connect(ui->buttonGenerate,
            &QPushButton::clicked,
            this,
            &PaneGenTemplate::generate);
    connect(ui->buttonViewFormatExtraInfos,
            &QPushButton::clicked,
            this,
            &PaneGenTemplate::viewFormatCustomInstructions);
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

QCoro::Task<void> PaneGenTemplate::generate()
{
    auto progress = new QProgressDialog(
                tr("Filling template..."),
                QString{}, 0, 0, this);
    progress->setWindowModality(Qt::WindowModal);
    progress->setCancelButton(nullptr);
    progress->setMinimumDuration(0);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    progress->setValue(0);
    progress->show();
    QPointer<QProgressDialog> progressGuard{progress};

    try {
        if (co_await baseControlsWithoutPopup())
        {
            qDebug() << "Filling templates...";
            co_await m_templateFiller->fillValues();
            if (m_templateFiller->aiFailureTable()->rowCount() == 0)
            {
                QMessageBox::information(
                            this,
                            tr("Generation done"),
                            tr("Template filled successfully"));
            }
        }
    }
    catch (const ExceptionTemplate &exception)
    {
        QMessageBox::critical(
                    this,
                    exception.title(),
                    exception.error());
    }
    /* // Here we remove the catch so we know where it crashes
    catch (const std::exception &e)
    {
        int status = 0;
        char *demangled = abi::__cxa_demangle(typeid(e).name(), nullptr, nullptr, &status);
        QString typeName = (status == 0 && demangled) ? QString(demangled) : QString(typeid(e).name());
        free(demangled);
        qCritical() << "Exception type:" << typeName << "| what():" << e.what();
        QMessageBox::critical(
                    this,
                    tr("Unknown Error"),
                    QString("Type: %1\n%2").arg(typeName, e.what()));
    }
    catch (...)
    {
        qCritical() << "Unknown non-std exception during template filling";
        QMessageBox::critical(
                    this,
                    tr("Unknown Error"),
                    tr("An unknown error occurred during template filling."));
    }
//*/
    if (progressGuard)
    {
        progressGuard->close();
        progressGuard->deleteLater();
    }
    displayAiErrors();
}

void PaneGenTemplate::displayAiErrors()
{
    auto aiFailureTalbe = m_templateFiller->aiFailureTable();
    if (aiFailureTalbe != nullptr && aiFailureTalbe->rowCount() > 0)
    {
        auto tableModel = m_templateFiller->aiFailureTable();
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Generation done with AI Failures"));
        dialog.resize(800, 600);
        auto *layout = new QVBoxLayout(&dialog);
        auto *tableView = new QTableView(&dialog);
        tableView->setModel(tableModel);
        tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        layout->addWidget(tableView);

        auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
        QObject::connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        layout->addWidget(buttonBox);

        dialog.exec();
    }
}

void PaneGenTemplate::_clearTemplateFiller()
{
    if (m_templateFiller != nullptr)
    {
        delete m_templateFiller;
        m_templateFiller = nullptr;
    }
}

void PaneGenTemplate::_setControlButtonsEnabled(bool enable)
{
    ui->buttonBasicControls->setEnabled(enable);
    ui->buttonFindMandatoryFieldIds->setEnabled(enable);
    ui->buttonViewAttributes->setEnabled(enable);
    ui->buttonExtractProductInfos->setEnabled(enable);
}

void PaneGenTemplate::browseSourceMain()
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

QSharedPointer<QSettings> PaneGenTemplate::settingsFolder() const
{
    return QSharedPointer<QSettings>{new QSettings{m_settingsFilePath, QSettings::IniFormat}};
}

QMap<QString, QString> PaneGenTemplate::get_skuPattern_customInstructions() const
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

void PaneGenTemplate::_enableGenerateButtonIfValid()
{
    if (!SettingsTable::instance()->value(SettingsTable::KEY_OPENAI_API_KEY).isEmpty()
        && !ui->lineEditTo->text().isEmpty())
    {
        _setGenerateButtonsEnabled(true);
    }
    else
    {
        _setGenerateButtonsEnabled(false);
    }
}

void PaneGenTemplate::_setGenerateButtonsEnabled(bool enable)
{
    ui->buttonGenerate->setEnabled(enable);
    ui->buttonGenAiDesc->setEnabled(enable);
    ui->buttonReviewAiDesc->setEnabled(enable);
    ui->buttonDisplayPossibleValues->setEnabled(enable);
    ui->buttonRunPromptsManually->setEnabled(enable);
}

QCoro::Task<void> PaneGenTemplate::baseControls()
{
    auto progress = new QProgressDialog(
                tr("Checking basics..."),
                QString{}, 0, 0, this);
    progress->setWindowModality(Qt::ApplicationModal);
    progress->setCancelButton(nullptr);
    progress->setMinimumDuration(0);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    progress->setValue(0);
    progress->show();
    QPointer<QProgressDialog> progressGuard{progress};

    try
    {
        if (co_await baseControlsWithoutPopup())
        {
            QMessageBox::information(
                        this,
                        tr("Controls done"),
                        tr("Controls successfully done"));
        }
    }
    catch (const ExceptionOpenAiNotInitialized &exception)
    {
        QMessageBox::warning(
                    this,
                    exception.title(),
                    exception.error());
    }
    catch (const ExceptionOpenAiError &exception)
    {
        QMessageBox::warning(
                    this,
                    exception.title(),
                    exception.error());
    }
    catch (const ExceptionTemplate &exception)
    {
        QMessageBox::warning(
                    this,
                    exception.title(),
                    exception.error());
    }
    catch (const std::exception &e)
    {
        QMessageBox::critical(
                    this,
                    tr("Unknown Error"),
                    QString("An unexpected checking error occurred: %1").arg(e.what()));
    }
    catch (...)
    {
        QMessageBox::critical(
                    this,
                    tr("Unknown Error"),
                    tr("An unknown error occurred during base controls."));
    }
//*/

    if (progressGuard)
    {
        progressGuard->close();
        progressGuard->deleteLater();
    }
}

QCoro::Task<bool> PaneGenTemplate::baseControlsWithoutPopup()
{
    qDebug() << "m_templateFiller->checkParentSkus()...";
    m_templateFiller->checkParentSkus();
    qDebug() << "m_templateFiller->checkPossibleValues()...";
    m_templateFiller->checkPossibleValues();
    qDebug() << "m_templateFiller->prepareForValidation()...";
    co_await m_templateFiller->prepareForValidation();
    qDebug() << "m_templateFiller->checkColumnsFilled()...";
    m_templateFiller->checkColumnsFilled();
    qDebug() << "m_templateFiller->checkPreviewImages()...";
    m_templateFiller->checkPreviewImages();
    qDebug() << "m_templateFiller->checkKeywords()...";
    m_templateFiller->checkKeywords();
    qDebug() << "m_templateFiller->checks...DONE SUCCESSFULLY";
    co_return true;
}

void PaneGenTemplate::findValidateMandatoryFieldIds()
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

void PaneGenTemplate::viewFormatCustomInstructions()
{
    QMessageBox::information(
        this,
        tr("Format"),
        tr("Custom instructions for every products\n"
           "[SKUSTART1,SKUSTART2]\nCustom instructions for products that include the skus\n"
           "[SKUSTART3,SKUSTART4]\nCustom instructions for products that include the skus"));
}

void PaneGenTemplate::viewAttributes()
{
    DialogAttributes dialog{m_templateFiller};
    dialog.exec();
}

void PaneGenTemplate::extractProductInfos()
{
    DialogExtractInfos dialog{m_workingDir.path()};
    dialog.exec();
}
