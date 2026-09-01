#pragma once

#include <QDialog>
#include <QString>

#include <memory>

namespace Ui { class AboutDialog; }

// Who made this, which version it is, and which engine plugins were found.
class AboutDialog : public QDialog
{
    Q_OBJECT

public:
    // The version comes from the caller for the same reason the window title
    // does: the generated header stays out of everything else's compile graph.
    explicit AboutDialog(const QString &version, QWidget *parent = nullptr);
    ~AboutDialog() override;

private:
    std::unique_ptr<Ui::AboutDialog> m_ui;
};
