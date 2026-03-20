#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <shobjidl.h> // Untuk AppUserModelID (Taskbar Fix)

#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QRadioButton>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QTextEdit>
#include <QCheckBox>
#include <QKeySequenceEdit>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QDir>
#include <QCoreApplication>
#include <QSettings>
#include <QClipboard>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>
#include <QProcess>
#include <QScreen>
#include <QGuiApplication>
#include <QPointer>
#include <QMutex>
#include <QSharedMemory>
#include <QElapsedTimer>
#include <QMimeData>
#include <QThread>
#include <QToolTip>
#include <QWindow>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>

#include <vector>
#include <memory>
#include <mutex>
#include <functional>

// --- [CIEL V5] STEALTH & HOOK GLOBALS ---
std::mutex g_bufferMutex;
std::wstring g_smartBuffer;
QPointer<QMainWindow> g_mainWindow;
QPointer<QWidget> g_currentHUD;
HHOOK g_kbdHook = nullptr;
HHOOK g_mouseHook = nullptr;

bool g_stealthMode = false;
bool g_swallowNextEnterUp = false;
bool g_swallowWriterUp = false; 
bool g_swallowReaderUp = false; 

UINT g_modWriter = MOD_ALT;
UINT g_vkWriter = 0x57; // W
UINT g_modReader = MOD_ALT;
UINT g_vkReader = 0x51; // Q
// ----------------------------------------

//smart path
QString getDir(const QString& folderName) {
    QDir dir(QCoreApplication::applicationDirPath());
    if (dir.exists(folderName)) return dir.absoluteFilePath(folderName) + '/';
    dir.cdUp();
    if (dir.exists(folderName)) return dir.absoluteFilePath(folderName) + '/';
    QString fallback = QCoreApplication::applicationDirPath() + '/' + folderName + '/';
    QDir().mkpath(fallback);
    return fallback;
}

//sql
bool initDatabase() {
    QString dbPath = getDir("Data") + "memory.db";
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName(dbPath);
    if (!db.open()) return false;
    
    QSqlQuery query;
    query.exec("PRAGMA journal_mode=WAL;");
    query.exec("PRAGMA synchronous=NORMAL;");
    query.exec("PRAGMA temp_store=MEMORY;");

    query.exec("CREATE TABLE IF NOT EXISTS translations ("
               "id INTEGER PRIMARY KEY AUTOINCREMENT, "
               "engine TEXT, source_lang TEXT, target_lang TEXT, "
               "original_text TEXT, translated_text TEXT, "
               "UNIQUE(engine, source_lang, target_lang, original_text))");
               
    query.exec("CREATE INDEX IF NOT EXISTS idx_trans_lookup ON translations(engine, target_lang, original_text);");
    return true;
}

//guesser off
QString detectOfflineLanguage(const QString& text, const QString& fallbackLang) {
    if (text.contains(QRegularExpression("[\\x{3040}-\\x{309F}\\x{30A0}-\\x{30FF}\\x{4E00}-\\x{9FAF}]"))) return "ja";
    QString lowerText = text.toLower();
    
    QRegularExpression enRegex("\\b(the|is|are|you|i|to|a|it|dont|do|what|how|why|my|me|this|that|in|on|at|for|of|and|with|but|bro|yes|no)\\b");
    QRegularExpression idRegex("\\b(yang|di|ke|dari|ini|itu|dan|untuk|dengan|gabisa|nggak|gw|lu|aku|kamu|bisa|ada|apa|kenapa|gimana|aja|saja|udah|belum|tapi|karena|anjir|bang)\\b");

    int enCount = 0; QRegularExpressionMatchIterator enIt = enRegex.globalMatch(lowerText);
    while (enIt.hasNext()) { enIt.next(); enCount++; }
    int idCount = 0; QRegularExpressionMatchIterator idIt = idRegex.globalMatch(lowerText);
    while (idIt.hasNext()) { idIt.next(); idCount++; }

    if (enCount > idCount) return "en";
    if (idCount > enCount) return "id";
    return fallbackLang; 
}

//bridge argos
void ensureArgosBridge() {
    QString bridgePath = getDir("Data") + "argos_bridge.py";
    QFile file(bridgePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << "import sys, json, os, traceback\n"
               "def send_prog(msg):\n"
               "    print(json.dumps({'progress': msg}))\n"
               "    sys.stdout.flush()\n"
               "try:\n"
               "    models_dir = sys.argv[1]\n"
               "    pkg_dir = os.path.join(models_dir, 'packages')\n"
               "    cache_dir = os.path.join(models_dir, 'cache')\n"
               "    os.makedirs(pkg_dir, exist_ok=True)\n"
               "    os.makedirs(cache_dir, exist_ok=True)\n"
               "    os.environ['ARGOS_PACKAGES_DIR'] = pkg_dir\n"
               "    os.environ['XDG_DATA_HOME'] = cache_dir\n"
               "    os.environ['XDG_CACHE_HOME'] = cache_dir\n"
               "except Exception: pass\n\n"
               "def main():\n"
               "    try:\n"
               "        import argostranslate.package, argostranslate.translate\n"
               "    except ImportError:\n"
               "        print(json.dumps({'error': 'Argos NMT is not installed. Run: pip install argostranslate'}))\n"
               "        return\n\n"
               "    try:\n"
               "        req = json.loads(sys.stdin.read())\n"
               "        action = req.get('action', 'translate')\n\n"
               
               "        if action == 'list':\n"
               "            installed = argostranslate.translate.get_installed_languages()\n"
               "            routes =[]\n"
               "            for lang in installed:\n"
               "                for target in lang.translations_from:\n"
               "                    routes.append(f'{lang.code} ➔ {target.to_lang.code}')\n"
               "            print(json.dumps({'list_result': routes}))\n"
               "            return\n\n"
               
               "        if action == 'install':\n"
               "            f_code = req.get('from', '')\n"
               "            t_code = req.get('to', '')\n"
               "            send_prog('Updating index from Argos server...')\n"
               "            try:\n"
               "                argostranslate.package.update_package_index()\n"
               "                available = argostranslate.package.get_available_packages()\n"
               "                pkg = next((p for p in available if p.from_code == f_code and p.to_code == t_code), None)\n"
               "                if pkg:\n"
               "                    send_prog(f'Downloading {f_code} ➔ {t_code} model (~30MB). Please wait...')\n"
               "                    path = pkg.download()\n"
               "                    send_prog('Installing model to database...')\n"
               "                    argostranslate.package.install_from_path(path)\n"
               "                    try:\n"
               "                        os.remove(path)\n"
               "                        send_prog('Cleaning up installer cache (Zero Bloat)...')\n"
               "                    except Exception: pass\n"
               "                    print(json.dumps({'result': f'SUCCESS! Model {f_code} ➔ {t_code} installed.', 'trigger_list': True}))\n"
               "                else:\n"
               "                    print(json.dumps({'error': f'Model {f_code} ➔ {t_code} does not exist on Argos server.'}))\n"
               "            except Exception as e:\n"
               "                print(json.dumps({'error': f'Download failed. ISP Blocked? {str(e)}'}))\n"
               "            return\n\n"

               "        text, from_code, to_code = req.get('text', ''), req.get('from', ''), req.get('to', '')\n"
               "        def get_lang(code):\n"
               "            installed = argostranslate.translate.get_installed_languages()\n"
               "            return next((l for l in installed if l.code == code), None)\n\n"
               
               "        from_lang = get_lang(from_code)\n"
               "        to_lang = get_lang(to_code)\n\n"
               
               "        if not from_lang or not to_lang:\n"
               "            print(json.dumps({'error': f'Model {from_code} ➔ {to_code} is MISSING. Please use Model Manager to download it!'}))\n"
               "            return\n\n"
               
               "        translation = from_lang.get_translation(to_lang)\n"
               "        if translation:\n"
               "            res = translation.translate(text)\n"
               "        else:\n"
               "            en_lang = get_lang('en')\n"
               "            if en_lang:\n"
               "                t1 = from_lang.get_translation(en_lang)\n"
               "                t2 = en_lang.get_translation(to_lang)\n"
               "                if t1 and t2:\n"
               "                    res = t2.translate(t1.translate(text))\n"
               "                else:\n"
               "                    err_msg = f'Pivot route broken! Missing model: '\n"
               "                    if not t1: err_msg += f'{from_code}➔en '\n"
               "                    if not t2: err_msg += f'en➔{to_code}'\n"
               "                    print(json.dumps({'error': err_msg.strip()}))\n"
               "                    return\n"
               "            else:\n"
               "                print(json.dumps({'error': 'Pivot language (en) is missing. Download it first!'}))\n"
               "                return\n\n"
               "        print(json.dumps({'result': res}))\n"
               "    except Exception as e:\n"
               "        print(json.dumps({'error': str(e), 'trace': traceback.format_exc()}))\n\n"
               "if __name__ == '__main__': main()\n";
        file.close();
    }
}

//engine aware
QString resolveEngineLanguage(const QString& engine, const QString& displayLang, const QString& customCode) {
    if (displayLang == "Custom...") return customCode.trimmed();
    if (engine == "Groq" || engine == "Gemini") return displayLang; 
    if (displayLang == "English") return "en";
    if (displayLang == "Indonesian") return "id";
    if (displayLang == "Japanese") return "ja";
    return displayLang;
}

//anti halu
QString humanizeText(QString text, const QString& origText, bool isWriter, bool enableSlangMode) {
    text = text.trimmed();
    QString orig = origText.trimmed();
    if (text.isEmpty()) return text;
    
    if (!orig.contains('/') && text.contains('/')) text = text.split('/').first().trimmed();
    if (text.contains('\n')) text = text.split('\n').first().trimmed();
    
    text = text.trimmed();

    if (orig.endsWith("?") && !text.endsWith("?")) text += "?";
    else if (orig.endsWith("!") && !text.endsWith("!")) text += "!";
    else if (orig.endsWith("...") && !text.endsWith("...")) text += "...";

    if (!isWriter) return text;

    if (enableSlangMode) {
        text = text.toLower();
        text.remove('\''); text.remove(QChar(0x2019)); text.remove(QChar(0x2018)); text.remove('"'); text.remove('`');
        text.replace(QRegularExpression("([,?!.]+)([a-zA-Z])"), "\\1 \\2");
        if (text.endsWith('.') && !text.endsWith("..")) text.chop(1);
        text = text.simplified();
    }
    
    return text + " ";
}

