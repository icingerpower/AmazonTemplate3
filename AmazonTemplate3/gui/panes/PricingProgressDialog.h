#pragma once
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDialog>
#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <memory>

// Keep the log visible during cancellation; Close becomes available on unwind.
class PricingProgressDialog : public QDialog {
  public:
    PricingProgressDialog(QWidget *parent, std::shared_ptr<bool> cancelled, bool updating = false)
        : QDialog(parent), m_updating(updating), m_cancelled(std::move(cancelled)) {
        setObjectName("PricingProgressDialog");
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowTitle(updating ? tr("Updating Amazon prices") : tr("Retrieving pricing data"));
        resize(780, 460);
        auto *layout = new QVBoxLayout(this);
        m_status = new QLabel(updating ? tr("Starting price updates…") : tr("Starting retrieval…"), this);
        m_status->setObjectName("retrievalStatus");
        m_status->setWordWrap(true);
        m_bar = new QProgressBar(this);
        m_bar->setObjectName("retrievalProgress");
        m_bar->setRange(0, 0);
        m_time = new QLabel(this);
        m_log = new QTextEdit(this);
        m_log->setObjectName("retrievalLog");
        m_log->setReadOnly(true);
        layout->addWidget(m_status);
        layout->addWidget(m_bar);
        layout->addWidget(m_time);
        layout->addWidget(m_log);
        auto *buttons = new QHBoxLayout;
        auto *copy = new QPushButton(tr("Copy log"), this);
        m_button = new QPushButton(tr("Cancel"), this);
        m_button->setObjectName("retrievalCancelClose");
        buttons->addWidget(copy);
        buttons->addStretch();
        buttons->addWidget(m_button);
        layout->addLayout(buttons);
        connect(copy, &QPushButton::clicked, this,
                [this] { QApplication::clipboard()->setText(m_log->toPlainText()); });
        connect(m_button, &QPushButton::clicked, this, [this] {
            if (m_running)
                cancel();
            else
                close();
        });
        m_elapsed.start();
        m_activity.start();
        connect(&m_timer, &QTimer::timeout, this, [this] {
            m_time->setText(tr("Elapsed: %1m %2s · Last activity: %3s ago")
                                .arg(m_elapsed.elapsed() / 60000)
                                .arg(m_elapsed.elapsed() / 1000 % 60)
                                .arg(m_activity.elapsed() / 1000));
        });
        m_timer.start(1000);
    }
    void appendLog(const QString &text) {
        m_activity.restart();
        m_log->append(text.toHtmlEscaped());
    }
    void progress(const QString &phase, int done, int total, const QString &detail) {
        m_activity.restart();
        if (*m_cancelled)
            return;
        m_status->setText(total > 0 ? tr("%1: %2 / %3 — %4").arg(phase).arg(done).arg(total).arg(detail)
                                    : phase + " — " + detail);
        m_bar->setRange(0, total > 0 ? total : 0);
        if (total > 0)
            m_bar->setValue(done);
    }
    void finish(const QString &message, bool completed) {
        m_running = false;
        m_timer.stop();
        m_status->setText(message);
        appendLog(message);
        m_bar->setRange(0, 1);
        m_bar->setValue(completed ? 1 : 0);
        m_button->setText(tr("Close"));
        m_button->setEnabled(true);
    }
    void reject() override {
        if (m_running)
            cancel();
        else
            QDialog::reject();
    }

  protected:
    void closeEvent(QCloseEvent *event) override {
        if (m_running) {
            cancel();
            event->ignore();
        } else
            QDialog::closeEvent(event);
    }

  private:
    void cancel() {
        if (*m_cancelled)
            return;
        *m_cancelled = true;
        const QString message = m_updating ? tr("Cancellation requested. Stopping after the current "
                                                "operation; changes already sent cannot be undone.")
                                           : tr("Cancelling… Completed cache records are kept.");
        m_status->setText(message);
        appendLog(message);
        m_button->setText(tr("Cancelling…"));
        m_button->setEnabled(false);
    }
    bool m_running = true;
    bool m_updating = false;
    std::shared_ptr<bool> m_cancelled;
    QElapsedTimer m_elapsed, m_activity;
    QTimer m_timer;
    QLabel *m_status, *m_time;
    QProgressBar *m_bar;
    QTextEdit *m_log;
    QPushButton *m_button;
};
