#include "VanPt.h"
#include "LaneImage.hpp"
#include "Line.hpp"
#include "LaneMark.h"

VanPt::VanPt(float alpha_w, float alpha_h , vector<float>& params) : ALPHA_W(alpha_w), ALPHA_H(alpha_h), chnl_thresh(vector<int>(6, 0))
{
	#ifndef HIGH_BOT
	y_bottom_warp = min(img_size.height * 14/15, img_size.height -1 );
	y_bottom_warp_max = y_bottom_warp;
	#else
	y_bottom_warp = min(img_size.height *7/10, img_size.height -1 );  // for caltech data
	y_bottom_warp_max = y_bottom_warp;
	#endif

	// #ifdef CALI_VAN
	cali_van = true;
	if (params[0] == 0)
	{
		cali_van = false;
	}
	int margin_side;
	if (cali_van)
	{
		coef_pix_per_cm = params[0];
		van_pt_cali_y = params[1];
		float van_pt_cali_x = params[2];

		// coef_pix_per_cm = 0.0074;
		// van_pt_cali_y = 181;		//161.1941 // y = 0.0074(x-161.1941)
		// float van_pt_cali_x = 340;

		van_pt_cali = Point2f(van_pt_cali_x, van_pt_cali_y);
		van_pt_ini = van_pt_cali;
		theta_w = atan(tan(ALPHA_W)*((van_pt_ini.x - img_size.width/2)/(img_size.width/2))); 	// yaw angle 
		theta_h = atan(tan(ALPHA_H)*((van_pt_ini.y - img_size.height/2)/(img_size.height/2)));	// pitch angle

		float theta_h_cali = atan(tan(alpha_h)*(1- 2*van_pt_cali_y/img_size.height));

		float real_width_center = 1 / (coef_pix_per_cm*(img_size.height/2 - van_pt_cali_y)) * 60;
		float beta_w = atan(tan(alpha_w)*(2*60/img_size.width)); // 60 is half_width when calibrating van_pt
		float axis_dist = real_width_center / tan(beta_w) ;
		float cam_height = axis_dist * sin(theta_h_cali);

		float real_width_bottom = 1 / (coef_pix_per_cm*(y_bottom_warp - van_pt_cali_y)) * img_size.width;
		float warp_pix_per_cm = warp_col / real_width_bottom;
		min_width_pixel_warp = warp_pix_per_cm * 150; // assume lane width should be no less than 200cm

		cout << "warp_pix_per_cm: " << warp_pix_per_cm << ", min_width_pixel_warp: " << min_width_pixel_warp << endl; 
		cout << "real_width_bottom: " << real_width_bottom << endl;
		getchar();
		int corr_width = min(0.55*real_width_bottom, (double)warp_col-10);
		margin_side = (warp_col - corr_width)/2;
		margin_side = warp_col/6;
		min_width_pixel_warp = warp_col/6;
	}
	else
	{
		van_pt_ini = Point2f(img_size.width/2, 305); //img_size.height/2, 305 is estimated by eye for new data in Mcity
		van_pt_cali = van_pt_ini; 
		theta_w = 0; 	// yaw angle 
		theta_h = 0;	// pitch angle

		min_width_pixel_warp = warp_col/6;
		margin_side = warp_col/6;
	}
	

	// #else
	// van_pt_ini = Point2f(img_size.width/2, 305); //img_size.height/2, 305 is estimated by eye for new data in Mcity
	// van_pt_cali = van_pt_ini; 
	// theta_w = 0; 	// yaw angle 
	// theta_h = 0;	// pitch angle

	// int margin_side = warp_col/6;
	// #endif


	{
		float y_top_warp = (y_bottom_warp + 5*van_pt_ini.y)/6;
		float x_van = van_pt_ini.x;
		float y_van = van_pt_ini.y;
		float y_bottom = y_bottom_warp;
		float x_left = 0;
		float x_right = img_size.width - 1;
		vector<Point> warp_test;
		warp_test.push_back(Point((y_top_warp-y_bottom)/(y_van-y_bottom)*(x_van-x_left) + x_left, y_top_warp));
		warp_test.push_back(Point(x_left, y_bottom ));
		warp_test.push_back(Point(x_right, y_bottom ));
		warp_test.push_back(Point((y_top_warp-y_bottom)/(y_van-y_bottom)*(x_van-x_right)+ x_right, y_top_warp));
		warp_test_vec.clear();
		warp_test_vec.push_back(warp_test);
	}
	warp_dst.clear();
	// warp_dst.push_back(Point2f(warp_col/6, 0 )); 	// remind that calculation with int could result in weird values when division appears!
	// warp_dst.push_back(Point2f(warp_col/6, warp_row -1));
	// warp_dst.push_back(Point2f(warp_col*5/6, warp_row -1 ));
	// warp_dst.push_back(Point2f(warp_col*5/6, 0));
	warp_dst.push_back(Point2f(margin_side, 0 )); 	// remind that calculation with int could result in weird values when division appears!
	warp_dst.push_back(Point2f(margin_side, warp_row -1));
	warp_dst.push_back(Point2f(warp_col - margin_side, warp_row -1 ));
	warp_dst.push_back(Point2f(warp_col - margin_side, 0));
	warp_src.clear();
	warp_src.push_back(Point2f(warp_test_vec[0][0]));
	warp_src.push_back(Point2f(warp_test_vec[0][1]));
	warp_src.push_back(Point2f(warp_test_vec[0][2]));
	warp_src.push_back(Point2f(warp_test_vec[0][3]));

	per_mtx = getPerspectiveTransform(warp_src, warp_dst);
	inv_per_mtx = getPerspectiveTransform(warp_dst, warp_src);

	vote_lines_img = Mat(img_size, CV_8UC3, Scalar(0,0,0));
	valid_lines_map = Mat(img_size, CV_32FC1, Scalar(0));

	#ifdef CANNY_VOTE
	max_weight_left = 3;
	max_weight_right = 3;
	confidence = 0;
	conf_weight = 0;
	conf_mse = 0;
	conf_dist = 0;
	// kalman.init(2, 2, 0, CV_32F);
	conf_gamma_x = 1.0 / (20.0 / (480.0 * 0.7) * y_bottom_warp_max);
	conf_c_x_max = 150.0 / (480.0 * 0.7) * y_bottom_warp_max; // 100
	conf_c_x_min = 30.0 / (480.0 * 0.7) * y_bottom_warp_max;
	// conf_c_x = (conf_c_x_max + conf_c_x_min) / 2;
	conf_c_x = conf_c_x_max;

	conf_gamma_y = 1.0 / (10.0 / (480.0 * 0.7) * y_bottom_warp_max);
	conf_c_y_min = 15.0 / (480.0 * 0.7) * y_bottom_warp_max;
	conf_c_y_max = conf_c_x_max;
	conf_c_y = conf_c_x;

	conf_gamma_e = 1.0 / (20.0 / (480.0 * 0.7) * y_bottom_warp_max);
	conf_c_e = 30.0 / (480.0 * 0.7) * y_bottom_warp_max;

	line_gamma_dist = 1.0 / (100.0 / (480.0 * 0.7) * y_bottom_warp_max); // 40 originally
	line_c_dist = 70.0 / (480.0 * 0.7) * y_bottom_warp_max; // 70 originally
	line_gamma_dist_max = 1.0 / (40.0 / (480.0 * 0.7) * y_bottom_warp_max);
	line_gamma_dist_min = 1.0 / (100.0 / (480.0 * 0.7) * y_bottom_warp_max);
	#endif

	ini_flag = false;
	first_sucs = false;
	sucs_before = false;
	fail_ini_count = 0;
}

void VanPt::initialVan(Mat color_img, Mat image, Mat& warped_img, LaneMark& lane_mark)
{
	lines_vote.clear();
	vote_lines_img.setTo(Scalar(0,0,0));
	valid_lines_map.setTo(0);

    Mat edges;
	Canny(image, edges, 50, 100, 3);

	// #ifndef NDEBUG_IN
	// imshow("canny", edges);
	// #endif



	// Mat sobel_x, sobel_y, sobel_angle;
	// Sobel(image, sobel_x, CV_32F, 1, 0, 7); // 3
	// Sobel(image, sobel_y, CV_32F, 0, 1, 7);
	// phase(sobel_x, sobel_y, sobel_angle, true);  // output of phase in degree is [0~360]
	// Mat angle_mask;
	// angle_mask = (sobel_angle >= 10 & sobel_angle <= 80) | (sobel_angle >= 100 & sobel_angle <= 170) | (sobel_angle >= 190 & sobel_angle <= 260) | (sobel_angle >= 280 & sobel_angle <= 350);
	// bitwise_and(edges, angle_mask, edges); // remove edges with wrong angle

	Mat cali_mask;
	Mat erode_kernel = getStructuringElement(MORPH_RECT, Size(7, 7) );
	erode(image, cali_mask, erode_kernel );
	cali_mask = cali_mask > 0;
	bitwise_and(edges, cali_mask, edges); // remove the edges caused by warp effect

	int dist_top_thre = (y_bottom_warp_max - van_pt_cali.y)*1/6; 			// may need modification: depends on cali or detect result ?
	edges.rowRange(0, van_pt_cali.y + dist_top_thre) = 0;
	edges.rowRange(y_bottom_warp_max, image.rows) = 0;  // for caltech data

	#ifndef CANNY_VOTE
	// Mat steer_resp_mag(image.size(), CV_32FC1, Scalar(0));
	// Mat steer_angle_max(image.size(), CV_32FC1, Scalar(0));
	// Mat steer_resp_weight(image.size(), CV_32FC1, Scalar(0));
	// SteerFilter(image, steer_resp_mag, steer_angle_max, steer_resp_weight);

    Mat gabor_resp_dir(image.size(), CV_32FC1, Scalar(0));
    Mat gabor_resp_mag(image.size(), CV_32FC1, Scalar(0));
    Mat gabor_weight(image.size(), CV_32FC1, Scalar(0));
    Mat gabor_vote(image.size(), CV_32FC1, Scalar(0));
    GaborFilter(image, gabor_resp_mag, gabor_resp_dir, gabor_weight);
    cout << "Ok 3" << endl;
    ini_success = GaborVote(gabor_resp_dir, gabor_weight, gabor_vote, edges);
    // GaborVote(steer_angle_max, steer_resp_weight, gabor_vote);


    // Mat blur_edges;
	// LaneDirec(gabor_weight, edges, blur_edges); // steer_resp_mag

	#else
	ini_success = edgeVote(image, edges);
	updateFlags();
	if (first_sucs)
	{
		cout << "First success !!!!!!!!!!!!!!" << endl;
		van_pt_ini = van_pt_obsv;
		theta_w = atan(tan(ALPHA_W)*((van_pt_ini.x - img_size.width/2)/(img_size.width/2))); 	// yaw angle 
		theta_h = atan(tan(ALPHA_H)*((van_pt_ini.y - img_size.height/2)/(img_size.height/2)));	// pitch angle
	}
	updateTrackVar();

	if (ini_flag)	// van_pt is in good state
	{
		if (!lane_mark.new_result) 		// lane needs reinitialization
		{
			Mat blur_edges;
			LaneDirec(valid_lines_map, edges, blur_edges); // steer_resp_mag
			DecideChnlThresh(color_img, image, blur_edges); // use normalized one or ?
		}

		/// generate the trapezoid for warping and masking
		float y_top_warp = (y_bottom_warp + 5*van_pt_ini.y)/6;
		float x_van = van_pt_ini.x;
		float y_van = van_pt_ini.y;
		float y_bottom = y_bottom_warp;
		float x_left = 0;
		float x_right = image.cols - 1;
		//vector<vector<Point> > warp_test_vec;
		vector<Point> warp_test;
		warp_test.push_back(Point((y_top_warp-y_bottom)/(y_van-y_bottom)*(x_van-x_left) + x_left, y_top_warp));
		warp_test.push_back(Point(x_left, y_bottom ));
		warp_test.push_back(Point(x_right, y_bottom ));
		warp_test.push_back(Point((y_top_warp-y_bottom)/(y_van-y_bottom)*(x_van-x_right)+ x_right, y_top_warp));
		warp_test_vec.clear();
		warp_test_vec.push_back(warp_test);

		warp_src.clear();
		warp_src.push_back(Point2f(warp_test_vec[0][0] )); 	// remind that calculation with int could result in weird values when division appears!
		warp_src.push_back(Point2f(warp_test_vec[0][1] ));
		warp_src.push_back(Point2f(warp_test_vec[0][2] ));
		warp_src.push_back(Point2f(warp_test_vec[0][3] ));

		per_mtx = getPerspectiveTransform(warp_src, warp_dst);
		inv_per_mtx = getPerspectiveTransform(warp_dst, warp_src);
		
		warpPerspective(color_img, warped_img, per_mtx, Size(warp_col, warp_row), INTER_NEAREST);
	}

	#endif



	// #ifdef CALI_VAN
	if (cali_van)
	{
		float real_width_bottom = 1 / (coef_pix_per_cm*(y_bottom_warp - van_pt_cali_y)) * img_size.width;
		float warp_pix_per_cm = warp_col / real_width_bottom;
		min_width_pixel_warp = warp_pix_per_cm * 150; // assume lane width should be no less than 200cm
		cout << "warp_pix_per_cm: " << warp_pix_per_cm << ", min_width_pixel_warp: " << min_width_pixel_warp << endl; 
		min_width_pixel_warp = warp_col/6;
	}
	
	// #endif

}



