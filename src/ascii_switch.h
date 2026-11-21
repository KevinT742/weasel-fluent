#ifndef ASCII_SWITCH_H
#define ASCII_SWITCH_H

#include <QSet>
#include <QtGlobal>

/// 「中英文切换键」的判定：纯逻辑，不碰引擎，可以直接单测。
///
/// 为什么单独拎出来：判定必须按 ibus 的键码与掩码语义来做（TIP 送上来的
/// 键事件就是那个形状），而这些语义又必须和
/// `3rdparty/weasel/include/KeyEvent.h` 对齐。放在独立模块里，自检套件
/// 才能直接喂事件、断言行为，而不是只能靠真机敲键盘验证。
namespace ascii_switch {

/// ibus 掩码位 —— 与 `3rdparty/weasel/include/KeyEvent.h` 的 `ibus::Modifier`
/// 一一对应。weasel_engine.cpp 里有 static_assert 保证两边不漂移。
constexpr quint32 kShiftMask   = 1u << 0;
constexpr quint32 kControlMask = 1u << 2;
constexpr quint32 kAltMask     = 1u << 3;
constexpr quint32 kReleaseMask = 1u << 14;

/// TIP 实际会产生哪些修饰位。
///
/// 上游 `ConvertKeyEvent` 只设 SHIFT / LOCK / CONTROL / ALT 四种，
/// LOCK 是 CapsLock 状态（与切换键无关），所以这里只留三个。
constexpr quint32 kModifierMask = kShiftMask | kControlMask | kAltMask;

/// 切换键的匹配条件。
struct Spec {
    QSet<quint32> keycodes;   ///< 命中的键码（ibus 键码）
    quint32 expectedMods = 0; ///< 按下时应当按着的修饰位
    bool isModifier = false;  ///< 切换键本身是修饰键（要「抬手」才算一次单击）
    bool valid = false;       ///< 规格有效
};

/// 判定器：把一串按键事件判成「要不要切中英文」。
///
/// 语义：**单击**切换键（按下 → 抬手，期间没有别的键）才切。
/// 这样 Shift+A 仍然是大写 A，不会误切中英。
class Detector {
public:
    void setSpec(const Spec& spec);

    const Spec& spec() const { return m_spec; }

    /// 喂一个按键事件。
    ///
    /// @param keycode      ibus 键码
    /// @param pressed      true = 按下，false = 抬手
    /// @param mods         该时刻按着的修饰位（ibus 掩码）
    /// @param currentAscii 该时刻的 ascii_mode 状态
    /// @param desired      若返回 true，这里给出切换后的目标 ascii_mode
    /// @return true 表示这一串事件构成一次干净的切换
    bool handle(quint32 keycode, bool pressed, quint32 mods, bool currentAscii, bool* desired);

    /// 丢弃待定状态（规格变了、或焦点切走了）。
    void reset();

private:
    Spec m_spec;
    quint32 m_pending = 0; ///< 待抬手的切换键（0 = 没有待定）
    bool m_before = false; ///< 按下瞬间的 ascii_mode 状态
};

} // namespace ascii_switch

#endif // ASCII_SWITCH_H
