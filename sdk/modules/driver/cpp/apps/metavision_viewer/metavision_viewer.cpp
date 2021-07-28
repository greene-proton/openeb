/**********************************************************************************************************************
 * Copyright (c) Prophesee S.A.                                                                                       *
 *                                                                                                                    *
 * Licensed under the Apache License, Version 2.0 (the "License");                                                    *
 * you may not use this file except in compliance with the License.                                                   *
 * You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0                                 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License is distributed   *
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.                      *
 * See the License for the specific language governing permissions and limitations under the License.                 *
 **********************************************************************************************************************/

// Example of using Metavision SDK Driver API for visualizing events stream.

#include <time.h>
#include <stdlib.h>
#include <stdio.h>

#include <iostream>
#include <boost/program_options.hpp>
#include <metavision/sdk/driver/camera.h>
#include <thread>
#include <chrono>
#include <opencv2/highgui/highgui.hpp>
#if CV_MAJOR_VERSION >= 4
#include <opencv2/highgui/highgui_c.h>
#endif
#include <metavision/sdk/base/utils/log.h>
#include <metavision/sdk/core/utils/cd_frame_generator.h>

static const int ESCAPE = 27;
static const int SPACE  = 32;

namespace po = boost::program_options;

std::string get_str_time(void);

int process_ui_for(int delay_ms) {
    auto then = std::chrono::high_resolution_clock::now();
    int key   = cv::waitKey(delay_ms);
    auto now  = std::chrono::high_resolution_clock::now();
    // cv::waitKey will not wait if no window is opened, so we wait for it, if needed
    std::this_thread::sleep_for(std::chrono::milliseconds(
        delay_ms - std::chrono::duration_cast<std::chrono::milliseconds>(now - then).count()));

    return key;
}