// --- [CIEL V5] WIN32 HARDWARE INPUT HELPERS ---
void HardwareKeyAction(WORD vkCode, bool release = false) {
    INPUT input = {}; input.type = INPUT_KEYBOARD;
    input.ki.wScan = MapVirtualKeyW(vkCode, MAPVK_VK_TO_VSC);
    input.ki.dwFlags = KEYEVENTF_SCANCODE | (release ? KEYEVENTF_KEYUP : 0);
    SendInput(1, &input, sizeof(INPUT));
}
void ReleaseModifiers() {
    struct Mod { WORD vk; };
    Mod mods[] = { {VK_LCONTROL}, {VK_RCONTROL}, {VK_LMENU}, {VK_RMENU}, {VK_LSHIFT}, {VK_RSHIFT} };
    for (auto& m : mods) { if (GetAsyncKeyState(m.vk) & 0x8000) HardwareKeyAction(m.vk, true); }
}
// ----------------------------------------------

const QString GLOBAL_STYLE = R"(
    * { font-family: 'Segoe UI Variable', 'Segoe UI'; }
    QToolTip { background-color: #121218; color: #00D9FF; border: 1px solid rgba(0, 217, 255, 0.4); border-radius: 4px; padding: 6px; font-size: 9pt; }
    QLabel { color: #DCDCE6; font-size: 10pt; }
    QLabel#Header { color: #00D9FF; font-weight: 700; font-size: 11pt; margin-top: 10px; margin-bottom: 2px;}
    QLabel#Subtext { color: #8A8A9E; font-size: 9pt; margin-bottom: 2px;}
    QRadioButton { color: #DCDCE6; font-size: 10pt; font-weight: 500; spacing: 8px; }
    QRadioButton::indicator { width: 16px; height: 16px; border-radius: 8px; border: 2px solid rgba(255,255,255,0.2); background: rgba(0,0,0,0.2); }
    QRadioButton::indicator:checked { background: #00D9FF; border: 2px solid #00A8FF; }
    
    QComboBox, QLineEdit, QTextEdit, QKeySequenceEdit {
        background-color: rgba(20, 20, 26, 180); color: #00D9FF;
        border: 1px solid rgba(0, 217, 255, 0.15); border-radius: 6px; 
        padding: 6px 10px; font-size: 10pt; min-height: 30px;
    }
    QComboBox::drop-down { border: none; width: 24px; }
    QComboBox QAbstractItemView { background-color: #121218; color: #00D9FF; selection-background-color: rgba(0, 217, 255, 0.2); outline: none; border: 1px solid rgba(255,255,255,0.1); }
    QLineEdit:disabled, QTextEdit:disabled, QComboBox:disabled, QKeySequenceEdit:disabled { background-color: rgba(30, 30, 35, 100); color: #666; border: 1px solid rgba(255, 255, 255, 0.05); }
    
    QPushButton { background-color: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #00A8FF, stop:1 #00D9FF); color: #111; font-weight: 800; font-size: 9.5pt; border: none; border-radius: 6px; min-height: 36px; }
    QPushButton:hover { background-color: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #33B8FF, stop:1 #33E5FF); }
    QPushButton#TitleBtn { background: transparent; color: white; min-height: 24px; }
    QPushButton#TitleBtn:hover { background-color: rgba(255, 71, 87, 0.8); }
    QPushButton#LiveBtnOff { background-color: rgba(255, 71, 87, 0.15); border: 1px solid rgba(255, 71, 87, 0.5); color: #FF4757; }
    QPushButton#LiveBtnOn { background-color: rgba(46, 213, 115, 0.15); border: 1px solid rgba(46, 213, 115, 0.5); color: #2ED573; }
    QPushButton#ActionBtn { background-color: rgba(255,255,255,0.05); color: #DCDCE6; font-weight: 600; border: 1px solid rgba(255,255,255,0.1); }
    QPushButton#ActionBtn:hover { background-color: rgba(255,255,255,0.1); }
    QCheckBox { color: #DCDCE6; font-size: 9.5pt; spacing: 8px; }
    QCheckBox::indicator { width: 16px; height: 16px; border-radius: 4px; border: 1px solid rgba(255,255,255,0.3); background: rgba(0,0,0,0.2); }
    QCheckBox::indicator:checked { background: #00D9FF; border: 1px solid #00D9FF; image: url(none); }
)";

class DiagnosticHUD : public QWidget {
    Q_OBJECT
public:
    static DiagnosticHUD* instance; QLabel* label; QTimer* closeTimer;
    DiagnosticHUD() {
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool); setAttribute(Qt::WA_TranslucentBackground); setAttribute(Qt::WA_TransparentForMouseEvents);
        QVBoxLayout* layout = new QVBoxLayout(this); layout->setContentsMargins(12, 12, 12, 12);
        label = new QLabel(""); label->setFont(QFont("Segoe UI", 10, QFont::Bold)); label->setWordWrap(true); label->setMaximumWidth(600); label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum); layout->addWidget(label); 
        closeTimer = new QTimer(this); closeTimer->setSingleShot(true); connect(closeTimer, &QTimer::timeout, this, &QWidget::hide);
    }
    static void showMessage(const QString& msg, const QString& color = "#00D9FF", int timeout = 4000) {
        if (!instance) instance = new DiagnosticHUD();
        instance->label->setText(msg); instance->label->setStyleSheet(QString("background-color: rgba(18, 18, 24, 230); color: %1; padding: 12px 16px; border-radius: 8px; border: 1px solid rgba(255,255,255,0.1);").arg(color)); instance->adjustSize(); 
        QScreen* primary = QGuiApplication::primaryScreen();
        if (primary) { QRect geom = primary->availableGeometry(); instance->move(geom.left() + 30, geom.top() + 30); } else { instance->move(30, 30); }
        instance->show(); instance->closeTimer->start(timeout);
    }
};
DiagnosticHUD* DiagnosticHUD::instance = nullptr;

class GhostReader : public QWidget {
    Q_OBJECT
public:
    GhostReader(const QString& text) {
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool); setAttribute(Qt::WA_TranslucentBackground); setAttribute(Qt::WA_DeleteOnClose); setAttribute(Qt::WA_ShowWithoutActivating);
        QVBoxLayout* layout = new QVBoxLayout(this); layout->setContentsMargins(16, 16, 16, 16);
        QLabel* label = new QLabel(text); label->setFont(QFont("Segoe UI", 11, QFont::Bold)); label->setStyleSheet("color: rgba(230, 230, 240, 255); background: transparent;"); label->setWordWrap(true); label->setFixedWidth(450); layout->addWidget(label); adjustSize();
        QScreen* primary = QGuiApplication::primaryScreen();
        if (primary) { QRect geom = primary->availableGeometry(); move(geom.left() + 30, geom.top() + 30); } else { move(30, 30); }
        setWindowOpacity(0.0); QPropertyAnimation* anim = new QPropertyAnimation(this, "windowOpacity"); anim->setDuration(300); anim->setStartValue(0.0); anim->setEndValue(1.0); anim->setEasingCurve(QEasingCurve::OutCubic); anim->start(QPropertyAnimation::DeleteWhenStopped);
        QTimer::singleShot(4000, this, [this](){ QPropertyAnimation* fade = new QPropertyAnimation(this, "windowOpacity"); fade->setDuration(500); fade->setEndValue(0.0); connect(fade, &QPropertyAnimation::finished, this, &QWidget::close); fade->start(QPropertyAnimation::DeleteWhenStopped); });
    }
protected:
    void paintEvent(QPaintEvent*) override { QPainter p(this); p.setRenderHint(QPainter::Antialiasing); QPainterPath path; path.addRoundedRect(rect().adjusted(1,1,-1,-1), 12, 12); p.fillPath(path, QColor(18, 18, 24, 230)); p.setPen(QPen(QColor(255, 255, 255, 40), 1)); p.drawPath(path); }
    void mousePressEvent(QMouseEvent*) override { close(); } 
};

class GlassPanel : public QWidget {
    Q_OBJECT
    QPixmap bgCached;
public:
    GlassPanel(QWidget* parent = nullptr) : QWidget(parent) { QTimer::singleShot(0, this, &GlassPanel::loadBackground); }
    void loadBackground() { QString bgPath = getDir("App") + "background.png"; if (QFile::exists(bgPath)) { bgCached.load(bgPath); update(); } }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing); painter.setRenderHint(QPainter::SmoothPixmapTransform);
        QPainterPath path; path.addRoundedRect(rect(), 14, 14); painter.setClipPath(path);
        if (!bgCached.isNull()) { QPixmap scaled = bgCached.scaled(size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation); QRect crop((scaled.width() - width()) / 2, (scaled.height() - height()) / 2, width(), height()); painter.drawPixmap(rect(), scaled, crop); painter.fillRect(rect(), QColor(18, 18, 24, 220)); } 
        else { painter.fillRect(rect(), QColor(18, 18, 24, 245)); }
        painter.setPen(QPen(QColor(255, 255, 255, 20), 1.5)); painter.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 14, 14);
    }
};

class ModelManagerDialog : public QDialog {
    Q_OBJECT
    QTextEdit* logArea; QLineEdit *inputFrom, *inputTo; QPushButton *btnInstall, *btnCheck; QProcess* process;
public:
    ModelManagerDialog(QWidget* parent = nullptr) : QDialog(parent) {
        setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog); setAttribute(Qt::WA_TranslucentBackground); setStyleSheet(GLOBAL_STYLE); setFixedSize(500, 560);
        QVBoxLayout* mainLayout = new QVBoxLayout(this); mainLayout->setContentsMargins(0, 0, 0, 0); GlassPanel* glass = new GlassPanel(this); mainLayout->addWidget(glass);
        QVBoxLayout* contentLayout = new QVBoxLayout(glass); contentLayout->setContentsMargins(25, 25, 25, 25); contentLayout->setSpacing(10);

        QLabel* titleLabel = new QLabel("📦 Argos Model Manager"); titleLabel->setStyleSheet("color: #00D9FF; font-size: 14pt; font-weight: 800;"); contentLayout->addWidget(titleLabel);
        QLabel* descLabel = new QLabel("Download NMT models for strictly offline translation.\nExample: 'en' to 'id'."); descLabel->setObjectName("Subtext"); contentLayout->addWidget(descLabel);

        QHBoxLayout* inputLayout = new QHBoxLayout();
        QVBoxLayout* fromLayout = new QVBoxLayout(); fromLayout->addWidget(new QLabel("From (ISO):")); inputFrom = new QLineEdit("en"); fromLayout->addWidget(inputFrom);
        QVBoxLayout* toLayout = new QVBoxLayout(); toLayout->addWidget(new QLabel("To (ISO):")); inputTo = new QLineEdit("id"); toLayout->addWidget(inputTo);
        inputLayout->addLayout(fromLayout); inputLayout->addLayout(toLayout); contentLayout->addLayout(inputLayout);

        QHBoxLayout* actionLayout = new QHBoxLayout();
        btnCheck = new QPushButton("Check Installed"); btnCheck->setObjectName("ActionBtn");
        btnInstall = new QPushButton("Download / Install");
        actionLayout->addWidget(btnCheck); actionLayout->addWidget(btnInstall); contentLayout->addLayout(actionLayout);

        logArea = new QTextEdit(); logArea->setReadOnly(true); logArea->setPlaceholderText("Console Output...\nClick 'Check Installed' to see existing models."); contentLayout->addWidget(logArea);

        QPushButton* btnClose = new QPushButton("Close"); btnClose->setObjectName("ActionBtn"); connect(btnClose, &QPushButton::clicked, this, &QDialog::reject);
        contentLayout->addWidget(btnClose);

        QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(this); shadow->setBlurRadius(30); shadow->setColor(QColor(0,0,0,200)); shadow->setOffset(0,10); glass->setGraphicsEffect(shadow);

        process = new QProcess(this);
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        QString modelPath = QDir::toNativeSeparators(getDir("Data") + "models"); QDir().mkpath(modelPath); env.insert("PYTHONUTF8", "1"); 
        process->setProcessEnvironment(env);

        connect(btnCheck, &QPushButton::clicked, [=]() { runCommand("list", "", ""); });
        connect(btnInstall, &QPushButton::clicked, [=]() { runCommand("install", inputFrom->text().trimmed(), inputTo->text().trimmed()); });
        
        connect(process, &QProcess::readyReadStandardOutput, [=]() {
            while (process->canReadLine()) {
                QJsonDocument doc = QJsonDocument::fromJson(process->readLine().trimmed());
                if (doc.isObject()) {
                    QJsonObject obj = doc.object();
                    if (obj.contains("progress")) logArea->append("⏳ " + obj["progress"].toString());
                    else if (obj.contains("result")) {
                        logArea->append("✅ " + obj["result"].toString() + "\n");
                        if (obj.contains("trigger_list") && obj["trigger_list"].toBool()) { runCommand("list", "", ""); }
                    }
                    else if (obj.contains("error")) logArea->append("❌ [ERROR] " + obj["error"].toString() + "\n");
                    else if (obj.contains("list_result")) {
                        QJsonArray arr = obj["list_result"].toArray();
                        QStringList models; for (int i=0; i<arr.size(); ++i) models << arr[i].toString();
                        QSettings s(getDir("Data") + "config.ini", QSettings::IniFormat); s.setValue("ArgosInstalledModels", models);
                        if (models.isEmpty()) logArea->append("⚠️ No models installed.\n"); else logArea->append("✅ Found " + QString::number(models.size()) + " cached routes.\n");
                        if (g_mainWindow) QMetaObject::invokeMethod(g_mainWindow, "refreshArgosCombos", Qt::QueuedConnection);
                    }
                }
            }
        });
        connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), [=]() { btnInstall->setEnabled(true); btnCheck->setEnabled(true); });
    }

    void runCommand(const QString& action, const QString& from, const QString& to) {
        if (process->state() == QProcess::Running) return;
        btnInstall->setEnabled(false); btnCheck->setEnabled(false);
        logArea->clear(); logArea->append("⚡ Executing...");

        QString pythonExe = getDir("_internal") + "python/python.exe"; if (!QFile::exists(pythonExe)) pythonExe = "python"; 
        QString modelPath = QDir::toNativeSeparators(getDir("Data") + "models");
        
        QJsonObject req; req["action"] = action; req["from"] = from; req["to"] = to;
        QString payload = QString::fromUtf8(QJsonDocument(req).toJson(QJsonDocument::Compact));

        connect(process, &QProcess::started, [=, this]() {
            process->write(payload.toUtf8()); process->closeWriteChannel();
            disconnect(process, &QProcess::started, nullptr, nullptr);
        });
        process->start(pythonExe, QStringList() << getDir("Data") + "argos_bridge.py" << modelPath);
    }
protected:
    void mousePressEvent(QMouseEvent* event) override { if (event->button() == Qt::LeftButton) window()->windowHandle()->startSystemMove(); }
    void paintEvent(QPaintEvent*) override { QPainterPath path; path.addRoundedRect(rect(), 14, 14); setMask(path.toFillPolygon().toPolygon()); }
};

