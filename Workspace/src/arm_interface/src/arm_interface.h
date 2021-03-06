#include <algorithm>
#include <arm_interface/ArmCmd.h>
#include <arm_interface/arm_mode.h>
#include <can_msgs/Frame.h>
#include <cmath>
#include <ik/Roboarm.h>
#include <ros/ros.h>
#include <std_msgs/Float64MultiArray.h>
#include <std_msgs/Int32.h>
#include <std_msgs/String.h>
#include <stdbool.h>
#include <console_message/console_message.h>

class ArmControlInterface {
public:
  ArmControlInterface(float dT)
      : m_dT(dT), m_desiredAnglesPublisher(NULL), m_desiredPosPublisher(NULL), m_actualAnglesPublisher(NULL),
        m_canPublisher(NULL), m_isReady(false), m_isFirstRun(true), m_currentMode(s_modeOpenLoop) {
    ikControl = new Roboarm((double *)s_linkLengths, (double *)s_defaultAngles);

    m_jointsReady = std::vector<bool>(s_numJoints, false);
    m_desiredEndEffectorPos = std::vector<double>(s_numJoints, 0);
    m_desiredJointPos = std::vector<double>(s_numJoints, 0);
    m_actualJointPos = std::vector<double>(s_numJoints, 0);
    m_actualEndEffectorPos = std::vector<double>(s_numEndEffectorPos, 0);
    m_fkArmCmdVels = std::vector<double>(s_numJoints, 0);
    m_ikArmCmdVels = std::vector<double>(s_numJoints, 0);
    m_canCmds = std::vector<double>(s_numJoints, 0);
  }

  ~ArmControlInterface() { delete ikControl; }

  void Initialize() {}

  void ArmCmdCallback(arm_interface::ArmCmdConstPtr msg) {
    ROS_INFO("Received Arm Cmd msg");
    for (int i = 0; i < 6; i++) {
      if (m_currentMode == s_modeOpenLoop) {
        m_fkArmCmdVels[i] = msg->fk_arm_cmds[i];
      } else {
        m_ikArmCmdVels[i] = msg->ik_arm_cmds[i];
      }
    }
  }

  void CanCallback(can_msgs::FrameConstPtr msg) {
    // ROS_INFO("Received Can Cmd msg");
    size_t jointId = msg->id - s_jointIdBase;
    m_jointsReady[jointId] = true;
    m_actualJointPos[jointId] = *(float *)(msg->data.data());

    // Check if received initial position for all
    if (!m_isReady) {
      m_isReady = std::all_of(m_jointsReady.cbegin(), m_jointsReady.cend(),
                              [](bool i) { return i == true; });
    }
  }

  bool SetMode(arm_interface::arm_mode::Request &req,
               arm_interface::arm_mode::Response &res) {
    m_currentMode = req.mode;
    res.mode = m_currentMode;
    ROS_INFO("request: req=%d, res=%d", req.mode, res.mode);
    ROS_INFO("mode set to %s", s_modes[m_currentMode].c_str());

    can_msgs::Frame canMsg;
    canMsg.dlc = 1;
    // Publish modes
    for (int i = 0; i < s_numModeIds; i++) {
      canMsg.id = s_modeCanIds[i];
      // *(int *)(canMsg.data.data()) = m_currentMode;
      canMsg.data[0] = m_currentMode;
      m_canPublisher->publish(canMsg);
    }


    for (int i = 0; i < s_numJoints; i++) {
      m_canCmds[0] = 0;
      m_fkArmCmdVels[0] = 0;         // commands
      m_ikArmCmdVels[0] = 0;         // commands

    }
    // Reset positions if mode changed back to ik position
    if (m_currentMode == arm_interface::arm_modeRequest::IK_POS) {
      InitializePositions();
    }

    ConsoleMessage::SendMessage("Switched arm interface to " + s_modes[m_currentMode] + " mode");

    return true;
  }

  void Run() {
    if (!m_isReady) {
      ROS_INFO("Not ready");
      return;
    }

    // if (m_isFirstRun) {
    //   InitializePositions();
    //   m_isFirstRun = false;
    // }

    // Process service requests and run

    switch (m_currentMode) {
    case arm_interface::arm_modeRequest::OPEN_LOOP:
      RunOpenLoop(m_fkArmCmdVels);
      break;
    case arm_interface::arm_modeRequest::IK_VEL:
      RunIkVel(m_ikArmCmdVels);
      break;
    case arm_interface::arm_modeRequest::IK_POS:
      RunIkPos(m_ikArmCmdVels);
      break;
    default:
      break;
    }

    PublishCan();
    PublishAngles();
  }

