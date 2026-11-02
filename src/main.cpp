// EyeGazePassword - 课设 demo 主程序
// 功能：摄像头采集 / 人脸检测 / 人眼检测 / 九宫格视线追踪 / 密码输入
// 按键：ESC或Q退出，R重新输入密码，M切换模式(视线演示/密码输入)
// 菜单：文件-退出；模式-切换/重置；帮助-关于
#include <opencv2/opencv.hpp>
#include <iostream>
#include <string>
#include <cmath>
#include "GazeTracker.h"
#include "PasswordGate.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX   // 防止 windows.h 的 min/max 宏吃掉 std::min/std::max
#endif
#include <windows.h>

// ---------------- Win32 菜单（给 OpenCV 窗口挂真正的菜单栏） ----------------
// OpenCV 自带窗口过程不认识菜单命令，所以子类化它：收到 WM_COMMAND 时
// 记下命令码，主循环每帧读取执行（同一 UI 线程，安全）。
static bool g_menuQuit = false;
static int  g_menuCmd = 0;   // 1=重置密码  2=切换模式
static WNDPROC g_origWndProc = nullptr;

enum MenuId {
    IDM_EXIT   = 1001,
    IDM_RESET  = 1002,
    IDM_MODE   = 1003,
    IDM_ABOUT  = 1004,
};

static LRESULT CALLBACK MenuWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_COMMAND) {
        switch (LOWORD(wParam)) {
        case IDM_EXIT:  g_menuQuit = true; break;
        case IDM_RESET: g_menuCmd  = 1;    break;
        case IDM_MODE:  g_menuCmd  = 2;    break;
        case IDM_ABOUT:
            MessageBoxW(hWnd,
                L"EyeGazePassword 课设 Demo\n\n"
                L"注视九宫格数字约 1 秒即选中，\n"
                L"演示密码 1234（M 键或菜单切换密码模式）。",
                L"关于", MB_OK | MB_ICONINFORMATION);
            break;
        }
        return 0;
    }
    return CallWindowProcW(g_origWndProc, hWnd, msg, wParam, lParam);
}

static void installMenu(const std::string& title)
{
    HWND hWnd = FindWindowW(NULL, std::wstring(title.begin(), title.end()).c_str());
    if (!hWnd) return;

    HMENU hBar   = CreateMenu();
    HMENU hFile  = CreatePopupMenu();   // 文件
    HMENU hMode  = CreatePopupMenu();   // 模式
    HMENU hHelp  = CreatePopupMenu();   // 帮助

    AppendMenuW(hFile, MF_STRING, IDM_EXIT,  L"退出(&X)\tAlt+F4");
    AppendMenuW(hMode, MF_STRING, IDM_MODE,  L"切换 视线/密码 模式(&M)");
    AppendMenuW(hMode, MF_STRING, IDM_RESET, L"重置密码(&R)");
    AppendMenuW(hHelp, MF_STRING, IDM_ABOUT, L"关于(&A)...");

    AppendMenuW(hBar, MF_POPUP, (UINT_PTR)hFile, L"文件(&F)");
    AppendMenuW(hBar, MF_POPUP, (UINT_PTR)hMode, L"模式(&M)");
    AppendMenuW(hBar, MF_POPUP, (UINT_PTR)hHelp, L"帮助(&H)");

    SetMenu(hWnd, hBar);
    // 子类化：替换窗口过程，接管菜单命令，其余消息仍交回 OpenCV
    g_origWndProc = (WNDPROC)SetWindowLongPtrW(hWnd, GWLP_WNDPROC, (LONG_PTR)MenuWndProc);
    DrawMenuBar(hWnd);
}
#endif // _WIN32

// ---------------- 九宫格参数 ----------------
// 3x3 宫格半透明覆盖在视频画面正中央：
//   看正前稍偏 = 5，看左上 = 1 ... 方向与屏幕位置一致（所见即所得）
static const int kGridN = 3;
struct GridUI {
    cv::Rect cells[3][3];
    cv::Rect area;          // 整个宫格区域
    int dwellFrames = 25;   // 凝视多少帧算"选中"（约 1 秒）
    int progress[3][3] = {}; // 每格已凝视帧数
};

