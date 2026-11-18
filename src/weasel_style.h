#ifndef WEASEL_STYLE_H
#define WEASEL_STYLE_H

#include <QString>

/// Weasel「内容层」样式 —— `weasel.yaml` → `weasel::UIStyle`。
///
/// 与「框架层」的 Fluent token 皮肤是**两套作用域**（设计文档 §6.4）：
///   * **Fluent token 覆盖文件** → 改的是**我们**渲染候选窗用的配色 / 圆角 / 字体；
///   * **`weasel.yaml`** → 改的是 **Weasel 引擎侧**的样式键（字号、字体、横竖排、
///     行内预编辑……），由服务端 `LoadWeaselUIStyle()` 读取后生效。
///
/// 设置中心把两者分栏展示，避免用户改了 A 却以为 B 也会变。
struct WeaselStyle {
    QString fontFace;                            ///< style/font_face
    int fontPoint = 12;                          ///< style/font_point
    int labelFontPoint = 12;                     ///< style/label_font_point
    int commentFontPoint = 12;                   ///< style/comment_font_point
    bool horizontal = true;                      ///< style/horizontal（false = 竖排）
    bool inlinePreedit = false;                  ///< style/inline_preedit
    QString preeditType = QStringLiteral("composition");  ///< style/preedit_type
    QString markText;                            ///< style/mark_text
    int candidateAbbreviateLength = 0;           ///< style/candidate_abbreviate_length
    bool displayTrayIcon = true;                 ///< style/display_tray_icon
};

/// 读写用户 Rime 目录下的 `weasel.yaml`。
///
/// 只管理上面列出的那几个键，**其余内容原样保留**（用户可能自己写了别的样式键）。
class WeaselStyleManager {
public:
    static WeaselStyleManager& instance();

    /// `weasel.yaml` 的完整路径（用户 Rime 目录）。
    static QString styleFilePath();

    /// 读取当前样式（文件不存在时返回默认值）。
    WeaselStyle load(QString* error = nullptr) const;

    /// 写回 `weasel.yaml`（只改我们管理的键）。
    bool save(const WeaselStyle& style, QString* error = nullptr);

    /// 供测试使用：在给定 yaml 文本里解析我们管理的键。
    static WeaselStyle parse(const QString& yamlText);

    /// 供测试使用：把给定 yaml 文本里的我们管理的键替换掉。
    static QString rewrite(const QString& yamlText, const WeaselStyle& style);
};

#endif // WEASEL_STYLE_H
