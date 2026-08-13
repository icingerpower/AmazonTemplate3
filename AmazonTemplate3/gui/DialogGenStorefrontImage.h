#ifndef DIALOGGENSTOREFRONTIMAGE_H
#define DIALOGGENSTOREFRONTIMAGE_H

#include <QDialog>
#include <QDir>
#include <QHash>
#include <QList>
#include <QPixmap>
#include <QString>

#include "apis/AmazonCatalogApi.h"
#include "AbstractCli.h"

QT_BEGIN_NAMESPACE
class QComboBox;
class QListWidget;
class QPushButton;
class QTextEdit;
class QWidget;
QT_END_NAMESPACE

class DialogGenStorefrontImage : public QDialog
{
    Q_OBJECT

public:
    DialogGenStorefrontImage(const QDir &workingDir,
                             const QList<AmazonCatalogApi::StoreItem> &selectedItems,
                             const QHash<QString, QPixmap> &asinToPixmap,
                             AbstractCli *cli = nullptr,
                             const QStringList &nodePath = {},
                             QWidget *parent = nullptr);
    ~DialogGenStorefrontImage() override;

signals:
    void imageGenerated(const QString &desktopPath, const QString &mobilePath);

private:
    // --- widgets ---
    QListWidget *m_productList      = nullptr;
    QComboBox   *m_comboBoxSize     = nullptr;
    QTextEdit   *m_promptEdit       = nullptr;
    QListWidget *m_versionsList     = nullptr;
    QPushButton *m_buttonViewImage      = nullptr;
    QPushButton *m_buttonCopyPath       = nullptr;
    QPushButton *m_buttonDeleteVersion  = nullptr;
    QTextEdit   *m_logEdit          = nullptr;
    QPushButton *m_buttonGenerate   = nullptr;

    // --- data ---
    QDir                                       m_workingDir;
    QList<AmazonCatalogApi::StoreItem>         m_items;
    QHash<QString, QPixmap>                    m_asinToPixmap;
    AbstractCli                               *m_cli = nullptr;
    QStringList                                m_nodePath;

    // --- helpers ---
    QString _storefrontDir() const;
    QString _versionsJsonPath() const;

    void _populateProductList();
    void _autoFillPrompt();
    // Absolute path to this ASIN's real product photo (sizing/{ASIN}-*/{ASIN}_main.jpg,
    // falling back to stores/thumbs/{ASIN}.jpg), or {} if neither exists on disk.
    QString _referenceImagePath(const QString &asin) const;
    void _loadVersions();          // (re)reads versions.json, repopulates m_versionsList
    void _onGenerateClicked();
    void _viewImage();
    void _copyImagePath();
    void _deleteSelectedVersion();
    void _log(const QString &line);

    void _generate();

    // Returns the version entry with the given timestamp, or {}.
    QJsonObject _versionByTs(qint64 ts) const;
};

#endif // DIALOGGENSTOREFRONTIMAGE_H
