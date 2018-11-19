/*
 * DatasetBrowser.cpp
 *
 *  Created on: Aug 9, 2018
 *      Author: sujiwo
 */

#include <iostream>
#include <sstream>
#include <string>
#include <chrono>
#include <thread>
#include <functional>

#include <opencv2/core.hpp>
#include <SequenceSLAM.h>
#include <QImage>
#include <QFileDialog>

#include "DatasetBrowser.h"
#include "BaseFrame.h"
#include "datasets/MeidaiBagDataset.h"


using namespace std;


// XXX: Find a way to specify these values from external input
CameraPinholeParams meidaiCamera1Params(
	1150.96938467,	// fx
	1150.96938467,	// fy
	988.511326762,	// cx
	692.803953253,	// cy
	1920,			// width
	1440			// height
);



DatasetBrowser::DatasetBrowser(QWidget *parent):
	QWidget(parent)
{
	ui.setupUi(this);

	timelineSlider = ui.timelineSlider;
	frame = ui.frame;
	timeOffsetLabel = ui.timeOffsetLabel;
	saveImageButton = ui.saveImageButton;
	playButton = ui.playButton;
}

DatasetBrowser::~DatasetBrowser()
{}


void
DatasetBrowser::on_timelineSlider_sliderMoved(int v)
{
	return setImageOnPosition(v);
}


void
DatasetBrowser::on_saveImageButton_clicked(bool checked)
{
	QString fname = QFileDialog::getSaveFileName(this, tr("Save Image"));
	if (fname.length()==0)
		return;
	cv::Mat image = openDs->get(timelineSlider->value())->getImage();
	cv::imwrite(fname.toStdString(), image);
}


// XXX: Change this
const string lidarCalibrationParams("/home/sujiwo/Autoware/ros/src/computing/perception/localization/packages/vmml/params/64e-S2.yaml");

void
DatasetBrowser::changeDataset(GenericDataset *ds, datasetType ty)
{
	openDs = ds;
	timelineSlider->setRange(0, ds->size()-1);
	dataItem0 = ds->get(0);

	if (ty==DatasetBrowser::MeidaiType) {
		MeidaiBagDataset* meidaiDs = static_cast<MeidaiBagDataset*>(ds);
		meidaiDs->setLidarParameters(lidarCalibrationParams, string(), defaultLidarToCameraTransform);
		meidaiPointClouds = meidaiDs->getLidarScanBag();
	}

	setImageOnPosition(0);
}


bool isInside (const LidarScanBag::Ptr &bg, ros::Time Tx)
{
	return (Tx>=bg->startTime() and Tx<bg->stopTime());
}


void
DatasetBrowser::setImageOnPosition (int v)
{
	if (v<0 or v>=openDs->size())
		throw runtime_error("Invalid time position");

	auto curItem = openDs->get(v);

	auto ts = curItem->getTimestamp() - dataItem0->getTimestamp();
	double tsd = double(ts.total_microseconds())/1e6;

	stringstream ss;
	ss << fixed << setprecision(2) << tsd;
	timeOffsetLabel->setText(QString::fromStdString(ss.str()));

	cv::Mat image = curItem->getImage();
	cv::cvtColor(image, image, CV_BGR2RGB);

	try {
		auto imageTime = ros::Time::fromBoost(curItem->getTimestamp());

		if (meidaiPointClouds!=nullptr and isInside(meidaiPointClouds, imageTime)) {

			uint32_t pcIdx = meidaiPointClouds->getPositionAtTime(imageTime);
			auto pointCloud = meidaiPointClouds->at(pcIdx);
			vector<cv::Point2f> projections = projectScan(pointCloud);

			for (auto &pt2d: projections) {
				if ((pt2d.x>=0 and pt2d.x<image.cols) and (pt2d.y>=0 and pt2d.y<image.rows)) {
					cv::circle(image, pt2d, 3, cv::Scalar(0,0,255));
				}
			}
		}
	} catch (const std::exception &e) {}

	QImage curImage (image.data, image.cols, image.rows, image.step[0], QImage::Format_RGB888);
	frame->setImage(curImage);
}


void
DatasetBrowser::disableControlsOnPlaying (bool state)
{
	timelineSlider->setDisabled(state);
	saveImageButton->setDisabled(state);
}


void
DatasetBrowser::on_playButton_clicked(bool checked)
{
	static bool playStarted = false;
	static std::thread *playerThread = NULL;

	std::function<void()> playThreadFn =
	[&]()
	{
		const int startPos = timelineSlider->sliderPosition();
		disableControlsOnPlaying(true);
		for (int p=startPos; p<=timelineSlider->maximum(); p++) {

			ptime t1x = getCurrentTime();
			timelineSlider->setSliderPosition(p);
			setImageOnPosition(p);
			if (playStarted == false)
				break;

			if(p < timelineSlider->maximum()) {
				ptime t1 = openDs->get(p)->getTimestamp();
				ptime t2 = openDs->get(p+1)->getTimestamp();
				ptime t2x = getCurrentTime();
				tduration tdx = t2x - t1x;	// processing overhead
				tduration td = (t2-t1) - tdx;
				std::this_thread::sleep_for(std::chrono::milliseconds(td.total_milliseconds()));
			}
		}
		disableControlsOnPlaying(false);
	};

	if (checked==true) {
		cout << "Play\n";
		playStarted = true;
		playerThread = new std::thread(playThreadFn);
	}

	else {
		cout << "Stop\n";
		playStarted = false;
		playerThread->join();
		delete(playerThread);
	}

	return;
}


std::vector<cv::Point2f>
DatasetBrowser::projectScan
(pcl::PointCloud<pcl::PointXYZ>::ConstPtr lidarScan)
const
{
	vector<cv::Point2f> projections;

	// Create fake frame
	BaseFrame frame;
	frame.setPose(defaultLidarToCameraTransform);
	frame.setCameraParam(&meidaiCamera1Params);

	for (auto it=lidarScan->begin(); it!=lidarScan->end(); ++it) {
		auto &pts = *it;
		Vector3d pt3d (pts.x, pts.y, pts.z);
		auto p2d = frame.project(pt3d);
		projections.push_back(cv::Point2f(p2d.x(), p2d.y()));
	}

	return projections;
}
