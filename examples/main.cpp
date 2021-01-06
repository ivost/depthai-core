#include <iostream>
#include <unistd.h>

#include <csignal>

#include "depthai/depthai.hpp"
#include "../src/utility/Resources.hpp"

#include "depthai/pipeline/node/ColorCamera.hpp"
#include "depthai/pipeline/node/MonoCamera.hpp"
#include "depthai/pipeline/node/XLinkOut.hpp"
#include "depthai/pipeline/node/NeuralNetwork.hpp"
#include "depthai/pipeline/node/SPIOut.hpp"
#include "depthai/pipeline/node/XLinkIn.hpp"
#include "depthai/pipeline/node/MyProducer.hpp"
#include "depthai/pipeline/node/VideoEncoder.hpp"
#include "depthai/pipeline/node/ImageManip.hpp"
#include "depthai/pipeline/node/StereoDepth.hpp"

#include "depthai/pipeline/datatype/NNData.hpp"
#include "depthai/pipeline/datatype/ImgFrame.hpp"
#include "depthai/pipeline/datatype/ImageManipConfig.hpp"

#include "opencv2/opencv.hpp"
#include "fp16/fp16.h"


#include "XLinkLog.h"


static XLinkDeviceState_t xlinkState = X_LINK_UNBOOTED;

std::string protocolToString(XLinkProtocol_t p){
    
    switch (p){
        case X_LINK_USB_VSC : return {"X_LINK_USB_VSC"}; break;
        case X_LINK_USB_CDC : return {"X_LINK_USB_CDC"}; break;
        case X_LINK_PCIE : return {"X_LINK_PCIE"}; break;
        case X_LINK_IPC : return {"X_LINK_IPC"}; break;
        case X_LINK_NMB_OF_PROTOCOLS : return {"X_LINK_NMB_OF_PROTOCOLS"}; break;
        case X_LINK_ANY_PROTOCOL : return {"X_LINK_ANY_PROTOCOL"}; break;
    }    
    return {"UNDEFINED"};
}

std::string platformToString(XLinkPlatform_t p){
    
    switch (p) {
        case X_LINK_ANY_PLATFORM : return {"X_LINK_ANY_PLATFORM"}; break;
        case X_LINK_MYRIAD_2 : return {"X_LINK_MYRIAD_2"}; break;
        case X_LINK_MYRIAD_X : return {"X_LINK_MYRIAD_X"}; break;
    }
    return {"UNDEFINED"};
}

std::string stateToString(XLinkDeviceState_t p){
    
    switch (p) {
        case X_LINK_ANY_STATE : return {"X_LINK_ANY_STATE"}; break;
        case X_LINK_BOOTED : return {"X_LINK_BOOTED"}; break;
        case X_LINK_UNBOOTED : return {"X_LINK_UNBOOTED"}; break;
        case X_LINK_BOOTLOADER : return {"X_LINK_BOOTLOADER"}; break;
    }    
    return {"UNDEFINED"};
}

cv::Mat toMat(const std::vector<uint8_t>& data, int w, int h , int numPlanes, int bpp){
    
    cv::Mat frame;

    if(numPlanes == 3){
        frame = cv::Mat(h, w, CV_8UC3);

        // optimization (cache)
        for(int i = 0; i < w*h; i++) {
            uint8_t b = data.data()[i + w*h * 0];
            frame.data[i*3+0] = b;
        }
        for(int i = 0; i < w*h; i++) {                
            uint8_t g = data.data()[i + w*h * 1];    
            frame.data[i*3+1] = g;
        }
        for(int i = 0; i < w*h; i++) {
            uint8_t r = data.data()[i + w*h * 2];
            frame.data[i*3+2] = r;
        }
                    
    } else {
        if(bpp == 3){
            frame = cv::Mat(h, w, CV_8UC3);
            for(int i = 0; i < w*h*bpp; i+=3) {
                uint8_t b,g,r;
                b = data.data()[i + 2];
                g = data.data()[i + 1];
                r = data.data()[i + 0];    
                frame.at<cv::Vec3b>( (i/bpp) / w, (i/bpp) % w) = cv::Vec3b(b,g,r);
            }

        } else if(bpp == 6) {
            //first denormalize
            //dump
            
            frame = cv::Mat(h, w, CV_8UC3);
            for(int y = 0; y < h; y++){
                for(int x = 0; x < w; x++){

                    const uint16_t* fp16 = (const uint16_t*) (data.data() + (y*w+x)*bpp);                        
                    uint8_t r = (uint8_t) (fp16_ieee_to_fp32_value(fp16[0]) * 255.0f);
                    uint8_t g = (uint8_t) (fp16_ieee_to_fp32_value(fp16[1]) * 255.0f);
                    uint8_t b = (uint8_t) (fp16_ieee_to_fp32_value(fp16[2]) * 255.0f);
                    frame.at<cv::Vec3b>(y, x) = cv::Vec3b(b,g,r);
                }
            }
            
        }
    }

    return frame;
}