static void layoutGrid(GridUI& g, const cv::Rect& area)
{
    g.area = area;
    int w = area.width / kGridN, h = area.height / kGridN;
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            g.cells[r][c] = cv::Rect(area.x + c * w, area.y + r * h, w, h);
}

// 由视线偏移 (-1..1) 推出宫格行列
// idle：正中央很小的"待机区"，避免发呆时在 5 附近抖动
// dead：三段分界线，大于它才到左右/上下列
static bool gazeToCell(const cv::Point2f& gaze, int& r, int& c,
                       float dead = 0.35f, float idle = 0.15f)
{
    if (std::fabs(gaze.x) < idle && std::fabs(gaze.y) < idle) return false;
    c = gaze.x < -dead ? 0 : (gaze.x > dead ? 2 : 1);
    r = gaze.y < -dead ? 0 : (gaze.y > dead ? 2 : 1);
    return true;
}

// 画宫格（半透明覆盖在视频上）；返回本帧"选中"的数字（1~9），0 表示没有
static int drawGrid(cv::Mat& canvas, GridUI& g, int curR, int curC, bool aiming)
{
    static const char* labels[3][3] = { {"1","2","3"}, {"4","5","6"}, {"7","8","9"} };
    int picked = 0;
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            bool hot = aiming && r == curR && c == curC;
            if (hot) {
                g.progress[r][c]++;
                if (g.progress[r][c] >= g.dwellFrames) {
                    picked = r * 3 + c + 1;
                    g.progress[r][c] = 0;
                    for (int i = 0; i < 3; i++)
                        for (int j = 0; j < 3; j++) g.progress[i][j] = 0;
                }
            } else {
                g.progress[r][c] = 0;
            }

            // 半透明底色：人脸和检测框仍透得出来
            cv::Rect cr = g.cells[r][c];
            cv::Mat roi = canvas(cr);
            cv::Mat lay(roi.size(), CV_8UC3,
                        hot ? cv::Scalar(0, 150, 0) : cv::Scalar(50, 50, 50));
            cv::addWeighted(lay, hot ? 0.55 : 0.40, roi, hot ? 0.45 : 0.60, 0, roi);
            cv::rectangle(canvas, cr, hot ? cv::Scalar(0, 220, 0) : cv::Scalar(90, 90, 90), 2);

            // 凝视进度条
            if (hot) {
                float p = (float)g.progress[r][c] / g.dwellFrames;
                cv::rectangle(canvas,
                    cv::Rect(cr.x + 2, cr.y + 2, (int)((cr.width - 4) * p), 6),
                    cv::Scalar(0, 255, 255), cv::FILLED);
            }
            int baseline = 0;
            cv::Size ts = cv::getTextSize(labels[r][c], cv::FONT_HERSHEY_SIMPLEX, 1.6, 3, &baseline);
            cv::Point org(cr.x + (cr.width - ts.width) / 2,
                          cr.y + (cr.height + ts.height) / 2);
            cv::putText(canvas, labels[r][c], org, cv::FONT_HERSHEY_SIMPLEX, 1.6,
                        hot ? cv::Scalar(0, 255, 0) : cv::Scalar(230, 230, 230),
                        3, cv::LINE_AA);
        }
    }
    return picked;
}

// 在摄像头上画检测框和视线十字
static void drawOverlay(cv::Mat& frame, const TrackResult& tr)
{
    if (!tr.faceFound) {
        cv::putText(frame, "No face", cv::Point(10, 30),
                    cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 0, 255), 2);
        return;
    }
    cv::rectangle(frame, tr.faceBox, cv::Scalar(0, 255, 255), 2);
    int pupils = 0;
    for (auto& e : tr.eyes) {
        cv::rectangle(frame, e.box, cv::Scalar(255, 0, 0), 1);
        if (e.hasPupil) {
            cv::circle(frame, e.pupil, 3, cv::Scalar(0, 0, 255), cv::FILLED);
            pupils++;
        }
    }
    // 调试信息：期望 eyes=2 pupil=2，不足时说明眼睛没检到/瞳孔没找到
    cv::putText(frame, "eyes=" + std::to_string(tr.eyes.size()) +
                       "  pupil=" + std::to_string(pupils),
                cv::Point(10, 58), cv::FONT_HERSHEY_SIMPLEX, 0.6,
                pupils >= 2 ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 165, 255), 2);
    // 用一个小十字显示估计的视线方向（画在画面中心附近）
    cv::Point center(frame.cols / 2, frame.rows / 2);
    cv::Point tip = center + cv::Point((int)(tr.gaze.x * 80), (int)(tr.gaze.y * 80));
    cv::line(frame, center, tip, cv::Scalar(0, 255, 0), 2);
    cv::circle(frame, tip, 6, cv::Scalar(0, 255, 0), 2);
}

