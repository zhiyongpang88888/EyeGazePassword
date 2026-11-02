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
// 屏幕右侧画 3x3 宫格，每格对应一个数字 1~9
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

// 由视线偏移 (-1..1) 推出宫格行列；死区在正中间
static bool gazeToCell(const cv::Point2f& gaze, int& r, int& c, float dead = 0.35f)
{
    if (std::fabs(gaze.x) < dead && std::fabs(gaze.y) < dead) return false;
    c = gaze.x < -dead ? 0 : (gaze.x > dead ? 2 : 1);
    r = gaze.y < -dead ? 0 : (gaze.y > dead ? 2 : 1);
    return true;
}

// 画宫格；返回本帧"选中"的数字（1~9），0 表示没有
static int drawGrid(cv::Mat& canvas, GridUI& g, int curR, int curC, bool aiming)
{
    static const char* labels[3][3] = { {"1","2","3"}, {"4","5","6"}, {"7","8","9"} };
    int picked = 0;
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            cv::Scalar color(60, 60, 60);
            if (aiming && r == curR && c == curC) {
                g.progress[r][c]++;
                color = cv::Scalar(0, 200, 0); // 当前注视格变亮
                if (g.progress[r][c] >= g.dwellFrames) {
                    picked = r * 3 + c + 1;
                    g.progress[r][c] = 0;
                    // 全部清零，避免连续触发
                    for (int i = 0; i < 3; i++)
                        for (int j = 0; j < 3; j++) g.progress[i][j] = 0;
                }
            } else {
                g.progress[r][c] = 0;
            }
            cv::rectangle(canvas, g.cells[r][c], color, 2);
            // 凝视进度条
            if (aiming && r == curR && c == curC) {
                float p = (float)g.progress[r][c] / g.dwellFrames;
                cv::rectangle(canvas,
                    cv::Rect(g.cells[r][c].x + 2, g.cells[r][c].y + 2,
                             (int)((g.cells[r][c].width - 4) * p), 6),
                    cv::Scalar(0, 255, 255), cv::FILLED);
            }
            int baseline = 0;
            cv::Size ts = cv::getTextSize(labels[r][c], cv::FONT_HERSHEY_SIMPLEX, 1.6, 3, &baseline);
            cv::Point org(g.cells[r][c].x + (g.cells[r][c].width - ts.width) / 2,
                          g.cells[r][c].y + (g.cells[r][c].height + ts.height) / 2);
            cv::putText(canvas, labels[r][c], org, cv::FONT_HERSHEY_SIMPLEX, 1.6,
                        aiming && r == curR && c == curC ? cv::Scalar(0, 255, 0) : cv::Scalar(200, 200, 200),
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
    for (auto& e : tr.eyes) {
        cv::rectangle(frame, e.box, cv::Scalar(255, 0, 0), 1);
        if (e.hasPupil)
            cv::circle(frame, e.pupil, 3, cv::Scalar(0, 0, 255), cv::FILLED);
    }
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

int main(int argc, char** argv)
{
    int camIndex = 0;
    std::string videoPath;
    for (int i = 1; i < argc - 1; i++) {
        if (std::string(argv[i]) == "--camera") camIndex = std::atoi(argv[i + 1]);
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
    if (!videoPath.empty())
        cap.open(videoPath);
    else
        cap.open(camIndex);
    if (!cap.isOpened()) {
        std::cerr << "[ERROR] cannot open camera/video. Try: --camera 1  or  --video test.mp4\n";
        return 1;
    }

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

        // 布局：左边摄像头，右边 3x3 宫格
        int panelW = frame.rows; // 正方形面板
        cv::Mat canvas(frame.rows, frame.cols + panelW, CV_8UC3, cv::Scalar(30, 30, 30));
        frame.copyTo(canvas(cv::Rect(0, 0, frame.cols, frame.rows)));
        layoutGrid(grid, cv::Rect(frame.cols, 0, panelW, panelW));

        int curR = -1, curC = -1;
        bool aiming = gazeToCell(tr.gaze, curR, curC);
        // 简单防抖：连续 3 帧落在同一格才算数
        static int lastR = -1, lastC = -1, same = 0;
        if (aiming && curR == lastR && curC == lastC) same++;
        else { same = 0; lastR = curR; lastC = curC; }
        bool stable = aiming && same >= 3;

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
    }
    return 0;
}