void toPlanar(cv::Mat& bgr, std::vector<std::uint8_t>& data){

    data.resize(bgr.cols * bgr.rows * 3);
    for(int y = 0; y < bgr.rows; y++){
        for(int x = 0; x < bgr.cols; x++){
            auto p = bgr.at<cv::Vec3b>(y,x);
            data[x + y*bgr.cols + 0 * bgr.rows*bgr.cols] = p[0];
            data[x + y*bgr.cols + 1 * bgr.rows*bgr.cols] = p[1];
            data[x + y*bgr.cols + 2 * bgr.rows*bgr.cols] = p[2];
        }
    }


/*
    std::vector<cv::Mat> planes(3);
    for(unsigned int i = 0; planes.size(); i++){ 
        cv::extractChannel(bgr, planes[i], i);
    }

    cv::Mat planarBgr;
    cv::vconcat(planes, planarBgr);

    data.clear();
    data.assign(planarBgr.data, planarBgr.data + planarBgr.total()*planarBgr.channels());
*/
}


cv::Mat resizeKeepAspectRatio(const cv::Mat &input, const cv::Size &dstSize, const cv::Scalar &bgcolor)
{
    cv::Mat output;

    double h1 = dstSize.width * (input.rows/(double)input.cols);
    double w2 = dstSize.height * (input.cols/(double)input.rows);
    if( h1 <= dstSize.height) {
        cv::resize( input, output, cv::Size(dstSize.width, h1));
    } else {
        cv::resize( input, output, cv::Size(w2, dstSize.height));
    }

    int top = (dstSize.height-output.rows) / 2;
    int down = (dstSize.height-output.rows+1) / 2;
    int left = (dstSize.width - output.cols) / 2;
    int right = (dstSize.width - output.cols+1) / 2;

    cv::copyMakeBorder(output, output, top, down, left, right, cv::BORDER_CONSTANT, bgcolor );

    return output;
}



dai::Pipeline createNNPipeline(std::string nnPath){


    dai::Pipeline p;

    auto colorCam = p.create<dai::node::ColorCamera>();
    auto xlinkOut = p.create<dai::node::XLinkOut>();
    auto nn1 = p.create<dai::node::NeuralNetwork>();
    auto nnOut = p.create<dai::node::XLinkOut>();


    nn1->setBlobPath(nnPath);

    xlinkOut->setStreamName("preview");
    nnOut->setStreamName("detections");    

    colorCam->setPreviewSize(300, 300);
    colorCam->setResolution(dai::ColorCameraProperties::SensorResolution::THE_1080_P);
    colorCam->setInterleaved(false);
    colorCam->setCamId(0);
    colorCam->setColorOrder(dai::ColorCameraProperties::ColorOrder::BGR);

    // Link plugins CAM -> NN -> XLINK
    colorCam->preview.link(nn1->input);
    colorCam->preview.link(xlinkOut->input);
    nn1->out.link(nnOut->input);

    return p;

}

dai::Pipeline createCameraPipeline(){



    dai::Pipeline p;

    auto colorCam = p.create<dai::node::ColorCamera>();
    auto xlinkOut = p.create<dai::node::XLinkOut>();
    xlinkOut->setStreamName("preview");
    
    colorCam->setPreviewSize(300, 300);
    colorCam->setResolution(dai::ColorCameraProperties::SensorResolution::THE_1080_P);
    colorCam->setInterleaved(true);
    colorCam->setCamId(0);

    // Link plugins CAM -> XLINK
    colorCam->preview.link(xlinkOut->input);
    
    return p;

}


dai::Pipeline createCameraFullPipeline(){



    dai::Pipeline p;

    auto colorCam = p.create<dai::node::ColorCamera>();
    auto xlinkOut = p.create<dai::node::XLinkOut>();
    xlinkOut->setStreamName("video");
    
    colorCam->setPreviewSize(300, 300);
    colorCam->setResolution(dai::ColorCameraProperties::SensorResolution::THE_1080_P);
    colorCam->setInterleaved(true);
    colorCam->setCamId(0);

    // Link plugins CAM -> XLINK
    colorCam->video.link(xlinkOut->input);
    
    return p;

}