bool window_was_closed(const std::string &window_name) {
// Small hack: if the window has been closed, it is not visible anymore or property changes from the one we set
#if CV_MAJOR_VERSION >= 3 && CV_MINOR_VERSION >= 2
    if (cv::getWindowProperty(window_name, cv::WND_PROP_VISIBLE) == 0) {
#else
    if (cv::getWindowProperty(window_name, cv::WND_PROP_AUTOSIZE) != 0) {
#endif
        return true;
    }
    return false;
}

int setup_cd_callback_and_window(Metavision::Camera &camera, cv::Mat &cd_frame,
                                 Metavision::CDFrameGenerator &cd_frame_generator, const std::string &window_name) {
    auto &geometry = camera.geometry();
    auto id        = camera.cd().add_callback(
        [&cd_frame_generator](const Metavision::EventCD *ev_begin, const Metavision::EventCD *ev_end) {
            cd_frame_generator.add_events(ev_begin, ev_end);
        });
    cd_frame_generator.start(
        30, [&cd_frame](const Metavision::timestamp &ts, const cv::Mat &frame) { frame.copyTo(cd_frame); });
    cv::namedWindow(window_name, CV_GUI_EXPANDED);
    cv::resizeWindow(window_name, geometry.width(), geometry.height());
    cv::moveWindow(window_name, 0, 0);
    return id;
}

std::string get_str_time()
{
    time_t rawtime;
    struct tm * timeinfo;
    char buffer [80];

    time(&rawtime);
    timeinfo = localtime(&rawtime);

    strftime(buffer, 80, "%Y%m%d-%H%M%S", timeinfo);
    puts(buffer);
    return buffer;
}

int main(int argc, char *argv[]) {
    std::string serial;
    std::string biases_file;
    std::string in_raw_file_path;
    std::string out_raw_file_path;
    std::vector<uint16_t> roi;
    std::string str_time = get_str_time();
    std::string raw_filepath_w_date = ("%s-data.raw", str_time.c_str());
    std::cout << raw_filepath_w_date << std::endl;
    bool record_only;

    bool do_retry = false;

    const std::string short_program_desc(
        "Simple viewer to stream events from a RAW file or device, using the SDK driver API.\n");
    const std::string long_program_desc(short_program_desc +
                                        "Press SPACE key while running to record or stop recording raw data\n"
                                        "Press 'q' or Escape key to leave the program.\n"
                                        "Press 'r' to toggle the hardware ROI given as input.\n"
                                        "Press 'h' to print this help.\n");

    po::options_description options_desc("Options");
    // clang-format off
    options_desc.add_options()
        ("help,h", "Produce help message.")
        ("serial,s",          po::value<std::string>(&serial),"Serial ID of the camera. This flag is incompatible with flag '--input-raw-file'.")
        ("input-raw-file,i",  po::value<std::string>(&in_raw_file_path), "Path to input RAW file. If not specified, the camera live stream is used.")
        ("biases,b",          po::value<std::string>(&biases_file), "Path to a biases file. If not specified, the camera will be configured with the default biases.")
        ("output-raw-file,o", po::value<std::string>(&out_raw_file_path)->default_value(raw_filepath_w_date), "Path to an output RAW file used for data recording. Default value is 'data.raw'. It also works when reading data from a RAW file.")
        ("roi,r",             po::value<std::vector<uint16_t>>(&roi)->multitoken(), "Hardware ROI to set on the sensor in the format [x y width height].")
        ("record-only,g",     po::bool_switch(&record_only)->default_value(false), "Record only, do not show display")
    ;
    // clang-format on

    po::variables_map vm;
    try {
        po::store(po::command_line_parser(argc, argv).options(options_desc).run(), vm);
        po::notify(vm);
    } catch (po::error &e) {
        MV_LOG_ERROR() << short_program_desc;
        MV_LOG_ERROR() << options_desc;
        MV_LOG_ERROR() << "Parsing error:" << e.what();
        return 1;
    }

    if (vm.count("help")) {
        MV_LOG_INFO() << short_program_desc;
        MV_LOG_INFO() << options_desc;
        return 0;
    }

    MV_LOG_INFO() << long_program_desc;

    if (vm.count("roi")) {
        if (!in_raw_file_path.empty()) {
            MV_LOG_ERROR() << "Options --roi and --input-raw-file are not compatible.";
            return 1;
        }
        if (roi.size() != 4) {
            MV_LOG_WARNING() << "ROI as argument must be in the format 'x y width height '. Roi has not been set.";
            roi.clear();
        }
    }

    do {
        Metavision::Camera camera;
        bool camera_is_opened = false;

        // If the filename is set, then read from the file
        if (!in_raw_file_path.empty()) {
            if (!serial.empty()) {
                MV_LOG_ERROR() << "Options --serial and --input-raw-file are not compatible.";
                return 1;
            }

            try {
                camera           = Metavision::Camera::from_file(in_raw_file_path);
                camera_is_opened = true;
            } catch (Metavision::CameraException &e) { MV_LOG_ERROR() << e.what(); }
            // Otherwise, set the input source to the first available camera
        } else {
            try {
                if (!serial.empty()) {
                    camera = Metavision::Camera::from_serial(serial);
                } else {
                    camera = Metavision::Camera::from_first_available();
                }

                if (biases_file != "") {
                    camera.biases().set_from_file(biases_file);
                }

                if (!roi.empty()) {
                    camera.roi().set({roi[0], roi[1], roi[2], roi[3]});
                }
                camera_is_opened = true;
            } catch (Metavision::CameraException &e) { MV_LOG_ERROR() << e.what(); }
        }

        if (!camera_is_opened) {
            if (do_retry) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                MV_LOG_INFO() << "Trying to reopen camera...";
                continue;
            } else {
                return -1;
            }
        } else {
            MV_LOG_INFO() << "Camera has been opened successfully.";
        }

        // Add runtime error callback
        camera.add_runtime_error_callback([&do_retry](const Metavision::CameraException &e) {
            MV_LOG_ERROR() << e.what();
            do_retry = true;
        });
            
        // Get the geometry of the camera
        auto &geometry = camera.geometry(); // Get the geometry of the camera

        int cd_events_cb_id = 0;
        std::string cd_window_name("CD Events");
        cv::Mat cd_frame;
        Metavision::CDFrameGenerator cd_frame_generator(geometry.width(), geometry.height());
        cd_frame_generator.set_display_accumulation_time_us(10000);

        if (!record_only){

            // All cameras have CDs events
            cd_events_cb_id = setup_cd_callback_and_window(camera, cd_frame, cd_frame_generator, cd_window_name);
        }

        // Start the camera streaming
        camera.start();

        bool recording  = false;
        bool is_roi_set = true;

        if (record_only)
        {
            if (!recording) {
                camera.start_recording(out_raw_file_path);
                recording = true;
            } else {
                camera.stop_recording();
            }
        }
        MV_LOG_INFO() << (recording ? "true":"false");
        while (camera.is_running()) {
            if (!record_only)
            {
                if (!cd_frame.empty()) {
                    cv::imshow(cd_window_name, cd_frame);
                }
                // Wait for a pressed key for 33ms, that means that the display is refreshed at 30 FPS
                int key = process_ui_for(33);
                switch (key) {
                case 'q':
                    if (recording) {
                        camera.stop_recording();
                        recording = false;
                    }
                case ESCAPE:
                    camera.stop();
                    do_retry = false;
                    break;
                case SPACE:
                    if (!recording) {
                        camera.start_recording(out_raw_file_path);
                    } else {
                        camera.stop_recording();
                    }
                    recording = !recording;
                    break;
                case 'r': {
                    if (roi.size() == 0) {
                        break;
                    }
                    if (!is_roi_set) {
                        camera.roi().set({roi[0], roi[1], roi[2], roi[3]});
                    } else {
                        camera.roi().unset();
                    }
                    is_roi_set = !is_roi_set;
                    break;
                }
                case 'h':
                    MV_LOG_INFO() << long_program_desc;
                    break;
                default:
                    break;
                }
            }
            else
            {
                // Wait for a pressed key for 33ms, that means that the display is refreshed at 30 FPS
                int key = process_ui_for(33);
                switch (key) {
                case 'q':
                    if (recording) {
                        camera.stop_recording();
                        recording = false;
                    }
                }
            }
        }

        if (cd_events_cb_id >= 0) {
            camera.cd().remove_callback(cd_events_cb_id);
        }

        // Stop the camera streaming, optional, the destructor will automatically do it
        camera.stop();
    } while (do_retry);

    return 0;
}
