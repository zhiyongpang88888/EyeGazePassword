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

    // 在眼睛 ROI 内找瞳孔（暗区质心）
    bool findPupil(const cv::Mat& gray, const cv::Rect& eyeRoi, cv::Point2f& pupilOut);
};
