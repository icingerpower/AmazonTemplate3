#ifndef PANESIZING_H
#define PANESIZING_H

#include <QWidget>
#include <QDir>
#include <QImage>
#include <QList>
#include <memory>

#include <QCoro/QCoroTask>

QT_BEGIN_NAMESPACE
namespace Ui { class PaneSizing; }
QT_END_NAMESPACE

class AmazonCatalogApi;
class TreeSizingAsins;
class QStandardItemModel;
class QDoubleSpinBox;
class AbstractSizeCategory;

class PaneSizing : public QWidget
{
    Q_OBJECT
public:
    explicit PaneSizing(QWidget *parent = nullptr);
    ~PaneSizing();
    void setWorkingDir(const QDir &workingDir);

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

    void _ensureModel(const QDir &dir);
    void _refreshApi();
    void _populateSizeRangeCombos();
    void _tryGuessSizeRange();
    void _rebuildMeasurementForm();
    const AbstractSizeCategory* _currentCategory() const;
    QCoro::Task<void> _uploadSizeChart(QString marketplaceId, QString productType);
};

#endif
