#include "key_bindings.h"

#include "config.h"

#include <QHash>

namespace KeyActions {
const char* ToggleAscii    = "toggle_ascii";
const char* Commit         = "commit";
const char* PagePrev       = "page_prev";
const char* PageNext       = "page_next";
const char* ExpandMatrix   = "expand_matrix";
const char* EmojiPanel     = "emoji_panel";
const char* ExtensionPanel = "extension_panel";
} // namespace KeyActions

namespace {

/// 默认绑定。取值与旧版 switchKey / pageKeys 预设一致，
/// 因此旧配置的用户行为不会改变。
QHash<QString, QString> defaultBindings() {
    QHash<QString, QString> h;
    h.insert(QString::fromLatin1(KeyActions::ToggleAscii),    QStringLiteral("Shift"));
    h.insert(QString::fromLatin1(KeyActions::Commit),         QStringLiteral("Space"));
    h.insert(QString::fromLatin1(KeyActions::PagePrev),       QStringLiteral("Comma"));
    h.insert(QString::fromLatin1(KeyActions::PageNext),       QStringLiteral("Period"));
    h.insert(QString::fromLatin1(KeyActions::ExpandMatrix),   QStringLiteral("Tab"));
    h.insert(QString::fromLatin1(KeyActions::EmojiPanel),     QString());
    h.insert(QString::fromLatin1(KeyActions::ExtensionPanel), QString());
    return h;
}

/// 可打印键 -> 规格名。
QString keyName(int key) {
    if (key <= 0) {
        return QString();
    }
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        return QString(QChar(key)).toLower();
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        return QString(QChar(key));
    }
    switch (key) {
    case Qt::Key_Space:        return QStringLiteral("Space");
    case Qt::Key_Tab:          return QStringLiteral("Tab");
    case Qt::Key_Backtab:      return QStringLiteral("Backtab");
    case Qt::Key_Shift:        return QStringLiteral("Shift");
    case Qt::Key_Control:      return QStringLiteral("Ctrl");
    case Qt::Key_Alt:          return QStringLiteral("Alt");
    case Qt::Key_Meta:         return QStringLiteral("Win");
    case Qt::Key_Comma:        return QStringLiteral("Comma");
    case Qt::Key_Period:       return QStringLiteral("Period");
    case Qt::Key_Semicolon:    return QStringLiteral("Semicolon");
    case Qt::Key_QuoteLeft:    return QStringLiteral("QuoteLeft");
    case Qt::Key_Apostrophe:   return QStringLiteral("QuoteLeft");
    case Qt::Key_BracketLeft:  return QStringLiteral("BracketLeft");
    case Qt::Key_BracketRight: return QStringLiteral("BracketRight");
    case Qt::Key_BraceLeft:    return QStringLiteral("BracketLeft");
    case Qt::Key_BraceRight:   return QStringLiteral("BracketRight");
    case Qt::Key_Minus:        return QStringLiteral("Minus");
    case Qt::Key_Equal:        return QStringLiteral("Equal");
    case Qt::Key_Slash:        return QStringLiteral("Slash");
    case Qt::Key_Backslash:    return QStringLiteral("Backslash");
    case Qt::Key_Up:           return QStringLiteral("Up");
    case Qt::Key_Down:         return QStringLiteral("Down");
    case Qt::Key_Left:         return QStringLiteral("Left");
    case Qt::Key_Right:        return QStringLiteral("Right");
    case Qt::Key_PageUp:       return QStringLiteral("PageUp");
    case Qt::Key_PageDown:     return QStringLiteral("PageDown");
    case Qt::Key_Home:         return QStringLiteral("Home");
    case Qt::Key_End:          return QStringLiteral("End");
    case Qt::Key_Escape:       return QStringLiteral("Escape");
    case Qt::Key_Return:       return QStringLiteral("Return");
    case Qt::Key_Enter:        return QStringLiteral("Return");
    case Qt::Key_Backspace:    return QStringLiteral("Backspace");
    case Qt::Key_Delete:       return QStringLiteral("Delete");
    case Qt::Key_Insert:       return QStringLiteral("Insert");
    default: return QString();
    }
}