void startPreview(){

    dai::Pipeline p = createCameraPipeline();

    using namespace std;

    bool found;
    dai::DeviceInfo deviceInfo;
    std::tie(found, deviceInfo) = dai::XLinkConnection::getFirstDevice(xlinkState);

    if(found) {
        dai::Device d(p, deviceInfo);

        d.startPipeline();

        
        cv::Mat frame;
        auto preview = d.getOutputQueue("preview");

        while(1){

            auto imgFrame = preview->get<dai::ImgFrame>();
            if(imgFrame){

                printf("Frame - w: %d, h: %d\n", imgFrame->getWidth(), imgFrame->getHeight());

                frame = cv::Mat(imgFrame->getHeight(), imgFrame->getWidth(), CV_8UC3, imgFrame->getData().data());
                           
                cv::imshow("preview", frame);
                cv::waitKey(1);
            } else {
                std::cout << "Not ImgFrame" << std::endl;
            }
            
        }

    } else {
        cout << "No devices with state " << stateToString(xlinkState) << " found..." << endl;
    }

    
}

void startVideo(){

    dai::Pipeline p = createCameraFullPipeline();

    using namespace std;

    bool found;
    dai::DeviceInfo deviceInfo;
    std::tie(found, deviceInfo) = dai::XLinkConnection::getFirstDevice(xlinkState);

    if(found) {
        dai::Device d(p, deviceInfo);

        d.startPipeline();

        
        cv::Mat frame;
        auto preview = d.getOutputQueue("preview");
        auto detections = d.getOutputQueue("detections");



        while(1){

            auto imgFrame = preview->get<dai::ImgFrame>();
            if(imgFrame){

                printf("Frame - w: %d, h: %d\n", imgFrame->getWidth(), imgFrame->getHeight());

                frame = cv::Mat(imgFrame->getHeight() * 3 / 2, imgFrame->getWidth(), CV_8UC1, imgFrame->getData().data());
                
                cv::Mat rgb(imgFrame->getHeight(), imgFrame->getWidth(), CV_8UC3);

                cv::cvtColor(frame, rgb, cv::COLOR_YUV2BGR_NV12);
                
                cv::imshow("video", rgb);
                cv::waitKey(1);
            } else {
                std::cout << "Not ImgFrame" << std::endl;
            }
            
        }

    } else {
        cout << "No devices with state " << stateToString(xlinkState) << " found..." << endl;
    }


}


void startNN(std::string nnPath){
    using namespace std;

    dai::Pipeline p = createNNPipeline(nnPath);

    bool found;
    dai::DeviceInfo deviceInfo;
    std::tie(found, deviceInfo) = dai::XLinkConnection::getFirstDevice(xlinkState);

    if(found) {
        dai::Device d(p, deviceInfo);

        d.startPipeline();

        
        cv::Mat frame;
        auto preview = d.getOutputQueue("preview");
        auto detections = d.getOutputQueue("detections");

        while(1){

            auto imgFrame = preview->get<dai::ImgFrame>();
            if(imgFrame){

                printf("Frame - w: %d, h: %d\n", imgFrame->getWidth(), imgFrame->getHeight());
                frame = toMat(imgFrame->getData(), imgFrame->getWidth(), imgFrame->getHeight(), 3, 1);

            }

            struct Detection {
                unsigned int label;
                float score;
                float x_min;
                float y_min;
                float x_max;
                float y_max;
            };

            vector<Detection> dets;

            auto det = detections->get<dai::NNData>();
            std::vector<float> detData = det->getFirstLayerFp16();
            if(detData.size() > 0){
                int i = 0;
                while (detData[i*7] != -1.0f) {
                    Detection d;
                    d.label = detData[i*7 + 1];
                    d.score = detData[i*7 + 2];
                    d.x_min = detData[i*7 + 3];
                    d.y_min = detData[i*7 + 4];
                    d.x_max = detData[i*7 + 5];
                    d.y_max = detData[i*7 + 6];
                    i++;
                    dets.push_back(d);
                }
            }

            for(const auto& d : dets){
                int x1 = d.x_min * frame.cols;
                int y1 = d.y_min * frame.rows;
                int x2 = d.x_max * frame.cols;
                int y2 = d.y_max * frame.rows;

                cv::rectangle(frame, cv::Rect(cv::Point(x1, y1), cv::Point(x2, y2)), cv::Scalar(255,255,255));
            }

            printf("===================== %lu detection(s) =======================\n", dets.size());
            for (unsigned det = 0; det < dets.size(); ++det) {
                printf("%5d | %6.4f | %7.4f | %7.4f | %7.4f | %7.4f\n",
                        dets[det].label,
                        dets[det].score,
                        dets[det].x_min,
                        dets[det].y_min,
                        dets[det].x_max,
                        dets[det].y_max);
            }


            cv::imshow("preview", frame);
            cv::waitKey(1);

        }

    } else {
        cout << "No devices with state " << stateToString(xlinkState) << " found..." << endl;
    }

}



