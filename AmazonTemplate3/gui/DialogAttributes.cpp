#include <QMessageBox>
#include <QInputDialog>

#include <AttributeValueReplacedTable.h>
#include <AttributePossibleMissingTable.h>
#include <AttributeEquivalentTable.h>
#include <AttributeFlagsTable.h>
#include <AttributesMandatoryTable.h>
#include <TemplateFiller.h>

#include "DialogAddPossibleValues.h"
#include "DialogAddValueToReplace.h"

#include "DialogAttributes.h"
#include "ui_DialogAttributes.h"

DialogAttributes::DialogAttributes(TemplateFiller *templateFiller, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DialogAttributes)
{
    ui->setupUi(this);
    m_templateFiller = templateFiller;
    ui->tableViewEquivalences->setModel(m_templateFiller->attributeEquivalentTable());
    ui->tableViewFlags->setModel(m_templateFiller->attributeFlagsTable());
    ui->tableViewFlags->resizeColumnsToContents();
    ui->tableViewMandatory->setModel(m_templateFiller->mandatoryAttributesTable());
    ui->tableViewMandatory->resizeColumnsToContents();
    ui->tableViewMissingPossibleValues->setModel(m_templateFiller->attributePossibleMissingTable());
    ui->tableViewReplaced->setModel(m_templateFiller->attributeValueReplacedTable());
    _connectSlots();
}

DialogAttributes::~DialogAttributes()
{
    delete ui;
}

QCoro::Task<bool> DialogAttributes::editAttributes(
        TemplateFiller *templateFiller, const QString &title, const QString &message)
{
    QMessageBox::information(
                nullptr,
                title,
                tr("You will be asked to fix the following error") + ". " + message);
    DialogAttributes dialog(templateFiller);
    auto ret = dialog.exec();
    co_return ret == QDialog::Accepted;
}

void DialogAttributes::_connectSlots()
{
    connect(ui->buttonMissingPossibleAdd,
            &QPushButton::clicked,
            this,
            &DialogAttributes::missingPossibleAdd);
    connect(ui->buttonMissingPossibleRemove,
            &QPushButton::clicked,
            this,
            &DialogAttributes::missingPossibleRemove);
    connect(ui->buttonReplacedRemove,
            &QPushButton::clicked,
            this,
            &DialogAttributes::replaceAdd);
    connect(ui->buttonReplacedAdd,
            &QPushButton::clicked,
            this,
            &DialogAttributes::replaceRemove);
    connect(ui->buttonAddFlag,
            &QPushButton::clicked,
            this,
            &DialogAttributes::flagsAdd);
    connect(ui->buttonRemoveEquivalence,
            &QPushButton::clicked,
            this,
            &DialogAttributes::equivalentRemove);
}

void DialogAttributes::missingPossibleAdd()
{
    DialogAddPossibleValues dialogMissing;
    auto ret = dialogMissing.exec();
    if (ret == QDialog::Accepted)
    {
        m_templateFiller->attributePossibleMissingTable()->recordAttribute(
                    dialogMissing.getMarketplaceId(),
                    dialogMissing.getCountryCode(),
                    dialogMissing.getLangCode(),
                    dialogMissing.getProductType(),
                    dialogMissing.getAttributeId(),
                    dialogMissing.getPossibleValues());
    }
}

void DialogAttributes::missingPossibleRemove()
{
    const auto &selIndexes
            = ui->tableViewMissingPossibleValues
            ->selectionModel()->selectedIndexes();
    if (selIndexes.size() > 0)
    {
        m_templateFiller->attributePossibleMissingTable()->remove(selIndexes[0]);
    }
}

void DialogAttributes::replaceAdd()
{
    DialogAddValueToReplace dialogReplace;
    auto ret = dialogReplace.exec();
    if (ret == QDialog::Accepted)
    {
        m_templateFiller->attributeValueReplacedTable()->recordAttribute(
                    dialogReplace.getMarketplaceId(),
                    dialogReplace.getCountryCode(),
                    dialogReplace.getLangCode(),
                    dialogReplace.getProductType(),
                    dialogReplace.getAttributeId(),
                    dialogReplace.getValueFrom(),
                    dialogReplace.getValueTo());
    }
}

void DialogAttributes::replaceRemove()
{
    const auto &selIndexes
            = ui->tableViewMissingPossibleValues
            ->selectionModel()->selectedIndexes();
    if (selIndexes.size() > 0)
    {
        m_templateFiller->attributeValueReplacedTable()->remove(selIndexes[0]);
    }
}

void DialogAttributes::equivalentRemove()
{
    const auto &selIndexes
            = ui->tableViewEquivalences
            ->selectionModel()->selectedIndexes();
    if (selIndexes.size() > 0)
    {
        m_templateFiller->attributeEquivalentTable()->remove(selIndexes[0]);
    }
}

void DialogAttributes::flagsAdd()
{
    const auto &fieldIds = m_templateFiller->getAllFieldIds();
    const auto &marketplace = m_templateFiller->marketplaceFrom();
    const auto &unrecordedFieldIds = m_templateFiller->attributeFlagsTable()->getUnrecordedFieldIds(
                marketplace, fieldIds);
    if (unrecordedFieldIds.isEmpty())
    {
        QMessageBox::information(
                    this,
                    tr("No more flags"),
                    tr("There is no more flags to add."));
        return;
    }
    QStringList items{unrecordedFieldIds.begin(), unrecordedFieldIds.end()};
    items.sort();
    bool ok;
    QString item = QInputDialog::getItem(this, tr("Select attribute"),
                                         tr("Attribute:"), items, 0, false, &ok);
    if (ok && !item.isEmpty())
    {
        m_templateFiller->attributeFlagsTable()->recordAttribute({{marketplace, item}});
    }
}
