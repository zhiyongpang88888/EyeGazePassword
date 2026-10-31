#include "GazeTracker.h"

bool GazeTracker::load(const std::string& cascadeDir)
{
    // 人脸级联分类器（OpenCV 自带，训练好的 xml 文件）
    bool okFace = faceCascade_.load(cascadeDir + "/haarcascade_frontalface_alt2.xml");
    // 戴眼镜也能识别的眼睛分类器
    bool okEye  = eyeCascade_.load(cascadeDir + "/haarcascade_eye_tree_eyeglasses.xml");
    if (!okEye)  // 退而求其次
        okEye = eyeCascade_.load(cascadeDir + "/haarcascade_eye.xml");
    return okFace && okEye;
}

bool GazeTracker::findPupil(const cv::Mat& gray, const cv::Rect& eyeRoi, cv::Point2f& pupilOut)
{
    cv::Rect roi = eyeRoi & cv::Rect(0, 0, gray.cols, gray.rows);
    if (roi.width < 8 || roi.height < 8) return false;

    cv::Mat eye = gray(roi);
    cv::GaussianBlur(eye, eye, cv::Size(5, 5), 0);

    // 自适应阈值：瞳孔/虹膜比皮肤暗很多
    cv::Mat bin;
    cv::threshold(eye, bin, 0, 255, cv::THRESH_BINARY_INV | cv::THRESH_OTSU);

    // 形态学去掉噪点
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(3, 3));
    cv::morphologyEx(bin, bin, cv::MORPH_OPEN, kernel);

    cv::Moments m = cv::moments(bin, true);
    if (m.m00 < 20.0) return false; // 暗像素太少，认为没找到瞳孔

    pupilOut.x = roi.x + (float)(m.m10 / m.m00);
    pupilOut.y = roi.y + (float)(m.m01 / m.m00);
    return true;
}

TrackResult GazeTracker::process(const cv::Mat& frame)
{
    TrackResult res;
    cv::Mat gray;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    cv::equalizeHist(gray, gray);

    // ---------- 任务2：人脸检测 ----------
    std::vector<cv::Rect> faces;
    faceCascade_.detectMultiScale(gray, faces, 1.1, 5, 0, cv::Size(80, 80));
    if (faces.empty()) return res;

    res.faceFound = true;
    res.faceBox = faces[0]; // 只取最大/第一张脸

    // ---------- 任务3：人眼检测 ----------
    // 眼睛一般在人脸的上半部分，缩小搜索范围可以提速+降误检
    cv::Rect upper(res.faceBox.x, res.faceBox.y,
                   res.faceBox.width, res.faceBox.height * 55 / 100);
    upper &= cv::Rect(0, 0, gray.cols, gray.rows);

    std::vector<cv::Rect> eyes;
    eyeCascade_.detectMultiScale(gray(upper), eyes, 1.1, 6, 0,
                                 cv::Size(res.faceBox.width / 10, res.faceBox.height / 12));
    for (auto& e : eyes) {
        EyeInfo info;
        info.box = e + cv::Point(upper.x, upper.y);
        info.hasPupil = findPupil(gray, info.box, info.pupil);
        res.eyes.push_back(info);
    }

    // ---------- 任务4：视线估计（简化版） ----------
    // 思路：瞳孔在眼睛框中的相对位置 ≈ 视线方向
    //   pupil 在眼框左侧  -> gaze.x < 0 （看左边）
    //   pupil 在眼框上侧  -> gaze.y < 0 （看上边）
    // 双眼结果取平均，更稳一些
    float gx = 0, gy = 0; int n = 0;
    for (auto& e : res.eyes) {
        if (!e.hasPupil || e.box.width < 8) continue;
        float nx = (e.pupil.x - (e.box.x + e.box.width * 0.5f)) / (e.box.width * 0.5f);
        float ny = (e.pupil.y - (e.box.y + e.box.height * 0.5f)) / (e.box.height * 0.5f);
        gx += nx; gy += ny; ++n;
    }
    if (n > 0) {
        res.gaze = cv::Point2f(gx / n, gy / n);
        // 限幅
        res.gaze.x = std::max(-1.f, std::min(1.f, res.gaze.x));
        res.gaze.y = std::max(-1.f, std::min(1.f, res.gaze.y));
    }
    return res;
}
