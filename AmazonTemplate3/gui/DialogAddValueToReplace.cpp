#include <QPushButton>
#include "DialogAddValueToReplace.h"
#include "ui_DialogAddValueToReplace.h"

DialogAddValueToReplace::DialogAddValueToReplace(TemplateFiller *templateFiller, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DialogAddValueToReplace),
    m_templateFiller(templateFiller)
{
    ui->setupUi(this);

    m_locales = m_templateFiller->getTargetTemplateLocales();
    for (const auto &locale : m_locales)
        ui->comboBoxTemplate->addItem(locale.displayName);

    const auto &fieldIds = m_templateFiller->getAllFieldIds().values();
    QStringList sortedIds = fieldIds;
    std::sort(sortedIds.begin(), sortedIds.end());
    ui->comboBoxAttributeId->addItems(sortedIds);

    ui->lineEditProductType->setText(m_templateFiller->productTypeFrom());

    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);

    connect(ui->comboBoxTemplate, &QComboBox::currentIndexChanged, this, &DialogAddValueToReplace::_updateOkButton);
    connect(ui->comboBoxAttributeId, &QComboBox::currentIndexChanged, this, &DialogAddValueToReplace::_updateOkButton);
    connect(ui->lineEditProductType, &QLineEdit::textChanged, this, &DialogAddValueToReplace::_updateOkButton);
    connect(ui->lineEditValueFrom, &QLineEdit::textChanged, this, &DialogAddValueToReplace::_updateOkButton);
    connect(ui->lineEditValueTo, &QLineEdit::textChanged, this, &DialogAddValueToReplace::_updateOkButton);

    _updateOkButton();
}

DialogAddValueToReplace::~DialogAddValueToReplace()
{
    delete ui;
}

void DialogAddValueToReplace::_updateOkButton()
{
    const bool valid = !m_locales.isEmpty()
        && !ui->lineEditProductType->text().trimmed().isEmpty()
        && !ui->comboBoxAttributeId->currentText().isEmpty()
        && !ui->lineEditValueFrom->text().trimmed().isEmpty()
        && !ui->lineEditValueTo->text().trimmed().isEmpty();
    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(valid);
}

QString DialogAddValueToReplace::getMarketplace() const
{
    return m_templateFiller->marketplaceFrom();
}

QString DialogAddValueToReplace::getCountryCode() const
{
    const int idx = ui->comboBoxTemplate->currentIndex();
    if (idx < 0 || idx >= m_locales.size()) return {};
    return m_locales[idx].countryCode;
}

QString DialogAddValueToReplace::getLangCode() const
{
    const int idx = ui->comboBoxTemplate->currentIndex();
    if (idx < 0 || idx >= m_locales.size()) return {};
    return m_locales[idx].langCode;
}

QString DialogAddValueToReplace::getProductType() const
{
    return ui->lineEditProductType->text().trimmed();
}

QString DialogAddValueToReplace::getAttributeId() const
{
    return ui->comboBoxAttributeId->currentText();
}

QString DialogAddValueToReplace::getValueFrom() const
{
    return ui->lineEditValueFrom->text().trimmed();
}

QString DialogAddValueToReplace::getValueTo() const
{
    return ui->lineEditValueTo->text().trimmed();
}