bool VanPt::edgeVote(Mat image, Mat edges)
{
	/// vote for vanishing point based on Hough
	vector<Vec4i> lines;
	HoughLinesP(edges, lines, 1, CV_PI/180, 10, 10, 10 );
	//HoughLinesP(edges, lines, 20, CV_PI/180*3, 30, 10, 50 );

	if (lines.size() <= 0) /// safe return
	{
		cout << "Initilization failed: no Hough lines found. " << endl;
		return false;
	}
	// find valid lines
	int num_lines = lines.size();
	vector<int> valid_lines_idx_left;
	vector<float> valid_lines_w_left;
	vector<int> valid_lines_idx_right;
	vector<float> valid_lines_w_right;
	for (int i = 0; i < lines.size(); i++)
	{
		int check_result = checkValidLine(lines[i]);
		if (check_result == 1)
		{
			valid_lines_idx_left.push_back(i);
			valid_lines_w_left.push_back(getLineWeight(lines[i]));
		}
		else if (check_result == 2)
		{
			valid_lines_idx_right.push_back(i);
			valid_lines_w_right.push_back(getLineWeight(lines[i]));
		}
		#ifdef DRAW
		// else if (check_result == 3) // comment to remove the lines for voting van_pt
		// {
		// 	line(vote_lines_img, Point(lines[i][0],lines[i][1]), Point(lines[i][2], lines[i][3]), Scalar(0,255,0),1);
		// }
		// else if (check_result == 4)
		// {
		// 	line(vote_lines_img, Point(lines[i][0],lines[i][1]), Point(lines[i][2], lines[i][3]), Scalar(255,255,0),1);
		// }
		// else
		// {
		// 	line(vote_lines_img, Point(lines[i][0],lines[i][1]), Point(lines[i][2], lines[i][3]), Scalar(255,0,0),1);
		// }
		#endif
	}
	#ifdef DRAW
	float gamma_dist = line_gamma_dist;
	float c_dist = line_c_dist;
	circle(vote_lines_img, Point(van_pt_ini), (int)c_dist, Scalar(255,0,0));
	circle(vote_lines_img, Point(van_pt_ini), (int)c_dist + (int)(1/gamma_dist), Scalar(255,0,0));
	#endif

	if (valid_lines_idx_left.size() <= 0 || valid_lines_idx_right.size() <= 0)
	{
		cout << "Initilization failed: no valid lines found. " << endl;
		return false;
	}

	// find van_pt_candi and synthesize to an estimate
	vector<Point2f> van_pt_candi;
	vector<float> van_pt_candi_w;
	float weight_sum = 0, x_sum = 0, y_sum = 0;
	for (int i = 0; i < valid_lines_idx_left.size(); i++)
	{
		for (int j = 0; j < valid_lines_idx_right.size(); j++)
		{
			float xl1 = lines[valid_lines_idx_left[i]][0];
			float yl1 = lines[valid_lines_idx_left[i]][1];
			float xl2 = lines[valid_lines_idx_left[i]][2];
			float yl2 = lines[valid_lines_idx_left[i]][3];
			float xr1 = lines[valid_lines_idx_right[j]][0];
			float yr1 = lines[valid_lines_idx_right[j]][1];
			float xr2 = lines[valid_lines_idx_right[j]][2];
			float yr2 = lines[valid_lines_idx_right[j]][3];

			float kl = (yl2 - yl1)/(xl2 - xl1);
			float kr = (yr2 - yr1)/(xr2 - xr1);

			float beta = 1/(kl - kr);
			float xp = beta*(yr1 - yl1 + kl*xl1 - kr*xr1);
			float yp = beta*(kl*kr*(xl1 - xr1) + kl*yr1 - kr*yl1);
			van_pt_candi.push_back(Point2f(xp, yp));

			float weight_cur = valid_lines_w_left[i] * valid_lines_w_right[j]; // or use sqrt?
			van_pt_candi_w.push_back(weight_cur);

			weight_sum += weight_cur;
			x_sum += weight_cur*xp;
			y_sum += weight_cur*yp;

			#ifdef DRAW
			line(vote_lines_img, Point(xl1,yl1), Point(xl2, yl2), Scalar(0,0,255),1); // comment to remove the lines voting for van_pt
			line(vote_lines_img, Point(xr1,yr1), Point(xr2, yr2), Scalar(0,0,255),1);
			circle(vote_lines_img, Point(xp,yp), 2, Scalar(0,255,0), -1); // it makes the image not clean
			#endif
			line(valid_lines_map, Point(xl1,yl1), Point(xl2, yl2), Scalar(255),1);
		}
	}

	if (x_sum == 0 || y_sum == 0 || weight_sum == 0)
	{
		cout << "Initilization failed: weight = 0. " << endl;
		return false;
	}

	// Point2f van_pt_obsv;
	van_pt_obsv.x = x_sum/weight_sum;
	van_pt_obsv.y = y_sum/weight_sum;

	// get confidence
	confidence = getConfidence(van_pt_candi, van_pt_candi_w, valid_lines_w_left, valid_lines_w_right); //, van_pt_obsv);

	// track with Kalman
	van_pt_ini = confidence*van_pt_obsv + (1-confidence)*van_pt_ini;

	theta_w = atan(tan(ALPHA_W)*((van_pt_ini.x - img_size.width/2)/(img_size.width/2))); 	// yaw angle 
	theta_h = atan(tan(ALPHA_H)*((van_pt_ini.y - img_size.height/2)/(img_size.height/2)));	// pitch angle

	// int fontFace = FONT_HERSHEY_SIMPLEX;
	// double fontScale = 0.5;
	// int thickness = 1;
	// string Text1 = "pitch: " + x2str(theta_h*180/CV_PI) + ", yaw: " + x2str(theta_w*180/CV_PI);
	// putText(vote_lines_img, Text1, Point(10, 140), fontFace, fontScale, Scalar(0,0,255), thickness, LINE_AA);

	return true;

}

int VanPt::checkValidLine(Vec4i line)
{
	Point2f ref_pt = van_pt_ini; // van_pt_cali

	float length_thre_far = 10;
	float length_thre_near = 25;
	float k_thre_min_large = 0.3;
	float k_thre_min_small = 0.1;
	float k_thre_max_large = 5;
	float k_thre_max_small = 1;
	// float dist_top_thre = (y_bottom_warp_max - van_pt_cali.y)*1/6;

	float x1 = line[0];
	float y1 = line[1];
	float x2 = line[2];
	float y2 = line[3];

	float length = sqrt((x2-x1)*(x2-x1) + (y2-y1)*(y2-y1));
	float k = (y2-y1)/(x2-x1+0.00001);
	float y_bottom = y1>y2 ? y1 : y2;
	float x_side = abs(x1 - ref_pt.x) > abs(x2 - ref_pt.x) ? abs(x1 - ref_pt.x) : abs(x2 - ref_pt.x);
	float k_thre_min_cur_verti = (y_bottom - ref_pt.y)/(y_bottom_warp_max - ref_pt.y)*(k_thre_min_large - k_thre_min_small) + k_thre_min_small;
	float k_thre_min_cur_hori = x_side / (img_size.width/2)*(k_thre_min_small - k_thre_min_large) + k_thre_min_large;
	float k_thre_min_cur = (k_thre_min_cur_verti + k_thre_min_cur_hori)/2;

	float k_thre_max_cur_verti = (y_bottom - ref_pt.y)/(y_bottom_warp_max - ref_pt.y)*(k_thre_max_large - k_thre_max_small) + k_thre_max_small;
	float k_thre_max_cur_hori = x_side / (img_size.width/2)*(k_thre_max_small - k_thre_max_large) + k_thre_max_large;
	float k_thre_max_cur = (k_thre_max_cur_verti + k_thre_max_cur_hori)/2;

	float length_thre_cur = (y_bottom - ref_pt.y)/(y_bottom_warp_max - ref_pt.y)*(length_thre_near - length_thre_far) + length_thre_far;

	float vx = ref_pt.x - img_size.width/2;
	float vy = ref_pt.y - (img_size.height-1); // should < 0
	float nx = -vy;
	float ny = vx;
	float x_m = (x1+x2)/2 - ref_pt.x;
	float y_m = (y1+y2)/2 - ref_pt.y;
	int side = x_m*nx + y_m*ny > 0 ? 2 : 1; // right:2, left:1

	bool valid_side = ( (side == 1 && k < 0 ) || (side == 2 && k > 0) ) && y_bottom > ref_pt.y; // 10/30 added vertical constraints, but have risk if ref_pt is not reasonable
	bool valid_length = length >= length_thre_cur;
	bool valid_angle = abs(k) >= k_thre_min_cur && abs(k) <= k_thre_max_cur;
	bool valid = valid_side && valid_length && valid_angle ; //   // && y_bottom - ref_pt.y >= dist_top_thre
	if (valid)
	{
		return side;
	}
	else if (!valid_length && valid_angle)
	{
		return 3;
	}
	else if(!valid_angle && valid_length)
	{
		return 4;
	}
	else
	{
		return 0;
	}

}

float VanPt::getLineWeight(Vec4i line)
{
	Point2f ref_pt = van_pt_ini; //van_pt_cali

	float x1 = line[0];
	float y1 = line[1];
	float x2 = line[2];
	float y2 = line[3];

	float length = sqrt((x2-x1)*(x2-x1) + (y2-y1)*(y2-y1));
	float y_bottom = y1>y2 ? y1 : y2;

	float a = y2 - y1, b = x1 - x2, c = x2*y1 - x1*y2;
	float dist2ref = abs(a*ref_pt.x + b*ref_pt.y + c) / sqrt(a*a + b*b);
	// float gamma_dist = 1.0/40.0, c_dist = 70;
	float gamma_dist = line_gamma_dist;
	float c_dist = line_c_dist;
	float dist_weight = 0.5*(1-tanh(gamma_dist*(dist2ref - c_dist)));


	float weight = length/(y_bottom_warp_max-ref_pt.y)*(y_bottom - ref_pt.y)/(y_bottom_warp_max - ref_pt.y)*dist_weight;

	return weight;
}

