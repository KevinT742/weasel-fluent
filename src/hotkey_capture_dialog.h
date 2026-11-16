#ifndef HOTKEY_CAPTURE_DIALOG_H
#define HOTKEY_CAPTURE_DIALOG_H

#include <QDialog>
#include <QString>

/// 模态按键捕获对话框：用户按下组合键后确认或取消。
class HotkeyCaptureDialog : public QDialog {
    Q_OBJECT
public:
    explicit HotkeyCaptureDialog(const QString& currentSpec, QWidget* parent = nullptr);

    /// 捕获到的键规格，形如 "Ctrl+Shift+M"；取消时为空串。
    QString capturedSpec() const { return m_spec; }

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void updateValueLabel();

    QString m_spec;
    QString m_current;
};

#endif // HOTKEY_CAPTURE_DIALOG_H
