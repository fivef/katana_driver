/*
 * Copyright (c) 2009, Willow Garage, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Willow Garage, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived from
 *       this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

// Author: Stuart Glaser

#include "katana/gripper_controller.h"

namespace katana
{

 GripperAction::GripperAction(boost::shared_ptr<AbstractKatana> katana) :
    node_(ros::NodeHandle()),
    katana_(katana),
    action_server_(node_, "gripper_action_controller/gripper_command",
                   boost::bind(&GripperAction::goalCB, this, _1),
                   boost::bind(&GripperAction::cancelCB, this, _1), true),
    has_active_goal_(false)
  {
    ros::NodeHandle pn("~");

    pn.param("goal_threshold", goal_threshold_, 0.01);
    pn.param("stall_velocity_threshold", stall_velocity_threshold_, 1e-6);
    pn.param("stall_timeout", stall_timeout_, 0.1);

    pub_controller_command_ =
      node_.advertise<control_msgs::GripperCommand>("gripper_action_controller/control_command", 1);
    sub_controller_state_ =
      node_.subscribe("state", 1, &GripperAction::controlStateCB, this);

    watchdog_timer_ = node_.createTimer(ros::Duration(1.0), &GripperAction::watchdog, this);

    ROS_INFO("Gripper Controller started");
  }

 GripperAction::~GripperAction()
  {
    pub_controller_command_.shutdown();
    sub_controller_state_.shutdown();
    watchdog_timer_.stop();
  }


  void GripperAction::watchdog(const ros::TimerEvent &e)
  {

	  /*
	  ros::Time now = ros::Time::now();

    // Aborts the active goal if the controller does not appear to be active.
    if (has_active_goal_)
    {
      bool should_abort = false;
      if (!last_controller_state_)
      {
        should_abort = true;
        ROS_WARN("Aborting goal because we have never heard a controller state message.");
      }
      else if ((now - last_controller_state_->header.stamp) > ros::Duration(5.0))
      {
        should_abort = true;
        ROS_WARN("Aborting goal because we haven't heard from the controller in %.3lf seconds",
                 (now - last_controller_state_->header.stamp).toSec());
      }

      if (should_abort)
      {
        // Marks the current goal as aborted.
        active_goal_.setAborted();
        has_active_goal_ = false;
      }
    }
    */
  }

  void GripperAction::goalCB(GoalHandle gh)
  {
    // Cancels the currently active goal.
    if (has_active_goal_)
    {
      // Marks the current goal as canceled.
      active_goal_.setCanceled();
      has_active_goal_ = false;
    }

    gh.setAccepted();
    active_goal_ = gh;
    has_active_goal_ = true;
    goal_received_ = ros::Time::now();
    min_error_seen_ = 1e10;

    // Sends the command along to the controller.
    pub_controller_command_.publish(active_goal_.getGoal()->command);


    ROS_INFO("Move Joint");
    if(!katana_->moveJoint(GRIPPER_INDEX, active_goal_.getGoal()->command.position)){
    	active_goal_.setCanceled();
    	ROS_ERROR("Cancelling goal: moveJoint didn't work.");
    }


    // wait for gripper to open/close
     ros::Duration(GRIPPER_OPENING_CLOSING_DURATION).sleep();



     control_msgs::GripperCommandResult result;
         result.position = active_goal_.getGoal()->command.position;
         result.effort = active_goal_.getGoal()->command.max_effort;
         result.reached_goal = true;
         result.stalled = false;


         active_goal_.setSucceeded(result);
         has_active_goal_ = false;


         control_msgs::GripperCommandFeedback feedback;
          feedback.position = active_goal_.getGoal()->command.position;
          feedback.effort = active_goal_.getGoal()->command.max_effort;
          feedback.reached_goal = true;
          feedback.stalled = false;

     active_goal_.publishFeedback(feedback);

     ROS_INFO("Gripper goal achieved");


    last_movement_time_ = ros::Time::now();
  }

  void GripperAction::cancelCB(GoalHandle gh)
  {
    if (active_goal_ == gh)
    {
      // Stops the controller.
      if (last_controller_state_)
      {
        control_msgs::GripperCommand stop;
        stop.position = last_controller_state_->process_value;
        stop.max_effort = 0.0;
        pub_controller_command_.publish(stop);
      }

      // Marks the current goal as canceled.
      active_goal_.setCanceled();
      has_active_goal_ = false;
    }
  }


  void GripperAction::controlStateCB(const control_msgs::JointControllerStateConstPtr &msg)
  {
    last_controller_state_ = msg;
    ros::Time now = ros::Time::now();

    if (!has_active_goal_)
      return;

    // Ensures that the controller is tracking my setpoint.
    if (fabs(msg->set_point - active_goal_.getGoal()->command.position) > goal_threshold_)
    {
      if (now - goal_received_ < ros::Duration(1.0))
      {
        return;
      }
      else
      {
        ROS_ERROR("Cancelling goal: Controller is trying to achieve a different setpoint.");
        active_goal_.setCanceled();
        has_active_goal_ = false;
      }
    }


    control_msgs::GripperCommandFeedback feedback;
    feedback.position = msg->process_value;
    feedback.effort = msg->command;
    feedback.reached_goal = false;
    feedback.stalled = false;

    control_msgs::GripperCommandResult result;
    result.position = msg->process_value;
    result.effort = msg->command;
    result.reached_goal = false;
    result.stalled = false;

    if (fabs(msg->process_value - active_goal_.getGoal()->command.position) < goal_threshold_)
    {
      feedback.reached_goal = true;

      result.reached_goal = true;
      active_goal_.setSucceeded(result);
      has_active_goal_ = false;
    }
    else
    {
      // Determines if the gripper has stalled.
      if (fabs(msg->process_value_dot) > stall_velocity_threshold_)
      {
        last_movement_time_ = ros::Time::now();
      }
      else if ((ros::Time::now() - last_movement_time_).toSec() > stall_timeout_ &&
               active_goal_.getGoal()->command.max_effort != 0.0)
      {
        feedback.stalled = true;

        result.stalled = true;
        active_goal_.setAborted(result);
        has_active_goal_ = false;
      }
    }
    active_goal_.publishFeedback(feedback);
  }

}