  void SetPublishers(ros::Publisher *desiredAnglesPublisher,
                     ros::Publisher *desiredPosPublisher,
                     ros::Publisher *actualAnglesPublisher,
                     ros::Publisher *canPublisher) {
    m_desiredAnglesPublisher = desiredAnglesPublisher;
    m_desiredPosPublisher = desiredPosPublisher;
    m_actualAnglesPublisher = actualAnglesPublisher;
    m_canPublisher = canPublisher;
  }

private:
  float m_dT; // time step

  bool m_isReady;
  bool m_isFirstRun;
  ros::Publisher *m_desiredAnglesPublisher;
  ros::Publisher *m_actualAnglesPublisher;
  ros::Publisher *m_desiredPosPublisher;
  ros::Publisher *m_actualPosPublisher;
  ros::Publisher *m_canPublisher;

  Roboarm *ikControl; // ik controls

  std::vector<bool> m_jointsReady; // come back
  // To do: remove angles from positions to remove overlap
  std::vector<double>
      m_desiredEndEffectorPos; // Desired angles/pos  in degrees/cm
                               // 0: end effector x position
                               // 1: end effector y position
                               // 2: end effector theta angle / wrist pitch
  std::vector<double> m_desiredJointPos;      // Desired angles in degrees
                                              // 0: turn table angle
                                              // 1: shoulder angle
                                              // 2: elbow angle
                                              // 3: wrist pitch angle
                                              // 4: wrist Roll angle
                                              // 5: claw position
  std::vector<double> m_actualJointPos;       // Actual angles
  std::vector<double> m_actualEndEffectorPos; // Actual angles
  std::vector<double> m_fkArmCmdVels;         // commands
  std::vector<double> m_ikArmCmdVels;         // commands
  std::vector<double> m_canCmds;              // commands

  const double s_linkLengths[3] = {35.56, 40.64, 30.48};
  const double s_defaultAngles[3] = {45.0 * M_PI / 180, -30.0 * M_PI / 180,
                                     -15.0 * M_PI / 180};

  static const size_t s_jointIdBase = 0x500;
  static const size_t s_numJoints = 6;
  static const size_t s_numEndEffectorPos = 3;

  // Arm joints indexes
  // General
  static const int s_turntableIdx = 0;
  static const int s_shoulderIdx = 1;
  static const int s_elbowIdx = 2;
  static const int s_wristPitchIdx = 3;
  static const int s_wristRollIdx = 4;
  static const int s_clawIdx = 5;

  // Ik mode
  static const int s_endEffectorXIdx = 0;
  static const int s_endEffectorYIdx = 1;
  static const int s_endEffectorThetaIdx = 2;

  static const int s_numModeIds = 5;
  const int s_modeCanIds[5] = {0x300, 0x302, 0x304, 0x400, 0x403};
  const int s_ctrlCanIds[6] = {0x301, 0x303, 0x305, 0x401, 0x402, 0x404};

  // Arm control modes
  static const u_int8_t s_modeOpenLoop = 0x00;
  static const u_int8_t s_modeIkVel = 0x01;
  static const u_int8_t s_modeIkPos = 0x02;
  const std::string s_modes[3] = {"Open loop", "IK Velocity", "IK Position"};

  u_int8_t m_currentMode; // arm mode

  static const int s_dlc = 8; // not sure about this

  void InitializePositions() {
    ROS_INFO("Initializing position");
    double outPos[3];
    double linkAngles[] = {m_actualJointPos[s_shoulderIdx] * M_PI / 180,
                            m_actualJointPos[s_elbowIdx] * M_PI / 180,
                            m_actualJointPos[s_wristPitchIdx] * M_PI / 180};
    CalculateFk(linkAngles, outPos);

    // m_desiredJointPos[s_turntableIdx] = m_actualJointPos[s_turntableIdx];
    // m_desiredJointPos[s_wristRollIdx] = m_actualJointPos[s_wristRollIdx];
    // m_desiredJointPos[s_clawIdx] = m_actualJointPos[s_clawIdx];

    m_desiredEndEffectorPos[s_endEffectorXIdx] = outPos[s_endEffectorXIdx];
    m_desiredEndEffectorPos[s_endEffectorYIdx] = outPos[s_endEffectorYIdx];
    m_desiredEndEffectorPos[s_endEffectorThetaIdx] = outPos[s_endEffectorThetaIdx] * 180 / M_PI;
  }