void startWebcam(int camId, std::string nnPath){

    using namespace std;


    // CREATE PIPELINE
    dai::Pipeline p;

    auto xin = p.create<dai::node::XLinkIn>();
    //auto producer = p.create<dai::node::MyProducer>();
    auto nn = p.create<dai::node::NeuralNetwork>();
    auto xout = p.create<dai::node::XLinkOut>();


    //producer->setProcessor(dai::ProcessorType::LOS);
    nn->setBlobPath(nnPath);
    
    xin->setStreamName("nn_in");
    xin->setMaxDataSize(300*300*3);
    xin->setNumFrames(4);

    xout->setStreamName("nn_out");

    // Link plugins XLINK -> NN -> XLINK
    xin->out.link(nn->input);
    //producer->out.link(nn->input);

    nn->out.link(xout->input);



    // CONNECT TO DEVICE

    bool found;
    dai::DeviceInfo deviceInfo;
    std::tie(found, deviceInfo) = dai::XLinkConnection::getFirstDevice(xlinkState);

    if(found) {
        dai::Device d(p, deviceInfo);

        d.startPipeline();

        
        cv::VideoCapture webcam(camId);
    
        cv::Mat frame;
        auto in = d.getInputQueue("nn_in");
        auto detections = d.getOutputQueue("nn_out");

        while(1){
            
            // data to send further
            auto tensor = std::make_shared<dai::RawBuffer>();

            // Read frame from webcam
            webcam >> frame;

            // crop and resize
            frame = resizeKeepAspectRatio(frame, cv::Size(300,300), cv::Scalar(0));

            // transform to BGR planar 300x300
            toPlanar(frame, tensor->data);

            //tensor->data = std::vector<std::uint8_t>(frame.data, frame.data + frame.total());
            in->send(tensor);

            struct Detection {
                unsigned int label;
                float score;
                float x_min;
                float y_min;
                float x_max;
                float y_max;
            };

            vector<Detection> dets;

            auto det = detections->get<dai::NNData>();
            std::vector<float> detData = det->getFirstLayerFp16();
            if(detData.size() > 0){
                int i = 0;
                while (detData[i*7] != -1.0f) {
                    Detection d;
                    d.label = detData[i*7 + 1];
                    d.score = detData[i*7 + 2];
                    d.x_min = detData[i*7 + 3];
                    d.y_min = detData[i*7 + 4];
                    d.x_max = detData[i*7 + 5];
                    d.y_max = detData[i*7 + 6];
                    i++;
                    dets.push_back(d);
                }
            }

            for(const auto& d : dets){
                int x1 = d.x_min * frame.cols;
                int y1 = d.y_min * frame.rows;
                int x2 = d.x_max * frame.cols;
                int y2 = d.y_max * frame.rows;

                cv::rectangle(frame, cv::Rect(cv::Point(x1, y1), cv::Point(x2, y2)), cv::Scalar(255,255,255));
            }

            printf("===================== %lu detection(s) =======================\n", dets.size());
            for (unsigned det = 0; det < dets.size(); ++det) {
                printf("%5d | %6.4f | %7.4f | %7.4f | %7.4f | %7.4f\n",
                        dets[det].label,
                        dets[det].score,
                        dets[det].x_min,
                        dets[det].y_min,
                        dets[det].x_max,
                        dets[det].y_max);
            }

            cv::imshow("preview", frame);
            cv::waitKey(1);

        }

    } else {
        cout << "No devices with state " << stateToString(xlinkState) << " found..." << endl;
    }

}


/*
void startTest(int id){


    using namespace std;

    // CONNECT TO DEVICE
    bool found;
    dai::DeviceInfo deviceInfo;
    std::tie(found, deviceInfo) = dai::XLinkConnection::getFirstDevice(xlinkState);

    if(found) {
        dai::Device d(p, deviceInfo);
        d.startTestPipeline(id);
        while(1);
    }

}
*/