float VanPt::getConfidence(const vector<Point2f>& van_pt_candi, const vector<float>& van_pt_candi_w,
	const vector<float>& valid_lines_w_left, const vector<float>& valid_lines_w_right) // , Point2f van_pt_obsv)
{
	Point2f ref_pt = van_pt_ini; //van_pt_cali


	// conf_weight
	float weight_left = 0, weight_right = 0;
	for (int i = 0; i < valid_lines_w_left.size(); i++)
	{
		weight_left += valid_lines_w_left[i];
	}
	for (int i = 0; i < valid_lines_w_right.size(); i++)
	{
		weight_right += valid_lines_w_right[i];
	}

	if (weight_left > max_weight_left && weight_right > max_weight_right )
	{
		conf_weight = 1;
		max_weight_left = weight_left;
		max_weight_right = weight_right;
	}
	else if (weight_left > max_weight_left)
	{
		conf_weight = sqrt(weight_right / max_weight_right); //pow(weight_right / max_weight_right, 0.7)
		max_weight_left = weight_left;
		max_weight_right *= 0.99;
	}
	else if (weight_right > max_weight_right)
	{
		conf_weight = sqrt(weight_left / max_weight_left);
		max_weight_right = weight_right;
		max_weight_left *= 0.99;
	}
	else
	{
		conf_weight = sqrt(min(weight_left / max_weight_left, weight_right / max_weight_right));
		max_weight_left *= 0.99;
		max_weight_right *= 0.99;
	}

	// conf_mse
	float van_pt_weight_sum = 0;
	float van_pt_error_sum = 0;
	for (int i = 0; i < van_pt_candi_w.size(); i++)
	{
		float error_x = van_pt_candi[i].x - van_pt_obsv.x;
		float error_y = van_pt_candi[i].y - van_pt_obsv.y;
		float error_sqr_wted = van_pt_candi_w[i]*(error_x*error_x + error_y*error_y);
		van_pt_error_sum += error_sqr_wted;
		van_pt_weight_sum += van_pt_candi_w[i];
	}
	float van_pt_mse = sqrt(van_pt_error_sum / (van_pt_weight_sum+0.00001));
	float gamma_e = conf_gamma_e, c_e = conf_c_e; // gamma_e = 1.0/20.0, c_e = 30;

	conf_mse = 0.5*(1-tanh(gamma_e*(van_pt_mse - c_e)));


	// conf_dist
	float c_x = conf_c_x;
	float gamma_x = conf_gamma_x;
	float gamma_y = conf_gamma_y; // gamma_x = 1.0/20.0, gamma_y = 1.0/10.0
	float c_y = conf_c_y; // c_x = 30, c_y = 20
	float dist_x = abs(van_pt_obsv.x - ref_pt.x);
	float dist_y = abs(van_pt_obsv.y - ref_pt.y);
	conf_dist = 0.25*(1-tanh(gamma_x*(dist_x - c_x)))*(1-tanh(gamma_y*(dist_y - c_y)));

	float cur_confidence = conf_weight * conf_mse * conf_dist;
	float filter_confidence = cur_confidence;
	if (cur_confidence > confidence)
	{
		filter_confidence = (cur_confidence + confidence)/2;
	}

	#ifdef DRAW
	// circle(vote_lines_img, Point(van_pt_obsv), max(0, (int)van_pt_mse), Scalar(0,0,255)); // indicating the variance of this estimation
	circle(vote_lines_img, Point(van_pt_obsv), 3, Scalar(0,0,255), -1);
	rectangle(vote_lines_img, Point(ref_pt.x - c_x, ref_pt.y - c_y), Point(ref_pt.x + c_x, ref_pt.y + c_y), Scalar(255,0,0)); // showing conf_dist
	cout << "first draw finished. " << endl;
	#endif

	int fontFace = FONT_HERSHEY_SIMPLEX;
	double fontScale = 0.5;
	int thickness = 1;
	string Text1 = "conf_w: " + x2str(conf_weight) + ", conf_mse: " + x2str(conf_mse) + ", conf_dist: " + x2str(conf_dist);
	string Text2 = "conf_cur: " + x2str(cur_confidence) + ", conf: " + x2str(filter_confidence);
	string Text3 = "w: (" + x2str(weight_left) + ", " + x2str(weight_right) + "), w_max: (" + x2str(max_weight_left) + ", " + x2str(max_weight_right) + ")";
	string Text4 = "van_pt_obsv: (" + x2str((int)van_pt_obsv.x) + ", " + x2str((int)van_pt_obsv.y) + ")";
	putText(vote_lines_img, Text1, Point(10, 80), fontFace, fontScale, Scalar(0,0,255), thickness, LINE_AA);
	putText(vote_lines_img, Text2, Point(10, 100), fontFace, fontScale, Scalar(0,0,255), thickness, LINE_AA);
	putText(vote_lines_img, Text3, Point(10, 120), fontFace, fontScale, Scalar(0,0,255), thickness, LINE_AA);
	putText(vote_lines_img, Text4, Point(270, 60), fontFace, fontScale, Scalar(0,0,255), thickness, LINE_AA);

	// imshow("vote_lines_img", vote_lines_img);

	return filter_confidence;

}

void VanPt::updateFlags()
{
	if (ini_success && ( (confidence >= 0.01) || (!sucs_before && conf_mse*conf_weight >= 0.01) ) ) //  && conf_c_y <= 3*conf_c_y_min
	{
		if (!sucs_before){
			first_sucs = true;}
		else{
			first_sucs = false;}
		sucs_before = true;

		fail_ini_count = max(0, fail_ini_count - 1);
		if (fail_ini_count == 0)
			ini_flag = true;
	}
	else
	{
		first_sucs = false;

		fail_ini_count = min(10, fail_ini_count + 1);
		confidence *= 0.5;
		if (fail_ini_count >= 5)
		{
			ini_flag = false;
			confidence = 0;
		}
	}
}
void VanPt::updateTrackVar()
{
	if (!ini_success || confidence < 0.01)
	{
		conf_c_x = min(conf_c_x_max, float(1.1*conf_c_x));
		conf_c_y = min(conf_c_y_max, float(1.1*conf_c_y));
		line_gamma_dist = max(line_gamma_dist_min, float(0.9*line_gamma_dist));
	}
	else if (confidence >= 0.03)
	{
		conf_c_x = max(conf_c_x_min, float(0.9*conf_c_x));
		conf_c_y = max(conf_c_y_min, float(0.9*conf_c_y));
		line_gamma_dist = min(line_gamma_dist_max, float(1.1*line_gamma_dist));
	}
}

void VanPt::getSteerKernel(Mat& kernel_x, Mat& kernel_y, Mat& kernel_xy, int ksize, double sigma)
{
	if (ksize % 2 == 0)
		ksize = ksize + 1;
	int center = (ksize + 1)/2;

	Mat coord_x(ksize, ksize, CV_32FC1);
	Mat coord_y(ksize, ksize, CV_32FC1);
	for (int i = 1; i <= ksize; i++)
	{
		coord_x.col(i-1) = i - center;
		coord_y.row(i-1) = i - center;
	}


	Mat exp_xy, x_sqr, y_sqr;
	pow(coord_x, 2, x_sqr);
	pow(coord_y, 2, y_sqr);
	// Mat sqr_norm = -(x_sqr + y_sqr)/(sigma*sigma);
	exp( -(x_sqr + y_sqr)/(sigma*sigma) , exp_xy);

	kernel_x = ( -(2*x_sqr/(sigma*sigma) - 1)*2/(sigma*sigma) ).mul(exp_xy);
	kernel_y = ( -(2*y_sqr/(sigma*sigma) - 1)*2/(sigma*sigma) ).mul(exp_xy);
	kernel_xy = ( 4*coord_x.mul(coord_y)/(sigma*sigma*sigma*sigma) ).mul(exp_xy);

}