class SettingsDialog : public QDialog {
    Q_OBJECT
    QSettings& settings;

    QLineEdit *nativeLangInput, *apiKeysInput, *geminiKeysInput;
    QTextEdit *writerPromptInput, *readerPromptInput;
    QKeySequenceEdit *hotkeyWriter, *hotkeyReader;
    QCheckBox *chkSlang, *chkDeleteOriginal, *chkAutoStart;
    QCheckBox *chkStealthMode; // --- [CIEL V5] STEALTH CHECKBOX ---

public:
    SettingsDialog(QSettings& s, QWidget* parent = nullptr) : QDialog(parent), settings(s) {
        setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog); setAttribute(Qt::WA_TranslucentBackground); setStyleSheet(GLOBAL_STYLE); setFixedSize(500, 780);
        QVBoxLayout* mainLayout = new QVBoxLayout(this); mainLayout->setContentsMargins(0, 0, 0, 0); GlassPanel* glass = new GlassPanel(this); mainLayout->addWidget(glass);
        QVBoxLayout* contentLayout = new QVBoxLayout(glass); contentLayout->setContentsMargins(30, 25, 30, 25); contentLayout->setSpacing(10);

        QLabel* titleLabel = new QLabel("Advanced Configuration"); titleLabel->setStyleSheet("color: #00D9FF; font-size: 14pt; font-weight: 800;"); contentLayout->addWidget(titleLabel);

        QLabel* lblNative = new QLabel("My Native Language (Use 'id' for Indo, NOT 'in'):"); lblNative->setObjectName("Subtext"); contentLayout->addWidget(lblNative);
        nativeLangInput = new QLineEdit(settings.value("NativeLang", "id").toString()); contentLayout->addWidget(nativeLangInput);

        QHBoxLayout* keysLayout = new QHBoxLayout();
        QVBoxLayout* groqLayout = new QVBoxLayout(); QLabel* lblGroqKeys = new QLabel("Groq Keys (CSV):"); lblGroqKeys->setObjectName("Subtext"); groqLayout->addWidget(lblGroqKeys);
        apiKeysInput = new QLineEdit(settings.value("GroqKeys").toString()); groqLayout->addWidget(apiKeysInput);
        
        QVBoxLayout* geminiLayout = new QVBoxLayout(); QLabel* lblGeminiKeys = new QLabel("Gemini Keys (CSV):"); lblGeminiKeys->setObjectName("Subtext"); geminiLayout->addWidget(lblGeminiKeys);
        geminiKeysInput = new QLineEdit(settings.value("GeminiKeys").toString()); geminiLayout->addWidget(geminiKeysInput);
        
        keysLayout->addLayout(groqLayout); keysLayout->addLayout(geminiLayout); contentLayout->addLayout(keysLayout);

        QString defaultWriter = "You are a translation API. Translate slang from {source_lang} to {target_lang}. Provide EXACTLY ONE translation. Do NOT use slashes (/) for alternatives. PRESERVE ALL original punctuation exactly (..., ?, !). Output ONLY the translated string.";
        QString currentWriter = settings.value("WriterPrompt", defaultWriter).toString();
        if (!currentWriter.contains("slashes")) { currentWriter = defaultWriter; settings.setValue("WriterPrompt", currentWriter); }

        QLabel* lblWriterPrmpt = new QLabel("Writer Prompt ({source_lang}, {target_lang}):"); lblWriterPrmpt->setObjectName("Subtext"); contentLayout->addWidget(lblWriterPrmpt);
        writerPromptInput = new QTextEdit(currentWriter); writerPromptInput->setFixedHeight(50); contentLayout->addWidget(writerPromptInput);

        QString defaultReader = "You are a translation API. Translate from {source_lang} to {target_lang}. Provide EXACTLY ONE translation. Do NOT use slashes (/) for alternatives. PRESERVE ALL original punctuation exactly (..., ?, !). Output ONLY the translated string.";
        QString currentReader = settings.value("ReaderPrompt", defaultReader).toString();
        if (!currentReader.contains("slashes")) { currentReader = defaultReader; settings.setValue("ReaderPrompt", currentReader); }

        QLabel* lblReaderPrmpt = new QLabel("Reader Prompt ({source_lang}, {target_lang}):"); lblReaderPrmpt->setObjectName("Subtext"); contentLayout->addWidget(lblReaderPrmpt);
        readerPromptInput = new QTextEdit(currentReader); readerPromptInput->setFixedHeight(50); contentLayout->addWidget(readerPromptInput);

