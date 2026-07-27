#include <QComboBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "AbstractCli.h"
#include "../../common/workingdirectory/WorkingDirectoryManager.h"

#include "DialogSuggestEquivalent.h"

DialogSuggestEquivalent::DialogSuggestEquivalent(
    AbstractCli *cli,
    const QString &title,
    const QString &message,
    const FillerSelectable::MissingValueInfo &info,
    QWidget *parent)
    : QDialog(parent)
    , m_cli(cli)
    , m_info(info)
{
    setWindowTitle(title);
    setMinimumSize(700, 560);

    auto *layout = new QVBoxLayout(this);

    // Keep the original error message visible so the user knows the choices.
    auto *messageEdit = new QPlainTextEdit(message);
    messageEdit->setReadOnly(true);
    messageEdit->setMaximumHeight(140);
    layout->addWidget(messageEdit);

    auto *suggestionGroup = new QGroupBox(tr("AI diagnosis"));
    auto *suggestionLayout = new QVBoxLayout(suggestionGroup);
    m_statusLabel = new QLabel(
        tr("Asking %1 what happened (it can read the equivalence CSV files)…")
            .arg(cli->getName()));
    m_statusLabel->setWordWrap(true);
    suggestionLayout->addWidget(m_statusLabel);
    m_diagnosisEdit = new QPlainTextEdit;
    m_diagnosisEdit->setReadOnly(true);
    suggestionLayout->addWidget(m_diagnosisEdit);
    layout->addWidget(suggestionGroup);

    auto *choiceGroup = new QGroupBox(
        tr("Equivalent value for \"%1\" (field %2)")
            .arg(m_info.fromValue, m_info.fieldIdAmzV02));
    auto *choiceLayout = new QVBoxLayout(choiceGroup);
    m_comboValue = new QComboBox;
    m_comboValue->setEditable(true);
    QStringList values = m_info.possibleValues.values();
    values.sort();
    m_comboValue->addItems(values);
    m_comboValue->setCurrentIndex(-1);
    choiceLayout->addWidget(m_comboValue);
    layout->addWidget(choiceGroup);

    auto *buttonBox = new QDialogButtonBox;
    m_validateButton = buttonBox->addButton(tr("Validate"), QDialogButtonBox::AcceptRole);
    m_validateButton->setEnabled(false);
    auto *editButton = buttonBox->addButton(tr("Edit manually…"), QDialogButtonBox::ActionRole);
    buttonBox->addButton(QDialogButtonBox::Cancel);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(editButton, &QPushButton::clicked, this, [this]() {
        m_editManually = true;
        reject();
    });
    connect(m_comboValue, &QComboBox::currentTextChanged, this, [this](const QString &text) {
        m_validateButton->setEnabled(!text.trimmed().isEmpty());
    });
    layout->addWidget(buttonBox);

    _askCli(message);
}

void DialogSuggestEquivalent::_askCli(const QString &message)
{
    QStringList values = m_info.possibleValues.values();
    values.sort();

    QStringList lines;
    lines << "You are helping fix a template-filling error in an Amazon listing tool."
          << ""
          << "Error message:"
          << message
          << ""
          << QString("The equivalence table key (field id) is \"%1\" and the source value "
                     "with no known equivalent is \"%2\".").arg(m_info.fieldIdAmzV02, m_info.fromValue)
          << ""
          << "The fix will add an equivalence linking the source value to exactly ONE of these allowed values:"
          << "- " + values.join("\n- ")
          << ""
          << "Your working directory contains the tool's knowledge base as CSV files:"
          << "- attributeEquivalent.csv: header line, then one line per equivalence group formatted "
             "\"<fieldId>,<value1>;<value2>;...\" (values in a group are equivalent across languages/marketplaces)."
          << "- attributeReplacement.csv: explicit value replacements per marketplace/country/language."
          << "- attributeFlags.csv and attributePossibleMissing.csv: per-field flags and manually added possible values."
          << "Search these files (e.g. grep for the field id and the value) to understand what happened and where."
          << ""
          << "Reply with ONLY a JSON object, no markdown fences, in this exact format:"
          << "{\"explanation\": \"<2-4 sentences: what happened, where, and why you suggest this value>\", "
             "\"suggestedValue\": \"<exactly one of the allowed values, or an empty string if none fits>\"}";

    const QString workingDirPath = WorkingDirectoryManager::instance()->workingDir().path();
    m_cli->runPromptAsync(lines.join("\n"), workingDirPath, this,
                          [this](CliRunResult result) {
        if (!result.processStarted)
        {
            m_statusLabel->setText(
                tr("Could not start %1 — pick a value manually below.").arg(m_cli->getName()));
            return;
        }
        if (result.output.trimmed().isEmpty())
        {
            m_statusLabel->setText(
                tr("%1 returned no output (exit code %2) — pick a value manually below.")
                    .arg(m_cli->getName(), QString::number(result.exitCode)));
            m_diagnosisEdit->setPlainText(result.errorOutput);
            return;
        }
        // Note: don't use extractTextFromOutput() here — it expects the
        // stream-json format of translationPromptArgs(); runPromptAsync()
        // uses promptArgs(), which outputs plain text for every CLI.
        _onCliReply(result.output);
    });
}

void DialogSuggestEquivalent::_onCliReply(const QString &output)
{
    // The CLI may print text around the JSON — extract the outermost object.
    QString explanation;
    QString suggestedValue;
    const int start = output.indexOf('{');
    const int end = output.lastIndexOf('}');
    if (start >= 0 && end > start)
    {
        const QJsonDocument doc = QJsonDocument::fromJson(
            output.mid(start, end - start + 1).toUtf8());
        if (doc.isObject())
        {
            explanation = doc.object().value("explanation").toString();
            suggestedValue = doc.object().value("suggestedValue").toString().trimmed();
        }
    }
    if (explanation.isEmpty() && suggestedValue.isEmpty())
    {
        m_statusLabel->setText(
            tr("Could not parse the %1 reply — pick a value manually below.")
                .arg(m_cli->getName()));
        m_diagnosisEdit->setPlainText(output.trimmed());
        return;
    }

    m_diagnosisEdit->setPlainText(explanation);
    if (m_info.possibleValues.contains(suggestedValue))
    {
        m_statusLabel->setText(
            tr("%1 suggests \"%2\" — validate it, or pick another value below.")
                .arg(m_cli->getName(), suggestedValue));
        m_comboValue->setCurrentText(suggestedValue);
    }
    else if (!suggestedValue.isEmpty())
    {
        m_statusLabel->setText(
            tr("%1 suggested \"%2\" but it is not an allowed value — pick one below.")
                .arg(m_cli->getName(), suggestedValue));
    }
    else
    {
        m_statusLabel->setText(
            tr("%1 could not find a matching value — pick one below.")
                .arg(m_cli->getName()));
    }
}

QString DialogSuggestEquivalent::selectedValue() const
{
    return m_comboValue->currentText().trimmed();
}

void DialogSuggestEquivalent::accept()
{
    const QString text = selectedValue();
    if (!m_info.possibleValues.contains(text))
    {
        // Tolerate a case-only mismatch by mapping back to the exact value.
        for (const auto &allowed : m_info.possibleValues)
        {
            if (allowed.compare(text, Qt::CaseInsensitive) == 0)
            {
                m_comboValue->setCurrentText(allowed);
                QDialog::accept();
                return;
            }
        }
        QMessageBox::warning(
            this,
            tr("Value not allowed"),
            tr("\"%1\" is not one of the possible values for this field. "
               "Pick one of the values from the list.").arg(text));
        return;
    }
    QDialog::accept();
}