void VanPt::SteerFilter(Mat image, Mat& steer_resp_mag, Mat& steer_angle_max, Mat& steer_resp_weight)
{
	int ksize = 21;
	double sigma = 4; // 8

	Mat kernel_steer_x(ksize, ksize, CV_32F);
	Mat kernel_steer_y(ksize, ksize, CV_32F);
	Mat kernel_steer_xy(ksize, ksize, CV_32F);
	cout <<"lalala 0 " << endl;
	getSteerKernel(kernel_steer_x, kernel_steer_y, kernel_steer_xy, ksize, sigma);
	cout <<"lalala 1 " << endl;
	Mat steer_resp_x, steer_resp_y, steer_resp_xy;
	filter2D(image, steer_resp_x, CV_32F, kernel_steer_x );
	filter2D(image, steer_resp_y, CV_32F, kernel_steer_y );
	filter2D(image, steer_resp_xy, CV_32F, kernel_steer_xy );


	Mat steer_resp_max_x(image.size(), CV_32FC1, Scalar(0));
	Mat steer_resp_max_y(image.size(), CV_32FC1, Scalar(0));
	Mat steer_resp_max_xy(image.size(), CV_32FC1, Scalar(0));

	Mat steer_resp_min_x(image.size(), CV_32FC1, Scalar(0));
	Mat steer_resp_min_y(image.size(), CV_32FC1, Scalar(0));
	Mat steer_resp_min_xy(image.size(), CV_32FC1, Scalar(0));


	// sobel_angle = sobel_angle *CV_PI/180 - CV_PI/2;
	// Mat A;
	// Mat steer_resp_x_2, steer_resp_y_2;
	// pow(steer_resp_x, 2, steer_resp_x_2);
	// pow(steer_resp_y, 2, steer_resp_y_2);
	// sqrt(steer_resp_x_2 - 2*steer_resp_x.mul(steer_resp_y)+steer_resp_y_2 + 4*steer_resp_xy, A);
	// Mat steer_angle_tan_max = (steer_resp_x - steer_resp_y + A )/(2*steer_resp_xy);
	// Mat steer_angle_tan_min = (steer_resp_x - steer_resp_y - A )/(2*steer_resp_xy);

	// Mat steer_angle_max(image.size(), CV_32FC1);
	Mat steer_angle_min(image.size(), CV_32FC1);

	Mat steer_angle_max_cos_2(image.size(), CV_32FC1);
	Mat steer_angle_max_sin_2(image.size(), CV_32FC1);

	// cout << sobel_angle.depth() << endl;
	int top_row = van_pt_cali.y;
	for (int i = top_row; i < max(top_row, y_bottom_warp_max-ksize); i ++)
	{
		for (int j = 0; j < image.cols; j++)
		{
			// steer_angle_max.at<float>(i,j) = atan(steer_angle_tan_max.at<float>(i,j));
			// steer_angle_max_cos_2.at<float>(i,j) = cos(steer_angle_max.at<float>(i,j))*cos(steer_angle_max.at<float>(i,j));
			// steer_angle_max_sin_2.at<float>(i,j) = sin(steer_angle_max.at<float>(i,j))*sin(steer_angle_max.at<float>(i,j));
			float Ixx = steer_resp_x.at<float>(i,j);
			float Iyy = steer_resp_y.at<float>(i,j);
			float Ixy = steer_resp_xy.at<float>(i,j);

			if (Ixx > Iyy)
			{
				steer_angle_max.at<float>(i,j) = 0.5*atan(Ixy/(Ixx-Iyy));
			}
			else if (Ixx < Iyy)
			{
				steer_angle_max.at<float>(i,j) = 0.5*(CV_PI+atan(Ixy/(Ixx-Iyy)));
			}
			else
			{
				steer_angle_max.at<float>(i,j) = CV_PI/4;
			}
			steer_angle_min.at<float>(i,j) = steer_angle_max.at<float>(i,j) + CV_PI/2;

			steer_resp_max_x.at<float>(i,j) = steer_resp_x.at<float>(i,j)*cos(steer_angle_max.at<float>(i,j))*cos(steer_angle_max.at<float>(i,j)); //*abs(cos(steer_angle_max.at<float>(i,j)))
			steer_resp_max_y.at<float>(i,j) = steer_resp_y.at<float>(i,j)*sin(steer_angle_max.at<float>(i,j))*sin(steer_angle_max.at<float>(i,j));
			steer_resp_max_xy.at<float>(i,j) = steer_resp_xy.at<float>(i,j)*sin(steer_angle_max.at<float>(i,j))*cos(steer_angle_max.at<float>(i,j));

			// steer_angle_min.at<float>(i,j) = atan(steer_angle_tan_min.at<float>(i,j));
			steer_resp_min_x.at<float>(i,j) = steer_resp_x.at<float>(i,j)*cos(steer_angle_min.at<float>(i,j))*cos(steer_angle_min.at<float>(i,j));
			steer_resp_min_y.at<float>(i,j) = steer_resp_y.at<float>(i,j)*sin(steer_angle_min.at<float>(i,j))*sin(steer_angle_min.at<float>(i,j));
			steer_resp_min_xy.at<float>(i,j) = steer_resp_xy.at<float>(i,j)*sin(steer_angle_min.at<float>(i,j))*cos(steer_angle_min.at<float>(i,j));

			float angle_2_center = atan((van_pt_cali.x - j)/( i - van_pt_cali.y ) );
			steer_resp_weight.at<float>(i,j) =abs(cos(steer_angle_max.at<float>(i,j)) *cos(angle_2_center + steer_angle_max.at<float>(i,j))*(float)(i-top_row)/(y_bottom_warp_max-top_row))*((float)(image.cols/2 - abs(j-image.cols/2))/(image.cols/2));
		}
	}
	Mat steer_resp_max = steer_resp_max_x + steer_resp_max_y + 2*steer_resp_max_xy;
	Mat steer_resp_min = steer_resp_min_x + steer_resp_min_y + 2*steer_resp_min_xy;

	steer_resp_mag = steer_resp_max - steer_resp_min;
	steer_resp_weight = steer_resp_weight.mul(steer_resp_mag);

	steer_angle_max = -steer_angle_max;
	for (int j = 217; j < 227; j++)
	{
		cout << "angle at (317, " << j << "): " <<  steer_angle_max.at<float>(317,j)*180/CV_PI << endl;
		cout << "mag at (317, " << j << "): " <<  steer_resp_mag.at<float>(317,j)*180/CV_PI << endl;
		cout << "x at (317, " << j << "): " <<  steer_resp_x.at<float>(317,j)*180/CV_PI << endl;
		cout << "y at (317, " << j << "): " <<  steer_resp_y.at<float>(317,j)*180/CV_PI << endl;
		cout << "xy at (317, " << j << "): " <<  steer_resp_xy.at<float>(317,j)*180/CV_PI << endl;

	}

	cout <<"lalala 2 " << endl;
	Mat steer_resp_x_show, steer_resp_y_show, steer_resp_xy_show, steer_resp_mag_show, steer_resp_weight_show;


	// cout << "steer_angle_max_cos_2 at (10,464): " << steer_angle_max_cos_2.at<float>(464,10) <<  endl;
	// cout << "steer_angle_max_sin_2 at (10,464): " << steer_angle_max_sin_2.at<float>(464,10) <<  endl;
	// cout << "steer_angle_tan_max at (10,464): " << steer_angle_tan_max.at<float>(464,10) <<  endl;
	cout << "steer_angle_max at (10,464): " << steer_angle_max.at<float>(464,10) <<  endl;
	cout << "steer_resp_xy at (10,464): " << steer_resp_xy.at<float>(464,10) <<  endl;
	cout << "steer_resp_x at (10,464): " << steer_resp_x.at<float>(464,10) <<  endl;
	cout << "steer_resp_y at (10,464): " << steer_resp_y.at<float>(464,10) <<  endl;
	// cout << "A at (10,464): " << A.at<float>(464,10) <<  endl;
	// cout << "result at (10,464)" << (steer_resp_x.at<float>(464,10)  -steer_resp_y.at<float>(464,10) + A.at<float>(464,10)) / (2*steer_resp_xy.at<float>(464,10)) << endl;


	// normalize(steer_angle_max_cos_2, steer_angle_max_cos_2, 0, 255, NORM_MINMAX , CV_8U);
	// normalize(steer_angle_max_sin_2, steer_angle_max_sin_2, 0, 255, NORM_MINMAX , CV_8U);
	// imshow("steer_angle_max_cos_2", steer_angle_max_cos_2);
	// imshow("steer_angle_max_sin_2", steer_angle_max_sin_2);

	#ifndef NDEBUG_IN
	normalize(steer_resp_mag, steer_resp_mag_show, 0, 255, NORM_MINMAX , CV_8U);
	normalize(steer_resp_weight, steer_resp_weight_show, 0, 255, NORM_MINMAX , CV_8U);
	normalize(steer_resp_max, steer_resp_max, 0, 255, NORM_MINMAX , CV_8U);
	normalize(steer_resp_min, steer_resp_min, 0, 255, NORM_MINMAX , CV_8U);
	imshow("steer_resp_max", steer_resp_max);
	imshow("steer_resp_min", steer_resp_min);
	imshow("steer_resp_mag", steer_resp_mag_show);
	imshow("steer_resp_weight", steer_resp_weight_show);
	#endif


	// Mat steer_resp_max_x_show, steer_resp_max_y_show, steer_resp_max_xy_show;
	// Mat steer_resp_min_x_show, steer_resp_min_y_show, steer_resp_min_xy_show;
	// normalize(steer_resp_max_x, steer_resp_max_x_show, 0, 255, NORM_MINMAX , CV_8U);
	// normalize(steer_resp_max_y, steer_resp_max_y_show, 0, 255, NORM_MINMAX , CV_8U);
	// normalize(steer_resp_max_xy, steer_resp_max_xy_show, 0, 255, NORM_MINMAX , CV_8U);
	// normalize(steer_resp_min_x, steer_resp_min_x_show, 0, 255, NORM_MINMAX , CV_8U);
	// normalize(steer_resp_min_y, steer_resp_min_y_show, 0, 255, NORM_MINMAX , CV_8U);
	// normalize(steer_resp_min_xy, steer_resp_min_xy_show, 0, 255, NORM_MINMAX , CV_8U);

	// imshow("steer_resp_max_x_show", steer_resp_max_x_show);
	// imshow("steer_resp_max_y_show", steer_resp_max_y_show);
	// imshow("steer_resp_max_xy_show", steer_resp_max_xy_show);
	// imshow("steer_resp_min_x_show", steer_resp_min_x_show);
	// imshow("steer_resp_min_y_show", steer_resp_min_y_show);
	// imshow("steer_resp_min_xy_show", steer_resp_min_xy_show);

	// normalize(steer_resp_x, steer_resp_x_show, 0, 255, NORM_MINMAX , CV_8U);
	// normalize(steer_resp_y, steer_resp_y_show, 0, 255, NORM_MINMAX , CV_8U);
	// normalize(steer_resp_xy, steer_resp_xy_show, 0, 255, NORM_MINMAX , CV_8U);
	// imshow("steer_resp_x_show", steer_resp_x_show);
	// imshow("steer_resp_y_show", steer_resp_y_show);
	// imshow("steer_resp_xy_show", steer_resp_xy_show);




	#ifndef NDEBUG_IN
	// Mat kernel_steer_x_show, kernel_steer_y_show, kernel_steer_xy_show;
	// normalize(kernel_steer_x, kernel_steer_x_show, 0, 255, NORM_MINMAX , CV_8U);
	// normalize(kernel_steer_y, kernel_steer_y_show, 0, 255, NORM_MINMAX , CV_8U);
	// normalize(kernel_steer_xy, kernel_steer_xy_show, 0, 255, NORM_MINMAX , CV_8U);

	// imshow("steer_kern_x", kernel_steer_x_show);
	// imshow("steer_kern_y", kernel_steer_y_show);
	// imshow("steer_kern_xy", kernel_steer_xy_show);

	// waitKey(0);
	#endif



}