        QLabel* hotkeyHeader = new QLabel("Hotkeys (Click to rebind):"); hotkeyHeader->setObjectName("Header"); contentLayout->addWidget(hotkeyHeader);
        
        QHBoxLayout* hotkeyLayout = new QHBoxLayout();
        QVBoxLayout* wHotLayout = new QVBoxLayout(); wHotLayout->addWidget(new QLabel("Writer:")); hotkeyWriter = new QKeySequenceEdit(QKeySequence(settings.value("HotkeyWriter", "Alt+W").toString())); wHotLayout->addWidget(hotkeyWriter);
        QVBoxLayout* rHotLayout = new QVBoxLayout(); rHotLayout->addWidget(new QLabel("Reader:")); hotkeyReader = new QKeySequenceEdit(QKeySequence(settings.value("HotkeyReader", "Alt+Q").toString())); rHotLayout->addWidget(hotkeyReader);
        hotkeyLayout->addLayout(wHotLayout); hotkeyLayout->addLayout(rHotLayout); contentLayout->addLayout(hotkeyLayout);

        QLabel* sysHeader = new QLabel("System Options:"); sysHeader->setObjectName("Header"); contentLayout->addWidget(sysHeader);

        chkAutoStart = new QCheckBox("Run Automatically at Windows Startup"); chkAutoStart->setChecked(settings.value("AutoStart", false).toBool()); contentLayout->addWidget(chkAutoStart);
        chkSlang = new QCheckBox("Lowercased natural output (Writer)"); chkSlang->setChecked(settings.value("SlangMode", true).toBool()); contentLayout->addWidget(chkSlang);
        chkDeleteOriginal = new QCheckBox("Erase source text before translation"); chkDeleteOriginal->setChecked(settings.value("DeleteOriginal", true).toBool()); contentLayout->addWidget(chkDeleteOriginal);
        
        // --- [CIEL V5] STEALTH CHECKBOX INJECTION ---
        chkStealthMode = new QCheckBox("⚡ Stealth Hook Mode (Anti-Ding & Anti-Desync)");
        chkStealthMode->setChecked(settings.value("StealthMode", false).toBool());
        contentLayout->addWidget(chkStealthMode);
        // --------------------------------------------

        contentLayout->addStretch();
        QHBoxLayout* btnLayout = new QHBoxLayout();
        QPushButton* btnCancel = new QPushButton("Cancel"); btnCancel->setObjectName("ActionBtn"); connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
        QPushButton* btnSave = new QPushButton("Save Config"); connect(btnSave, &QPushButton::clicked, this, &SettingsDialog::saveAndAccept);
        btnLayout->addWidget(btnCancel); btnLayout->addWidget(btnSave); contentLayout->addLayout(btnLayout);

        QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(this); shadow->setBlurRadius(30); shadow->setColor(QColor(0,0,0,200)); shadow->setOffset(0,10); glass->setGraphicsEffect(shadow);
    }
protected:
    void mousePressEvent(QMouseEvent* event) override { if (event->button() == Qt::LeftButton) window()->windowHandle()->startSystemMove(); }
    void paintEvent(QPaintEvent*) override { QPainterPath path; path.addRoundedRect(rect(), 14, 14); setMask(path.toFillPolygon().toPolygon()); }
private slots:
    void saveAndAccept() {
        QString nLang = nativeLangInput->text().trimmed().toLower(); if (nLang == "in" || nLang == "indo" || nLang == "indonesian") { nLang = "id"; }
        settings.setValue("NativeLang", nLang); settings.setValue("GroqKeys", apiKeysInput->text()); settings.setValue("GeminiKeys", geminiKeysInput->text()); settings.setValue("WriterPrompt", writerPromptInput->toPlainText());
        settings.setValue("ReaderPrompt", readerPromptInput->toPlainText()); settings.setValue("HotkeyWriter", hotkeyWriter->keySequence().toString()); settings.setValue("HotkeyReader", hotkeyReader->keySequence().toString()); 
        settings.setValue("SlangMode", chkSlang->isChecked()); settings.setValue("DeleteOriginal", chkDeleteOriginal->isChecked());
        
        // --- [CIEL V5] SIMPAN STATE STEALTH ---
        settings.setValue("StealthMode", chkStealthMode->isChecked());
        g_stealthMode = chkStealthMode->isChecked();
        // --------------------------------------
        
        bool runAtStartup = chkAutoStart->isChecked(); settings.setValue("AutoStart", runAtStartup);
        QSettings reg("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);
        if (runAtStartup) { QString appPath = QDir::toNativeSeparators(QCoreApplication::applicationFilePath()); reg.setValue("XYZTranslate", "\"" + appPath + "\""); } else { reg.remove("XYZTranslate"); }
        accept();
    }
};

class XYZControlCenter : public QMainWindow {
    Q_OBJECT
    bool isSmartEnterActive = false;

    QSettings settings;
    QNetworkAccessManager* netManager;
    QString backupClip;
    QPointer<GhostReader> currentGhost;

    QPushButton *btnSmartEnter, *btnModelManager;
    QComboBox *cmbWriter, *cmbReader;
    QLineEdit *wCustomInput, *rCustomInput;
    QRadioButton *radioGtx, *radioGroq, *radioGemini, *radioArgos; 
    QLabel *wLabel, *rLabel;

public:
    XYZControlCenter() : settings(getDir("Data") + "config.ini", QSettings::IniFormat) {
        g_mainWindow = this;
        
        // --- [CIEL V5] LOAD STEALTH STATE AWAL ---
        g_stealthMode = settings.value("StealthMode", false).toBool();
        // -----------------------------------------

        netManager = new QNetworkAccessManager(this);
        netManager->setTransferTimeout(10000);

        setWindowFlags(Qt::FramelessWindowHint);
        setAttribute(Qt::WA_TranslucentBackground);
        setFixedSize(480, 620); 

        QWidget* container = new QWidget(this); setCentralWidget(container);
        QVBoxLayout* containerLayout = new QVBoxLayout(container); containerLayout->setContentsMargins(15, 15, 15, 15);
        GlassPanel* glass = new GlassPanel(); containerLayout->addWidget(glass);

        QVBoxLayout* mainLayout = new QVBoxLayout(glass); mainLayout->setContentsMargins(28, 24, 28, 24); mainLayout->setSpacing(12);
        setStyleSheet(GLOBAL_STYLE);

        QHBoxLayout* titleLayout = new QHBoxLayout();
        QLabel* titleLabel = new QLabel("XYZ Translate"); titleLabel->setStyleSheet("color: #00D9FF; font-size: 14pt; font-weight: 800; letter-spacing: 1px;");
        QPushButton* btnMin = new QPushButton("—"); btnMin->setObjectName("TitleBtn"); btnMin->setFixedSize(32, 32);
        QPushButton* btnClose = new QPushButton("✕"); btnClose->setObjectName("TitleBtn"); btnClose->setFixedSize(32, 32);
        connect(btnMin, &QPushButton::clicked, this, &QWidget::showMinimized); connect(btnClose, &QPushButton::clicked, this, &QWidget::close);
        titleLayout->addWidget(titleLabel); titleLayout->addStretch(); titleLayout->addWidget(btnMin); titleLayout->addWidget(btnClose);
        mainLayout->addLayout(titleLayout);

        isSmartEnterActive = settings.value("SmartEnterActive", false).toBool();
        btnSmartEnter = new QPushButton(isSmartEnterActive ? "⚡ Super Enter (Ctrl+Alt+Enter) [ON]" : "⚡ Super Enter (Ctrl+Alt+Enter) [OFF]");
        btnSmartEnter->setObjectName(isSmartEnterActive ? "LiveBtnOn" : "LiveBtnOff");
        connect(btnSmartEnter, &QPushButton::clicked, this, &XYZControlCenter::toggleSmartEnter);
        mainLayout->addWidget(btnSmartEnter);

        QHBoxLayout* engineHeaderLayout = new QHBoxLayout();
        QLabel* engineHeader = new QLabel("Translation Engine"); engineHeader->setObjectName("Header");
        engineHeaderLayout->addWidget(engineHeader); engineHeaderLayout->addStretch();
        
        QPushButton* btnSettings = new QPushButton("⚙ Settings"); btnSettings->setObjectName("ActionBtn"); btnSettings->setFixedSize(110, 30);
        connect(btnSettings, &QPushButton::clicked, this, &XYZControlCenter::openSettingsPopup);
        engineHeaderLayout->addWidget(btnSettings); mainLayout->addLayout(engineHeaderLayout);
        
        radioGtx = new QRadioButton("Google GTX (Supersonic)"); 
        radioGroq = new QRadioButton("Groq Llama-3 (Cognitive AI)"); 
        radioGemini = new QRadioButton("Google Gemini 2.5 (Advanced AI)"); 
        radioArgos = new QRadioButton("Argos NMT (Offline)"); 
        
        mainLayout->addWidget(radioGtx); mainLayout->addWidget(radioGroq); mainLayout->addWidget(radioGemini); mainLayout->addWidget(radioArgos);

        btnModelManager = new QPushButton("📦 Download Offline Models (Argos)");
        btnModelManager->setObjectName("ActionBtn");
        connect(btnModelManager, &QPushButton::clicked, [this]() { ModelManagerDialog dialog(this); dialog.exec(); });
        mainLayout->addWidget(btnModelManager);

        QLabel* routingHeader = new QLabel("Target Language Routing"); routingHeader->setObjectName("Header");
        mainLayout->addWidget(routingHeader);
        
        QHBoxLayout* routeLayout = new QHBoxLayout(); routeLayout->setSpacing(16);
        
        QVBoxLayout* wLayout = new QVBoxLayout(); wLayout->setSpacing(4);
        wLabel = new QLabel("Writer Target:"); wLabel->setObjectName("Subtext");
        cmbWriter = new QComboBox(); wCustomInput = new QLineEdit();
        wLayout->addWidget(wLabel); wLayout->addWidget(cmbWriter); wLayout->addWidget(wCustomInput);

        QVBoxLayout* rLayout = new QVBoxLayout(); rLayout->setSpacing(4);
        rLabel = new QLabel("Reader Target:"); rLabel->setObjectName("Subtext");
        cmbReader = new QComboBox(); rCustomInput = new QLineEdit();
        rLayout->addWidget(rLabel); rLayout->addWidget(cmbReader); rLayout->addWidget(rCustomInput);

        routeLayout->addLayout(wLayout); routeLayout->addLayout(rLayout); mainLayout->addLayout(routeLayout);

        connect(cmbWriter, &QComboBox::currentTextChanged, [=, this](const QString& t){
            if(t.isEmpty()) return;
            if (radioArgos->isChecked() && t.contains(" ➔ ")) settings.setValue("ArgosWriterRoute", t);
            else if (!radioArgos->isChecked()) { settings.setValue("CmbWriter", t); wCustomInput->setVisible(t == "Custom..."); }
        });
        
        connect(cmbReader, &QComboBox::currentTextChanged, [=, this](const QString& t){
            if(t.isEmpty()) return;
            if (radioArgos->isChecked() && t.contains(" ➔ ")) settings.setValue("ArgosReaderRoute", t);
            else if (!radioArgos->isChecked()) { settings.setValue("CmbReader", t); rCustomInput->setVisible(t == "Custom..."); }
        });

        connect(wCustomInput, &QLineEdit::textChanged,[=, this](const QString& t){ settings.setValue("WCustom", t); });
        connect(rCustomInput, &QLineEdit::textChanged, [=, this](const QString& t){ settings.setValue("RCustom", t); });

        auto updateUI = [=, this](const QString& engine) {
            QString prevEngine = settings.value("Engine", "GTX").toString();
            settings.setValue("Engine", engine);
            
            bool isArgos = (engine == "Argos");
            bool wasArgos = (prevEngine == "Argos");

            if (isArgos) {
                btnModelManager->show();
                wLabel->setText("Writer Route:"); rLabel->setText("Reader Route:");
                wCustomInput->hide(); rCustomInput->hide();
                if (!wasArgos || cmbWriter->count() == 0 || !cmbWriter->itemText(0).contains("➔")) {
                    refreshArgosCombos(); 
                }
            } else {
                btnModelManager->hide();
                wLabel->setText("Writer Target (Auto-Source):"); rLabel->setText("Reader Target (Auto-Source):");
                if (wasArgos || cmbWriter->count() == 0 || cmbWriter->itemText(0).contains("➔")) {
                    setupCloudComboboxes();
                }
                QString hint = (engine == "Groq" || engine == "Gemini") ? "Full Name (e.g. French)" : "ISO Code (e.g. fr)";
                wCustomInput->setPlaceholderText(hint); rCustomInput->setPlaceholderText(hint);
                wCustomInput->setVisible(cmbWriter->currentText() == "Custom..."); rCustomInput->setVisible(cmbReader->currentText() == "Custom...");
            }
        };

        connect(radioGtx, &QRadioButton::toggled, [=](bool c){ if(c) updateUI("GTX"); });
        connect(radioGroq, &QRadioButton::toggled,[=](bool c){ if(c) updateUI("Groq"); });
        connect(radioGemini, &QRadioButton::toggled, [=](bool c){ if(c) updateUI("Gemini"); });
        connect(radioArgos, &QRadioButton::toggled, [=](bool c){ if(c) updateUI("Argos"); });

        QString savedEngine = settings.value("Engine", "GTX").toString();
        if(savedEngine == "Groq") radioGroq->setChecked(true);
        else if(savedEngine == "Gemini") radioGemini->setChecked(true);
        else if(savedEngine == "Argos") radioArgos->setChecked(true);
        else radioGtx->setChecked(true);
        updateUI(savedEngine);

        mainLayout->addStretch(); 
        QPushButton* btnStealth = new QPushButton("ENGAGE STEALTH MODE");
        connect(btnStealth, &QPushButton::clicked, this, &QWidget::hide);
        mainLayout->addWidget(btnStealth);

        QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(this); shadow->setBlurRadius(30); shadow->setColor(QColor(0,0,0,200)); shadow->setOffset(0,10); glass->setGraphicsEffect(shadow);

        setupTrayIcon(); 
        
        QTimer::singleShot(50, this, [this]() {
            if (!initDatabase()) { }
            ensureArgosBridge();
            applyHotkeys();
        });
    }

