#include "hotkey_capture_dialog.h"

#include "key_bindings.h"

#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QLabel>
#include <QVBoxLayout>

HotkeyCaptureDialog::HotkeyCaptureDialog(const QString& currentSpec, QWidget* parent)
    : QDialog(parent), m_current(currentSpec) {
    setWindowTitle(QStringLiteral("录制快捷键"));
    setModal(true);
    resize(380, 170);

    auto* layout = new QVBoxLayout(this);

    auto* hint = new QLabel(QStringLiteral("请按下新的组合键（Esc 取消）"), this);
    layout->addWidget(hint);

    m_spec = currentSpec;
    auto* value = new QLabel(currentSpec.isEmpty() ? QStringLiteral("（未绑定）") : currentSpec, this);
    value->setObjectName(QStringLiteral("capturedValue"));
    value->setAlignment(Qt::AlignCenter);
    value->setMinimumHeight(28);
    layout->addWidget(value);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void HotkeyCaptureDialog::updateValueLabel() {
    if (auto* lbl = findChild<QLabel*>(QStringLiteral("capturedValue"))) {
        lbl->setText(m_spec.isEmpty() ? QStringLiteral("（未绑定）") : m_spec);
    }
}

void HotkeyCaptureDialog::keyPressEvent(QKeyEvent* event) {
    const int key = event->key();

    if (key == Qt::Key_Escape) {
        m_spec.clear();
        QDialog::reject();
        return;
    }
    // 单独的修饰键按下不构成一次绑定，继续等待。
    if (key == Qt::Key_Shift || key == Qt::Key_Control ||
        key == Qt::Key_Alt || key == Qt::Key_Meta) {
        event->accept();
        return;
    }

    m_spec = KeyBindings::formatSpec(key, event->modifiers());
    updateValueLabel();
    event->accept();
}
