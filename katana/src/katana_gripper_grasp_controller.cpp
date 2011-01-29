/*
 * UOS-ROS packages - Robot Operating System code by the University of Osnabrück
 * Copyright (C) 2010  University of Osnabrück
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * katana_gripper_grasp_controller.cpp
 *
 *  Created on: 29.01.2011
 *      Author: Martin Günther <mguenthe@uos.de>
 *
 * based on pr2_gripper_grasp_controller
 *
 */

#include <katana/katana_gripper_grasp_controller.h>

namespace katana
{

KatanaGripperGraspController::KatanaGripperGraspController(boost::shared_ptr<AbstractKatana> katana) :
      katana_(katana), last_command_was_close_(false)
{
  ros::NodeHandle root_nh("");
  ros::NodeHandle pn("~");

  pn.param<double> ("gripper_object_presence_threshold", gripper_object_presence_threshold_,
                    DEFAULT_GRIPPER_OBJECT_PRESENCE_THRESHOLD);

  std::string query_service_name = root_nh.resolveName("grasp_query_name");
  query_srv_ = root_nh.advertiseService(query_service_name, &KatanaGripperGraspController::serviceCallback, this);
  ROS_INFO_STREAM("katana gripper grasp query service started on topic " << query_service_name);

  std::string posture_action_name = root_nh.resolveName("posture_action_name");
  action_server_ = new actionlib::SimpleActionServer<object_manipulation_msgs::GraspHandPostureExecutionAction>(root_nh,
      posture_action_name, boost::bind(&KatanaGripperGraspController::executeCB, this, _1));
  ROS_INFO_STREAM("katana gripper grasp hand posture action server started on topic " << posture_action_name);
}

KatanaGripperGraspController::~KatanaGripperGraspController()
{
  delete action_server_;
}

void KatanaGripperGraspController::executeCB(const object_manipulation_msgs::GraspHandPostureExecutionGoalConstPtr &goal)
{
  bool move_gripper_success;
  switch (goal->goal)
  {
    case object_manipulation_msgs::GraspHandPostureExecutionGoal::GRASP:
      if (goal->grasp.grasp_posture.position.empty())
      {
        ROS_ERROR("katana gripper grasp execution: position vector empty in requested grasp");
        action_server_->setAborted();
        return;
      }

      // well, we don't really use the grasp_posture.position value here, we just instruct
      // the gripper to close all the way...
      // that might change in the future and we might do something more interesting
      move_gripper_success = katana_->moveGripper(GRIPPER_CLOSED_ANGLE);
      last_command_was_close_ = true;
      break;

    case object_manipulation_msgs::GraspHandPostureExecutionGoal::PRE_GRASP:
      if (goal->grasp.pre_grasp_posture.position.empty())
      {
        ROS_ERROR("katana gripper grasp execution: position vector empty in requested pre_grasp");
        action_server_->setAborted();
        return;
      }

      // well, we don't really use the grasp_posture.position value here, we just instruct
      // the gripper to open  all the way...
      // that might change in the future and we might do something more interesting
      move_gripper_success = katana_->moveGripper(GRIPPER_OPEN_ANGLE);
      last_command_was_close_ = false;
      break;

    case object_manipulation_msgs::GraspHandPostureExecutionGoal::RELEASE:
      move_gripper_success = katana_->moveGripper(GRIPPER_OPEN_ANGLE);
      last_command_was_close_ = false;
      break;

    default:
      ROS_ERROR("katana gripper grasp execution: unknown goal code (%d)", goal->goal);
      action_server_->setAborted();
      return;
  }

  // process the result
  if (move_gripper_success)
  {
    ROS_INFO("Gripper goal achieved");
    action_server_->setSucceeded();
  }
  else
  {
    if (goal->goal == object_manipulation_msgs::GraspHandPostureExecutionGoal::GRASP)
    {
      // this is probably correct behavior, since there is an object in the gripper
      // we can not expect the gripper to fully close
      action_server_->setSucceeded();
    }
    else
    {
      ROS_WARN("Gripper goal not achieved for pre-grasp or release");
      // this might become a failure later, for now we're still investigating
      action_server_->setSucceeded();
    }
  }
}

bool KatanaGripperGraspController::serviceCallback(object_manipulation_msgs::GraspStatus::Request &request,
                                                   object_manipulation_msgs::GraspStatus::Response &response)
{
  if (!last_command_was_close_)
  {
    ROS_INFO("Gripper grasp query false: the last gripper command was not 'close' (= not holding any object)");
    response.is_hand_occupied = false;
    return true;
  }

  double gripperAngle = katana_->getMotorAngles()[GRIPPER_INDEX];
  if (gripperAngle < gripper_object_presence_threshold_)
  {
    ROS_INFO("Gripper grasp query false: gripper angle %f below threshold %f (= not holding any object)",
        gripperAngle, gripper_object_presence_threshold_);
    response.is_hand_occupied = false;
  }
  else
  {
    ROS_INFO("Gripper grasp query true: gripper angle %f above threshold %f (= holding an object)",
        gripperAngle, gripper_object_presence_threshold_);
    response.is_hand_occupied = true;
  }
  return true;
}

}