    void setupCloudComboboxes() {
        cmbWriter->blockSignals(true); cmbReader->blockSignals(true);
        cmbWriter->clear(); cmbReader->clear();
        cmbWriter->setEnabled(true); cmbReader->setEnabled(true);
        
        cmbWriter->addItems({"English", "Indonesian", "Japanese", "Custom..."});
        cmbReader->addItems({"English", "Indonesian", "Japanese", "Custom..."});
        
        cmbWriter->setCurrentText(settings.value("CmbWriter", "English").toString());
        cmbReader->setCurrentText(settings.value("CmbReader", "Indonesian").toString());
        wCustomInput->setText(settings.value("WCustom", "").toString());
        rCustomInput->setText(settings.value("RCustom", "").toString());
        
        cmbWriter->blockSignals(false); cmbReader->blockSignals(false);
    }

    ~XYZControlCenter() {
        if (g_kbdHook) { UnhookWindowsHookEx(g_kbdHook); g_kbdHook = nullptr; }
        if (g_mouseHook) { UnhookWindowsHookEx(g_mouseHook); g_mouseHook = nullptr; }
    }

    void resizeEvent(QResizeEvent* event) override { QMainWindow::resizeEvent(event); QPainterPath path; path.addRoundedRect(rect(), 14, 14); setMask(path.toFillPolygon().toPolygon()); }

protected:
    void mousePressEvent(QMouseEvent* event) override { if (event->button() == Qt::LeftButton) { std::lock_guard<std::mutex> lock(g_bufferMutex); g_smartBuffer.clear(); window()->windowHandle()->startSystemMove(); } }
    void closeEvent(QCloseEvent* event) override {
        if(!radioArgos->isChecked()) {
            settings.setValue("CmbWriter", cmbWriter->currentText()); settings.setValue("CmbReader", cmbReader->currentText());
            settings.setValue("WCustom", wCustomInput->text()); settings.setValue("RCustom", rCustomInput->text());
        }
        event->ignore(); hide();
    }

public slots:
    void refreshArgosCombos() {
        QStringList models = settings.value("ArgosInstalledModels").toStringList();
        cmbWriter->blockSignals(true); cmbReader->blockSignals(true); 
        cmbWriter->clear(); cmbReader->clear();
        
        if (models.isEmpty()) {
            cmbWriter->addItem("No models. Download first!"); cmbReader->addItem("No models. Download first!");
            cmbWriter->setEnabled(false); cmbReader->setEnabled(false);
        } else {
            cmbWriter->addItems(models); cmbReader->addItems(models);
            cmbWriter->setEnabled(true); cmbReader->setEnabled(true);
            int wIdx = cmbWriter->findText(settings.value("ArgosWriterRoute").toString()); if(wIdx != -1) cmbWriter->setCurrentIndex(wIdx);
            int rIdx = cmbReader->findText(settings.value("ArgosReaderRoute").toString()); if(rIdx != -1) cmbReader->setCurrentIndex(rIdx);
        }
        cmbWriter->blockSignals(false); cmbReader->blockSignals(false);
    }

    // --- [CIEL V5] CLIPBOARD FIX: Phantom Pulse Anti-Desync ---
    void fetchClipboardAsync(std::function<void(QString)> callback) {
        QClipboard* clipboard = QGuiApplication::clipboard();
        backupClip = clipboard->text(); clipboard->clear();

        ReleaseModifiers(); 
        
        if (g_stealthMode) {
            INPUT phantom[2] = {};
            phantom[0].type = INPUT_KEYBOARD; phantom[0].ki.wVk = 0x87; 
            phantom[1].type = INPUT_KEYBOARD; phantom[1].ki.wVk = 0x87; phantom[1].ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(2, phantom, sizeof(INPUT));
            Sleep(20);
        } else {
            Sleep(30);
        }

        HardwareKeyAction(VK_CONTROL, false); 
        HardwareKeyAction(0x43, false); 
        Sleep(10); 
        HardwareKeyAction(0x43, true); 
        HardwareKeyAction(VK_CONTROL, true);

        QTimer* timer = new QTimer(this); timer->setInterval(20);
        int* attempts = new int(0);
        connect(timer, &QTimer::timeout, [=]() {
            (*attempts)++; QString result = clipboard->text();
            if (!result.isEmpty() || *attempts > 40) {
                timer->stop(); timer->deleteLater(); delete attempts;
                clipboard->setText(backupClip); callback(result.trimmed());
            }
        });
        timer->start();
    }
    // ----------------------------------------------------------

    void onWriterHotkeyTriggered() {
        { std::lock_guard<std::mutex> lock(g_bufferMutex); g_smartBuffer.clear(); } 
        fetchClipboardAsync([this](QString copiedText) {
            if (!copiedText.isEmpty()) executeAPI("Writer", copiedText, false);
            else DiagnosticHUD::showMessage("[ERROR] No text selected! Try copying again.", "#FF4757");
        });
    }

    void onReaderHotkeyTriggered() {
        { std::lock_guard<std::mutex> lock(g_bufferMutex); g_smartBuffer.clear(); } 
        fetchClipboardAsync([this](QString copiedText) {
            if (!copiedText.isEmpty()) executeAPI("Reader", copiedText, false);
            else DiagnosticHUD::showMessage("[ERROR] No text selected! Try copying again.", "#FF4757");
        });
    }