  void CalculateFk(double linkAngles[3], double outPos[3]) {
    double c1 = cos(linkAngles[0]);
    double s1 = sin(linkAngles[0]);

    double c12 = cos(linkAngles[0] + linkAngles[1]);
    double s12 = sin(linkAngles[0] + linkAngles[1]);

    double c123 = cos(linkAngles[0] + linkAngles[1] + linkAngles[2]);
    double s123 = sin(linkAngles[0] + linkAngles[1] + linkAngles[2]);

    double l1 = s_linkLengths[0];
    double l2 = s_linkLengths[1];
    double l3 = s_linkLengths[2];

    outPos[0] = l1 * c1 + l2 * c12 + l3 * c123;
    outPos[1] = l1 * s1 + l2 * s12 + l3 * s123;
    outPos[2] = linkAngles[0] + linkAngles[1] + linkAngles[2];
  }

  void RunOpenLoop(std::vector<double> fkCmdVels) {
    m_canCmds[s_turntableIdx] = fkCmdVels[s_turntableIdx];
    m_canCmds[s_wristRollIdx] = fkCmdVels[s_wristRollIdx];
    m_canCmds[s_clawIdx] = fkCmdVels[s_clawIdx];
    m_canCmds[s_shoulderIdx] = fkCmdVels[s_shoulderIdx];
    m_canCmds[s_elbowIdx] = fkCmdVels[s_elbowIdx];
    m_canCmds[s_wristPitchIdx] = fkCmdVels[s_wristPitchIdx];
  }

  void RunIkVel(std::vector<double> ikCmdVels) {
    // TODO ik integration
    double endEffectorSpeed[] = {// To do: better naming for indexes
                                 ikCmdVels[s_shoulderIdx],
                                 ikCmdVels[s_elbowIdx],
                                 ikCmdVels[s_wristPitchIdx] * M_PI / 180};
    double currentAngles[] = {m_actualJointPos[s_shoulderIdx] * M_PI / 180,
                              m_actualJointPos[s_elbowIdx] * M_PI / 180,
                              m_actualJointPos[s_wristPitchIdx] * M_PI / 180};
    if (ikControl->calculateVelocities(endEffectorSpeed, currentAngles)) {
      ROS_INFO("ik succeeded");
      for (int i = 0; i < 3; i++) {
        ROS_INFO("joint: %f", ikControl->linkVelocities[i]);
        if (ikControl->linkVelocities[i] > 5.0 * M_PI / 180) {

          double divisor = ikControl->linkVelocities[i] / (5.0 * M_PI / 180);
          ikControl->linkVelocities[0] /= divisor;
          ikControl->linkVelocities[1] /= divisor;
          ikControl->linkVelocities[2] /= divisor;
        }
      }
    } else {
      ROS_INFO("ik failed");
      ConsoleMessage::SendMessage("IK Calculation failed -- switch to PWM mode and flex elbow", ConsoleMessage::WARN);
      ikControl->linkVelocities[0] = ikControl->linkVelocities[1] = ikControl->linkVelocities[2] = 0;
    }

    m_canCmds[s_turntableIdx] = ikCmdVels[s_turntableIdx];
    m_canCmds[s_wristRollIdx] = ikCmdVels[s_wristRollIdx];
    m_canCmds[s_clawIdx] = ikCmdVels[s_clawIdx];
    m_canCmds[s_shoulderIdx] =
        ikControl->linkVelocities[0] * 180 / M_PI;
    m_canCmds[s_elbowIdx] =
        ikControl->linkVelocities[1] * 180 / M_PI;
    m_canCmds[s_wristPitchIdx] =
        ikControl->linkVelocities[2] * 180 / M_PI;
  }

