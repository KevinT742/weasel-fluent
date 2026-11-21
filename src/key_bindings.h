#ifndef KEY_BINDINGS_H
#define KEY_BINDINGS_H

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <Qt>

/// 可绑定的动作 id。
namespace KeyActions {
extern const char* ToggleAscii;    ///< 中英文切换
extern const char* Commit;         ///< 上屏首选词
extern const char* PagePrev;       ///< 候选翻页（上一页）
extern const char* PageNext;       ///< 候选翻页（下一页）
extern const char* ExpandMatrix;   ///< 展开多候选矩阵
extern const char* EmojiPanel;     ///< 打开表情面板
extern const char* ExtensionPanel; ///< 打开扩展面板

/// 中英文切换键当前生效的规格串。
///
/// 显式绑定（config.json 的 key_bindings 表）优先；没有显式绑定时按旧
/// switch_key 字段推导；两者都没有则用内置默认（左 Shift）。
/// 返回空串表示「禁用单键切换」。
QString effectiveAsciiSwitchSpec();

/// 旧 switch_key 字段值 → 规格串（"Shift" / "RightShift" / "Ctrl" / "None"）。
QString asciiSwitchSpecFromLegacy(const QString& legacy);

/// 拆掉规格串的左右侧后缀："Shift_L" → base="Shift"、isLeft=true。
/// 返回 true 表示原串带后缀。
bool splitSideSuffix(const QString& name, QString* base, bool* isLeft);
} // namespace KeyActions

/// 快捷键绑定表。持久化在 config.json 的 key_bindings 字段。
///
/// 键规格串形如 "Ctrl+Shift+M"、"Shift"、"Space"、"Comma"。
class KeyBindings : public QObject {
    Q_OBJECT
public:
    static KeyBindings& instance();

    /// 所有可绑定动作 id。
    QStringList actions() const;

    /// 取动作对应的键规格（空串表示未绑定）。
    QString binding(const QString& action) const;

    /// 绑定表里是否**显式**设置过这个动作（设成空串也算，即「禁用」）。
    bool hasExplicitBinding(const QString& action) const;

    /// 绑定表里的原始值（没显式设置过时返回空串）。
    QString explicitBinding(const QString& action) const;

    /// 内置默认绑定（没显式设置时用这个）。
    QString defaultBinding(const QString& action) const;

    /// 设置动作对应的键规格，并立即落盘。
    void setBinding(const QString& action, const QString& spec);

    /// 动作的中文显示名。
    QString humanName(const QString& action) const;

    /// 把键码 + 修饰键格式化为规格串，如 "Ctrl+Shift+M"。
    static QString formatSpec(int key, Qt::KeyboardModifiers mods);

    /// 解析规格串，失败返回 false。
    static bool parseSpec(const QString& spec, int* key, Qt::KeyboardModifiers* mods);

    /// 判断给定的键事件是否命中该规格（忽略 CapsLock 等无关修饰位）。
    static bool specMatches(const QString& spec, int key, Qt::KeyboardModifiers mods);

    /// 把 config.json 中读到的绑定表灌入本对象。
    void loadFrom(const QHash<QString, QString>& bindings);

private:
    QHash<QString, QString> m_bindings;
};

#endif // KEY_BINDINGS_H
