#ifndef ARMVIZWIDGET_HPP
#define ARMVIZWIDGET_HPP

#include <QBrush>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QPointF>
#include <QString>
#include <QWidget>
#include <QtCore>
#include <vector>
#ifndef Q_MOC_RUN
#include <ros/ros.h>
#include <std_msgs/Float64MultiArray.h>
#endif

namespace Ui {
class armvizwidget;
}

struct ArmLink {
  double length;
  double thickness;
  double jointRad;
  QColor linkColor;
  QColor jointColor;
};

struct Claw {
  double length;
  double width;
  double thickness;
  QColor outlineColor;
};

struct TurnTable {
  double radius;
  double offset;
  QColor outlineColor;
  QColor fillColor;
};

struct RobotFrame {
  double length;
  double width;
  double offset;
  QColor outlineColor;
  QColor fillColor;
};

class Arm : public QGraphicsItem {
public:
  Arm(ArmLink shoulder, ArmLink elbow, ArmLink wrist, Claw claw,
      TurnTable turnTable, RobotFrame robotFrame, QPointF startPos,
      bool isSideView, std::vector<double> angles,
      QGraphicsItem *parent = nullptr);
  ~Arm();
  void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
             QWidget *widget = nullptr);
  QRectF boundingRect() const;
  void setAngles(std::vector<double> angles);

private:
  // index for input angles that defines the geomerty of the arm, also the index
  // of the angles in the received ROS message
  const double TURNTABLE_YAW = 0;
  const double SHOULDER_PITCH = 1;
  const double ELBOW_PITCH = 2;
  const double WRIST_PITCH = 3;
  const double WRIST_ROLL = 4;
  const double CLAW_CLOSURE_ANGLE = 5;
  const int NUM_ANGLES = 6;

  QPen *mPen;
  QBrush *mBrush;
  QPointF startPos;
  ArmLink shoulder, elbow, wrist;
  Claw claw;
  TurnTable turnTable;
  RobotFrame robotFrame;

  bool isSideView;
  std::vector<double> mAngles;

  QPointF DrawLink(QPainter *painter, ArmLink armLink, QPointF startPos,
                   double angle);
  void DrawJoint(QPainter *painter, ArmLink armLink, QPointF startPos);
  void DrawClaw(QPainter *painter, Claw claw, QPointF startPos);
  void DrawTurnTable(QPainter *painter, TurnTable turnTable, QPointF startPos);
  void DrawRobotFrame(QPainter *painter, RobotFrame robotFrame,
                      QPointF startPos, qreal offset);
};

class armvizwidget : public QWidget {
  Q_OBJECT

public:
  explicit armvizwidget(QWidget *parent = nullptr);
  ~armvizwidget();

  bool Init(ros::NodeHandle &nh);

private:
  // expects an ID array of six angles in radians
  void ArmPosCallback(std_msgs::Float64MultiArrayConstPtr armPos);
  Arm *createArm(bool isSideView, std::vector<double> angles);

  Ui::armvizwidget *ui;
  QGraphicsScene *topScene;
  QGraphicsScene *sideScene;
  Arm *topViewArm, *sideViewArm;

  ros::NodeHandle *avNh;
  ros::Subscriber mPoseSub;

  // dimensions for drawing
  const double SHOULDER_LEN = 35.5, ELBOW_LEN = 40.64, WRIST_LEN = 30.48;
  const double SHOULDER_THICK = 2, ELBOW_THICK = 2, WRIST_THICK = 2;
  const double SHOULDER_JOINT_RAD = 2, ELBOW_JOINT_RAD = 2, WRIST_JOINT_RAD = 2;
  const double CLAW_LEN = 10, CLAW_WIDTH = 10, CLAW_THICK = 2;
  const double TURNTABLE_RAD = 15, TURNTABLE_OFFSET = 2;
  const double ROBOT_LEN = 60, ROBOT_WIDTH = 45, FRAME_OFFSET = 2;
};

#endif // ARMVIZWIDGET_HPP
