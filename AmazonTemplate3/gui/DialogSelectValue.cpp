#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

#include "DialogSelectValue.h"

DialogSelectValue::DialogSelectValue(
    const QString &title,
    const QString &description,
    const QString &aiPrompt,
    const QString &imagePath,
    const QStringList &possibleValues,
    QWidget *parent)
    : QDialog(parent)
    , m_allValues(possibleValues)
{
    setWindowTitle(title);
    setMinimumSize(540, 520);

    auto *layout = new QVBoxLayout(this);

    auto *descLabel = new QLabel(description);
    descLabel->setWordWrap(true);
    descLabel->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    layout->addWidget(descLabel);

    if (!imagePath.isEmpty()) {
        auto *imageRow = new QHBoxLayout;
        auto *imageEdit = new QLineEdit(imagePath);
        imageEdit->setReadOnly(true);
        imageEdit->setCursorPosition(0);
        imageRow->addWidget(imageEdit);
        auto *copyImageBtn = new QPushButton(tr("Copy image path"));
        connect(copyImageBtn, &QPushButton::clicked, this, [imagePath]() {
            QApplication::clipboard()->setText(imagePath);
        });
        imageRow->addWidget(copyImageBtn);
        layout->addLayout(imageRow);

        QPixmap pixmap(imagePath);
        if (!pixmap.isNull()) {
            auto *imageLabel = new QLabel;
            imageLabel->setPixmap(
                pixmap.scaledToHeight(400, Qt::SmoothTransformation));
            imageLabel->setAlignment(Qt::AlignCenter);
            layout->addWidget(imageLabel);
        }
    }

    if (!aiPrompt.isEmpty()) {
        auto *promptGroup = new QGroupBox(
            tr("AI Prompt — copy, attach product image in your AI, then select the returned value below"));
        auto *promptLayout = new QVBoxLayout(promptGroup);

        auto *promptEdit = new QTextEdit;
        promptEdit->setPlainText(aiPrompt);
        promptEdit->setReadOnly(true);
        promptEdit->setMaximumHeight(160);
        promptLayout->addWidget(promptEdit);

        auto *copyBtn = new QPushButton(tr("Copy to clipboard"));
        connect(copyBtn, &QPushButton::clicked, this, [aiPrompt]() {
            QApplication::clipboard()->setText(aiPrompt);
        });
        promptLayout->addWidget(copyBtn);
        layout->addWidget(promptGroup);
    }

    auto *selectGroup = new QGroupBox(tr("Select the correct value"));
    auto *selectLayout = new QVBoxLayout(selectGroup);

    auto *filter = new QLineEdit;
    filter->setPlaceholderText(tr("Filter…"));
    selectLayout->addWidget(filter);

    m_list = new QListWidget;
    for (const auto &val : possibleValues)
        m_list->addItem(val);
    selectLayout->addWidget(m_list);
    layout->addWidget(selectGroup);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    m_okButton = buttonBox->button(QDialogButtonBox::Ok);
    m_okButton->setEnabled(false);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttonBox);

    connect(filter, &QLineEdit::textChanged, this, &DialogSelectValue::applyFilter);
    connect(m_list, &QListWidget::itemSelectionChanged, this, [this]() {
        m_okButton->setEnabled(m_list->currentItem() != nullptr);
    });
    connect(m_list, &QListWidget::itemDoubleClicked, this, &QDialog::accept);
}

void DialogSelectValue::applyFilter(const QString &text)
{
    m_list->clear();
    for (const auto &val : m_allValues) {
        if (text.isEmpty() || val.contains(text, Qt::CaseInsensitive))
            m_list->addItem(val);
    }
    m_okButton->setEnabled(m_list->currentItem() != nullptr);
}

QString DialogSelectValue::selectedValue() const
{
    const auto *item = m_list->currentItem();
    return item ? item->text() : QString();
}

QCoro::Task<QString> DialogSelectValue::selectValue(
    TemplateFiller * /*templateFiller*/,
    const QString &title,
    const QString &description,
    const QString &aiPrompt,
    const QString &imagePath,
    const QSet<QString> &possibleValues)
{
    QStringList valueList = possibleValues.values();
    valueList.sort();
    DialogSelectValue dialog(title, description, aiPrompt, imagePath, valueList);
    const int ret = dialog.exec();
    co_return (ret == QDialog::Accepted) ? dialog.selectedValue() : QString();
}