void VanPt::GaborFilter(Mat image, Mat& gabor_resp_mag, Mat& gabor_resp_dir, Mat& gabor_weight)
{
	Mat gaborKernelReal[4];
	Mat gaborKernelImage[4];
	Mat gabor_resp_real[4];
	Mat gabor_resp_image[4];
	Mat gabor_resp_engy[4];
	// Mat gabor_resp_mag(image.size(), CV_32F, Scalar(0));
	// Mat gabor_resp_dir(image.size(), CV_32F, Scalar(0));
	// Mat gabor_weight(image.size(), CV_32F, Scalar(0));

    double lambda = 16; // 4*sqrt(2)
    double radial_frequency = 2*CV_PI/lambda;
    double K = CV_PI/2;
    double sigma = K/radial_frequency;
    double gamma = 0.5;
    int ksize = ((int)(sigma*9))/2*2+1;
    double psi=CV_PI*0;
	int ktype=CV_32F;
	cout << "Ok 1" << endl;
    for (int i = 0; i < 4; i++)
    {
        double theta = i*CV_PI/4;
        gaborKernelReal[i] = getGaborKernel(Size(ksize,ksize), sigma, theta, lambda, gamma, psi, ktype);
		gaborKernelImage[i] = getGaborKernel(Size(ksize,ksize), sigma, theta, lambda, gamma, psi-CV_PI/2, ktype); // cos(x - pi/2) = sin(x)

		stringstream ss;
		ss << "gabor_kernel " << i;
		string image_name = ss.str();

		#ifndef NDEBUG_IN
		// Mat show_garbor;
		// normalize(gaborKernelReal[i], show_garbor, 0, 255, NORM_MINMAX, CV_8U);
		// imshow(image_name, show_garbor);
		// normalize(gaborKernelImage[i], show_garbor, 0, 255, NORM_MINMAX, CV_8U);
		// imshow(image_name+" im", show_garbor);
		#endif

		filter2D(image, gabor_resp_real[i], CV_32F, gaborKernelReal[i]);
		filter2D(image, gabor_resp_image[i], CV_32F, gaborKernelImage[i]);

		sqrt(gabor_resp_real[i].mul(gabor_resp_real[i])+gabor_resp_image[i].mul(gabor_resp_image[i]), gabor_resp_engy[i] );
	}
	int top_row = van_pt_cali.y;
	for (int i = top_row; i < max(top_row, y_bottom_warp_max-ksize); i++)
	{
		for (int j = 0; j < image.cols; j++)
		{
			Vec4f resp_engy(gabor_resp_engy[0].at<float>(i,j), gabor_resp_engy[1].at<float>(i,j), gabor_resp_engy[2].at<float>(i,j), gabor_resp_engy[3].at<float>(i,j) );

			Vec4i sort_idx;
			sortIdx(resp_engy, sort_idx, CV_SORT_EVERY_COLUMN + CV_SORT_DESCENDING );

			float v1 = resp_engy[sort_idx[0]] - resp_engy[sort_idx[3]];
			float v2 = resp_engy[sort_idx[1]] - resp_engy[sort_idx[2]];

			float x_proj = v1*sin(sort_idx[0]*CV_PI/4) + v2*sin(sort_idx[1]*CV_PI/4);
			float y_proj = v1*cos(sort_idx[0]*CV_PI/4) + v2*cos(sort_idx[1]*CV_PI/4);
			gabor_resp_mag.at<float>(i,j) = sqrt( x_proj*x_proj + y_proj*y_proj );
			gabor_resp_dir.at<float>(i,j) = atan(x_proj/y_proj) + (y_proj<0)*CV_PI ;
            float angle_to_last_van = atan((van_pt_cali.x - j)/( i - van_pt_cali.y ) );
            // if (i == 258 && j >= 236 && j <= 253) // 456-492
			// {
			// 	cout << "resp_descend: " << i<< " " << j << " " << resp_engy[0] << " " << resp_engy[1] << " " << resp_engy[2] << " " << resp_engy[3]<< endl;
            //     cout << "resp_descend: " << i<< " " << j << " " << gabor_resp_dir.at<float>(i,j)*180/CV_PI << " " << angle_to_last_van*180/CV_PI << endl;
			// }
			gabor_weight.at<float>(i,j) = gabor_resp_mag.at<float>(i,j)* abs(cos( angle_to_last_van-gabor_resp_dir.at<float>(i,j) ) * cos(gabor_resp_dir.at<float>(i,j)))*abs((float)(i-top_row)/(y_bottom_warp_max-top_row))*((float)(image.cols/2 - abs(j-image.cols/2))/(image.cols/2)); //* (v1 > resp_engy[sort_idx[3]])
			// weighted by angle to van_cali, angle to perpendicular, distance to bottom, distance to verticle center
		}
	}

	// // This part is for filtering out the response that are distributed together
	// Mat gabor_resp_mag_binary;
	// normalize(gabor_resp_mag, gabor_resp_mag_binary, 0, 255, NORM_MINMAX, CV_8U);
	// threshold(gabor_resp_mag_binary, gabor_resp_mag_binary, 40, 255, THRESH_BINARY);
	// imshow("gabor_mag_ori", gabor_resp_mag_binary);
	// gabor_resp_mag_binary.convertTo(gabor_resp_mag_binary, CV_32F );

	// cout << "mag before at 363,292: " << gabor_resp_mag.at<float>(292,363) << endl;
	// cout << "mag before at 454,254: " << gabor_resp_mag.at<float>(254,454) << endl;

	// // Mat ridge_kernel(1, 41, CV_32FC1, Scalar(-1));
	// // ridge_kernel.colRange(10,30) = 1;
	// Mat ridge_kernel_x, ridge_kernel_y;
	// double ksize_DOG = 121;
	// double sigma_DOG = 0.1*((ksize_DOG-1)*0.5 - 1) + 0.8;
	// ridge_kernel_x = (getGaussianKernel(ksize_DOG, sigma_DOG, CV_32F ) - getGaussianKernel(ksize_DOG, sigma_DOG*5, CV_32F )).t();
	// ridge_kernel_y = getGaussianKernel(11, -1, CV_32F );
	// sepFilter2D(gabor_resp_mag_binary, gabor_resp_mag_binary, -1, ridge_kernel_x, ridge_kernel_y );
	// // filter2D(gabor_resp_mag_binary, gabor_resp_mag_binary, -1, ridge_kernel );s
	// threshold(gabor_resp_mag_binary, gabor_resp_mag_binary, 0, 0, THRESH_TOZERO);
	// normalize(gabor_resp_mag_binary, gabor_resp_mag_binary, 0, 255, NORM_MINMAX, CV_8U);
	// threshold(gabor_resp_mag_binary, gabor_resp_mag_binary, 120, 255, THRESH_BINARY_INV);
	// gabor_resp_mag.setTo(0, gabor_resp_mag_binary);

	// for (int i = top_row; i < max(top_row, y_bottom_warp_max-ksize); i++)
	// {
	// 	for (int j = 0; j < image.cols; j++)
	// 	{
    //         float angle_to_last_van = atan((van_pt_cali.x - j)/( i - van_pt_cali.y ) );
	// 		gabor_weight.at<float>(i,j) = gabor_resp_mag.at<float>(i,j)* abs(cos( angle_to_last_van-gabor_resp_dir.at<float>(i,j) ) * cos(gabor_resp_dir.at<float>(i,j)))*abs((float)(i-top_row)/(y_bottom_warp_max-top_row))*((float)(image.cols/2 - abs(j-image.cols/2))/(image.cols/2)); //* (v1 > resp_engy[sort_idx[3]])
	// 	}
	// }

	// cout << "mag before at 363,292: " << gabor_resp_mag.at<float>(292,363) << endl;
	// cout << "mag before at 454,254: " << gabor_resp_mag.at<float>(254,454) << endl;

	// Mat show_garbor_;
	// normalize(gabor_resp_mag_binary, show_garbor_, 0, 255, NORM_MINMAX, CV_8U);
	// imshow("gabor_mag_ridge", show_garbor_);
	// normalize(ridge_kernel_x, show_garbor_, 0, 255, NORM_MINMAX, CV_8U);
	// imshow("ridge_kernel", show_garbor_);
	// waitKey(0);


	cout << "Ok 4" << endl;
	#ifndef NDEBUG_IN
	Mat show_garbor;
	normalize(gabor_weight, show_garbor, 0, 255, NORM_MINMAX, CV_8U);
    imshow("gabor_weight", show_garbor);
    normalize(gabor_resp_mag, show_garbor, 0, 255, NORM_MINMAX, CV_8U);
    imshow("gabor_resp_mag", show_garbor);
	// waitKey(0);
	#endif

}