// 找级联分类器目录：优先程序目录 data/haarcascades，其次环境变量 OPENCV_DIR
static std::string findCascadeDir()
{
    const char* candidates[] = {
        "data/haarcascades",
        "../data/haarcascades",
        "../../data/haarcascades",
        "EyeGazePassword/data/haarcascades",
    };
    for (auto p : candidates) {
        cv::FileStorage fs(std::string(p) + "/haarcascade_frontalface_alt2.xml", cv::FileStorage::READ);
        if (fs.isOpened()) return p;
    }
    const char* opencvDir = getenv("OPENCV_DIR");
    if (opencvDir)
        return std::string(opencvDir) + "/etc/haarcascades";
    return "";
}

// 没指定 --camera 时，逐个试摄像头，挑第一个"画面不黑"的
// （笔记本 0 号常是红外/虚拟摄像头，取到的帧接近全黑）
static int autoPickCamera()
{
    for (int i = 0; i < 4; i++) {
        cv::VideoCapture c(i);
        if (!c.isOpened()) continue;
        cv::Mat f;
        for (int k = 0; k < 10; k++) c >> f;   // 丢掉前几帧再判断
        if (f.empty()) continue;
        cv::Scalar m = cv::mean(f);
        double avg = (m[0] + m[1] + m[2]) / 3.0;
        std::cout << "[INFO] camera " << i << " mean=" << avg
                  << (avg > 30 ? "  <- use" : "  (dark, skip)") << "\n";
        if (avg > 30) return i;
    }
    std::cout << "[WARN] no bright camera found, fallback to 0\n";
    return 0;
}