void startMjpegCam(){

    using namespace std;


    dai::Pipeline p;

    auto colorCam = p.create<dai::node::ColorCamera>();
    auto xout = p.create<dai::node::XLinkOut>();
    auto xout2 = p.create<dai::node::XLinkOut>();
    auto videnc = p.create<dai::node::VideoEncoder>();


    // XLinkOut
    xout->setStreamName("mjpeg");
    xout2->setStreamName("preview");

    // ColorCamera    
    colorCam->setPreviewSize(300, 300);
    colorCam->setResolution(dai::ColorCameraProperties::SensorResolution::THE_1080_P);
    //colorCam->setFps(5.0);
    colorCam->setInterleaved(true);
    colorCam->setCamId(0);

    // VideoEncoder
    videnc->setDefaultProfilePreset(1920, 1080, 30, dai::VideoEncoderProperties::Profile::MJPEG);

    // Link plugins CAM -> XLINK
    colorCam->video.link(videnc->input);
    colorCam->preview.link(xout2->input);
    videnc->bitstream.link(xout->input);


    bool found;
    dai::DeviceInfo deviceInfo;
    std::tie(found, deviceInfo) = dai::XLinkConnection::getFirstDevice(xlinkState);

    std::cout << "Device info desc name: " << deviceInfo.desc.name << "\n";

    if(found) {
        dai::Device d(p, deviceInfo);

        d.startPipeline();
    
        auto mjpegQueue = d.getOutputQueue("mjpeg", 8, true);
        auto previewQueue = d.getOutputQueue("preview", 8, true);

        while(1){

            auto t1 = std::chrono::steady_clock::now();            
            
            auto preview = previewQueue->get<dai::ImgFrame>();

            auto t2 = std::chrono::steady_clock::now();
            cv::imshow("preview", cv::Mat(preview->getHeight(), preview->getWidth(), CV_8UC3, preview->getData().data()));
            auto t3 = std::chrono::steady_clock::now();
            auto mjpeg = mjpegQueue->get<dai::ImgFrame>();
            auto t4 = std::chrono::steady_clock::now();
            cv::Mat decodedFrame = cv::imdecode( cv::Mat(mjpeg->getData()), cv::IMREAD_COLOR);
            auto t5 = std::chrono::steady_clock::now();
            cv::imshow("mjpeg", decodedFrame);


            double tsPreview = preview->getTimestamp().sec + preview->getTimestamp().nsec / 1000000000.0;
            double tsMjpeg = mjpeg->getTimestamp().sec + mjpeg->getTimestamp().nsec / 1000000000.0;

            //for(int i = 0; i < 100; i++) cv::waitKey(1);

            int ms1 = std::chrono::duration_cast<std::chrono::milliseconds>(t2-t1).count();
            int ms2 = std::chrono::duration_cast<std::chrono::milliseconds>(t3-t2).count();
            int ms3 = std::chrono::duration_cast<std::chrono::milliseconds>(t4-t3).count();
            int ms4 = std::chrono::duration_cast<std::chrono::milliseconds>(t5-t4).count();
            int loop = std::chrono::duration_cast<std::chrono::milliseconds>(t5-t1).count();

            std::cout << ms1 << " " << ms2 << " " << ms3 << " " << ms4 << " loop: " << loop << "sync offset: " << tsPreview << " sync mjpeg " << tsMjpeg << std::endl;
            int key = cv::waitKey(1);
            if (key == 'q') raise(SIGINT);


        }

    } else {
        cout << "No devices with state " << stateToString(xlinkState) << " found..." << endl;
    }


}