bool VanPt::GaborVote(Mat gabor_resp_dir, Mat gabor_weight, Mat& vote_map, Mat edges)
{
	// first transform it with Hough
	Mat gabor_weight_8U;
	normalize(gabor_weight, gabor_weight_8U, 0, 255, NORM_MINMAX, CV_8U);
	double thresh = 50;
	threshold(gabor_weight_8U, gabor_weight_8U, thresh, 0, THRESH_TOZERO);

	Mat gabor_weight_8U_nms(gabor_resp_dir.size(), CV_8UC1, Scalar(0));

	#ifndef CLUSTER_FOR_VOTE     // using NMS+Hough
	// narrowlize the Gabor weight using Non-maximum-suppressing before fed to Hough
	Size ksize(9,9);
	double sigmaX = 3;
	double sigmaY = 3;
	GaussianBlur(gabor_weight_8U, gabor_weight_8U, ksize, sigmaX, sigmaY );
	// NMS(gabor_weight_8U, gabor_weight_8U_nms);					// narrow using NMS
	// Mat dilate_kern = getStructuringElement(MORPH_RECT, Size(3,2));
	// dilate(gabor_weight_8U_nms, gabor_weight_8U_nms, dilate_kern);
	threshold(gabor_weight_8U, gabor_weight_8U, 0, 255, THRESH_BINARY);	  // narrow using canny edges of original image
	gabor_weight_8U_nms = edges & gabor_weight_8U;
	imshow("edges", edges);

	#ifndef NDEBUG_IN
	// imshow("gabor_weight_8U", gabor_weight_8U);
	// imshow("gabor_weight_8U_nms", gabor_weight_8U_nms);
	#endif

	// // Use canny of Gabor weight to narrow
	// Mat gabor_weight_8U_nms(gabor_resp_dir.size(), CV_8UC1, Scalar(0));
	// Canny(gabor_weight_8U, gabor_weight_8U_nms, 100, 30, 3 );
	// #ifndef NDEBUG_IN
	// imshow("gabor_weight_CV_8U_nms", gabor_weight_8U_nms);
	// #endif


	// feed to hough
	vector<Vec4i> lines;
	vector<Vec4i> lines_right;
	double rho = 1;
	double theta = 1*CV_PI/180;
	int threshol = 10; //20
	double minLineLength = 10; //20
	double maxLineGap = 10;
	HoughLinesP(gabor_weight_8U_nms, lines, rho, theta, threshol, minLineLength, maxLineGap );
	// HoughLines(gabor_weight_8U_nms, lines, rho, theta, threshol, 0, 0, CV_PI/12, CV_PI*11/12 );


	Mat vote_line_ori(gabor_resp_dir.size(), CV_8UC1, Scalar(0));
	int num_left_line = 0, num_right_line = 0;
	for (int i = 0; i < lines.size(); i++)
	{
		line(vote_line_ori, Point(lines[i][0], lines[i][1]), Point(lines[i][2], lines[i][3]), Scalar(255) );
		float k = ((float)(lines[i][0] - lines[i][2]))/(lines[i][1] - lines[i][3]);
		if (k > 0.3 && k < 3 && (lines[i][0] >= img_size.width / 2 || lines[i][2] >= img_size.width / 2)) // right
		{
			num_right_line++;
		}
		else if (k > -3 && k < -0.3 && (lines[i][0] <= img_size.width / 2 || lines[i][2] <= img_size.width / 2))  // left
		{
			num_left_line++;
		}
	}
	// #ifndef NDEBUG_IN
	// imshow("vote_line_ori",vote_line_ori);
	// #endif

	// supplementary hough if one side has not line
	if (num_left_line == 0)
	{
		Mat gabor_weight_8U_nms_left(gabor_resp_dir.size(), CV_8UC1, Scalar(0));
		gabor_weight_8U_nms_left.colRange(0, img_size.width/2) = gabor_weight_8U_nms.colRange(0, img_size.width/2) + 0;
		vector<Vec4i> lines_left;
		HoughLinesP(gabor_weight_8U_nms_left, lines_left, rho, theta, threshol, minLineLength, maxLineGap );
		lines.insert(lines.end(), lines_left.begin(), lines_left.end() );
		cout << "left lines extracted seperately. " << endl;
	}
	if (num_right_line == 0)
	{
		Mat gabor_weight_8U_nms_right(gabor_resp_dir.size(), CV_8UC1, Scalar(0));
		gabor_weight_8U_nms_right.colRange(img_size.width/2, img_size.width) = gabor_weight_8U_nms.colRange(img_size.width/2, img_size.width) + 0;
		vector<Vec4i> lines_right;
		HoughLinesP(gabor_weight_8U_nms_right, lines_right, rho, theta, threshol, minLineLength, maxLineGap );
		lines.insert(lines.end(), lines_right.begin(), lines_right.end() );
		cout << "right lines extracted seperately. " << endl;
	}

	#else     // using cluster+fit

	Mat gabor_weight_cluster_label, gabor_weight_stats, gabor_weight_center;
	int num_cluster = connectedComponentsWithStats(gabor_weight_8U, gabor_weight_cluster_label, gabor_weight_stats, gabor_weight_center);
	vector<Vec4i> lines;
	cout << "finish clustering" << endl;
	// for (int i = 1; i < num_cluster; i++) 		// finding best point-pair
	// {
	// 	if (gabor_weight_stats.at<int>(i,CC_STAT_WIDTH) >= 30 || gabor_weight_stats.at<int>(i,CC_STAT_HEIGHT) >= 30 )
	// 	{
	// 		Mat current_label_mask = gabor_weight_cluster_label == i;
	// 		vector<Point> locations;   // output, locations of non-zero pixels
	// 		findNonZero(current_label_mask, locations);
	// 		Mat line_mask(img_size, CV_8UC1, Scalar(0));
	// 		Mat line_overlay(img_size, CV_8UC1, Scalar(0));
	// 		valarray<float> length(0.0, locations.size());
	// 		valarray<int> end_idx(0, locations.size());
	// 		float max_length = 0;
	// 		int max_start_idx = 0;
	// 		for (int j = 0; j < locations.size(); j+=3 )
	// 		{
	// 			float max_length_cur_start = 0;
	// 			int max_pt_end_idx = j;
	// 			for (int k = locations.size()-1; k >= j; k-=3)
	// 			{
	// 				float line_length = sqrt((locations[j].x - locations[k].x)*(locations[j].x - locations[k].x) + (locations[j].y - locations[k].y)*(locations[j].y - locations[k].y) );
	// 				if (line_length > max_length_cur_start)
	// 				{
	// 					line_mask.setTo(0);
	// 					line(line_mask, locations[j], locations[k], Scalar(255));
	// 					line_overlay = current_label_mask & line_mask;
	// 					float overlay_length = countNonZero(line_overlay);
	// 					if (overlay_length/ line_length > 0.9)
	// 					{
	// 						max_length_cur_start = line_length;
	// 						max_pt_end_idx = k;
	// 					}
	// 				}
	// 			}
	// 			length[j] = max_length_cur_start;
	// 			end_idx[j] = max_pt_end_idx;
	// 			if (length[j] > max_length)
	// 			{
	// 				max_length = length[j];
	// 				max_start_idx = j;
	// 			}
	// 		}
	// 		lines.push_back(Vec4i(locations[max_start_idx].x, locations[max_start_idx].y, locations[end_idx[max_start_idx]].x, locations[end_idx[max_start_idx]].y ));
	// 		line(gabor_weight_8U_nms, locations[max_start_idx], locations[end_idx[max_start_idx]], Scalar(255));
	// 	}
	// }
	for (int i = 1; i < num_cluster; i++) // use fitline
	{
		float width_clus = gabor_weight_stats.at<int>(i,CC_STAT_WIDTH);
		float height_clus = gabor_weight_stats.at<int>(i,CC_STAT_HEIGHT);
		float left_clus = gabor_weight_stats.at<int>(i,CC_STAT_LEFT);
		float top_clus = gabor_weight_stats.at<int>(i,CC_STAT_TOP );
		if ((width_clus >= 30 && height_clus/width_clus > 0.2 )|| height_clus >= 30 || (width_clus >= 20 && height_clus >= 20))
		{
			Mat line_mask(img_size, CV_8UC1, Scalar(0));
			Mat line_overlay(img_size, CV_8UC1, Scalar(0));
			Mat current_label_mask = gabor_weight_cluster_label == i;
			Mat current_label_mask_cut(img_size, CV_8UC1, Scalar(0));
			int top_cut = top_clus;
			float max_overlay_rate = 0;
			for (int j = 1; j <= 2; j ++)
			{
				cout << "cluster # " << i << endl;
				line_mask.setTo(0);
				current_label_mask_cut.setTo(0);
				current_label_mask_cut.rowRange(top_cut, top_clus + height_clus) = current_label_mask.rowRange(top_cut, top_clus + height_clus) + 0;
				vector<Point> locations;   // output, locations of non-zero pixels
				findNonZero(current_label_mask_cut, locations);
				Vec4f line_polar;
				fitLine(locations, line_polar, CV_DIST_L2, 0, 0.01, 0.01);
				if(abs(line_polar[1]/line_polar[0]) > 0.2)
				{
					int start_y = top_cut;
					int start_x = line_polar[2] + line_polar[0]/line_polar[1]*(top_cut - line_polar[3]);
					int end_y = top_clus + height_clus;
					int end_x = line_polar[2] + line_polar[0]/line_polar[1]*(top_clus + height_clus - line_polar[3]);
					Vec4i line_curr(start_x,start_y,end_x,end_y);
					line(line_mask, Point(start_x, start_y), Point(end_x, end_y), Scalar(255));
					line_overlay = current_label_mask_cut & line_mask;
					float overlay_length = countNonZero(line_overlay);
					float line_length = sqrt((start_x - end_x)*(start_x - end_x) + (start_y - end_y)*(start_y - end_y));
					cout << "overlay: " << overlay_length << " , line: " << line_length << endl;
					float overlay_rate = overlay_length/ line_length;
					if (overlay_length/line_length > 0.9)
					{
						lines.push_back(Vec4i(start_x, start_y, end_x, end_y));
						line(gabor_weight_8U_nms,Point(start_x, start_y), Point(end_x, end_y), Scalar(255));
						break;
					}
					else if(j==1)
					{
						top_cut = top_clus + height_clus - (top_clus + height_clus - top_cut)*0.7;
						max_overlay_rate = overlay_rate;
						lines.push_back(Vec4i(start_x, start_y, end_x, end_y));
					}
					else
					{
						if (overlay_rate > max_overlay_rate)
						{
							lines.pop_back();
							lines.push_back(Vec4i(start_x, start_y, end_x, end_y));
							line(gabor_weight_8U_nms,Point(start_x, start_y), Point(end_x, end_y), Scalar(255));
						}
						else
						{
							Vec4i last_vec4i = lines.back();
							line(gabor_weight_8U_nms,Point(last_vec4i[0], last_vec4i[1]), Point(last_vec4i[2], last_vec4i[3]), Scalar(255));
						}
					}
				}
				else
				{break;}
			}


		}
	}

	// imshow("gabor_weight_8U", gabor_weight_8U);
	// imshow("gabor_weight_8U_nms",gabor_weight_8U_nms);
	// waitKey(0);

	#endif

	// vote based on lines extracted
	Mat vote_left(gabor_resp_dir.size(), CV_32FC1, Scalar(0));
	Mat vote_right(gabor_resp_dir.size(), CV_32FC1, Scalar(0));
	Mat vote_line(gabor_resp_dir.size(), CV_32FC1, Scalar(0));
	float y_bottom_left = 0, y_bottom_right= 0;
	for (int i = 0; i < lines.size(); i++)
	{
		float x1 = lines[i][0];
		float y1 = lines[i][1];
		float x2 = lines[i][2];
		float y2 = lines[i][3];
		double w = 0; //(abs(x1-x2) +abs(y1-y2)); // accumulative weight
		float k = (x1-x2)/(y1-y2);

		if (abs(k) < 0.3 || abs(k) > 3 )
		{
			line(vote_lines_img, Point(lines[i][0], lines[i][1]), Point(lines[i][2], lines[i][3]), Scalar(0,255,0), 1);
			continue;
		}
		float x0, y0; // find lower point
		if (y1 > y2)
		{
			x0 = x1;
			y0 = y1;
		}
		else
		{
			x0 = x2;
			y0 = y2;
		}
		if (k > 0) 	// right
		{
			if (x0 < img_size.width / 2)
			{
				line(vote_lines_img, Point(lines[i][0], lines[i][1]), Point(lines[i][2], lines[i][3]), Scalar(0,255,0), 1);
				continue;
			}
			for (int j = 0; j < y0 ; j++ )
			{
				int x_cur = x0 - k*j;
				int y_cur = y0 - j;
				if (x_cur > img_size.width - 1 || x_cur < 0)
					break;
				// w += gabor_weight.at<float>(y_cur, x_cur);
				w += gabor_weight_8U.at<uchar>(y_cur, x_cur);
				vote_right.at<float>(y_cur, x_cur)+= w;
			}
			if (x0 + k*(y_bottom_warp_max - y0)> img_size.width - 1) // not approaching bottom
			{
				float lower_y = y0 + (img_size.width - 1 - x0)/k;
				if (lower_y > y_bottom_right)
					y_bottom_right = lower_y;
			}
			else
				y_bottom_right = y_bottom_warp_max;
			line(vote_line, Point(lines[i][0], lines[i][1]), Point(lines[i][2], lines[i][3]), Scalar(255), 1);
			line(vote_lines_img, Point(lines[i][0], lines[i][1]), Point(lines[i][2], lines[i][3]), Scalar(0,0,255), 1);

			lines_vote.push_back(lines[i]);
		}
		else // left
		{
			if (x0 > img_size.width / 2)
			{
				line(vote_lines_img, Point(lines[i][0], lines[i][1]), Point(lines[i][2], lines[i][3]), Scalar(0,255,0), 1);
				continue;
			}
			for (int j = 0; j < y0 ; j++ )
			{
				int x_cur = x0 - k*j;
				int y_cur = y0 - j;
				if (x_cur > img_size.width - 1 || x_cur < 0)
					break;
				// w += gabor_weight.at<float>(y_cur, x_cur);
				w += gabor_weight_8U.at<uchar>(y_cur, x_cur);
				vote_left.at<float>(y_cur, x_cur)+= w;
			}
			if (x0 + k*(y_bottom_warp_max - y0)< 0) // not approaching bottom
			{
				float lower_y = y0 + (0 - x0)/k;
				if (lower_y > y_bottom_left)
					y_bottom_left = lower_y;
			}
			else
				y_bottom_left = y_bottom_warp_max;
			line(vote_line, Point(lines[i][0], lines[i][1]), Point(lines[i][2], lines[i][3]), Scalar(255), 1);
			line(vote_lines_img, Point(lines[i][0], lines[i][1]), Point(lines[i][2], lines[i][3]), Scalar(0,0,255), 1);

			lines_vote.push_back(lines[i]);
		}
	}

	if ((y_bottom_left != y_bottom_warp_max || y_bottom_right != y_bottom_warp_max ) && y_bottom_left != 0 && y_bottom_right != 0)
	{
		y_bottom_warp = min(y_bottom_left, y_bottom_right);
	}

	// vote_left and right need blurring, because the line is not strictly continuous
	GaussianBlur(vote_left, vote_left, Size(3,3), 1.5, 1.5 );
	GaussianBlur(vote_right, vote_right, Size(3,3), 1.5, 1.5 );



	vote_map = 2*vote_left.mul(vote_right)/(vote_left + vote_right);
	Point van_pt_candi;
	double maxval;
	minMaxLoc(vote_map, NULL, &maxval, NULL, &van_pt_candi);
	if (maxval > 0)
    {
		van_pt_ini = Point2f(van_pt_candi);
		cout << "maxval of vote: " << maxval << endl;
	}
	else
	{
		cout << "no enough vote" << endl;
		return false;
	}

	int thickness = ini_success ? -1:2;
	#ifndef NDEBUG_IN
	Mat show_garbor;
	normalize(vote_left+vote_right, show_garbor, 0, 255, NORM_MINMAX, CV_8U);
	circle(show_garbor, Point(van_pt_ini), 5, Scalar( 255), thickness);
	imshow("vote_left_right", show_garbor);
	// imshow("vote_left", vote_left);
	// imshow("vote_right", vote_right);
	normalize(gabor_weight, show_garbor, 0, 255, NORM_MINMAX, CV_8U);
	circle(show_garbor, Point(van_pt_ini), 5, Scalar( 255), thickness);
	imshow("gabor_weight", show_garbor);
	circle(vote_line, Point(van_pt_ini), 5, Scalar( 255), thickness);
	imshow("vote_line", vote_line);
	normalize(gabor_weight_8U, show_garbor, 0, 255, NORM_MINMAX, CV_8U);
	circle(show_garbor, Point(van_pt_ini), 5, Scalar( 255), thickness);
	imshow("gabor_weight_8U", gabor_weight_8U);
	normalize(gabor_weight_8U_nms, show_garbor, 0, 255, NORM_MINMAX, CV_8U);
	circle(show_garbor, Point(van_pt_ini), 5, Scalar( 255), thickness);
	imshow("gabor_weight_8U_nms", gabor_weight_8U_nms);
	// waitKey(0);
	#endif

	// Mat gabor_vote_l(gabor_resp_dir.size(), CV_32FC1, Scalar(0)); 	// vote based on all pixels
	// Mat gabor_vote_r(gabor_resp_dir.size(), CV_32FC1, Scalar(0));
	// for (int i = gabor_resp_dir.rows/2; i < y_bottom_warp_max; i+=3)
	// {
	// 	for (int j = 0; j < gabor_resp_dir.cols; j+=3)
	// 	{
	// 		float cur_weight = gabor_weight.at<float>(i,j);
	// 		if (cur_weight <= 0)
	// 		{
	// 			continue;
	// 		}
	// 		float tan_angle = tan(gabor_resp_dir.at<float>(i, j));
	// 		if (tan_angle > 0)
	// 		{
	// 			for (int y = i - 1; y > gabor_resp_dir.rows / 4; y-- )
	// 			{
	// 				int x = j + (i - y) * tan_angle;
	// 				if (x >= gabor_resp_dir.cols || x < 0)
	// 				{
	// 					break;
	// 				}
	// 				gabor_vote_l.at<float>(y, x) += cur_weight;//*(1-(float)(i-y)/(i-gabor_resp_dir.rows/4)*(i-y)/(i-gabor_resp_dir.rows/4));
	// 			}
	// 		}
	// 		else
	// 		{
	// 			for (int y = i - 1; y > gabor_resp_dir.rows / 4; y-- )
	// 			{
	// 				int x = j + (i - y) * tan_angle;
	// 				if (x >= gabor_resp_dir.cols || x < 0)
	// 				{
	// 					break;
	// 				}
	// 				gabor_vote_r.at<float>(y, x) += cur_weight;//*(1-(float)(i-y)/(i-gabor_resp_dir.rows/4)*(i-y)/(i-gabor_resp_dir.rows/4));
	// 			}
	// 		}

	// 	}
	// }
	// gabor_vote = 2*gabor_vote_l.mul(gabor_vote_r)/(gabor_vote_l + gabor_vote_r);
	// Point van_pt_candi;
	// minMaxLoc(gabor_vote, NULL, NULL, NULL, &van_pt_candi);
	// van_pt_ini = Point2f(van_pt_candi);

	// Mat show_garbor;
	// normalize(gabor_vote, show_garbor, 0, 255, NORM_MINMAX, CV_8U);
	// circle(show_garbor, van_pt_candi, 5, Scalar( 255), -1);
	// imshow("gabor_vote", show_garbor);
	// normalize(gabor_weight, show_garbor, 0, 255, NORM_MINMAX, CV_8U);
	// circle(show_garbor, van_pt_candi, 5, Scalar( 255), -1);
	// imshow("gabor_weight", show_garbor);
	// waitKey(0);

	return true;
}

