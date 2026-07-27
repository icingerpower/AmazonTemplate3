#include <QMessageBox>
#include <QInputDialog>

#include <AttributeValueReplacedTable.h>
#include <AttributePossibleMissingTable.h>
#include <AttributeEquivalentTable.h>
#include <AttributeFlagsTable.h>
#include <AttributesMandatoryTable.h>
#include <TemplateFiller.h>
#include <Attribute.h>

#include "DialogAddPossibleValues.h"
#include "DialogAddValueToReplace.h"
#include "DialogSuggestEquivalent.h"
#include "ComboBoxColumnDelegate.h"

#include "DialogAttributes.h"
#include "ui_DialogAttributes.h"

AbstractCli *DialogAttributes::SUGGESTION_CLI = nullptr;

void DialogAttributes::setSuggestionCli(AbstractCli *cli)
{
    SUGGESTION_CLI = cli;
}

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
    _setupReplacedDelegate();
    _connectSlots();
}

DialogAttributes::~DialogAttributes()
{
    delete ui;
}

QCoro::Task<bool> DialogAttributes::editAttributes(
        TemplateFiller *templateFiller, const QString &title, const QString &message,
        const FillerSelectable::MissingValueInfo &info)
{
    bool showIntroMessage = true;
    if (SUGGESTION_CLI != nullptr
            && !info.fieldIdAmzV02.isEmpty()
            && !info.fromValue.isEmpty()
            && !info.possibleValues.isEmpty())
    {
        DialogSuggestEquivalent dialogSuggest(SUGGESTION_CLI, title, message, info);
        const int ret = dialogSuggest.exec();
        if (ret == QDialog::Accepted)
        {
            const QString &chosenValue = dialogSuggest.selectedValue();
            if (!chosenValue.isEmpty())
            {
                templateFiller->attributeEquivalentTable()->recordAttribute(
                            info.fieldIdAmzV02, {info.fromValue, chosenValue});
                co_return true;
            }
        }
        else if (!dialogSuggest.editManuallyRequested())
        {
            co_return false;
        }
        showIntroMessage = false; // The suggestion dialog already displayed the error
    }
    if (showIntroMessage)
    {
        QMessageBox::information(
                    nullptr,
                    title,
                    tr("You will be asked to fix the following error") + ". " + message);
    }
    DialogAttributes dialog(templateFiller);
    auto ret = dialog.exec();
    co_return ret == QDialog::Accepted;
}

void DialogAttributes::_setupReplacedDelegate()
{
    const auto &locales = m_templateFiller->getTargetTemplateLocales();
    QStringList countryCodes, langCodes;
    for (const auto &locale : locales)
    {
        if (!countryCodes.contains(locale.countryCode)) countryCodes.append(locale.countryCode);
        if (!langCodes.contains(locale.langCode))       langCodes.append(locale.langCode);
    }
    std::sort(countryCodes.begin(), countryCodes.end());
    std::sort(langCodes.begin(), langCodes.end());

    QStringList fieldIds = m_templateFiller->getAllFieldIds().values();
    std::sort(fieldIds.begin(), fieldIds.end());

    // Columns: 0=Marketplace, 1=Country, 2=Lang, 3=ProductType, 4=FieldId, 5=From, 6=To
    QHash<int, QStringList> columnItems{
        {0, Attribute::MARKETPLACES},
        {1, countryCodes},
        {2, langCodes},
        {4, fieldIds}
    };
    ui->tableViewReplaced->setItemDelegate(new ComboBoxColumnDelegate(columnItems, this));
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
    connect(ui->buttonReplacedAdd,
            &QPushButton::clicked,
            this,
            &DialogAttributes::replaceAdd);
    connect(ui->buttonReplacedRemove,
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
    DialogAddValueToReplace dialogReplace(m_templateFiller, this);
    auto ret = dialogReplace.exec();
    if (ret == QDialog::Accepted)
    {
        m_templateFiller->attributeValueReplacedTable()->recordAttribute(
                    dialogReplace.getMarketplace(),
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
            = ui->tableViewReplaced
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