    void onSuperEnterTriggered() {
        if (!isSmartEnterActive) return;
        QString textToTranslate; int bufferLength;
        { std::lock_guard<std::mutex> lock(g_bufferMutex); textToTranslate = QString::fromStdWString(g_smartBuffer).trimmed(); bufferLength = static_cast<int>(g_smartBuffer.length()); g_smartBuffer.clear(); }
        if (textToTranslate.isEmpty()) return;
        
        if (settings.value("DeleteOriginal", true).toBool() && bufferLength > 0) {
            ReleaseModifiers(); 
            
            if (g_stealthMode) {
                INPUT phantom[2] = {};
                phantom[0].type = INPUT_KEYBOARD; phantom[0].ki.wVk = 0x87; 
                phantom[1].type = INPUT_KEYBOARD; phantom[1].ki.wVk = 0x87; phantom[1].ki.dwFlags = KEYEVENTF_KEYUP;
                SendInput(2, phantom, sizeof(INPUT));
                Sleep(20);
            } else {
                Sleep(30);
            }

            std::vector<INPUT> backspaces;
            for (int i = 0; i < bufferLength; ++i) {
                INPUT inDown = {}; inDown.type = INPUT_KEYBOARD; inDown.ki.wVk = VK_BACK;
                INPUT inUp = {}; inUp.type = INPUT_KEYBOARD; inUp.ki.wVk = VK_BACK; inUp.ki.dwFlags = KEYEVENTF_KEYUP;
                backspaces.push_back(inDown); backspaces.push_back(inUp);
            }
            SendInput(static_cast<UINT>(backspaces.size()), backspaces.data(), sizeof(INPUT));
            Sleep(30);
        }
        executeAPI("SmartEnter", textToTranslate, true);
    }

private:
    void openSettingsPopup() { SettingsDialog dialog(settings, this); if (dialog.exec() == QDialog::Accepted) { applyHotkeys(); DiagnosticHUD::showMessage("Settings applied!", "#00D9FF"); } }
    void unregisterHotkeys() { HWND hwnd = reinterpret_cast<HWND>(winId()); UnregisterHotKey(hwnd, 1); UnregisterHotKey(hwnd, 2); }
    void registerHotkey(int id, UINT modifiers, UINT vk) { HWND hwnd = reinterpret_cast<HWND>(winId()); if (!RegisterHotKey(hwnd, id, modifiers | MOD_NOREPEAT, vk)) DiagnosticHUD::showMessage(QString("Hotkey %1 registration failed!").arg(id), "#FF4757"); }
    void applyHotkeys() {
        unregisterHotkeys();
        auto parseAndSet = [&](const QString& seqStr, UINT& outMod, UINT& outVk) {
            QKeySequence ks(seqStr);
            if (!ks.isEmpty()) {
                int key = ks[0].key(); Qt::KeyboardModifiers mods = ks[0].keyboardModifiers(); UINT winMods = 0;
                if (mods & Qt::AltModifier) { winMods |= MOD_ALT; } if (mods & Qt::ControlModifier) { winMods |= MOD_CONTROL; } if (mods & Qt::ShiftModifier) { winMods |= MOD_SHIFT; }
                UINT vk = key & ~(Qt::AltModifier | Qt::ControlModifier | Qt::ShiftModifier); if (vk != 0) { outMod = winMods; outVk = vk; }
            }
        };
        parseAndSet(settings.value("HotkeyWriter", "Alt+W").toString(), g_modWriter, g_vkWriter); parseAndSet(settings.value("HotkeyReader", "Alt+Q").toString(), g_modReader, g_vkReader);
    }
    void toggleSmartEnter() {
        isSmartEnterActive = !isSmartEnterActive; btnSmartEnter->setText(isSmartEnterActive ? "⚡ Super Enter (Ctrl+Alt+Enter) [ON]" : "⚡ Super Enter (Ctrl+Alt+Enter) [OFF]"); btnSmartEnter->setObjectName(isSmartEnterActive ? "LiveBtnOn" : "LiveBtnOff"); btnSmartEnter->style()->polish(btnSmartEnter); settings.setValue("SmartEnterActive", isSmartEnterActive);
    }
    void setupTrayIcon() {
        QSystemTrayIcon* trayIcon = new QSystemTrayIcon(this); QString iconPath = getDir("App") + "icon.ico";
        if (QFile::exists(iconPath)) trayIcon->setIcon(QIcon(iconPath)); else trayIcon->setIcon(style()->standardIcon(QStyle::SP_ComputerIcon));
        QMenu* trayMenu = new QMenu(this); trayMenu->addAction("Open UI", this, [this]() { showNormal(); activateWindow(); }); trayMenu->addAction("Terminate System", qApp, &QCoreApplication::quit); trayIcon->setContextMenu(trayMenu);
        connect(trayIcon, &QSystemTrayIcon::activated, [this](QSystemTrayIcon::ActivationReason r) { if (r == QSystemTrayIcon::DoubleClick) { showNormal(); activateWindow(); } }); trayIcon->show();
    }
    
    void triggerRollback(const QString& originalText, bool useDirectTyping) { 
        if (!originalText.isEmpty()) { 
            QClipboard* clipboard = QGuiApplication::clipboard();
            clipboard->setText(originalText);
            QTimer::singleShot(100,[this]() {
                ReleaseModifiers(); 
                if (g_stealthMode) {
                    INPUT phantom[2] = {};
                    phantom[0].type = INPUT_KEYBOARD; phantom[0].ki.wVk = 0x87; 
                    phantom[1].type = INPUT_KEYBOARD; phantom[1].ki.wVk = 0x87; phantom[1].ki.dwFlags = KEYEVENTF_KEYUP;
                    SendInput(2, phantom, sizeof(INPUT));
                }
                HardwareKeyAction(VK_CONTROL, false); 
                HardwareKeyAction(0x56, false); HardwareKeyAction(0x56, true); 
                HardwareKeyAction(VK_CONTROL, true);
            });
            QTimer::singleShot(1500, [this, clipboard]() { 
                if (!backupClip.isEmpty()) clipboard->setText(backupClip); 
            });
        } 
    }
    
    void dumpErrorLog(const QString& engine, const QString& errorMsg, const QString& fromCode, const QString& toCode, const QString& inputTxt) {
        QString logPath = getDir("Data") + "crash_logs.txt"; QFile logFile(logPath);
        if (logFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) { QTextStream out(&logFile); out << "=== [" << engine << "] ERROR LOG ===\n" << "Route: " << fromCode << " -> " << toCode << "\n" << "Input: " << inputTxt << "\n---------------------------\n" << errorMsg << "\n\n"; logFile.close(); }
    }
    
    bool isISPBlocking(QNetworkReply::NetworkError err, const QString& engineName) {
        if (err == QNetworkReply::ConnectionRefusedError || err == QNetworkReply::HostNotFoundError || err == QNetworkReply::TimeoutError || err == QNetworkReply::SslHandshakeFailedError || err == QNetworkReply::UnknownNetworkError || err == QNetworkReply::NetworkSessionFailedError) {
            DiagnosticHUD::showMessage(QString("[ISP BLOCKED] %1 dicekal WiFi lu! Nyalakan VPN/WARP.").arg(engineName), "#FF4757", 6000); return true;
        } return false;
    }