void startMonoCam(bool withDepth) {
    using namespace std;

    dai::Pipeline p;

    auto monoLeft  = p.create<dai::node::MonoCamera>();
    auto monoRight = p.create<dai::node::MonoCamera>();
    auto xoutLeft  = p.create<dai::node::XLinkOut>();
    auto xoutRight = p.create<dai::node::XLinkOut>();
    auto stereo    = withDepth ? p.create<dai::node::StereoDepth>() : nullptr;
    auto xoutDisp  = p.create<dai::node::XLinkOut>();
    auto xoutDepth = p.create<dai::node::XLinkOut>();
    auto xoutRectifL = p.create<dai::node::XLinkOut>();
    auto xoutRectifR = p.create<dai::node::XLinkOut>();

    // XLinkOut
    xoutLeft->setStreamName("left");
    xoutRight->setStreamName("right");
    if (withDepth) {
        xoutDisp->setStreamName("disparity");
        xoutDepth->setStreamName("depth");
        xoutRectifL->setStreamName("rectified_left");
        xoutRectifR->setStreamName("rectified_right");
    }

    // MonoCamera
    monoLeft->setResolution(dai::MonoCameraProperties::SensorResolution::THE_720_P);
    monoLeft->setCamId(1);
    //monoLeft->setFps(5.0);
    monoRight->setResolution(dai::MonoCameraProperties::SensorResolution::THE_720_P);
    monoRight->setCamId(2);
    //monoRight->setFps(5.0);

    bool outputDepth = false;
    bool outputRectified = true;
    bool lrcheck  = true;
    bool extended = false;
    bool subpixel = true;

    int maxDisp = 96;
    if (extended) maxDisp *= 2;
    if (subpixel) maxDisp *= 32; // 5 bits fractional disparity

    if (withDepth) {
        // StereoDepth
        stereo->setOutputDepth(outputDepth);
        stereo->setOutputRectified(outputRectified);
        stereo->setConfidenceThreshold(200);
        stereo->setRectifyEdgeFillColor(0); // black, to better see the cutout
        //stereo->loadCalibrationFile("../../../../depthai/resources/depthai.calib");
        //stereo->setInputResolution(1280, 720);
        // TODO: median filtering is disabled on device with (lrcheck || extended || subpixel)
        //stereo->setMedianFilter(dai::StereoDepthProperties::MedianFilter::MEDIAN_OFF);
        stereo->setLeftRightCheck(lrcheck);
        stereo->setExtendedDisparity(extended);
        stereo->setSubpixel(subpixel);

        // Link plugins CAM -> STEREO -> XLINK
        monoLeft->out.link(stereo->left);
        monoRight->out.link(stereo->right);

        stereo->syncedLeft.link(xoutLeft->input);
        stereo->syncedRight.link(xoutRight->input);
        if(outputRectified)
        {
            stereo->rectifiedLeft.link(xoutRectifL->input);
            stereo->rectifiedRight.link(xoutRectifR->input);
        }
        stereo->disparity.link(xoutDisp->input);
        stereo->depth.link(xoutDepth->input);

    } else {
        // Link plugins CAM -> XLINK
        monoLeft->out.link(xoutLeft->input);
        monoRight->out.link(xoutRight->input);
    }


    // CONNECT TO DEVICE

    bool found;
    dai::DeviceInfo deviceInfo;
    std::tie(found, deviceInfo) = dai::XLinkConnection::getFirstDevice(xlinkState);

    if (found) {
        dai::Device d(p, deviceInfo);

        d.startPipeline();

        auto leftQueue = d.getOutputQueue("left", 8, true);
        auto rightQueue = d.getOutputQueue("right", 8, true);
        auto dispQueue = withDepth ? d.getOutputQueue("disparity", 8, true) : nullptr;
        auto depthQueue = withDepth ? d.getOutputQueue("depth", 8, true) : nullptr;
        auto rectifLeftQueue = withDepth ? d.getOutputQueue("rectified_left", 8, true) : nullptr;
        auto rectifRightQueue = withDepth ? d.getOutputQueue("rectified_right", 8, true) : nullptr;

        while (1) {
            auto t1 = std::chrono::steady_clock::now();
            auto left = leftQueue->get<dai::ImgFrame>();
            auto t2 = std::chrono::steady_clock::now();
            cv::imshow("left", cv::Mat(left->getHeight(), left->getWidth(), CV_8UC1, left->getData().data()));
            auto t3 = std::chrono::steady_clock::now();
            auto right = rightQueue->get<dai::ImgFrame>();
            auto t4 = std::chrono::steady_clock::now();
            cv::imshow("right", cv::Mat(right->getHeight(), right->getWidth(), CV_8UC1, right->getData().data()));
            auto t5 = std::chrono::steady_clock::now();

            if (withDepth) {
                // Note: in some configurations (if depth is enabled), disparity may output garbage data
                auto disparity = dispQueue->get<dai::ImgFrame>();
                cv::Mat disp(disparity->getHeight(), disparity->getWidth(),
                        subpixel ? CV_16UC1 : CV_8UC1, disparity->getData().data());
                disp.convertTo(disp, CV_8UC1, 255.0 / maxDisp); // Extend disparity range
                cv::imshow("disparity", disp);
                cv::Mat disp_color;
                cv::applyColorMap(disp, disp_color, cv::COLORMAP_JET);
                cv::imshow("disparity_color", disp_color);

                if (outputDepth) {
                    auto depth = depthQueue->get<dai::ImgFrame>();
                    cv::imshow("depth", cv::Mat(depth->getHeight(), depth->getWidth(),
                            CV_16UC1, depth->getData().data()));
                }

                if (outputRectified) {
                    auto rectifL = rectifLeftQueue->get<dai::ImgFrame>();
                    cv::imshow("rectified_left", cv::Mat(rectifL->getHeight(), rectifL->getWidth(),
                            CV_8UC1, rectifL->getData().data()));
                    auto rectifR = rectifRightQueue->get<dai::ImgFrame>();
                    cv::imshow("rectified_right", cv::Mat(rectifR->getHeight(), rectifR->getWidth(),
                            CV_8UC1, rectifR->getData().data()));
                }
            }

            int ms1 = std::chrono::duration_cast<std::chrono::milliseconds>(t2-t1).count();
            int ms2 = std::chrono::duration_cast<std::chrono::milliseconds>(t3-t2).count();
            int ms3 = std::chrono::duration_cast<std::chrono::milliseconds>(t4-t3).count();
            int ms4 = std::chrono::duration_cast<std::chrono::milliseconds>(t5-t4).count();
            int loop = std::chrono::duration_cast<std::chrono::milliseconds>(t5-t1).count();

            std::cout << ms1 << " " << ms2 << " " << ms3 << " " << ms4 << " loop: " << loop << std::endl;
            int key = cv::waitKey(1);
            if (key == 'q')
                raise(SIGINT);
        }
    } else {
        cout << "No devices with state " << stateToString(xlinkState) << " found..." << endl;
    }
}




