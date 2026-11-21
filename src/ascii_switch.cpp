#include "ascii_switch.h"

namespace ascii_switch {

void Detector::setSpec(const Spec& spec) {
    m_spec = spec;
    reset();
}

void Detector::reset() {
    m_pending = 0;
}

bool Detector::handle(quint32 keycode, bool pressed, quint32 mods, bool currentAscii, bool* desired) {
    if (desired) {
        *desired = currentAscii;
    }
    if (!m_spec.valid) {
        m_pending = 0;
        return false;
    }

    if (!m_spec.keycodes.contains(keycode)) {
        // 不是切换键。切换键正按着的时候按了别的键（Shift+A），
        // 说明这次是被当修饰键用的 —— 本次单击作废。
        if (pressed) {
            m_pending = 0;
        }
        return false;
    }

    if (pressed) {
        // 按下时多按了别的修饰键（Ctrl+Shift）就不算单独点了一下。
        if ((mods & ~m_spec.expectedMods) != 0) {
            m_pending = 0;
            return false;
        }
        if (m_spec.isModifier) {
            // 只在**第一次**按下时记状态：TIP 同一个物理按键可能送两遍
            // （OnTestKeyDown 与 OnKeyDown 各一次），第二遍到达时
            // ascii_mode 可能已经被 ascii_composer 的 inline_ascii 改了。
            if (m_pending == 0) {
                m_before = currentAscii;
            }
            m_pending = keycode;
            return false; // 等抬手再判定
        }
        m_pending = 0;
        if (desired) {
            *desired = !currentAscii;
        }
        return true; // 非修饰键：按下即切
    }

    // ── 抬手 ────────────────────────────────────────────────────────────
    if (!m_spec.isModifier) {
        return false;
    }
    // 抬手时还按着别的修饰键 ⇒ 不是干净的单击。
    if ((mods & (kModifierMask & ~m_spec.expectedMods)) != 0) {
        m_pending = 0;
        return false;
    }
    if (m_pending != keycode) {
        m_pending = 0;
        return false;
    }
    m_pending = 0;
    // 用**按下瞬间**的状态取反，而不是抬手瞬间的：按住期间
    // ascii_composer 的 inline_ascii 会把 ascii_mode 临时置成 true，
    // 抬手时又恢复回去 —— 抬手瞬间读到的值不能代表用户的真实状态。
    if (desired) {
        *desired = !m_before;
    }
    return true;
}

} // namespace ascii_switch
