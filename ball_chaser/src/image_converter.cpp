#include <cv_bridge/cv_bridge.h>
#include <image_transport/image_transport.h>
#include <ros/ros.h>
#include <sensor_msgs/image_encodings.h>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

static const std::string RAW_WINDOW = "raw window";
static const std::string CONVERTED_WINDOW = "converted window";
static const std::string THRESHOLD_WINDOW = "threshold window";
static const std::string CONTOUR_WINDOW = "contour window";

// default thresholding values for selecting white
cv::Scalar min_thresh = cv::Scalar(0, 0, 250);
cv::Scalar max_thresh = cv::Scalar(5, 5, 255);

cv::Mat cv_hsv;
cv::Mat cv_grey;
cv::Mat thresh_img;
cv::Mat cv_bgr;

// calculates a range of values to threshold by, clamped at 0 and 180 or 255
std::tuple<int, int> get_min_max(int selected, int upper_bound = 255) {
  int lower = selected - 10;
  int upper = selected + 10;

  if (lower < 0) lower = 0;

  if (upper > upper_bound) upper = upper_bound;

  return std::make_tuple(lower, upper);
}

void onClick(int event, int x, int y, int, void*) {
  if (event != cv::EVENT_LBUTTONDOWN) return;

  // get hsv values at selected pixel
  cv::Vec3b selected_color = cv_hsv.at<cv::Vec3b>(cv::Point(x, y));
  /* std::cout << selected_color << std::endl; */

  // get upper and lower bounds for thresholding
  // hue has a max of 180
  int lower_hue, upper_hue, lower_saturation, upper_saturation, lower_value,
      upper_value;
  std::tie(lower_hue, upper_hue) = get_min_max(selected_color[0], 180);
  std::tie(lower_saturation, upper_saturation) = get_min_max(selected_color[1]);
  std::tie(lower_value, upper_saturation) = get_min_max(selected_color[2]);

  // threshold image
  min_thresh = cv::Scalar(lower_hue, lower_saturation, lower_value);
  max_thresh = cv::Scalar(upper_hue, upper_saturation, upper_value);
  inRange(cv_hsv, min_thresh, max_thresh, thresh_img);
}

class ImageConverter {
  ros::NodeHandle nh_;
  image_transport::ImageTransport it_;
  image_transport::Subscriber image_sub_;
  image_transport::Publisher image_pub_;

 public:
  ImageConverter() : it_(nh_) {
    // Subscrive to input video feed and publish output video feed
    image_sub_ = it_.subscribe("/camera/rgb/image_raw", 1,
                               &ImageConverter::imageCb, this);
    image_pub_ = it_.advertise("/image_converter/output_video", 1);

    cv::namedWindow(RAW_WINDOW);
  }

  ~ImageConverter() { cv::destroyWindow(RAW_WINDOW); }

  void imageCb(const sensor_msgs::ImageConstPtr& msg) {
    cv_bridge::CvImagePtr cv_ptr;
    try {
      cv_ptr = cv_bridge::toCvCopy(msg, sensor_msgs::image_encodings::BGR8);
    } catch (cv_bridge::Exception& e) {
      ROS_ERROR("cv_bridge exception: %s", e.what());
      return;
    }

    // convert input to OpenCV image
    cv_bgr = cv_ptr->image;
    // convert to hsv
    cv::cvtColor(cv_bgr, cv_hsv, cv::COLOR_BGR2HSV);

    cv::imshow(RAW_WINDOW, cv_ptr->image);

    // default thresholding to white
    inRange(cv_hsv, min_thresh, max_thresh, thresh_img);
    // threshold based on mouse click
    cv::setMouseCallback(RAW_WINDOW, onClick, 0);

    cv::imshow(CONVERTED_WINDOW, cv_hsv);
    cv::imshow(THRESHOLD_WINDOW, thresh_img);

    // find edge of ball
    std::vector<std::vector<cv::Point> > contours;
    std::vector<cv::Vec4i> hierarchy;
    cv::findContours(thresh_img, contours, hierarchy, CV_RETR_EXTERNAL,
                     CV_CHAIN_APPROX_SIMPLE, cv::Point(0, 0));

    // draw a contour to a copy of the input image
    cv::Mat contourOverlay = cv_bgr.clone();
    cv::drawContours(contourOverlay, contours, -1, CV_RGB(0, 0, 255), 2, 8,
                     hierarchy, 0);

    // find center of contour
    int center_x = 0;
    if (contours.size() > 0) {
      cv::Rect br = cv::boundingRect(contours[0]);
      center_x = br.x + br.width / 2;
      int center_y = br.y + br.height / 2;
      // draw center of contour on image
      cv::circle(contourOverlay, cv::Point(center_x, center_y), 5,
                 CV_RGB(255, 0, 0), -1);
    }

    // display image with contour and center, if there
    cv::imshow(CONTOUR_WINDOW, contourOverlay);

    // figure out location of ball in the image and set driving direction
    enum drive_direction { STOP = -1, RIGHT = 1, CENTER, LEFT };
    int img_width = cv_bgr.cols;

    drive_direction driving;
    if (center_x == 0)
      driving = STOP;
    else if (center_x < (img_width / 3))
      driving = LEFT;
    else if (center_x < (img_width / 3) * 2)
      driving = CENTER;
    else if (center_x <= img_width)
      driving = RIGHT;

    /* std::cout << driving << std::endl; */

    cv::waitKey(3);
  }
};

int main(int argc, char** argv) {
  ros::init(argc, argv, "image_converter");
  ImageConverter ic;
  ros::spin();
  return 0;
}
