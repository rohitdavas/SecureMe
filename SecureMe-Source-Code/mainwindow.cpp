#include <QApplication>
#include <QFileDialog>
#include <QMessageBox>
#include <QPixmap>
#include <QKeyEvent>
#include <QDebug>
#include <QCameraInfo>
#include <QGridLayout>
#include <QIcon>
#include <QStandardItem>
#include <QSize>


#include "opencv2/videoio.hpp"
#include "mainwindow.h"
#include "utilities.h"


MainWindow::MainWindow(QWidget *parent) :
           QMainWindow(parent),
           fileMenu(nullptr),
           capturer(nullptr)
{
    initUI();
    data_lock = new QMutex();
}

MainWindow::~MainWindow()
{
   }

void MainWindow::initUI()
{
    this->resize(1000, 1000);
    fileMenu = menuBar()->addMenu("&File");
    QGridLayout *main_layout = new QGridLayout();

    imageScene = new QGraphicsScene(this);
    imageView = new QGraphicsView(imageScene);

    main_layout-> addWidget(imageView, 0, 0, 12, 1);

    QGridLayout *tools_layout = new QGridLayout();
    main_layout->addLayout(tools_layout, 12, 0, 1, 1);

    // monitor checkbox
    monitorCheckBox = new QCheckBox(this);
    monitorCheckBox->setText("Monitor On/Off");
    tools_layout->addWidget(monitorCheckBox, 0, 0);
    connect(monitorCheckBox, SIGNAL(stateChanged(int)), this, SLOT(updateMonitorStatus(int)));

    // record button
    recordButton = new QPushButton(this);
    recordButton->setText("Record");
    tools_layout->addWidget(recordButton, 0, 1, Qt::AlignHCenter);
    tools_layout->addWidget(new QLabel(this), 0, 2);
    connect(recordButton, SIGNAL(clicked(bool)), this, SLOT(recordingStartStop()));

    // list of saved videos bar
    saved_list = new QListView(this);
    saved_list->setViewMode(QListView::IconMode);
    saved_list->setResizeMode(QListView::Adjust);
    saved_list->setSpacing(5);
    saved_list->setWrapping(false);
    list_model = new QStandardItemModel(this);
    saved_list->setModel(list_model);
    main_layout->addWidget(saved_list, 13, 0, 4, 1);

    // add the widget
    QWidget *widget = new QWidget();
    widget->setLayout(main_layout);
    setCentralWidget(widget);

    // setup status bar
    mainStatusBar = statusBar();
    mainStatusLabel = new QLabel(mainStatusBar);
    mainStatusBar->addPermanentWidget(mainStatusLabel);
    mainStatusLabel->setText("Software is ready");

    // other function for initUI
    createActions();
    populateSavedList();
}

void MainWindow::createActions(){

    // create actions, add them to menus
    cameraInfoAction = new QAction("Camera &Information", this);
    fileMenu->addAction(cameraInfoAction);

    openCameraAction = new QAction("&Open Camera", this);
    fileMenu->addAction(openCameraAction);

    calcFPSAction = new QAction("&Calculate FPS", this);
    fileMenu->addAction(calcFPSAction);

    exitAction = new QAction("&Exit", this);
    fileMenu->addAction(exitAction);

    // connect the signals and slots
    connect(exitAction, SIGNAL(triggered(bool)), QApplication::instance(), SLOT(quit()));
    connect(cameraInfoAction, SIGNAL(triggered(bool)), this, SLOT(showCameraInfo()));
    connect(openCameraAction, SIGNAL(triggered(bool)), this, SLOT(openCamera()));
    connect(calcFPSAction, SIGNAL(triggered(bool)), this, SLOT(calculateFPS()));
}

void MainWindow::populateSavedList(){
    QDir dir(Utilities::getDataPath());
    QStringList nameFilters;
    nameFilters << "*.jpg";
    QFileInfoList files = dir.entryInfoList(
        nameFilters, QDir::NoDotAndDotDot | QDir::Files, QDir::Name);

    foreach(QFileInfo cover, files) {
        QString name = cover.baseName();
        QStandardItem *item = new QStandardItem();
        list_model->appendRow(item);
        QModelIndex index = list_model->indexFromItem(item);
        list_model->setData(index, QPixmap(cover.absoluteFilePath()).scaledToHeight(145), Qt::DecorationRole);
        list_model->setData(index, name, Qt::DisplayRole);
    }
}

