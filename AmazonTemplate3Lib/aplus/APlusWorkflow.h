#pragma once
#include <QString>
#include <QStringList>
#include <QList>

class APlusContent;

struct ImageSlotSpec {
    QString elementId;
    QString displayName;
    QString desktopPrompt;
    QString mobilePrompt;
};

class APlusWorkflow
{
public:
    virtual ~APlusWorkflow() = default;
    virtual QString id()   const = 0;
    virtual QString name() const = 0;
    virtual int     stepCount()        const = 0;
    virtual QString stepName(int step) const = 0;

    // Builds ordered image slot specs with full prompts ready for CLI dispatch.
    // content: existing APlusContent (for Generic workflow to read existing elements)
    // colors: all color variant names (empty for single-color products)
    // focusColor: the "current" color to prioritize first (may be empty)
    // productDesc: text from the attributes text edit
    // mainImageHint: hint about the local main image file (e.g. "A product photo is available...")
    // stepInstructions: user-entered instructions per step (index = step number)
    virtual QList<ImageSlotSpec> buildSlots(
        const APlusContent *content,
        const QStringList  &colors,
        const QString      &focusColor,
        const QString      &productDesc,
        const QString      &mainImageHint,
        const QStringList  &stepInstructions
    ) const = 0;

    static const QList<APlusWorkflow *> &all();
    static APlusWorkflow *findById(const QString &id);
};
