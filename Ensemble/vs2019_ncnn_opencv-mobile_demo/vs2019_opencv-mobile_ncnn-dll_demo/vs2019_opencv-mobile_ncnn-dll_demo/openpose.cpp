#include "openpose.h"

OpenPose::OpenPose()
{
    net.load_param("assert/OpenPose/openpose-sim-opt.param");
    net.load_model("assert/OpenPose/openpose-sim-opt.bin");
}
OpenPose::~OpenPose()
{
    net.clear();
}

std::vector<cv::Mat> OpenPose::call(cv::Mat rgb)
{
    cv::Mat oriImg;
    cv::cvtColor(rgb, oriImg, cv::COLOR_RGB2BGR);


    //����
    cv::Mat imageToTest;
    cv::resize(oriImg, imageToTest, cv::Size(184, 184), 0.0, 0.0, cv::INTER_CUBIC);
    ncnn::Mat data = ncnn::Mat::from_pixels(imageToTest.data, ncnn::Mat::PIXEL_BGR, imageToTest.cols, imageToTest.rows);
    data.substract_mean_normalize(mean_vals, norm_vals);
    ncnn::Extractor ex = net.create_extractor();
    ex.input("data", data);
    ncnn::Mat Mconv7_stage6_L1, Mconv7_stage6_L2;
    ex.extract("Mconv7_stage6_L1", Mconv7_stage6_L1);
    ex.extract("Mconv7_stage6_L2", Mconv7_stage6_L2);


    // ��һ�������
    cv::Mat weightH(cv::Size(1, 25), CV_64FC1, weight_raw);
    cv::Mat weightW(cv::Size(25, 1), CV_64FC1, weight_raw);

    std::vector<std::vector<std::vector<float>>> all_peaks;
    int peak_counter = 0;
    for (int part = 0; part < 18; part++) {
        cv::Mat map_ori(cv::Size(256, 256), CV_32FC1, Mconv7_stage6_L2.channel(part));
        cv::Mat one_heatmap;
        cv::filter2D(map_ori, one_heatmap, -1, weightH, cv::Point(-1, -1), (0.0), CV_HAL_BORDER_REFLECT);
        cv::filter2D(one_heatmap, one_heatmap, -1, weightW, cv::Point(-1, -1), (0.0), CV_HAL_BORDER_REFLECT);

        cv::Mat map_left = cv::Mat::zeros(one_heatmap.size(), CV_32FC1);
        one_heatmap(cv::Range(0, 255), cv::Range(0, 256)).copyTo(map_left(cv::Range(1, 256), cv::Range(0, 256)));
        cv::Mat map_right = cv::Mat::zeros(one_heatmap.size(), CV_32FC1);
        one_heatmap(cv::Range(1, 256), cv::Range(0, 256)).copyTo(map_right(cv::Range(0, 255), cv::Range(0, 256)));
        cv::Mat map_up = cv::Mat::zeros(one_heatmap.size(), CV_32FC1);
        one_heatmap(cv::Range(0, 256), cv::Range(0, 255)).copyTo(map_up(cv::Range(0, 256), cv::Range(1, 256)));
        cv::Mat map_down = cv::Mat::zeros(one_heatmap.size(), CV_32FC1);
        one_heatmap(cv::Range(0, 256), cv::Range(1, 256)).copyTo(map_down(cv::Range(0, 256), cv::Range(0, 255)));

        cv::Mat peaks_binary = cv::Mat::zeros(one_heatmap.size(), CV_32FC1);
        for (int i = 0; i < 256; i++) {
            for (int j = 0; j < 256; j++) {
                peaks_binary.at<float>(i, j) = (one_heatmap.at<float>(i, j) >= map_left.at<float>(i, j))
                    && (one_heatmap.at<float>(i, j) >= map_right.at<float>(i, j))
                    && (one_heatmap.at<float>(i, j) >= map_up.at<float>(i, j))
                    && (one_heatmap.at<float>(i, j) >= map_down.at<float>(i, j))
                    && (one_heatmap.at<float>(i, j) > 0.1);
            }
        }

        std::vector<cv::Point2i> peaks;
        cv::findNonZero(peaks_binary, peaks);

        std::vector<std::vector<float>> peaks_with_score;
        for (int i = 0; i < peaks.size(); i++) {
            std::vector<float> x = { float(peaks[i].x), float(peaks[i].y), map_ori.at<float>(int(peaks[i].y),int(peaks[i].x)) };
            peaks_with_score.push_back(x);
        }

        std::vector<int> peak_id;
        for (int i = 0; i < peaks.size(); i++) {
            peak_id.push_back(peak_counter + i);
        }

        std::vector<std::vector<float>> peaks_with_score_and_id;
        for (int i = 0; i < peak_id.size(); i++) {
            std::vector<float> x = peaks_with_score[i];
            x.push_back(peak_id[i]);
            peaks_with_score_and_id.push_back(x);
        }

        all_peaks.push_back(peaks_with_score_and_id);
        peak_counter += peaks.size();
    }


    // �ڶ��������
    std::vector<cv::Mat> connection_all;
    std::vector<int> special_k;
    int mid_num = 10;
    for (int k = 0; k < 19; k++) {
        cv::Mat score_mid_0(cv::Size(256, 256), CV_32FC1, Mconv7_stage6_L1.channel(mapIdx[k][0] - 19));
        cv::Mat score_mid_1(cv::Size(256, 256), CV_32FC1, Mconv7_stage6_L1.channel(mapIdx[k][1] - 19));
        auto candA = all_peaks[limbSeq[k][0] - 1];
        auto candB = all_peaks[limbSeq[k][1] - 1];
        int nA = candA.size(), nB = candB.size();
        int indexA = limbSeq[k][0], indexB = limbSeq[k][1];
        if (nA != 0 && nB != 0) {
            std::vector<std::vector<float>> connection_candidate;
            for (int i = 0; i < nA; i++) {
                for (int j = 0; j < nB; j++) {
                    double vec[2] = { candB[j][0] - candA[j][0],candB[j][1] - candA[j][1] };
                    double norm = std::max(0.001, std::sqrt(vec[0] * vec[0] + vec[1] * vec[1]));
                    vec[0] = vec[0] / norm;
                    vec[1] = vec[1] / norm;

                    std::vector<float> startend_0 = linspace(candA[i][0], candB[j][0], mid_num);
                    std::vector<float> startend_1 = linspace(candA[i][1], candB[j][1], mid_num);

                    cv::Mat vec_x(1, 10, CV_32FC1);
                    cv::Mat vec_y(1, 10, CV_32FC1);
                    for (int I = 0; I < startend_0.size(); I++) {
                        vec_x.at<float>(0, I) = score_mid_0.at<float>(int(round(startend_1[I])), int(round(startend_0[I])));
                        vec_y.at<float>(0, I) = score_mid_1.at<float>(int(round(startend_1[I])), int(round(startend_0[I])));
                    }

                    cv::Mat score_midpts = vec_x * vec[0] + vec_y * vec[1];
                    float score_with_dist_prior = cv::sum(score_midpts)[0] / score_midpts.cols + std::min(0.5 * oriImg.rows / norm - 1, 0.0);
                    cv::threshold(score_midpts, score_midpts, 0.05, 1, cv::THRESH_BINARY);

                    std::vector<cv::Point2i> score_midpts_thresh;
                    cv::findNonZero(score_midpts, score_midpts_thresh);
                    bool criterion1 = score_midpts_thresh.size() > 0.8 * score_midpts.cols;
                    bool criterion2 = score_with_dist_prior > 0;

                    if (criterion1 && criterion2) {
                        std::vector<float> x = { float(i), float(j), score_with_dist_prior, score_with_dist_prior + candA[i][2] + candB[j][2] };
                        connection_candidate.push_back(x);
                    }
                }
            }
            std::sort(connection_candidate.begin(), connection_candidate.end(), [](std::vector<float>& v1, std::vector<float>& v2)->bool {return v1[2] > v2[2]; });

            cv::Mat connection(0, 5, CV_32FC1);
            for (int c = 0; c < connection_candidate.size(); c++) {
                float i = connection_candidate[c][0];
                float j = connection_candidate[c][1];
                float s = connection_candidate[c][2];
                if ((findValue(connection(cv::Range(0, connection.rows), cv::Range(3, 4)).clone(), i) == false)
                    && (findValue(connection(cv::Range(0, connection.rows), cv::Range(4, 5)).clone(), j) == false)) {
                    cv::Mat _connection_ = (cv::Mat_<float>(1, 5) << candA[i][3], candB[j][3], s, i, j);
                    cv::vconcat(connection, _connection_, connection);
                    if (connection.rows >= std::min(nA, nB)) {
                        break;
                    }
                }
            }
            connection_all.push_back(connection);
        }
        else {
            special_k.push_back(k);
            cv::Mat connection;
            connection_all.push_back(connection);
        }
    }


    // �����������
    cv::Mat candidate(0, 4, CV_32FC1);
    for (std::vector<std::vector<float>>& sublist : all_peaks) {
        for (std::vector<float>& item : sublist) {
            cv::Mat _candidate_ = cv::Mat(item);
            cv::transpose(_candidate_, _candidate_);
            cv::vconcat(candidate, _candidate_, candidate);
        }
    }
    cv::Mat subset = cv::Mat::ones(0, 20, CV_32FC1);


    // �����������
    for (int k = 0; k < 19; k++) {
        if (std::find(special_k.begin(), special_k.end(), k) == special_k.end()) {
            cv::Mat partAs = connection_all[k](cv::Range(0, connection_all[k].rows), cv::Range(0, 1)).clone();
            cv::Mat partBs = connection_all[k](cv::Range(0, connection_all[k].rows), cv::Range(1, 2)).clone();
            int indexA = limbSeq[k][0] - 1, indexB = limbSeq[k][1] - 1;

            for (int i = 0; i < connection_all[k].rows; i++) {
                int found = 0;
                int subset_idx[2] = { -1,-1 };
                for (int j = 0; j < subset.rows; j++) {
                    if ((subset.at<float>(j, indexA) == partAs.at<float>(i, 0)) || (subset.at<float>(j, indexB) == partBs.at<float>(i, 0))) {
                        subset_idx[found] = j;
                        found += 1;
                    }
                }

                if (found == 1) {
                    int j = subset_idx[0];
                    if (subset.at<float>(j, indexB) != partBs.at<float>(i, 0)) {
                        subset.at<float>(j, indexB) = partBs.at<float>(i, 0);
                        subset.at<float>(j, subset.cols - 1) += 1;
                        subset.at<float>(j, subset.cols - 2) += candidate.at<float>(partBs.at<float>(i, 0), 2) + connection_all[k].at<float>(i, 2);
                    }
                }
                else if (found == 2) { // ��һ����ûд����������ͽ���������
                    ;
                }
                else if ((found == 0) && (k < 17)) {
                    cv::Mat row = -1 * cv::Mat::ones(1, 20, CV_32FC1);
                    row.at<float>(0, indexA) = partAs.at<float>(i, 0);
                    row.at<float>(0, indexB) = partBs.at<float>(i, 0);
                    row.at<float>(0, row.cols - 1) = 2;
                    row.at<float>(0, row.cols - 2) = candidate.at<float>(connection_all[k].at<float>(i, 0), 2)
                        + candidate.at<float>(connection_all[k].at<float>(i, 1), 2)
                        + connection_all[k].at<float>(i, 2);
                    cv::vconcat(subset, row, subset);
                }
            }
        }
    }


    // deleteҲû������������Ҳ���������


    std::vector<cv::Mat> res = { candidate,subset };
    return res;
}

std::vector<float> OpenPose::linspace(int start, int end, int num)
{
    std::vector<float> res(num, 0);
    res[0] = start;
    double d = (end - start) / (num - 1.0);
    for (int i = 1; i < num; i++)
        res[i] = res[i - 1] + d;
    return res;
}

bool OpenPose::findValue(const cv::Mat& mat, double value) 
{
    for (int i = 0; i < mat.rows; i++) {
        const double* row = mat.ptr<double>(i);
        if (std::find(row, row + mat.cols, value) != row + mat.cols)
            return true;
    }
    return false;
}