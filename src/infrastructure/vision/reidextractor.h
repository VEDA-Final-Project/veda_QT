#ifndef REIDEXTRACTOR_H
#define REIDEXTRACTOR_H

#include <QString>
#include <vector>
#include <opencv2/opencv.hpp>
#include <memory>

/**
 * @brief ReID 특징 추출 클래스
 * 사용자님께서 제공해주신 ONNX Runtime 코드를 기반으로 구현되었습니다.
 */
class ReIDFeatureExtractor {
public:
    ReIDFeatureExtractor();
    ~ReIDFeatureExtractor();

    // 모델 로드
    bool loadModel(const QString &modelPath);
    
    // 특징 추출
    std::vector<float> extract(const cv::Mat &image);

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl;
};

#endif // REIDEXTRACTOR_H
