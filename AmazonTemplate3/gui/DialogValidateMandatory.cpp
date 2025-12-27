#include "DialogValidateMandatory.h"
#include "ui_DialogValidateMandatory.h"

DialogValidateMandatory::DialogValidateMandatory(
        const TemplateFiller::AttributesToValidate &attributesToValidate,
        QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DialogValidateMandatory)
{
    ui->setupUi(this);

    int row = 0;
    ui->tableWidgetFieldIds->setRowCount(
                attributesToValidate.addedAi.size() +
                attributesToValidate.removedAi.size());
    
    QStringList addedAiSorted = attributesToValidate.addedAi.values();
    addedAiSorted.sort();
    for (const auto &fieldId : addedAiSorted)
    {
        auto *itemFieldId = new QTableWidgetItem{fieldId};
        itemFieldId->setFlags(Qt::ItemIsEnabled);
        ui->tableWidgetFieldIds->setItem(row, 0, itemFieldId);

        auto *itemAi = new QTableWidgetItem{tr("Mandatory")};
        itemAi->setFlags(Qt::ItemIsEnabled);
        ui->tableWidgetFieldIds->setItem(row, 1, itemAi);
        
        auto *itemManual = new QTableWidgetItem{};
        itemManual->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        itemManual->setCheckState(Qt::Checked);
        ui->tableWidgetFieldIds->setItem(row, 2, itemManual);
        
        ++row;
    }

    QStringList removedAiSorted = attributesToValidate.removedAi.values();
    removedAiSorted.sort();
    for (const auto &fieldId : removedAiSorted)
    {
        auto *itemFieldId = new QTableWidgetItem{fieldId};
        itemFieldId->setFlags(Qt::ItemIsEnabled);
        ui->tableWidgetFieldIds->setItem(row, 0, itemFieldId);

        auto *itemAi = new QTableWidgetItem{tr("Not Mandatory")};
        itemAi->setFlags(Qt::ItemIsEnabled);
        ui->tableWidgetFieldIds->setItem(row, 1, itemAi);
        
        auto *itemManual = new QTableWidgetItem{};
        itemManual->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        itemManual->setCheckState(Qt::Unchecked);
        ui->tableWidgetFieldIds->setItem(row, 2, itemManual);
        
        ++row;
    }
    ui->tableWidgetFieldIds->resizeColumnsToContents();
}

QSet<QString> DialogValidateMandatory::getAttributeValidatedMandatory() const
{
    QSet<QString> fieldIds;
    for (int i=0; i<ui->tableWidgetFieldIds->rowCount(); ++i)
    {
        if (ui->tableWidgetFieldIds->item(i, 2)->checkState() == Qt::Checked)
        {
            fieldIds << ui->tableWidgetFieldIds->item(i, 0)->text();
        }
    }
    return fieldIds;
}

QSet<QString> DialogValidateMandatory::getAttributeValidatedNotMandatory() const
{
    QSet<QString> fieldIds;
    for (int i=0; i<ui->tableWidgetFieldIds->rowCount(); ++i)
    {
        if (ui->tableWidgetFieldIds->item(i, 2)->checkState() == Qt::Unchecked)
        {
            fieldIds << ui->tableWidgetFieldIds->item(i, 0)->text();
        }
    }
    return fieldIds;
}

DialogValidateMandatory::~DialogValidateMandatory()
{
    delete ui;
}
