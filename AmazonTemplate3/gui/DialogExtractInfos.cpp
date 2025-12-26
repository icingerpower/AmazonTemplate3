#include <QFileDialog>
#include <QClipboard>
#include <QApplication>
#include <QMessageBox>
#include <QInputDialog>
#include <QSettings>
#include <QDir>

#include <TableInfoExtractor.h>

#include "DialogExtractInfos.h"
#include "ui_DialogExtractInfos.h"

DialogExtractInfos::DialogExtractInfos(
    const QString &workingDir, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::DialogExtractInfos)
{
    ui->setupUi(this);
    ui->tableViewInfos->setModel(new TableInfoExtractor{ui->tableViewInfos});
    ui->tableViewInfos->horizontalHeader()->resizeSection(TableInfoExtractor::IND_TITLE, 300);
    ui->buttonGenerateImageNames->hide();
    QDir dir{workingDir};
    m_workingDir = workingDir;
    const auto &fileInfos = dir.entryInfoList(
        QStringList{"Mod*.xlsx"}, QDir::Files, QDir::Name);
    if (fileInfos.size() > 0)
    {
        ui->lineEditGtinTemplate->setText(fileInfos.first().absoluteFilePath());
    }
    _connectSlots();
}

void DialogExtractInfos::_connectSlots()
{
    connect(ui->buttonPasteSKUs,
            &QPushButton::clicked,
            this,
            &DialogExtractInfos::pasteSkus);
    connect(ui->buttonPasteTitles,
            &QPushButton::clicked,
            this,
            &DialogExtractInfos::pasteTitles);
    connect(ui->buttonBrowseGtinTemplate,
            &QPushButton::clicked,
            this,
            &DialogExtractInfos::browseGtinTemplateFile);
    connect(ui->buttonFillGtin,
            &QPushButton::clicked,
            this,
            &DialogExtractInfos::fillGtinTemplateFile);
    connect(ui->buttonClear,
            &QPushButton::clicked,
            getTableInfoExtractor(),
            &TableInfoExtractor::clear);
    connect(ui->buttonCopyColumn,
            &QPushButton::clicked,
            this,
            &DialogExtractInfos::copyColumn);
    connect(ui->buttonReadGtin,
            &QPushButton::clicked,
            this,
            &DialogExtractInfos::readGtinCodes);
    connect(ui->buttonGenerateImageNames,
            &QPushButton::clicked,
            this,
            &DialogExtractInfos::generateImageNames);
    connect(ui->buttonCheckImageFileNames,
            &QPushButton::clicked,
            this,
            &DialogExtractInfos::checkImageFileNames);
}

DialogExtractInfos::~DialogExtractInfos()
{
    delete ui;
}

TableInfoExtractor *DialogExtractInfos::getTableInfoExtractor() const
{
    return static_cast<TableInfoExtractor *>(
        ui->tableViewInfos->model());
}

void DialogExtractInfos::browseGtinTemplateFile()
{
    const QString &filePath = QFileDialog::getOpenFileName(
        this
        , tr("GTIN empty template")
        , m_workingDir
        , QString{"Xlsx (*.xlsx *.XLSX)"}
        //, nullptr
        //, QFileDialog::DontUseNativeDialog
        );
    if (!filePath.isEmpty())
    {
        ui->lineEditGtinTemplate->setText(filePath);
    }
}

void DialogExtractInfos::fillGtinTemplateFile()
{
    const auto &brand = ui->lineEditBrand->text();
    if (brand.isEmpty())
    {
        QMessageBox::information(
            this,
            tr("No brand"),
            tr("You need to enter the brand"));
        return;
    }
    const auto &category = ui->lineEditCategoryCode->text();
    if (category.isEmpty())
    {
        QMessageBox::information(
            this,
            tr("No category code"),
            tr("You need to enter the GTIN category code"));
        return;
    }
    const auto &fileGtinTempalteFrom = ui->lineEditGtinTemplate->text();
    auto fileGtinTempalteTo = fileGtinTempalteFrom;
    fileGtinTempalteTo.replace(".xlsx", "FILLED.xlsx");
    getTableInfoExtractor()->fillGtinTemplate(
        fileGtinTempalteFrom
        , fileGtinTempalteTo
        , brand
        , category
        );
}

