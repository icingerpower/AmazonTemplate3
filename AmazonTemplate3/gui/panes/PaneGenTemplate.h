#ifndef PANEGENTEMPLATE_H
#define PANEGENTEMPLATE_H

#include <QWidget>
#include <QDir>
#include <QSettings>
#include <QSharedPointer>
#include <QCoro/QCoroCore>

class TemplateFiller;
class AbstractCli;

QT_BEGIN_NAMESPACE
namespace Ui { class PaneGenTemplate; }
QT_END_NAMESPACE

class PaneGenTemplate : public QWidget
{
    Q_OBJECT

public:
    explicit PaneGenTemplate(QWidget *parent = nullptr);
    ~PaneGenTemplate();

    void setAvailableClis(const QList<AbstractCli *> &clis);
    QSharedPointer<QSettings> settingsFolder() const;
    QMap<QString, QString> get_skuPattern_customInstructions() const;

public slots:
    void browseSourceMain();
    QCoro::Task<void> baseControls();
    QCoro::Task<bool> baseControlsWithoutPopup();
    void findValidateMandatoryFieldIds();
    void viewFormatCustomInstructions();
    void viewAttributes();
    void extractProductInfos();
    QCoro::Task<void> generate();
    void displayAiErrors();

private:
    Ui::PaneGenTemplate *ui;
    void _connectSlots();
    TemplateFiller *m_templateFiller;
    QDir m_workingDir;
    QString m_settingsFilePath;
    QString m_settingsKeyExtraInfos;
    void _clearTemplateFiller();
    void _setControlButtonsEnabled(bool enable);
    void _setGenerateButtonsEnabled(bool enable);
    void _onCliChanged();
    void _enableGenerateButtonIfValid();
    QCoro::Task<void> m_taskGenerate;
};

#endif // PANEGENTEMPLATE_H
