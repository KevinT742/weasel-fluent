#ifndef RIME_ENGINE_H
#define RIME_ENGINE_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QLibrary>
#include <cstdint>

#include "rime_ui_state.h"
#include "ui_engine.h"

/// 过渡实现：自研的 librime 封装（设计文档 R6）。
///
/// ⚠️ 它只活到 **T7**：服务进程改用 `WeaselEngine`、设置中心改走 Weasel IPC 之后，
/// 这个类连同 `src/ipc_server.*`、旧 `tsf/` 一起整体删除。
/// 在此之前，它实现与 `WeaselEngine` 相同的 `UiEngine` 接口，
/// 因此 UI 层不需要为切换做两套代码。
class RimeEngine : public UiEngine {
    Q_OBJECT

public:
    explicit RimeEngine(QObject* parent = nullptr);
    ~RimeEngine() override;

    bool initialize(const QString& dllPath = QString(),
                    const QString& sharedDir = QString(),
                    const QString& userDir = QString());

    bool isLoaded() const { return m_isLoaded; }

    // ── UiEngine ───────────────────────────────────────────────────────
    RimeUiState uiState() const override;

    bool processKey(int keyCode, int mask = 0) override;
    bool processChar(QChar ch) override;
    bool selectCandidate(int index) override;
    void commitDirectText(const QString& text) override;

    bool isAsciiMode() const override { return m_asciiMode; }
    void setAsciiMode(bool ascii) override;
    bool isSimplified() const override { return m_simplified; }
    void setSimplified(bool simp) override;
    bool isFullShape() const override { return m_fullShape; }
    void setFullShape(bool full) override;

    bool selectSchema(const QString& schemaId) override;
    QString currentSchema() const override { return m_currentSchema; }
    bool deploy() override;

    RimeUiState getUiState() const;
    QString getCommitText();
    void clear();

    /// 全量重建：RimeStartMaintenance(RimeMaintenanceTypeFullDeploy) + JoinMaintenanceThread。
    /// 与 deploy() 的区别：deploy() 只重载已编译产物（不重编译词库），
    /// deployFull() 会真正重新编译方案与词库。
    bool deployFull(QString* error = nullptr);

signals:
    void modeChanged(bool isAsciiMode);

private:
    bool initRimeApi();
    void updateState();

    // Rime C API structures
    struct RimeTraits {
        int data_size;
        const char* shared_data_dir;
        const char* user_data_dir;
        const char* distribution_name;
        const char* distribution_code_name;
        const char* distribution_version;
        const char* app_name;
        const char** modules;
        int min_log_level;
        const char* log_dir;
        const char* prebuilt_data_dir;
        const char* staging_dir;
    };

    struct RimeComposition {
        int length;
        int cursor_pos;
        int sel_start;
        int sel_end;
        const char* preedit;
    };

    struct RimeCandidate {
        const char* text;
        const char* comment;
        void* reserved;
    };

    struct RimeMenu {
        int page_size;
        int page_no;
        int is_last_page;
        int highlighted_candidate_index;
        int num_candidates;
        RimeCandidate* candidates;
        const char* select_keys;
    };

    struct RimeContext {
        int data_size;
        RimeComposition composition;
        RimeMenu menu;
        const char* commit_text_preview;
        const char** select_labels;
    };

    struct RimeCommit {
        int data_size;
        const char* text;
    };

    struct RimeCandidateListIterator {
        void* ptr;
        int index;
        RimeCandidate candidate;
    };

    using FnRimeSetup = void (*)(RimeTraits*);
    using FnRimeInitialize = void (*)(RimeTraits*);
    using FnRimeStartMaintenance = int (*)(int);
    using FnRimeJoinMaintenanceThread = void (*)();
    using FnRimeCreateSession = uintptr_t (*)();
    using FnRimeDestroySession = int (*)(uintptr_t);
    using FnRimeProcessKey = int (*)(uintptr_t, int, int);
    using FnRimeGetContext = int (*)(uintptr_t, RimeContext*);
    using FnRimeFreeContext = int (*)(RimeContext*);
    using FnRimeGetCommit = int (*)(uintptr_t, RimeCommit*);
    using FnRimeFreeCommit = int (*)(RimeCommit*);
    using FnRimeSelectCandidate = int (*)(uintptr_t, size_t);
    using FnRimeCandidateListBegin = int (*)(uintptr_t, RimeCandidateListIterator*);
    using FnRimeCandidateListNext = int (*)(RimeCandidateListIterator*);
    using FnRimeCandidateListEnd = void (*)(RimeCandidateListIterator*);
    using FnRimeDeployWorkspace = int (*)();
    using FnRimeGetOption = int (*)(uintptr_t, const char*);
    using FnRimeSetOption = void (*)(uintptr_t, const char*, int);
    using FnRimeSelectSchema = int (*)(uintptr_t, const char*);

    QLibrary m_lib;
    bool m_isLoaded = false;
    uintptr_t m_sessionId = 0;

    FnRimeSetup m_fnSetup = nullptr;
    FnRimeInitialize m_fnInitialize = nullptr;
    FnRimeStartMaintenance m_fnStartMaintenance = nullptr;
    FnRimeJoinMaintenanceThread m_fnJoinMaintenanceThread = nullptr;
    FnRimeCreateSession m_fnCreateSession = nullptr;
    FnRimeDestroySession m_fnDestroySession = nullptr;
    FnRimeProcessKey m_fnProcessKey = nullptr;
    FnRimeGetContext m_fnGetContext = nullptr;
    FnRimeFreeContext m_fnFreeContext = nullptr;
    FnRimeGetCommit m_fnGetCommit = nullptr;
    FnRimeFreeCommit m_fnFreeCommit = nullptr;
    FnRimeSelectCandidate m_fnSelectCandidate = nullptr;
    FnRimeCandidateListBegin m_fnCandidateListBegin = nullptr;
    FnRimeCandidateListNext m_fnCandidateListNext = nullptr;
    FnRimeCandidateListEnd m_fnCandidateListEnd = nullptr;
    FnRimeDeployWorkspace m_fnDeployWorkspace = nullptr;
    FnRimeGetOption m_fnGetOption = nullptr;
    FnRimeSetOption m_fnSetOption = nullptr;
    FnRimeSelectSchema m_fnSelectSchema = nullptr;

    QByteArray m_sharedDirBytes;
    QByteArray m_userDirBytes;
    QByteArray m_appNameBytes;

    // Simulation / Standalone state
    bool m_asciiMode = false;
    bool m_fullShape = false;
    bool m_simplified = true;
    QString m_currentSchema = QStringLiteral("luna_pinyin");
    QString m_simInput;
    int m_simHighlight = 0;
};

#endif // RIME_ENGINE_H