/// 规格名 -> 键码。
int keyFromName(const QString& name) {
    const QString n = name.trimmed().toLower();
    if (n.isEmpty()) {
        return 0;
    }
    if (n.size() == 1) {
        const QChar c = n.at(0);
        if (c.isLetter() && c.isLower()) {
            return c.toUpper().unicode();
        }
        if (c.isDigit()) {
            return c.unicode();
        }
    }
    static const QHash<QString, int> map = {
        {QStringLiteral("space"), Qt::Key_Space},   {QStringLiteral("tab"), Qt::Key_Tab},
        {QStringLiteral("backtab"), Qt::Key_Backtab}, {QStringLiteral("shift"), Qt::Key_Shift},
        {QStringLiteral("ctrl"), Qt::Key_Control},  {QStringLiteral("alt"), Qt::Key_Alt},
        {QStringLiteral("win"), Qt::Key_Meta},       {QStringLiteral("comma"), Qt::Key_Comma},
        {QStringLiteral("period"), Qt::Key_Period}, {QStringLiteral("semicolon"), Qt::Key_Semicolon},
        {QStringLiteral("quoteleft"), Qt::Key_QuoteLeft},
        {QStringLiteral("bracketleft"), Qt::Key_BracketLeft},
        {QStringLiteral("bracketright"), Qt::Key_BracketRight},
        {QStringLiteral("minus"), Qt::Key_Minus},  {QStringLiteral("equal"), Qt::Key_Equal},
        {QStringLiteral("slash"), Qt::Key_Slash},  {QStringLiteral("backslash"), Qt::Key_Backslash},
        {QStringLiteral("up"), Qt::Key_Up},        {QStringLiteral("down"), Qt::Key_Down},
        {QStringLiteral("left"), Qt::Key_Left},    {QStringLiteral("right"), Qt::Key_Right},
        {QStringLiteral("pageup"), Qt::Key_PageUp}, {QStringLiteral("pagedown"), Qt::Key_PageDown},
        {QStringLiteral("home"), Qt::Key_Home},    {QStringLiteral("end"), Qt::Key_End},
        {QStringLiteral("escape"), Qt::Key_Escape}, {QStringLiteral("return"), Qt::Key_Return},
        {QStringLiteral("backspace"), Qt::Key_Backspace}, {QStringLiteral("delete"), Qt::Key_Delete},
        {QStringLiteral("insert"), Qt::Key_Insert},
    };
    return map.value(n, 0);
}

} // namespace

KeyBindings& KeyBindings::instance() {
    static KeyBindings s_inst;
    return s_inst;
}

void KeyBindings::loadFrom(const QHash<QString, QString>& bindings) {
    m_bindings = bindings;
}

QStringList KeyBindings::actions() const {
    return QStringList{
        QString::fromLatin1(KeyActions::ToggleAscii),
        QString::fromLatin1(KeyActions::Commit),
        QString::fromLatin1(KeyActions::PagePrev),
        QString::fromLatin1(KeyActions::PageNext),
        QString::fromLatin1(KeyActions::ExpandMatrix),
        QString::fromLatin1(KeyActions::EmojiPanel),
        QString::fromLatin1(KeyActions::ExtensionPanel),
    };
}

QString KeyBindings::binding(const QString& action) const {
    const QString v = m_bindings.value(action);
    if (!v.isEmpty()) {
        return v;
    }
    return defaultBindings().value(action);
}

void KeyBindings::setBinding(const QString& action, const QString& spec) {
    m_bindings.insert(action, spec);
    AppConfig::instance().setKeyBindings(m_bindings);
}

QString KeyBindings::humanName(const QString& action) const {
    if (action == QLatin1String(KeyActions::ToggleAscii))    return QStringLiteral("中英文切换");
    if (action == QLatin1String(KeyActions::Commit))         return QStringLiteral("上屏首选词");
    if (action == QLatin1String(KeyActions::PagePrev))       return QStringLiteral("候选翻页（上一页）");
    if (action == QLatin1String(KeyActions::PageNext))       return QStringLiteral("候选翻页（下一页）");
    if (action == QLatin1String(KeyActions::ExpandMatrix))   return QStringLiteral("展开多候选矩阵");
    if (action == QLatin1String(KeyActions::EmojiPanel))     return QStringLiteral("打开表情面板");
    if (action == QLatin1String(KeyActions::ExtensionPanel)) return QStringLiteral("打开扩展面板");
    return action;
}

QString KeyBindings::formatSpec(int key, Qt::KeyboardModifiers mods) {
    QStringList parts;
    if (mods & Qt::ControlModifier) parts << QStringLiteral("Ctrl");
    if (mods & Qt::AltModifier)     parts << QStringLiteral("Alt");
    if (mods & Qt::ShiftModifier)   parts << QStringLiteral("Shift");
    if (mods & Qt::MetaModifier)    parts << QStringLiteral("Win");
    const QString k = keyName(key);
    if (!k.isEmpty()) {
        parts << k;
    }
    return parts.join(QLatin1Char('+'));
}

bool KeyBindings::parseSpec(const QString& spec, int* key, Qt::KeyboardModifiers* mods) {
    if (spec.trimmed().isEmpty()) {
        return false;
    }
    const QStringList parts = spec.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    Qt::KeyboardModifiers m = Qt::NoModifier;
    int k = 0;
    for (const QString& raw : parts) {
        const QString p = raw.trimmed();
        const QString lower = p.toLower();
        if (lower == QLatin1String("ctrl"))  { m |= Qt::ControlModifier; continue; }
        if (lower == QLatin1String("alt"))   { m |= Qt::AltModifier; continue; }
        if (lower == QLatin1String("shift")) { m |= Qt::ShiftModifier; continue; }
        if (lower == QLatin1String("win"))   { m |= Qt::MetaModifier; continue; }
        k = keyFromName(p);
    }
    if (k == 0) {
        return false;
    }
    if (key)  *key = k;
    if (mods) *mods = m;
    return true;
}

bool KeyBindings::specMatches(const QString& spec, int key, Qt::KeyboardModifiers mods) {
    int specKey = 0;
    Qt::KeyboardModifiers specMods = Qt::NoModifier;
    if (!parseSpec(spec, &specKey, &specMods)) {
        return false;
    }
    if (specKey != key) {
        return false;
    }
    // 只比较有意义的修饰位，忽略 CapsLock / NumLock 等。
    const Qt::KeyboardModifiers relevant =
        Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier;
    return (mods & relevant) == (specMods & relevant);
}