void VanPt::LaneDirec(Mat steer_resp_mag, Mat edges, Mat& blur_edges)
{
	Mat mask_left, mask_right;

	RoughLaneDirec(steer_resp_mag, mask_left, 1);
	RoughLaneDirec(steer_resp_mag, mask_right, -1);

	Mat mask_both_sides = mask_left | mask_right;

	blur_edges = edges & mask_both_sides;
	Mat dilate_kernel = getStructuringElement(MORPH_RECT, Size(3,3) ); // 6*6 for big image, 3*3 for small image
	dilate(blur_edges, blur_edges, dilate_kernel);

	#ifndef NDEBUG_IN
	imshow("mask_by_steer", mask_both_sides);
	imshow("blur_edges", blur_edges);
	// waitKey(0);
	#endif
}

void VanPt::RoughLaneDirec(Mat steer_resp_mag, Mat& mask_side, int direction)
{
	mask_side = Mat(img_size, CV_8UC1, Scalar(0));
	float max_sum_left_steer = 0;

	int max_left_angle;
	int x_dest, y_dest;

	// search for the direction with maximal response from van_pt_ini
	int max_angle = max(60, ((int)(atan(max(img_size.width-van_pt_ini.x, van_pt_ini.x)/(y_bottom_warp_max - van_pt_ini.y))*180/CV_PI)/5+1)*5);
	cout << "max_angle: " << max_angle << endl;
	for (int i = 5; i <= max_angle; i += 5)
	{
		Mat mask_ray(img_size, CV_8UC1, Scalar(0));
		x_dest = van_pt_ini.x + (y_bottom_warp_max-van_pt_ini.y)*tan(-direction*i*CV_PI/180);
		y_dest = y_bottom_warp_max;
		if (x_dest <0 && direction == 1) // || x_dest >= img_size.width)
		{
			x_dest = 0;
			y_dest = van_pt_ini.y + (van_pt_ini.x - x_dest)/tan(direction*i*CV_PI/180);
		}
		else if (x_dest >= img_size.width && direction == -1)
		{
			x_dest = img_size.width - 1;
			y_dest = van_pt_ini.y + (van_pt_ini.x - x_dest)/tan(direction*i*CV_PI/180);
		}

		Point dest(x_dest, y_dest);
		line(mask_ray, Point(van_pt_ini), dest, Scalar(255), 10 );
		Point dist = Point(van_pt_ini) - dest;
		float length_line = sqrt(dist.x*dist.x + dist.y*dist.y);

		Mat masked_steer_resp;
		steer_resp_mag.copyTo(masked_steer_resp, mask_ray);

		Scalar sum_steer_sc = sum(masked_steer_resp);

		float sum_steer = sum_steer_sc[0]/length_line;

		if (sum_steer > max_sum_left_steer)
		{
			max_sum_left_steer = sum_steer;
			max_left_angle = i;
			mask_ray.copyTo(mask_side);
		}
	}

	// refine the found direction with finer grid
	int curr_max_angle = max_left_angle;
	for (int i = curr_max_angle-3; i <= curr_max_angle+3; i += 2)
	{
		Mat mask_ray(img_size, CV_8UC1, Scalar(0));
		x_dest = van_pt_ini.x + (y_bottom_warp_max-van_pt_ini.y)*tan(-direction*i*CV_PI/180);
		y_dest = y_bottom_warp_max;
		if (x_dest <0 && direction == 1) // || x_dest >= img_size.width)
		{
			x_dest = 0;
			y_dest = van_pt_ini.y + (van_pt_ini.x - x_dest)/tan(direction*i*CV_PI/180);
		}
		else if (x_dest >= img_size.width && direction == -1)
		{
			x_dest = img_size.width - 1;
			y_dest = van_pt_ini.y + (van_pt_ini.x - x_dest)/tan(direction*i*CV_PI/180);
		}
		Point dest(x_dest, y_dest);
		line(mask_ray, Point(van_pt_ini), dest, Scalar(255), 15 );
		Point dist = Point(van_pt_ini) - dest;
		float length_line = sqrt(dist.x*dist.x + dist.y*dist.y);

		Mat masked_steer_resp;
		steer_resp_mag.copyTo(masked_steer_resp, mask_ray);

		Scalar sum_steer_sc = sum(masked_steer_resp);

		float sum_steer = sum_steer_sc[0]/length_line;

		if (sum_steer > max_sum_left_steer)
		{
			max_sum_left_steer = sum_steer;
			max_left_angle = i;
			mask_ray.copyTo(mask_side);
		}
	}

	y_bottom_warp = min(y_bottom_warp_max, y_dest);
	y_bottom_warp = 0.5*(y_bottom_warp + y_bottom_warp_max);
}

void VanPt::NMS(Mat matrix, Mat& matrix_nms)
{
	for (int i = van_pt_cali.y; i < y_bottom_warp_max; i++)
	{
		int j = 2;
		uchar* rowptr = matrix.ptr<uchar>(i);
		uchar* rowptr_nms = matrix_nms.ptr<uchar>(i);

		while (j < img_size.width-2)
		{
			if (rowptr[j] > rowptr[j+1] )
			{
				if (rowptr[j] >= rowptr[j-1])
				{
					rowptr_nms[j] = rowptr[j];
					// rowptr_nms[j+1] = rowptr[j+1];
					// rowptr_nms[j-1] = rowptr[j-1];
					// rowptr_nms[j+2] = rowptr[j+2];
					// rowptr_nms[j-2] = rowptr[j-2];

				}
			}
			else
			{
				j++;
				while(j < img_size.width-2 && rowptr[j] <= rowptr[j+1])
				{
					j++;
				}
				if (j < img_size.width-2)
				{
					rowptr_nms[j] = rowptr[j];
					// rowptr_nms[j+1] = rowptr[j+1];
					// rowptr_nms[j-1] = rowptr[j-1];
					// rowptr_nms[j+2] = rowptr[j+2];
					// rowptr_nms[j-2] = rowptr[j-2];
				}
			}
			j += 2;
		}
	}
}