    void executeAPI(const QString& mode, const QString& textToTranslate, bool useDirectTyping) {
        QString currentEngine = settings.value("Engine", "GTX").toString();
        QString sourceLangIso, targetLangIso;

        if (currentEngine == "Argos") {
            QString route = (mode == "Writer" || mode == "SmartEnter") ? cmbWriter->currentText() : cmbReader->currentText();
            if (route.contains("No models") || route.contains("Loading") || route.contains("Error")) {
                DiagnosticHUD::showMessage("[ARGOS] Download Offline Models first!", "#FF4757"); triggerRollback(textToTranslate, useDirectTyping); return;
            }
            QStringList parts = route.split(" ➔ ");
            if (parts.size() == 2) { sourceLangIso = parts[0].trimmed(); targetLangIso = parts[1].trimmed(); }
            else { DiagnosticHUD::showMessage("[ARGOS] Invalid route selected!", "#FF4757"); triggerRollback(textToTranslate, useDirectTyping); return; }
        } else {
            sourceLangIso = "auto";
            QString displayLang = (mode == "Writer" || mode == "SmartEnter") ? cmbWriter->currentText() : cmbReader->currentText();
            QString customCode = (mode == "Writer" || mode == "SmartEnter") ? wCustomInput->text() : rCustomInput->text();
            targetLangIso = resolveEngineLanguage(currentEngine, displayLang, customCode);
        }

        QString standardizedOriginal = textToTranslate.trimmed();
        QSqlQuery checkQuery; checkQuery.prepare("SELECT translated_text FROM translations WHERE engine=? AND target_lang=? AND original_text=?");
        checkQuery.addBindValue(currentEngine); checkQuery.addBindValue(targetLangIso); checkQuery.addBindValue(standardizedOriginal);
        if (checkQuery.exec() && checkQuery.next()) { 
            QString cachedResult = checkQuery.value(0).toString(); 
            DiagnosticHUD::showMessage("⚡ Loaded from Memory Cache", "#2ED573"); 
            handleTranslationResult(mode, standardizedOriginal, cachedResult, useDirectTyping); return; 
        }

        if (currentEngine == "GTX") {
            DiagnosticHUD::showMessage("⚡ GTX Translating...");
            QUrl url("https://translate.googleapis.com/translate_a/single"); QUrlQuery query;
            query.addQueryItem("client", "gtx"); query.addQueryItem("sl", "auto"); query.addQueryItem("tl", targetLangIso); query.addQueryItem("dt", "t"); query.addQueryItem("q", textToTranslate); url.setQuery(query); 
            QNetworkRequest req(url); req.setRawHeader("User-Agent", "Mozilla/5.0");
            QNetworkReply* reply = netManager->get(req);
            connect(reply, &QNetworkReply::finished, [=, this]() {
                reply->deleteLater();
                if (reply->error() == QNetworkReply::NoError) {
                    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll()); QString result; QJsonArray arr = doc.array()[0].toArray();
                    for (int i = 0; i < arr.size(); ++i) { QJsonValue val = arr[i].toArray()[0]; if (val.isString()) { result += val.toString(); } }
                    result.remove("////"); saveToMemory(currentEngine, sourceLangIso, targetLangIso, standardizedOriginal, result); 
                    handleTranslationResult(mode, standardizedOriginal, result, useDirectTyping);
                } else { 
                    if (isISPBlocking(reply->error(), "GTX")) { triggerRollback(textToTranslate, useDirectTyping); return; }
                    dumpErrorLog("GTX", QString::fromUtf8(reply->readAll()), sourceLangIso, targetLangIso, textToTranslate);
                    DiagnosticHUD::showMessage("[GTX ERROR] Network Failed", "#FF4757"); triggerRollback(textToTranslate, useDirectTyping);
                }
            });
        }
        else if (currentEngine == "Groq") {
            DiagnosticHUD::showMessage("⚡ Groq Processing...");
            QStringList keys = settings.value("GroqKeys").toString().split(',', Qt::SkipEmptyParts); for (QString& k : keys) k = k.trimmed();
            if (keys.isEmpty()) { DiagnosticHUD::showMessage("[GROQ ERROR] No API keys! Open Settings.", "#FF4757"); triggerRollback(textToTranslate, useDirectTyping); return; }
            QString prompt = (mode == "Writer" || mode == "SmartEnter") ? settings.value("WriterPrompt").toString() : settings.value("ReaderPrompt").toString();
            prompt.replace("{source_lang}", "any language (auto-detect)"); prompt.replace("{target_lang}", targetLangIso);
            prompt += "\n\nCRITICAL INSTRUCTION: You MUST output ONLY a valid JSON object. The JSON object must contain exactly one key named \"translation\" with the translated string as its value. Do not include any other text or formatting.";

            auto tryKey = std::make_shared<std::function<void(int)>>();
            *tryKey = [=, this](int index) {
                if (index >= keys.size()) { DiagnosticHUD::showMessage("[GROQ ERROR] All API keys exhausted!", "#FF4757"); triggerRollback(textToTranslate, useDirectTyping); return; }
                QNetworkRequest req(QUrl("https://api.groq.com/openai/v1/chat/completions")); req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json"); req.setRawHeader("Authorization", QString("Bearer %1").arg(keys[index]).toUtf8());
                QJsonObject payload{ {"model", "llama-3.3-70b-versatile"}, {"messages", QJsonArray{QJsonObject{{"role", "system"}, {"content", prompt}}, QJsonObject{{"role", "user"}, {"content", textToTranslate}}}}, {"temperature", 0.0}, {"response_format", QJsonObject{{"type", "json_object"}}} };
                QNetworkReply* reply = netManager->post(req, QJsonDocument(payload).toJson());
                connect(reply, &QNetworkReply::finished, [=, this]() {
                    reply->deleteLater();
                    if (reply->error() == QNetworkReply::NoError) {
                        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll()); QString rawContent = doc.object()["choices"].toArray()[0].toObject()["message"].toObject()["content"].toString().trimmed();
                        QJsonDocument jsonRes = QJsonDocument::fromJson(rawContent.toUtf8()); QString result;
                        if(jsonRes.isObject() && jsonRes.object().contains("translation")) result = jsonRes.object()["translation"].toString(); else result = rawContent;
                        saveToMemory(currentEngine, sourceLangIso, targetLangIso, standardizedOriginal, result); 
                        handleTranslationResult(mode, standardizedOriginal, result, useDirectTyping);
                    } else { 
                        if (isISPBlocking(reply->error(), "Groq")) { triggerRollback(textToTranslate, useDirectTyping); return; }
                        dumpErrorLog("GROQ", QString::fromUtf8(reply->readAll()), sourceLangIso, targetLangIso, textToTranslate);
                        DiagnosticHUD::showMessage(QString("[GROQ] Key %1 failed, trying next...").arg(index+1), "#FFA502"); (*tryKey)(index + 1); 
                    }
                });
            }; (*tryKey)(0);
        }
        else if (currentEngine == "Gemini") {
            DiagnosticHUD::showMessage("⚡ Gemini Processing...");
            QStringList keys = settings.value("GeminiKeys").toString().split(',', Qt::SkipEmptyParts); for (QString& k : keys) k = k.trimmed();
            if (keys.isEmpty()) { DiagnosticHUD::showMessage("[GEMINI ERROR] No API keys! Open Settings.", "#FF4757"); triggerRollback(textToTranslate, useDirectTyping); return; }
            QString prompt = (mode == "Writer" || mode == "SmartEnter") ? settings.value("WriterPrompt").toString() : settings.value("ReaderPrompt").toString();
            prompt.replace("{source_lang}", "any language (auto-detect)"); prompt.replace("{target_lang}", targetLangIso);
            prompt += "\n\nCRITICAL INSTRUCTION: You MUST output ONLY a valid JSON object. The JSON object must contain exactly one key named \"translation\" with the translated string as its value. Do not include any other text, markdown, or explanations.";

            auto tryKey = std::make_shared<std::function<void(int)>>();
            *tryKey = [=, this](int index) {
                if (index >= keys.size()) { DiagnosticHUD::showMessage("[GEMINI ERROR] All API keys exhausted!", "#FF4757"); triggerRollback(textToTranslate, useDirectTyping); return; }
                QUrl url(QString("https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key=%1").arg(keys[index])); QNetworkRequest req(url); req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
                QJsonObject payload{ {"systemInstruction", QJsonObject{{"parts", QJsonArray{QJsonObject{{"text", prompt}}}}}}, {"contents", QJsonArray{QJsonObject{{"role", "user"}, {"parts", QJsonArray{QJsonObject{{"text", textToTranslate}}}}}}}, {"generationConfig", QJsonObject{{"temperature", 0.0}, {"responseMimeType", "application/json"}}} };
                QNetworkReply* reply = netManager->post(req, QJsonDocument(payload).toJson());
                connect(reply, &QNetworkReply::finished, [=, this]() {
                    QByteArray responseData = reply->readAll(); reply->deleteLater();
                    if (reply->error() == QNetworkReply::NoError) {
                        QJsonDocument doc = QJsonDocument::fromJson(responseData); 
                        QString rawContent = doc.object()["candidates"].toArray()[0].toObject()["content"].toObject()["parts"].toArray()[0].toObject()["text"].toString().trimmed();
                        QJsonDocument jsonRes = QJsonDocument::fromJson(rawContent.toUtf8()); QString result;
                        if(jsonRes.isObject() && jsonRes.object().contains("translation")) result = jsonRes.object()["translation"].toString(); else result = rawContent;
                        saveToMemory(currentEngine, sourceLangIso, targetLangIso, standardizedOriginal, result); 
                        handleTranslationResult(mode, standardizedOriginal, result, useDirectTyping);
                    } else { 
                        if (isISPBlocking(reply->error(), "Gemini")) { triggerRollback(textToTranslate, useDirectTyping); return; }
                        dumpErrorLog("GEMINI", QString::fromUtf8(responseData), sourceLangIso, targetLangIso, textToTranslate);
                        QJsonDocument errDoc = QJsonDocument::fromJson(responseData);
                        QString realErrorMsg = "Network Timeout / Unknown Error";
                        if (errDoc.isObject() && errDoc.object().contains("error")) realErrorMsg = errDoc.object()["error"].toObject()["message"].toString();

                        if (index + 1 >= keys.size()) {
                            DiagnosticHUD::showMessage("[GEMINI] " + realErrorMsg, "#FF4757", 8000);
                            triggerRollback(textToTranslate, useDirectTyping);
                        } else { 
                            DiagnosticHUD::showMessage(QString("[GEMINI] Key %1 failed, trying next...").arg(index+1), "#FFA502"); 
                            (*tryKey)(index + 1); 
                        }
                    }
                });
            }; (*tryKey)(0);
        }
        else { 
            DiagnosticHUD::showMessage(QString("⚡ Argos Translating [%1]...").arg(mode == "Writer" || mode == "SmartEnter" ? settings.value("ArgosWriterRoute").toString() : settings.value("ArgosReaderRoute").toString()));
            QJsonObject reqObj; reqObj["action"] = "translate"; reqObj["text"] = textToTranslate; reqObj["from"] = sourceLangIso; reqObj["to"] = targetLangIso;
            QString jsonPayload = QString::fromUtf8(QJsonDocument(reqObj).toJson(QJsonDocument::Compact));
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment(); QString modelPath = QDir::toNativeSeparators(getDir("Data") + "models"); QDir().mkpath(modelPath); env.insert("PYTHONUTF8", "1"); 
            QString pythonExe = getDir("_internal") + "python/python.exe"; if (!QFile::exists(pythonExe)) pythonExe = "python"; 

            QProcess* process = new QProcess(this); process->setProcessEnvironment(env);
            
            connect(process, &QProcess::started, [process, jsonPayload]() {
                process->write(jsonPayload.toUtf8()); process->closeWriteChannel();
            });
            
            connect(process, &QProcess::errorOccurred, [=](QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart) {
                    DiagnosticHUD::showMessage("[ARGOS ERROR] Python failed to start!", "#FF4757");
                    triggerRollback(textToTranslate, useDirectTyping); process->deleteLater();
                }
            });

            connect(process, &QProcess::readyReadStandardOutput,[=, this]() {
                while (process->canReadLine()) {
                    QByteArray line = process->readLine().trimmed(); if (line.isEmpty()) continue;
                    QJsonDocument doc = QJsonDocument::fromJson(line);
                    if (doc.isObject()) {
                        QJsonObject obj = doc.object();
                        if (obj.contains("progress")) { DiagnosticHUD::showMessage("⚡ " + obj["progress"].toString(), "#FFA502", 10000); } 
                        else if (obj.contains("error")) { DiagnosticHUD::showMessage("[ARGOS] " + obj["error"].toString(), "#FF4757"); if (obj.contains("trace")) dumpErrorLog("ARGOS", obj["trace"].toString(), sourceLangIso, targetLangIso, textToTranslate); triggerRollback(textToTranslate, useDirectTyping); } 
                        else if (obj.contains("result")) { 
                            QString result = obj["result"].toString(); 
                            saveToMemory(currentEngine, sourceLangIso, targetLangIso, standardizedOriginal, result); 
                            handleTranslationResult(mode, standardizedOriginal, result, useDirectTyping); 
                        }
                    }
                }
            });
            connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), [=, this](int exitCode, QProcess::ExitStatus) {
                    QString errOut = QString::fromUtf8(process->readAllStandardError()).trimmed();
                    if (!errOut.isEmpty() && exitCode != 0) { DiagnosticHUD::showMessage("[ARGOS CRASH] Hard failure. See crash_logs.txt", "#FF4757"); dumpErrorLog("ARGOS_CRITICAL", errOut, sourceLangIso, targetLangIso, textToTranslate); triggerRollback(textToTranslate, useDirectTyping); }
                    process->deleteLater();
                });
                
            process->start(pythonExe, QStringList() << getDir("Data") + "argos_bridge.py" << modelPath);
        }
    }

    void saveToMemory(const QString& engine, const QString& sl, const QString& tl, const QString& original, const QString& translated) {
        QSqlQuery insertQuery; insertQuery.prepare("INSERT OR IGNORE INTO translations (engine, source_lang, target_lang, original_text, translated_text) VALUES (?, ?, ?, ?, ?)");
        insertQuery.addBindValue(engine); insertQuery.addBindValue(sl); insertQuery.addBindValue(tl); insertQuery.addBindValue(original); insertQuery.addBindValue(translated);
        insertQuery.exec();
    }

    void handleTranslationResult(const QString& mode, const QString& originalText, QString result, bool useDirectTyping) {
        bool isWriter = (mode == "Writer" || mode == "SmartEnter");
        result = humanizeText(result, originalText, isWriter, settings.value("SlangMode", true).toBool());

        if (isWriter) {
            QClipboard* clipboard = QGuiApplication::clipboard();
            clipboard->setText(result);
            QTimer::singleShot(100,[this]() {
                ReleaseModifiers(); 
                if (g_stealthMode) {
                    INPUT phantom[2] = {};
                    phantom[0].type = INPUT_KEYBOARD; phantom[0].ki.wVk = 0x87; 
                    phantom[1].type = INPUT_KEYBOARD; phantom[1].ki.wVk = 0x87; phantom[1].ki.dwFlags = KEYEVENTF_KEYUP;
                    SendInput(2, phantom, sizeof(INPUT));
                }
                HardwareKeyAction(VK_CONTROL, false); 
                HardwareKeyAction(0x56, false); HardwareKeyAction(0x56, true); 
                HardwareKeyAction(VK_CONTROL, true);
            });
            QTimer::singleShot(1500, [this, clipboard]() { 
                if (!backupClip.isEmpty()) clipboard->setText(backupClip); 
            });
        } else {
            if (currentGhost) { currentGhost->close(); }
            currentGhost = new GhostReader(result); currentGhost->show();
        }
    }
};

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        if (wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN || wParam == WM_MBUTTONDOWN) {
            std::lock_guard<std::mutex> lock(g_bufferMutex); g_smartBuffer.clear();
        }
    }
    return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* pKeyBoard = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        
        if (pKeyBoard->flags & LLKHF_INJECTED) return CallNextHookEx(g_kbdHook, nCode, wParam, lParam);

        // --- [CIEL V5] STEALTH MODE: ANTI-DING PROTOCOL (KEYUP PHASE) ---
        if (g_stealthMode && (wParam == WM_KEYUP || wParam == WM_SYSKEYUP)) {
            if (pKeyBoard->vkCode == VK_RETURN && g_swallowNextEnterUp) {
                g_swallowNextEnterUp = false; return 1; 
            }
            if (pKeyBoard->vkCode == g_vkWriter && g_swallowWriterUp) {
                g_swallowWriterUp = false; return 1; 
            }
            if (pKeyBoard->vkCode == g_vkReader && g_swallowReaderUp) {
                g_swallowReaderUp = false; return 1; 
            }
        }
        // ----------------------------------------------------------------

        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
            bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0; 
            bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0; 
            bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0; 
            bool win = (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0 || (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;

            bool writerMatch = (pKeyBoard->vkCode == g_vkWriter) && (ctrl == ((g_modWriter & MOD_CONTROL) != 0)) && (alt == ((g_modWriter & MOD_ALT) != 0)) && (shift == ((g_modWriter & MOD_SHIFT) != 0));
            bool readerMatch = (pKeyBoard->vkCode == g_vkReader) && (ctrl == ((g_modReader & MOD_CONTROL) != 0)) && (alt == ((g_modReader & MOD_ALT) != 0)) && (shift == ((g_modReader & MOD_SHIFT) != 0));

            // --- [CIEL V5] ACTION TRIGGERS (THE F24 PHANTOM CHEAT) ---
            if (writerMatch) { 
                if (g_stealthMode) {
                    g_swallowWriterUp = true; 
                    INPUT phantom[2] = {};
                    phantom[0].type = INPUT_KEYBOARD; phantom[0].ki.wVk = 0x87; 
                    phantom[1].type = INPUT_KEYBOARD; phantom[1].ki.wVk = 0x87; phantom[1].ki.dwFlags = KEYEVENTF_KEYUP;
                    SendInput(2, phantom, sizeof(INPUT));
                }
                if (g_mainWindow) QMetaObject::invokeMethod(g_mainWindow, "onWriterHotkeyTriggered", Qt::QueuedConnection); 
                return 1; 
            }
            if (readerMatch) { 
                if (g_stealthMode) {
                    g_swallowReaderUp = true; 
                    INPUT phantom[2] = {};
                    phantom[0].type = INPUT_KEYBOARD; phantom[0].ki.wVk = 0x87; 
                    phantom[1].type = INPUT_KEYBOARD; phantom[1].ki.wVk = 0x87; phantom[1].ki.dwFlags = KEYEVENTF_KEYUP;
                    SendInput(2, phantom, sizeof(INPUT));
                }
                if (g_mainWindow) QMetaObject::invokeMethod(g_mainWindow, "onReaderHotkeyTriggered", Qt::QueuedConnection); 
                return 1; 
            }

            if (ctrl && alt && pKeyBoard->vkCode == VK_RETURN) { 
                if (g_stealthMode) {
                    g_swallowNextEnterUp = true; 
                    INPUT phantom[2] = {};
                    phantom[0].type = INPUT_KEYBOARD; phantom[0].ki.wVk = 0x87; 
                    phantom[1].type = INPUT_KEYBOARD; phantom[1].ki.wVk = 0x87; phantom[1].ki.dwFlags = KEYEVENTF_KEYUP;
                    SendInput(2, phantom, sizeof(INPUT));
                }
                if (g_mainWindow) QMetaObject::invokeMethod(g_mainWindow, "onSuperEnterTriggered", Qt::QueuedConnection); 
                return 1; 
            }
            // ---------------------------------------------------------

            if (!ctrl && !alt && !win) {
                std::lock_guard<std::mutex> lock(g_bufferMutex);
                if (pKeyBoard->vkCode == VK_LEFT || pKeyBoard->vkCode == VK_RIGHT || pKeyBoard->vkCode == VK_UP || pKeyBoard->vkCode == VK_DOWN || pKeyBoard->vkCode == VK_HOME || pKeyBoard->vkCode == VK_END || pKeyBoard->vkCode == VK_PRIOR || pKeyBoard->vkCode == VK_NEXT || pKeyBoard->vkCode == VK_TAB) { g_smartBuffer.clear(); }
                else if (pKeyBoard->vkCode == VK_BACK) { if (!g_smartBuffer.empty()) g_smartBuffer.pop_back(); } 
                else if (pKeyBoard->vkCode == VK_ESCAPE || pKeyBoard->vkCode == VK_RETURN) { g_smartBuffer.clear(); } 
                else {
                    BYTE state[256]; GetKeyboardState(state);
                    state[VK_SHIFT]   = (GetAsyncKeyState(VK_SHIFT) & 0x8000) ? 0x80 : 0;
                    state[VK_LSHIFT]  = (GetAsyncKeyState(VK_LSHIFT) & 0x8000) ? 0x80 : 0;
                    state[VK_RSHIFT]  = (GetAsyncKeyState(VK_RSHIFT) & 0x8000) ? 0x80 : 0;
                    state[VK_CONTROL] = (GetAsyncKeyState(VK_CONTROL) & 0x8000) ? 0x80 : 0;
                    state[VK_MENU]    = (GetAsyncKeyState(VK_MENU) & 0x8000) ? 0x80 : 0;
                    state[VK_CAPITAL] = (GetKeyState(VK_CAPITAL) & 0x0001) ? 0x01 : 0;

                    HWND fgWindow = GetForegroundWindow();
                    DWORD fgThreadId = GetWindowThreadProcessId(fgWindow, NULL);
                    HKL fgLayout = GetKeyboardLayout(fgThreadId);

                    WCHAR buffer[4] = {0};
                    int ret = ToUnicodeEx(pKeyBoard->vkCode, pKeyBoard->scanCode, state, buffer, 4, 4, fgLayout);
                    for (int i = 0; i < ret; ++i) g_smartBuffer += buffer[i];
                }
            }
        }
    }
    return CallNextHookEx(g_kbdHook, nCode, wParam, lParam);
}

int main(int argc, char* argv[]) {
    HANDLE hMutex = CreateMutexA(NULL, FALSE, "XYZ_Translate_Instance_Guard");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hMutex) CloseHandle(hMutex);
        return 0; 
    }

    SetCurrentProcessExplicitAppUserModelID(L"CIEL.XYZ.Translate.V5");

    QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
    
    XYZControlCenter window; window.show();
    
    QTimer::singleShot(100,[]() {
        if (!initDatabase()) { }
        g_kbdHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(nullptr), 0);
        g_mouseHook = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, GetModuleHandle(nullptr), 0);
    });

    int result = app.exec();

    if (g_kbdHook) { UnhookWindowsHookEx(g_kbdHook); g_kbdHook = nullptr; }
    if (g_mouseHook) { UnhookWindowsHookEx(g_mouseHook); g_mouseHook = nullptr; }
    if (hMutex) CloseHandle(hMutex);
    
    return result;
}

#include "main.moc"