void MainWindow::showCameraInfo()
{
    QMessageBox msgBox;
    //    QPushButton *button = new QPushButton("new button", this);

    msgBox.setText("Camera(s) Info");
    QList<QCameraInfo> cameras = QCameraInfo::availableCameras();
    QString info = QString("Available Cameras (%1):\n").arg(cameras.length());
    QString detailed_info;
    if (cameras.empty()){
        QMessageBox::information(this, "Information", "No Camera available.");
        return;
    }

    foreach(QCameraInfo c_info, cameras)
    {
        info += "    - " + c_info.deviceName() + "\n";
        detailed_info += " - " + c_info.description() + "\n";
    }

    msgBox.setInformativeText(info);
    msgBox.setDetailedText(detailed_info);
//    msgBox.addButton(button, QMessageBox::RejectRole);
    msgBox.exec();
}

void MainWindow::openCamera(){

    if(capturer != nullptr)
    {
        capturer->setRunning(false);
        disconnect(capturer, &CaptureThread::frameCaptured, this, &MainWindow::updateFrame);
        disconnect(capturer, &CaptureThread::fpsChanged, this, &MainWindow::updateFPS);
        disconnect(capturer, &CaptureThread::videoSaved, this, &MainWindow::appendSavedVideo);
        connect(capturer, &CaptureThread::finished, capturer, &CaptureThread::deleteLater);
    }

    int camID = 0;
    capturer = new CaptureThread(camID, data_lock);
    connect(capturer, &CaptureThread::frameCaptured, this, &MainWindow::updateFrame);
    connect(capturer, &CaptureThread::fpsChanged, this, &MainWindow::updateFPS);
    connect(capturer, &CaptureThread::videoSaved, this, &MainWindow::appendSavedVideo);
    capturer->start();

    mainStatusLabel->setText(QString("Capturing Camera %1").arg(camID));
    monitorCheckBox->setCheckState(Qt::Unchecked);
    recordButton->setText("Record");
    recordButton->setEnabled(true);
}

void MainWindow::updateFrame(cv::Mat* mat){

    data_lock->lock();
    currentFrame = *mat;
    data_lock->unlock();

    QImage frame(
        currentFrame.data,
        currentFrame.cols,
        currentFrame.rows,
        currentFrame.step,
        QImage::Format_RGB888);
    QPixmap image = QPixmap::fromImage(frame);

    imageScene->clear();
    imageView->resetMatrix();
    imageScene->addPixmap(image);
    imageScene->update();
    imageView->setSceneRect(image.rect());
}

void MainWindow::calculateFPS(){
    if(capturer != nullptr) {
            capturer->startCalcFPS();
        }
}

void MainWindow::updateFPS(float fps){
    mainStatusLabel->setText(QString("FPS of current camera is %1").arg(fps));
}

void MainWindow::recordingStartStop(){

    QString text = recordButton->text();

    if(text == "Record" && capturer != nullptr) {
        capturer->setVideoSavingStatus(CaptureThread::STARTING);
        recordButton->setText("Stop Recording");
        monitorCheckBox->setCheckState(Qt::Unchecked);
        monitorCheckBox->setEnabled(false);
    }

    else if(text == "Stop Recording" && capturer != nullptr) {
        capturer->setVideoSavingStatus(CaptureThread::STOPPING);
        recordButton->setText("Record");
        monitorCheckBox->setEnabled(true);
    }
}

void MainWindow::appendSavedVideo(QString name){
    QString cover = Utilities::getSavedVideoPath(name, "jpg");
    QStandardItem *item = new QStandardItem();
    list_model->appendRow(item);
    QModelIndex index = list_model->indexFromItem(item);
    list_model->setData(index, QPixmap(cover).scaledToHeight(145), Qt::DecorationRole);
    list_model->setData(index, name, Qt::DisplayRole);
    saved_list->scrollTo(index);
}

void MainWindow::updateMonitorStatus(int status){

    if(capturer == nullptr) {
           return;
       }
       if(status) {
           capturer->setMotionDetectingStatus(true);
           recordButton->setEnabled(false);
       } else {
           capturer->setMotionDetectingStatus(false);
           recordButton->setEnabled(true);
       }
}