void VanPt::DecideChnlThresh(Mat color_img, Mat image, Mat blur_edges) // use normalized one or ?
{
/// generate thresholds for color channels
	Mat pixhis_b, pixhis_g, pixhis_r;
	Mat pixhis_h, pixhis_l, pixhis_s;
	Mat img_hls(color_img.size(), CV_8UC3);
	cvtColor(color_img, img_hls, COLOR_BGR2HLS);
	vector<Mat> bgr_chnl, hls_chnl;
	split(color_img, bgr_chnl);
	split(img_hls, hls_chnl);



	#ifndef HIGH_BOT
	blur_edges.rowRange(0, image.rows*2/3) = 0;
	#else
	blur_edges.rowRange(0, image.rows/2) = 0;
	blur_edges.rowRange(image.rows*7/10, image.rows) = 0;  // for caltech data
	#endif

    Mat mask_trap(image.size(), CV_8UC1, Scalar(0));
    drawContours(mask_trap, warp_test_vec, -1, Scalar(255), CV_FILLED );

	blur_edges = blur_edges & mask_trap;
	int num_nonz = countNonZero(blur_edges);

	#ifndef NDEBUG_IN
	imshow("blured canny", blur_edges );
	// waitKey(0);
	#endif

	/// histogram
	int hist_size = 256;
	float range_bgrls[] = {0, 256};
	const float* range_5 = {range_bgrls};
	int hist_size_h = 180;
	float range_h[] = {0, 180};
	const float* range_1 = {range_h};
	bool uniform = true;
	bool accumulate = false;

	calcHist( &bgr_chnl[0], 1, 0, blur_edges, pixhis_b, 1, &hist_size, &range_5, uniform, accumulate ); // or edges, height_mask, Mat()
	calcHist( &bgr_chnl[1], 1, 0, blur_edges, pixhis_g, 1, &hist_size, &range_5, uniform, accumulate );
	calcHist( &bgr_chnl[2], 1, 0, blur_edges, pixhis_r, 1, &hist_size, &range_5, uniform, accumulate );
	calcHist( &hls_chnl[0], 1, 0, blur_edges, pixhis_h, 1, &hist_size_h, &range_1, uniform, accumulate );
	calcHist( &hls_chnl[1], 1, 0, blur_edges, pixhis_l, 1, &hist_size, &range_5, uniform, accumulate );
	calcHist( &hls_chnl[2], 1, 0, blur_edges, pixhis_s, 1, &hist_size, &range_5, uniform, accumulate );

	GaussianBlur(pixhis_b, pixhis_b, Size(25,25), 10); // smooth the histograms
	GaussianBlur(pixhis_g, pixhis_g, Size(25,25), 10);
	GaussianBlur(pixhis_r, pixhis_r, Size(25,25), 10);
	GaussianBlur(pixhis_h, pixhis_h, Size(19,19), 7);
	GaussianBlur(pixhis_l, pixhis_l, Size(25,15), 10);
	GaussianBlur(pixhis_s, pixhis_s, Size(25,25), 10);

	// find local maximum and desired threshold (minimum between maximum)
	/*vector<vector<int> > local_max_pt(6);
	vector<vector<float> > local_max(6);
	localMaxima(pixhis_b, 20, 15, 0, local_max_pt[0], local_max[0], chnl_thresh[0], "b");
	localMaxima(pixhis_g, 20, 15, 0, local_max_pt[1], local_max[1], chnl_thresh[1], "g");
	localMaxima(pixhis_r, 20, 15, 0, local_max_pt[2], local_max[2], chnl_thresh[2], "r");
	localMaxima(pixhis_h, 10, 15, 0, local_max_pt[3], local_max[3], chnl_thresh[3], "h");
	localMaxima(pixhis_l, 25, 15, 0, local_max_pt[4], local_max[4], chnl_thresh[4], "l");
	localMaxima(pixhis_s, 25, 15, 0, local_max_pt[5], local_max[5], chnl_thresh[5], "s");
	*/
	localMaxima(pixhis_b, num_nonz, chnl_thresh[0], hist_size);
	localMaxima(pixhis_g, num_nonz, chnl_thresh[1], hist_size);
	localMaxima(pixhis_r, num_nonz, chnl_thresh[2], hist_size);
	localMaxima(pixhis_h, num_nonz, chnl_thresh[3], hist_size_h);
	localMaxima(pixhis_l, num_nonz, chnl_thresh[4], hist_size);
	localMaxima(pixhis_s, num_nonz, chnl_thresh[5], hist_size);

	cout << "finish finding peaks: " << chnl_thresh[0] << " " << chnl_thresh[1] << " " << chnl_thresh[2] << " " << chnl_thresh[3] << " " << chnl_thresh[4] << " " << chnl_thresh[5] << " " << endl;


	#ifndef NDEBUG_IN
	// Draw the histograms
	int hist_w = 512; int hist_h = 400; // size of figure
	int bin_w = round( (double) hist_w/hist_size ); // width of a bin
	int bin_wh = round( (double) hist_w/hist_size_h );
	Mat hist_image_bgr( hist_h, hist_w, CV_8UC3, Scalar( 0,0,0) );
	Mat hist_image_hls( hist_h, hist_w, CV_8UC3, Scalar( 0,0,0) );

	// find global maximum
	double maxvalhis[6];
	Point mavvalhis_pt[6];
	minMaxLoc(pixhis_b, NULL, &maxvalhis[0], NULL, &mavvalhis_pt[0]);
	minMaxLoc(pixhis_g, NULL, &maxvalhis[1], NULL, &mavvalhis_pt[1]);
	minMaxLoc(pixhis_r, NULL, &maxvalhis[2], NULL, &mavvalhis_pt[2]);
	minMaxLoc(pixhis_h, NULL, &maxvalhis[3], NULL, &mavvalhis_pt[3]);
	minMaxLoc(pixhis_l, NULL, &maxvalhis[4], NULL, &mavvalhis_pt[4]);
	minMaxLoc(pixhis_s, NULL, &maxvalhis[5], NULL, &mavvalhis_pt[5]);

	cout << "max loc" << mavvalhis_pt[0] << " " << mavvalhis_pt[1] << " " << mavvalhis_pt[2] << " " << mavvalhis_pt[3] << " " << mavvalhis_pt[4] << " " << mavvalhis_pt[5] << endl;
	cout << "max val" << maxvalhis[0] << " " << maxvalhis[1] << " " << maxvalhis[2] << " " << maxvalhis[3] << " " << maxvalhis[4] << " " << maxvalhis[5] << endl;

	// normalize the histogram into the figure
	double max_bgr = max(max(maxvalhis[0], maxvalhis[1]), maxvalhis[2]);
	double max_hls = max(max(maxvalhis[3], maxvalhis[4]), maxvalhis[5]);

	pixhis_b = pixhis_b*(hist_h-1)/max_bgr;
	pixhis_g = pixhis_g*(hist_h-1)/max_bgr;
	pixhis_r = pixhis_r*(hist_h-1)/max_bgr;
	pixhis_h = pixhis_h*(hist_h-1)/max_hls;
	pixhis_l = pixhis_l*(hist_h-1)/max_hls;
	pixhis_s = pixhis_s*(hist_h-1)/max_hls;

	// Draw for each channel
	for( int i = 1; i < hist_size; i++ )
	{
		line( hist_image_bgr, Point( bin_w*(i-1), hist_h-1 - round(pixhis_b.at<float>(i-1)) ) ,
							  Point( bin_w*(i), hist_h-1 - round(pixhis_b.at<float>(i)) ),
							  Scalar( 255, 0, 0), 2, 8, 0  );
      line( hist_image_bgr, Point( bin_w*(i-1), hist_h-1 - round(pixhis_g.at<float>(i-1)) ) ,
							Point( bin_w*(i), hist_h-1 - round(pixhis_g.at<float>(i)) ),
							Scalar( 0, 255, 0), 2, 8, 0  );
      line( hist_image_bgr, Point( bin_w*(i-1), hist_h-1 - round(pixhis_r.at<float>(i-1)) ) ,
							Point( bin_w*(i), hist_h-1 - round(pixhis_r.at<float>(i)) ),
							Scalar( 0, 0, 255), 2, 8, 0  );
	  line( hist_image_hls, Point( bin_w*(i-1), hist_h-1 - round(pixhis_l.at<float>(i-1)) ) ,
							Point( bin_w*(i), hist_h-1 - round(pixhis_l.at<float>(i)) ),
							Scalar( 0, 255, 0), 2, 8, 0  );
      line( hist_image_hls, Point( bin_w*(i-1), hist_h-1 - round(pixhis_s.at<float>(i-1)) ) ,
							Point( bin_w*(i), hist_h-1 - round(pixhis_s.at<float>(i)) ),
							Scalar( 0, 0, 255), 2, 8, 0  );
	}
	for( int i = 1; i < hist_size_h; i++ )
	{
      line( hist_image_hls, Point( bin_wh*(i-1), hist_h-1 - round(pixhis_h.at<float>(i-1)) ) ,
							Point( bin_wh*(i), hist_h-1 - round(pixhis_h.at<float>(i)) ),
							Scalar( 255, 0, 0), 2, 8, 0  );
	}

	line(hist_image_bgr, Point(chnl_thresh[0]*bin_w, 0), Point(chnl_thresh[0]*bin_w, hist_h-1), Scalar(255, 255,255), 1, 4);
	line(hist_image_bgr, Point(chnl_thresh[1]*bin_w, 0), Point(chnl_thresh[1]*bin_w, hist_h-1), Scalar(255, 255,255), 1, 4);
	line(hist_image_bgr, Point(chnl_thresh[2]*bin_w, 0), Point(chnl_thresh[2]*bin_w, hist_h-1), Scalar(255, 255,255), 1, 4);

	line(hist_image_hls, Point(chnl_thresh[3]*bin_wh, 0), Point(chnl_thresh[3]*bin_wh, hist_h-1), Scalar(255, 255,255), 1, 4);
	line(hist_image_hls, Point(chnl_thresh[4]*bin_w, 0), Point(chnl_thresh[4]*bin_w, hist_h-1), Scalar(255, 255,255), 1, 4);
	line(hist_image_hls, Point(chnl_thresh[5]*bin_w, 0), Point(chnl_thresh[5]*bin_w, hist_h-1), Scalar(255, 255,255), 1, 4);

	// Display
	imshow("calcHist BGR", hist_image_bgr );
	imshow("calcHist HLS", hist_image_hls );

	// waitKey(0);

	// test the effect of the threshold
	Mat masked_edges, color_mask;

	Mat height_mask(image.size(), CV_8UC1, Scalar(255));
	#ifndef HIGH_BOT
	height_mask.rowRange(0, image.rows*2/3) = 0;
	#else
	height_mask.rowRange(0, image.rows/2) = 0;
	height_mask.rowRange(image.rows*7/10, image.rows) = 0;  // for caltech data
	#endif

	color_mask = bgr_chnl[0] > chnl_thresh[0] & bgr_chnl[0] < 255;
	bitwise_and(image, color_mask, masked_edges);
	bitwise_and(masked_edges, height_mask, masked_edges);
	imshow("masked b", masked_edges );

	color_mask = bgr_chnl[1] > chnl_thresh[1] & bgr_chnl[1] < 255;
	bitwise_and(image, color_mask, masked_edges);
	bitwise_and(masked_edges, height_mask, masked_edges);
	imshow("masked g", masked_edges );

	color_mask = bgr_chnl[2] > chnl_thresh[2] & bgr_chnl[2] < 255;
	bitwise_and(image, color_mask, masked_edges);
	bitwise_and(masked_edges, height_mask, masked_edges);
	imshow("masked r", masked_edges );

	color_mask = hls_chnl[0] > 0 & hls_chnl[0] < chnl_thresh[3];
	bitwise_and(image, color_mask, masked_edges);
	bitwise_and(masked_edges, height_mask, masked_edges);
	imshow("masked h", masked_edges );

	color_mask = hls_chnl[1] > chnl_thresh[4] & hls_chnl[1] < 255;
	bitwise_and(image, color_mask, masked_edges);
	bitwise_and(masked_edges, height_mask, masked_edges);
	imshow("masked l", masked_edges );

	color_mask = hls_chnl[2] > chnl_thresh[5] & hls_chnl[2] < 255;
	bitwise_and(image, color_mask, masked_edges);
	bitwise_and(masked_edges, height_mask, masked_edges);
	imshow("masked s", masked_edges );

	Mat blur_edges_bi = blur_edges > 0;
	bitwise_and(image, blur_edges_bi, masked_edges);
	imshow("masked edge", masked_edges );
	waitKey(0);

	#endif
}






void localMaxima(const Mat& src, int num_nonz, int& min_loc, int hist_size)
{
	float accu_num_pix = 0;
	//float total_pix = image.rows * image.cols/4 ;
	int i;
	for (i = 0; i < hist_size; i++)
	{
		accu_num_pix += src.at<float>(hist_size - 1 - i);
		if (accu_num_pix >= 0.5 * num_nonz)
			break;
	}
	min_loc = hist_size - 1 - i;

	return;
}




void VanPt::drawOn(Mat& newwarp, LaneMark& lane_mark)
{
	vote_lines_img.copyTo(newwarp);


	vector<Point> warp_src_int;
	warp_src_int.push_back(Point(warp_src[0]));
	warp_src_int.push_back(Point(warp_src[1]));
	warp_src_int.push_back(Point(warp_src[2]));
	warp_src_int.push_back(Point(warp_src[3]));
	warp_test_vec.clear();
	warp_test_vec.push_back(warp_src_int);
	// drawContours(newwarp, warp_test_vec, -1, Scalar(255, 0, 0), 5 ); // draw the blue region for wrapping 
	#ifndef NDEBUG_IN
	cout << " current warp vertex: " << warp_test_vec[0][0] << warp_test_vec[0][1] << warp_test_vec[0][2] << warp_test_vec[0][3] << endl;
	#endif
}

void VanPt::renewWarp()
{
	warp_src.clear();
	float x_van = van_pt_best.x;
	float y_van = van_pt_best.y;
	float y_bottom = y_bottom_warp;
	float y_top_warp = (y_bottom_warp + 5*y_van)/6;
	float x_left = 0;
	float x_right = img_size.width - 1;
	#ifdef COUT
	cout << "y_bottom_warp: " << y_bottom_warp << endl;
	#endif
	warp_src.push_back(Point2f((y_top_warp-y_bottom)/(y_van-y_bottom)*(x_van-x_left) + x_left, y_top_warp)); 	// remind that calculation with int could result in weird values when division appears!
	warp_src.push_back(Point2f(x_left, y_bottom ));
	warp_src.push_back(Point2f(x_right, y_bottom ));
	warp_src.push_back(Point2f((y_top_warp-y_bottom)/(y_van-y_bottom)*(x_van-x_right)+ x_right, y_top_warp));

	per_mtx = getPerspectiveTransform(warp_src, warp_dst);
	inv_per_mtx = getPerspectiveTransform(warp_dst, warp_src);
}


template <class T> string x2str(T num)
{
	stringstream ss;
	ss << num;
	return ss.str();
}