void startCamManip(){

    using namespace std;
    try{



        dai::Pipeline pipeline;

        auto colorCam = pipeline.create<dai::node::ColorCamera>();
        auto imageManip = pipeline.create<dai::node::ImageManip>();
        auto imageManip2 = pipeline.create<dai::node::ImageManip>();
        auto camOut = pipeline.create<dai::node::XLinkOut>();
        auto manipOut = pipeline.create<dai::node::XLinkOut>();
        auto manipOut2 = pipeline.create<dai::node::XLinkOut>();
        auto manip2In = pipeline.create<dai::node::XLinkIn>();


        camOut->setStreamName("preview");
        manipOut->setStreamName("manip");
        manipOut2->setStreamName("manip2");
        manip2In->setStreamName("manip2In");

        
        colorCam->setPreviewSize(304, 304);
        colorCam->setResolution(dai::ColorCameraProperties::SensorResolution::THE_1080_P);
        colorCam->setInterleaved(false);
        colorCam->setColorOrder(dai::ColorCameraProperties::ColorOrder::BGR);
        colorCam->setCamId(0);

        // Create a center crop image manipulation
        imageManip->setCenterCrop(0.7f);
        imageManip->setResizeThumbnail(300, 400);

        
        // Second image manipulator - Create a off center crop
        imageManip2->setCropRect(0.1, 0.1, 0.3, 0.3);
        imageManip2->setWaitForConfigInput(true);



        // Link nodes CAM -> XLINK
        colorCam->preview.link(camOut->input);

        // Link nodes CAM -> imageManip -> XLINK
        colorCam->preview.link(imageManip->inputImage);
        imageManip->out.link(manipOut->input);

        // ImageManip -> ImageManip 2
        imageManip->out.link(imageManip2->inputImage);

        // ImageManip2 -> XLinkOut
        imageManip2->out.link(manipOut2->input);

        // Host config -> image manip 2
        manip2In->out.link(imageManip2->inputConfig);

        dai::Device device(pipeline);
        device.startPipeline();


        auto previewQueue = device.getOutputQueue("preview", 8, true);
        auto manipQueue = device.getOutputQueue("manip", 8, true);
        auto manipQueue2 = device.getOutputQueue("manip2", 8, true);
        auto manip2InQueue = device.getInputQueue("manip2In");

        

        // keep processing data
        int frameCounter = 0;
        float xmin = 0.1f;
        float xmax = 0.3f;
        while(true){

            xmin += 0.003f;
            xmax += 0.003f;
            if(xmax >= 1.0f){
                xmin = 0.0f;
                xmax = 0.2f;
            }

            dai::ImageManipConfig cfg;
            cfg.setCropRect(xmin, 0.1f, xmax, 0.3f);
            manip2InQueue->send(cfg);


            // Gets both image frames
            auto preview = previewQueue->get<dai::ImgFrame>();
            auto manip = manipQueue->get<dai::ImgFrame>();
            auto manip2 = manipQueue2->get<dai::ImgFrame>();

            auto matPreview = toMat(preview->getData(), preview->getWidth(), preview->getHeight(), 3, 1);
            auto matManip = toMat(manip->getData(), manip->getWidth(), manip->getHeight(), 3, 1);
            auto matManip2 = toMat(manip2->getData(), manip2->getWidth(), manip2->getHeight(), 3, 1);
            
            // Display both 
            cv::imshow("preview", matPreview);
            cv::imshow("manip", matManip);
            cv::imshow("manip2", matManip2);


            cv::waitKey(1);

            frameCounter++;

        }

        
    } catch(const std::exception& ex){
        std::cout << "Error: " << ex.what() << "\nExiting...\n";
    }  

}