int main(int argc, char** argv)
{
    int camIndex = 0;
    bool camSpecified = false;
    std::string videoPath;
    for (int i = 1; i < argc - 1; i++) {
        if (std::string(argv[i]) == "--camera") { camIndex = std::atoi(argv[i + 1]); camSpecified = true; }
        if (std::string(argv[i]) == "--video")  videoPath = argv[i + 1];
    }

    GazeTracker tracker;
    std::string cascadeDir = findCascadeDir();
    if (cascadeDir.empty() || !tracker.load(cascadeDir)) {
        std::cerr << "[ERROR] cannot load haarcascades. Put xml files under data/haarcascades\n";
        return 1;
    }
    std::cout << "[INFO] cascades loaded from: " << cascadeDir << "\n";

    // ---------- 任务1：摄像头采集 ----------
    cv::VideoCapture cap;
    if (!videoPath.empty()) {
        cap.open(videoPath);
    } else {
        if (!camSpecified) camIndex = autoPickCamera();   // 自动挑正常摄像头
        cap.open(camIndex);
    }
    if (!cap.isOpened()) {
        std::cerr << "[ERROR] cannot open camera/video. Try: --camera 1  or  --video test.mp4\n";
        return 1;
    }
    std::cout << "[INFO] using " << (videoPath.empty() ? "camera " : "video ")
              << (videoPath.empty() ? std::to_string(camIndex) : videoPath) << "\n";

    const std::string winMain = "EyeGazePassword";
    cv::namedWindow(winMain, cv::WINDOW_AUTOSIZE);
#ifdef _WIN32
    installMenu(winMain);   // 挂菜单栏：文件/模式/帮助
#endif

    GridUI grid;
    PasswordGate gate;
    bool passwordMode = false;
    int smoothR = -1, smoothC = -1; // 平滑后的注视格

    cv::Mat frame;
    while (true) {
        cap >> frame;
        if (frame.empty()) break;
        cv::flip(frame, frame, 1); // 镜像，符合照镜子习惯

        TrackResult tr = tracker.process(frame);
        drawOverlay(frame, tr);

        // 单栏布局：九宫格半透明覆盖在视频正中央（底部留状态栏）
        cv::Mat canvas = frame.clone();
        int barH = 56;
        int gs = std::min(canvas.cols, canvas.rows - barH) * 85 / 100;
        layoutGrid(grid, cv::Rect((canvas.cols - gs) / 2,
                                  (canvas.rows - barH - gs) / 2, gs, gs));

        int curR = -1, curC = -1;
        // 没脸/没眼睛时不选格，避免视线归零后自动"选 5"
        bool aiming = tr.faceFound && gazeToCell(tr.gaze, curR, curC);
        // 简单防抖：连续 3 帧落在同一格才算数
        static int lastR = -1, lastC = -1, same = 0;
        if (aiming && curR == lastR && curC == lastC) same++;
        else { same = 0; lastR = curR; lastC = curC; }
        bool stable = aiming && same >= 3;

        // 视线调试：数值长期在 (-0.15,0.15) 说明停在待机区，要往格子方向看
        {
            char dbg[96];
            snprintf(dbg, sizeof(dbg), "gaze=(%.2f,%.2f)  cell=(%d,%d)%s",
                     tr.gaze.x, tr.gaze.y, curR, curC,
                     stable ? "  aiming" : "");
            cv::putText(canvas, dbg, cv::Point(10, 84),
                        cv::FONT_HERSHEY_SIMPLEX, 0.55, cv::Scalar(255, 255, 0), 1);
        }

        int picked = drawGrid(canvas, grid, curR, curC, stable);

        // ---------- 任务5：密码输入 ----------
        // 宫格数字 1~9 对应密码按键；演示密码为 1234
        if (picked > 0) {
            if (passwordMode) {
                gate.inputDigit((char)('0' + picked));
                std::cout << "[KEY] " << picked << "  password=" << gate.entered() << "\n";
            } else {
                std::cout << "[GAZE] looking at " << picked << "\n";
            }
        }

        // 底部状态栏
        std::string status = passwordMode ? "Mode: PASSWORD (demo pw = 1234)" : "Mode: GAZE DEMO";
        if (gate.state() == 1) status += "  >> ACCESS GRANTED";
        if (gate.state() == 2) status += "  >> WRONG PASSWORD";
        cv::putText(canvas, status, cv::Point(10, frame.rows - 40),
                    cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(255, 255, 255), 2);

        // 已输入的密码用 * 显示
        std::string mask(gate.entered().size(), '*');
        mask += std::string(PasswordGate::kLen - gate.entered().size(), '_');
        cv::putText(canvas, "PIN: " + mask, cv::Point(10, frame.rows - 12),
                    cv::FONT_HERSHEY_SIMPLEX, 0.9, cv::Scalar(0, 255, 255), 2);

        cv::imshow(winMain, canvas);
        int key = cv::waitKey(30);

        // 处理菜单命令（菜单过程只记录，真正执行放这里）
#ifdef _WIN32
        if (g_menuQuit) break;
        if (g_menuCmd == 1) { gate.clear(); g_menuCmd = 0; }
        if (g_menuCmd == 2) { passwordMode = !passwordMode; gate.clear(); g_menuCmd = 0; }
#endif

        if (key == 27 || key == 'q' || key == 'Q') break;      // ESC/Q 退出
        if (key == 'r' || key == 'R') gate.clear();             // 重输
        if (key == 'm' || key == 'M') {                         // 切换模式
            passwordMode = !passwordMode;
            gate.clear();
        }
        // 键盘数字路：与眼控同一入口，方便对比测试（仅密码模式）
        if (passwordMode && key >= '1' && key <= '9') {
            gate.inputDigit((char)key);
            std::cout << "[KEY-kbd] " << (char)key
                      << "  password=" << gate.entered() << "\n";
        }
    }
    return 0;
}
