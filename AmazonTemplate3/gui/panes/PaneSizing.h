#ifndef PANESIZING_H
#define PANESIZING_H

#include <QWidget>
#include <QDir>
#include <QImage>
#include <QList>
#include <memory>

#include <QCoro/QCoroTask>

#include "AbstractCli.h"

QT_BEGIN_NAMESPACE
namespace Ui { class PaneSizing; }
QT_END_NAMESPACE

class AmazonCatalogApi;
class TreeSizingAsins;
class QStandardItemModel;
class QDoubleSpinBox;
class AbstractSizeCategory;
class QNetworkAccessManager;

class PaneSizing : public QWidget
{
    Q_OBJECT
public:
    explicit PaneSizing(QWidget *parent = nullptr);
    ~PaneSizing();
    void setWorkingDir(const QDir &workingDir);
    void setAvailableClis(const QList<AbstractCli *> &clis);

private slots:
    void onAddFromAsinClicked();
    void onAddFromTemplateClicked();
    void onSizeTypeChanged(int index);
    void onGenSizeTablesClicked();
    void onMakeEditableToggled(bool checked);
    void updateButtonStates();
    void onSizeModeChanged();
    void onGroupImageSelected(int row);
    void onUploadSizeTableClicked();
    void onGenerateFaqClicked();
    void onVariantImageSelected(int row);

private:
    struct MeasurementWidgets {
        QString        fieldId;
        QDoubleSpinBox *refSpinBox   = nullptr;
        QDoubleSpinBox *stepSpinBox  = nullptr;
        QDoubleSpinBox *rangeSpinBox = nullptr;
    };

    Ui::PaneSizing   *ui;
    std::unique_ptr<AmazonCatalogApi> m_api;
    std::unique_ptr<TreeSizingAsins>  m_treeModel;
    QStandardItemModel               *m_sizeTableModel = nullptr;
    bool                              m_generatedSuccessfully = false;
    QList<MeasurementWidgets>         m_measurementWidgets;
    QList<QImage>                     m_groupImages;

    QDir                m_workingDir;
    QDir                m_productWorkingDir;
    QStringList         m_variantImagePaths;
    QString             m_mainImageLocalPath;
    QList<AbstractCli *>  m_availableClis;
    QNetworkAccessManager *m_imageNam = nullptr;

    void _ensureModel(const QDir &dir);
    void _refreshApi();
    QDir _resolveProductDir(const QString &asin, const QString &title);
    void _saveProductSettings();
    void _loadProductSettings();
    void _populateSizeRangeCombos();
    void _tryGuessSizeRange();
    void _rebuildMeasurementForm();
    const AbstractSizeCategory* _currentCategory() const;
    QCoro::Task<void> _uploadSizeChart(QString marketplaceId, QString productType);
    void _downloadMainImage(const QString &url, const QString &asin);
    void _downloadVariantImages(const QStringList &imageUrls);
    void _runCliPrompt(const QString &executable, const QStringList &args,
                       const QByteArray &stdinData, const QString &workDir,
                       QObject *guard, std::function<void(QString)> callback);
};

#endif
