# EyeGazePassword — 眼神密码（C++ 课设 Demo）

基于 OpenCV 的最小可运行框架：**摄像头采集 → 人脸检测 → 人眼检测 → 九宫格视线追踪 → 眼神输入密码**。
故意做得很"糙"，留出大量改进空间，供同学在此基础上扩展（含手机 App 方向）。

## 功能清单（对应任务书）

| # | 任务 | 现状（demo） | 代码位置 |
|---|------|-------------|---------|
| 1 | 摄像头采集视频 | `VideoCapture` 打开摄像头/视频文件，镜像显示 | `src/main.cpp` |
| 2 | 人脸检测 | Haar 级联 `haarcascade_frontalface_alt2.xml` | `src/GazeTracker.cpp` |
| 3 | 人眼检测 | 人脸**上半区域**内用 `haarcascade_eye_tree_eyeglasses.xml` | `src/GazeTracker.cpp` |
| 4 | 视线追踪 + 九宫格 | 瞳孔暗像素质心 → 相对偏移 → 映射 3×3 格，凝视约 1s 选中 | `src/GazeTracker.cpp` / `main.cpp` |
| 5 | 密码输入 | 模式下用九宫格数字输入 4 位密码（演示密码 `1234`） | `src/PasswordGate.*` / `main.cpp` |
| 6 | 扩展手机 App | **未实现**，见 `PROJECT_PLAN.md` 扩展方向 | — |

## 环境要求

- Visual Studio 2022/2026（含"使用 C++ 的桌面开发"工作负载）
- OpenCV 4.x（本机默认路径 `D:\opencv\build`，可用环境变量 `OPENCV_DIR` 覆盖）
- 一个 USB 摄像头（没有摄像头可用 `--video 文件` 代替）

## 编译运行

1. 打开 `EyeGazePassword.sln`
2. 选择 `Debug | x64`（或 Release），`F5` 运行
3. 命令行等价方式：

```bat
msbuild EyeGazePassword.sln -p:Configuration=Debug -p:Platform=x64
x64\Debug\EyeGazePassword.exe            :: 打开摄像头 0
x64\Debug\EyeGazePassword.exe --camera 1 :: 摄像头 1
x64\Debug\EyeGazePassword.exe --video data\test.avi
```

### 操作按键

| 键 | 作用 |
|----|------|
| `ESC` / `Q` | 退出 |
| `M` | 切换「视线演示 / 密码输入」模式 |
| `R` | 清空已输入密码 |

### 窗口菜单（Win32 菜单栏）

| 菜单项 | 作用 |
|--------|------|
| 文件 → 退出 | 关闭程序（等价 ESC） |
| 模式 → 切换 视线/密码 模式 | 等价 `M` 键 |
| 模式 → 重置密码 | 等价 `R` 键 |
| 帮助 → 关于 | 显示操作提示 |

菜单在 `src/main.cpp` 顶部 `installMenu()` 中用 Win32 API 创建，并子类化
OpenCV 窗口过程接管 `WM_COMMAND`——扩展菜单项可作为课设的入门练习。

### 玩法

1. 启动后坐在屏幕正前方，保证光线均匀、脸占画面约 1/4~1/3；
2. 右侧出现 3×3 九宫格，**盯住某个数字约 1 秒**，进度条走满即选中（控制台打印 `[GAZE] looking at N`）；
3. 按 `M` 切到密码模式，依次盯 `1 2 3 4`（演示密码），输对显示 `ACCESS GRANTED`；
4. 视线精度目前很一般——这是故意的，见下方"改进方向"。

## 目录结构

```
EyeGazePassword/
├── EyeGazePassword.sln          # VS 解决方案
├── EyeGazePassword.vcxproj      # 工程（已配好 OpenCV 头文件/库/DLL）
├── data/haarcascades/           # Haar 级联 xml（从 OpenCV 复制，保证可移植）
├── data/test.avi                # 无摄像头时的测试视频
├── src/
│   ├── main.cpp                 # 采集、菜单、九宫格 UI、主循环、按键
│   ├── GazeTracker.h/.cpp       # 人脸 + 眼睛 + 瞳孔 + 视线估计
│   └── PasswordGate.h/.cpp      # 密码缓冲与校验
├── PROJECT_PLAN.md              # 项目规划 / 评分点 / 扩展方向（含手机 App）
├── 眼神密码系统-课设指导书.docx  # 学生指导书（任务书+评分+报告要求）
└── README.md
```

## 改进方向（挑 2~3 个做进报告即可）

1. **标定**：让使用者依次注视九宫格 9 个点，记录每格的 `gaze` 均值/方差，用查表代替现在的固定阈值；
2. **视线算法**：改用 dlib 68 点 landmark / MediaPipe FaceMesh 取眼角-瞳孔几何关系，或做头部姿态补偿；
3. **防抖与交互**：卡尔曼滤波、眨眼确认（闭眼=回车）、长凝视取消；
4. **密码安全**：密码改存文件 + SHA-256 加盐哈希，支持修改密码、错误次数锁定；
5. **UI**：Qt/ImGui 重写界面，支持中文、数字键盘鼠标混合输入；
6. **手机 App**（任务 6）：算法核心已与 UI 解耦（`GazeTracker` 只吃 `cv::Mat`），可按 `PROJECT_PLAN.md` 中的思路移植到安卓 NDK / 鸿蒙 / iOS。

## 常见问题

- **打开摄像头失败**：换 `--camera 1`；检查摄像头是否被其他软件占用。
- **画面全黑但程序没报错**：多半是摄像头号不对（笔记本常有红外/虚拟摄像头占 0 号）。先用 `--camera 1` 试，找到正常画面的编号；Windows 设置 → 隐私 → 相机 也要允许桌面应用访问。
- **找不到级联 xml**：把 `data\haarcascades` 拷到 exe 同级目录，或设置 `OPENCV_DIR=D:\opencv\build`。
- **链接错误 LNK2019 opencv_world…**：确认工程是 **x64**，且 `D:\opencv\build\x64\vc16\lib` 下有对应 `.lib`（改 `vc15`/`vc16` 视你的 OpenCV 而定）。
- **工具集不匹配**：VS 版本不同可在工程属性 → 常规 → 平台工具集中"重定向解决方案"。