void DialogExtractInfos::copyColumn()
{
    const auto &selIndexes = ui->tableViewInfos->selectionModel()->selectedIndexes();
    if (selIndexes.size() == 0)
    {
        QMessageBox::information(
            this,
            tr("No sleection"),
            tr("You need to select a column"));
    }
    else
    {
        getTableInfoExtractor()->copyColumn(selIndexes.first());
    }
}

void DialogExtractInfos::pasteSkus()
{
    const QString &error = getTableInfoExtractor()->pasteSKUs();
    if (!error.isEmpty())
    {
        QMessageBox::warning(
            this,
            tr("Erreur"),
            error);
    }
}

void DialogExtractInfos::pasteTitles()
{
    const QString &error = getTableInfoExtractor()->pasteTitles();
    if (!error.isEmpty())
    {
        QMessageBox::warning(
            this,
            tr("Erreur"),
            error);
    }
}

void DialogExtractInfos::generateImageNames()
{
    const auto &baseUrl = QInputDialog::getText(
                this,
                tr("Base url path"),
                tr("Enter please the base url image path"),
                QLineEdit::Normal,
                "https://icinger.fr/cedricteam/images/");
    // TODO ask folder to retrieve the image name + add one column per image + trigger warning if images are not well named
    getTableInfoExtractor()->generateImageNames(baseUrl);
}

void DialogExtractInfos::checkImageFileNames()
{
    QSettings settings;
    QString settingKeyUrl = "DialogOpenConifg__checkImageFileNames_url";
    QString lastUrl = settings.value(
                settingKeyUrl, "https://icinger.fr/cedricteam/images/").toString();
    auto baseUrl = QInputDialog::getText(
                this,
                tr("Base url path"),
                tr("Enter the base url image path"),
                QLineEdit::Normal,
                lastUrl);
    if (!baseUrl.isEmpty())
    {
        if (!baseUrl.endsWith("/"))
        {
            baseUrl += "/";
        }
        settings.setValue(settingKeyUrl, baseUrl);
        QString settingKey = "DialogOpenConifg__checkImageFileNames";
        QString lastDirPath = settings.value(
                    settingKey, QDir().absolutePath()).toString();
        const QString &dirPath = QFileDialog::getExistingDirectory(
                    this,
                    tr("Choose a directory"),
                    lastDirPath,
                    QFileDialog::DontUseNativeDialog);
        if (!dirPath.isEmpty())
        {
            const QString &error
                    = getTableInfoExtractor()->readAvailableImage(baseUrl, dirPath);
            settings.setValue(settingKey, dirPath);
            if (!error.isEmpty())
            {
                QMessageBox::information(
                            this,
                            tr("Image issue"),
                            error);
            }
        }
    }
}

void DialogExtractInfos::readGtinCodes()
{
    QDir dir{m_workingDir};
    const auto &fileInfos = dir.entryInfoList(
        QStringList{"Mes-derniers-produits*.xlsx"}, QDir::Files, QDir::Name);
    if (fileInfos.size() > 0)
    {
        const auto &fileInfo = fileInfos.last();
        const auto &GTINs = getTableInfoExtractor()->readGtin(fileInfo.absoluteFilePath());
        if (GTINs.size() == 0)
        {
            QMessageBox::information(
                this,
                tr("No GTIN found"),
                tr("No GTIN could be rode in the report file Mes-derniers-produits*.xlsx"));
        }
        else
        {
            auto clipboard = QApplication::clipboard();
            clipboard->setText(GTINs.join("\n"));
            QMessageBox::information(
                this,
                tr("GTINs copied"),
                tr("%1 GTINs have been copied in the clipboard").arg(GTINs.size()));
        }
    }
    else
    {
        QMessageBox::information(
            this,
            tr("File not found"),
            tr("Can't find the GTIN GS1 report file Mes-derniers-produits*.xlsx"));
    }
}