dai::Pipeline createNNPipelineSPI(std::string nnPath){
    dai::Pipeline p;

    // set up NN node
    auto nn1 = p.create<dai::node::NeuralNetwork>();
    nn1->setBlobPath(nnPath);

    // set up color camera and link to NN node
    auto colorCam = p.create<dai::node::ColorCamera>();
    colorCam->setPreviewSize(300, 300);
    colorCam->setResolution(dai::ColorCameraProperties::SensorResolution::THE_1080_P);
    colorCam->setInterleaved(false);
    colorCam->setCamId(0);
    colorCam->setColorOrder(dai::ColorCameraProperties::ColorOrder::BGR);
    colorCam->preview.link(nn1->input);

    // set up SPI out node and link to nn1
    auto spiOut = p.create<dai::node::SPIOut>();
    spiOut->setStreamName("spimetaout");
    spiOut->setBusId(0);
    nn1->out.link(spiOut->input);

    // Watch out for memory usage on the target SPI device. It turns out ESP32 often doesn't have enough contiguous memory to hold a full 300x300 RGB preview image.
//    auto spiOut2 = p.create<dai::node::SPIOut>();
//    spiOut2->setStreamName("spipreview");
//    spiOut2->setBusId(0);
//    colorCam->preview.link(spiOut2->input);

    return p;
}

void startNNSPI(std::string nnPath){
    using namespace std;

    dai::Pipeline p = createNNPipelineSPI(nnPath);

    bool found;
    dai::DeviceInfo deviceInfo;
    std::tie(found, deviceInfo) = dai::XLinkConnection::getFirstDevice(xlinkState);

    if(found) {
        dai::Device d(p, deviceInfo);

        bool pipelineStarted = d.startPipeline();

        while(1){
            using namespace std::chrono_literals;
            std::this_thread::sleep_for(1s);
        }

    } else {
        cout << "No booted (debugger) devices found..." << endl;
    }

}



void listDevices(){
    mvLogDefaultLevelSet(MVLOG_DEBUG);

    // List all devices
    while(1){
        auto devices = dai::Device::getAllAvailableDevices();
        for(const auto& dev : devices){
            std::cout << "name: " << std::string(dev.desc.name);
            std::cout << ", state: " << stateToString(dev.state);
            std::cout << ", protocol: " << protocolToString(dev.desc.protocol);
            std::cout << ", platform: " << platformToString(dev.desc.platform);
            std::cout << std::endl;
        }


        using namespace std::chrono_literals;
        std::this_thread::sleep_for(500ms);
    }
}




int main(int argc, char** argv){
    using namespace std;
    using namespace std::chrono;
    using namespace std::chrono_literals;
    cout << "Hello World!" << endl;

    bool mono = false;
    bool depth = false;
    bool debug = false;
    bool manip = false;
    bool listdev = false;
    std::string nnPath;

    // Very basic argument parsing
    for (int i = 1; i < argc; i++) {
        std::string s = argv[i];
        if      (s == "mono")                mono    = true;
        else if (s == "depth")               depth   = true;
        else if (s == "manip")               manip   = true;
        else if (s == "debug"   || s == "d") debug   = true;
        else if (s == "listdev" || s == "l") listdev = true;
        else                                 nnPath  = s;
    }

    xlinkState = debug ? X_LINK_BOOTED : X_LINK_UNBOOTED;

    mvLogDefaultLevelSet(MVLOG_DEBUG);

    // List all devices
    while(listdev){
        auto devices = dai::Device::getAllAvailableDevices();
        for(const auto& dev : devices){
            std::cout << "name: " << std::string(dev.desc.name);
            std::cout << ", state: " << stateToString(dev.state);
            std::cout << ", protocol: " << protocolToString(dev.desc.protocol);
            std::cout << ", platform: " << platformToString(dev.desc.platform);
            std::cout << std::endl;
        }

        std::this_thread::sleep_for(100ms);
    }

    mvLogDefaultLevelSet(MVLOG_LAST);

    if (nnPath.empty()) {
        if (mono) {
            startMonoCam(false);
        } else if (depth) {
            startMonoCam(true);
        } else if (manip) {
            startCamManip();
        } else {
            startMjpegCam();
        }
    } else {
        startWebcam(0, nnPath);        
    }

    return 0;
}
