#pragma once
#include <opencv2/opencv.hpp>

// 单只眼睛的检测结果（相对人脸框的坐标）
struct EyeInfo {
    cv::Rect box;        // 眼睛在整幅图像中的矩形
    cv::Point2f pupil;   // 瞳孔中心（整幅图像坐标）
    bool hasPupil = false;
};

// 一帧的检测结果
struct TrackResult {
    bool faceFound = false;
    cv::Rect faceBox;
    std::vector<EyeInfo> eyes;
    // 视线偏移，范围大约 [-1, 1]，(0,0) 表示看正前方
    cv::Point2f gaze = {0.f, 0.f};
};

// 【任务2/3/4】人脸检测 + 人眼检测 + 粗略视线估计
// demo 用最简单的暗像素质心法，精度有限，留给大家用光流/landmark等方法改进
class GazeTracker {
public:
    // cascadeDir: haarcascade xml 文件所在目录
    bool load(const std::string& cascadeDir);

    // 输入 BGR 帧，输出检测结果
    TrackResult process(const cv::Mat& frame);

private:
    cv::CascadeClassifier faceCascade_;
    cv::CascadeClassifier eyeCascade_;

    // 稳定化状态：平滑 + 丢眼保持（防止十字乱跳，学生可换成卡尔曼滤波）
    cv::Point2f smoothGaze_{0.f, 0.f};   // 指数滑动平均后的视线
    int framesLostEyes_ = 0;             // 连续没检到眼睛的帧数

    // 自动量程：瞳孔物理移动小，原始偏移通常到不了 ±0.35 的分界线，
    // 用历史极值把实际范围拉伸到 [-1,1]（简化版"标定"，9 点标定留给学生做）
    float extX_ = 0.30f;                 // x 轴历史极值（缓慢遗忘）
    float extY_ = 0.30f;                 // y 轴历史极值

    // 在眼睛 ROI 内找瞳孔（暗区质心）
    bool findPupil(const cv::Mat& gray, const cv::Rect& eyeRoi, cv::Point2f& pupilOut);
};
