#ifndef WEASEL_FLUENT_LIBRIME_COMPAT_H
#define WEASEL_FLUENT_LIBRIME_COMPAT_H

// ---------------------------------------------------------------------------
// librime 的 C API 宏隔离
//
// librime 的 `rime_api.h`（deps/rime/dist/include/rime_api.h:45-52）用宏实现
// 了三个布尔量：
//
//     #ifndef Bool
//     #define Bool int
//     #endif
//     #ifndef False
//     #define False 0
//     #endif
//     #ifndef True
//     #define True 1
//     #endif
//
// 这三个名字在 C++ 里是地雷，最硬的雷是 Qt：
//
//   * `QVariant::Type` 枚举里有一个成员正好叫 `Bool`：
//         enum Type { Invalid = QMetaType::UnknownType,
//                      Bool    = QMetaType::Bool,     // ← 被宏替换成 int
//                      Int     = QMetaType::Int, ... };
//     `Bool` 宏先生效时，上面这行被预处理成
//         int = QMetaType::int,
//     MSVC 报 C2144（"int" 的前面应有 "}"）+ C2589（"::" 右边是非法标记），
//     错误位置却指向 qvariant.h:146 —— 离真正的病因十万八千里。
//
//   * 宏是「先定义先生效」的，所以**包含顺序决定会不会炸**：
//     「先 librime，后 Qt」必炸；「先 Qt，后 librime」侥幸没事。
//     本仓里 `main.cpp` 是先 Qt（<QApplication> 在第 1 行）后 librime
//     （weasel_engine.h 在第 41 行）⇒ 一直没出事；
//     而 `weasel_engine.cpp` 是先 weasel_engine.h（→ RimeWithWeasel.h →
//     rime_api.h）后 <QApplication> ⇒ 必炸。
//     这轮做语言栏时给 weasel_engine.cpp 加了 <QApplication>，当场就踩到了。
//
// 对策：在**引入 rime_api.h 之后立刻清掉这三个宏**。
//   * vendor 自己的 .cpp（3rdparty/weasel/RimeWithWeasel/*.cpp 等）直接 include
//     rime_api.h、不经过本文件 ⇒ 它们照常拿到 `Bool` 类型，行为不变；
//   * 我们的代码本来就不用 `Bool`/`True`/`False` 这三个名字（用 `bool`/`int`）。
//
// ⚠️ 必须在 rime_api.h **之后**包含本文件：
//    rime_api.h 自带 include guard（RIME_API_H_），第二次 include 是空操作，
//    所以清掉的宏不会「复活」；但若本文件先于 rime_api.h 被包含，
//    rime_api.h 里的 `#ifndef Bool` 又会把宏定义回来。
// ---------------------------------------------------------------------------

#ifdef Bool
#undef Bool
#endif

#ifdef False
#undef False
#endif

#ifdef True
#undef True
#endif

#endif // WEASEL_FLUENT_LIBRIME_COMPAT_H