  void RunIkPos(std::vector<double> ikCmdVels) {
    m_desiredEndEffectorPos[s_endEffectorXIdx] +=
        ikCmdVels[s_shoulderIdx] * m_dT;
    m_desiredEndEffectorPos[s_endEffectorYIdx] += ikCmdVels[s_elbowIdx] * m_dT;
    m_desiredEndEffectorPos[s_endEffectorThetaIdx] +=
        ikCmdVels[s_wristPitchIdx] * m_dT;

    double endEffectorPos [] = {
        m_desiredEndEffectorPos[s_endEffectorXIdx],
        m_desiredEndEffectorPos[s_endEffectorYIdx],
        m_desiredEndEffectorPos[s_endEffectorThetaIdx] * M_PI / 180};
    double currentAngles [] = {
        m_actualJointPos[s_shoulderIdx] * M_PI / 180,
        m_actualJointPos[s_elbowIdx] * M_PI / 180,
        m_actualJointPos[s_wristPitchIdx] * M_PI / 180};
    double anglesOut[s_numEndEffectorPos];
    if (ikControl->calculatePositionIK(endEffectorPos, currentAngles,
                                       anglesOut)) {
      ROS_INFO("ik succeeded");
      for (int i = 0; i < s_numEndEffectorPos; i++) {
        ROS_INFO("joint: %f", anglesOut[i]);
      }
    } else {
      ROS_INFO("ik failed");
      ConsoleMessage::SendMessage("IK Calculation failed -- switch to PWM mode and flex elbow", ConsoleMessage::WARN);
      anglesOut[0] = currentAngles[0];
      anglesOut[1] = currentAngles[1];
      anglesOut[2] = currentAngles[2];
    }

    m_desiredJointPos[s_turntableIdx] += ikCmdVels[s_turntableIdx] * m_dT;
    m_desiredJointPos[s_wristRollIdx] += ikCmdVels[s_wristRollIdx] * m_dT;
    m_desiredJointPos[s_clawIdx] += ikCmdVels[s_clawIdx] * m_dT;
    m_desiredJointPos[s_shoulderIdx] = anglesOut[0] * 180 / M_PI;
    m_desiredJointPos[s_elbowIdx] = anglesOut[1] * 180 / M_PI;
    m_desiredJointPos[s_wristPitchIdx] = anglesOut[2] * 180 / M_PI;

    m_canCmds[s_turntableIdx] = m_desiredJointPos[s_turntableIdx];
    m_canCmds[s_wristRollIdx] = m_desiredJointPos[s_wristRollIdx];
    m_canCmds[s_clawIdx] = m_desiredJointPos[s_clawIdx];
    m_canCmds[s_shoulderIdx] = m_desiredJointPos[s_shoulderIdx];
    m_canCmds[s_elbowIdx] = m_desiredJointPos[s_elbowIdx];
    m_canCmds[s_wristPitchIdx] = m_desiredJointPos[s_wristPitchIdx];
  }

  void PublishAngles() {
    //  Calculate actual positions
    double outPos[3];
    double linkAngles[3] = {m_actualJointPos[s_shoulderIdx] * M_PI / 180,
                            m_actualJointPos[s_elbowIdx] * M_PI / 180,
                            m_actualJointPos[s_wristPitchIdx] * M_PI / 180};
    CalculateFk(linkAngles, outPos);

    // Report angles and positions
    std_msgs::Float64MultiArray msg;
    msg.layout.dim.push_back(std_msgs::MultiArrayDimension());
    msg.layout.dim[0].size = 6;
    msg.layout.dim[0].stride = 1;
    msg.layout.dim[0].label =
        "angles"; // or whatever name you typically use to index vec1
    msg.data.clear();
    msg.data.insert(msg.data.end(), m_desiredJointPos.begin(),
                    m_desiredJointPos.end());
    m_desiredAnglesPublisher->publish(msg);

    msg.data.clear();
    msg.data.insert(msg.data.end(), m_actualJointPos.begin(),
                    m_actualJointPos.end());
    m_actualAnglesPublisher->publish(msg);


    // Report angles and positions
    // std_msgs::Float64MultiArray msg;
    // msg.layout.dim.push_back(std_msgs::MultiArrayDimension());
    msg.layout.dim[0].size = 3;
    msg.layout.dim[0].stride = 1;
    msg.layout.dim[0].label =
        "angles"; // or whatever name you typically use to index vec1
    msg.data.clear();
    msg.data.insert(msg.data.end(), m_desiredEndEffectorPos.begin(),
                    m_desiredEndEffectorPos.end());
    m_desiredPosPublisher->publish(msg);
  }

  void PublishCan() {
    can_msgs::Frame canMsg;
    canMsg.dlc = s_dlc;

    for (int i = 0; i < s_numJoints; i++) {
      canMsg.id = s_ctrlCanIds[i];
      *(float *)(canMsg.data.data()) = m_canCmds[i];
      m_canPublisher->publish(canMsg);
    }
  }
};
