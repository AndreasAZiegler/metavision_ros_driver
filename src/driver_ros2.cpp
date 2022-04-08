// -*-c++-*---------------------------------------------------------------------------------------
// Copyright 2021 Bernd Pfrommer <bernd.pfrommer@gmail.com>
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "metavision_ros_driver/driver_ros2.h"

#include <functional>
#include <iostream>
#include <rclcpp/parameter_events_filter.hpp>
#include <rclcpp_components/register_node_macro.hpp>
#include <sstream>
#include <vector>

#include "metavision_ros_driver/logging.h"
#include "metavision_ros_driver/metavision_wrapper.h"

namespace metavision_ros_driver
{
DriverROS2::DriverROS2(const rclcpp::NodeOptions & options)
: Node(
    "metavision_ros_driver",
    rclcpp::NodeOptions(options).automatically_declare_parameters_from_overrides(true))
{
  wrapper_ = std::make_shared<MetavisionWrapper>(get_name());
  std::string sn;
  this->get_parameter_or("serial", sn, std::string(""));
  wrapper_->setSerialNumber(sn);
  this->get_parameter_or("camerainfo_url", cameraInfoURL_, std::string(""));
  this->get_parameter_or("frame_id", frameId_, std::string(""));
  std::string syncMode;
  this->get_parameter_or("sync_mode", syncMode, std::string("standalone"));
  LOG_INFO("sync mode: " << syncMode);
  wrapper_->setSyncMode(syncMode);
  std::vector<long> roi_long;
  this->get_parameter_or("roi", roi_long, std::vector<long>());
  std::vector<int> r(roi_long.begin(), roi_long.end());
  if (!r.empty()) {
    LOG_INFO("using ROI with " << (r.size() / 4) << " rectangle(s)");
    for (size_t i = 0; i < r.size() / 4; i += 4) {
      LOG_INFO(r[4 * i] << " " << r[4 * i + 1] << " " << r[4 * i + 2] << " " << r[4 * i + 3]);
    }
  }
  wrapper_->setROI(r);
  if (syncMode == "primary") {  // defer starting until secondary is up
    const std::string topic = "~/ready";
    LOG_INFO("waiting for secondary to publish ready message on topic \"" << topic << "\"");
    auto qos = rclcpp::QoS(rclcpp::KeepLast(1)).best_effort().durability_volatile();
    secondaryReadySub_ = this->create_subscription<std_msgs::msg::Header>(
      topic, qos, std::bind(&DriverROS2::secondaryReady, this, std::placeholders::_1));
  } else {
    if (!start()) {
      LOG_ERROR("startup failed!");
      throw std::runtime_error("startup of DriverROS2 node failed!");
    }
  }
}

DriverROS2::~DriverROS2()
{
  stop();
  wrapper_.reset();  // invoke destructor
}

void DriverROS2::initializeBiasParameters()
{
  addBiasParameter("bias_diff", 0, 1800, "reference level for diff_off and diff_on");
  addBiasParameter("bias_diff_off", 0, 298, "off threshold level");
  addBiasParameter("bias_diff_on", 300, 1800, "on threshold level");
  addBiasParameter("bias_fo", 0, 1800, "source follower low pass filter");
  addBiasParameter("bias_hpf", 0, 1800, "differentiator high pass filter");
  addBiasParameter("bias_pr", 0, 1800, "photoreceptor (frontend) bias");
  addBiasParameter("bias_refr", 0, 1800, "refractory time bias");
}

void DriverROS2::addBiasParameter(
  const std::string & name, int min_val, int max_val, const std::string & desc)
{
  rcl_interfaces::msg::IntegerRange rg;
  rg.from_value = min_val;
  rg.to_value = max_val;
  rg.step = 1;
  rcl_interfaces::msg::ParameterDescriptor pd;
  pd.name = name;
  pd.type = rclcpp::ParameterType::PARAMETER_INTEGER;
  pd.integer_range.push_back(rg);
  pd.description = desc;
  biasParameters_.insert(ParameterMap::value_type(name, pd));
}

void DriverROS2::declareBiasParameters()
{
  initializeBiasParameters();
  for (const auto & p : biasParameters_) {
    const auto & name = p.first;
    try {
      const int defBias = wrapper_->getBias(name);
      try {
        this->declare_parameter(name, rclcpp::ParameterValue(defBias), p.second, false);
        LOG_INFO_FMT("%-20s value: %4d", name.c_str(), defBias);
      } catch (rclcpp::exceptions::InvalidParameterTypeException & e) {
        LOG_WARN("cannot declare parameter " << name << ": " << e.what());
      } catch (const std::exception & e) {
        LOG_WARN("error thrown " << e.what());
      }
    } catch (const std::runtime_error & e) {
      LOG_WARN("cannot get default bias for " << name << ", skipping it!");
    }
  }
}

bool DriverROS2::stop()
{
  if (wrapper_) {
    return (wrapper_->stop());
  }
  return false;
}

rcl_interfaces::msg::SetParametersResult DriverROS2::parameterChanged(
  const std::vector<rclcpp::Parameter> & params)
{
  rcl_interfaces::msg::SetParametersResult res;
  res.successful = false;
  res.reason = "not set";
  for (const auto & p : params) {
    const auto it = biasParameters_.find(p.get_name());
    if (it != biasParameters_.end()) {
      if (wrapper_) {
        // TODO (Bernd): check value if possible and reject if out of bounds
        res.successful = true;
        res.reason = "successfully set";
      }
    } else {
      res.successful = true;
      res.reason = "ignored unknown bias";
    }
  }
  return (res);
}

void DriverROS2::onParameterEvent(rcl_interfaces::msg::ParameterEvent::SharedPtr event)
{
  if (event->node != this->get_fully_qualified_name()) {
    return;
  }
  std::vector<std::string> validEvents;
  for (auto it = biasParameters_.begin(); it != biasParameters_.end(); ++it) {
    validEvents.push_back(it->first);
  }
  rclcpp::ParameterEventsFilter filter(
    event, validEvents, {rclcpp::ParameterEventsFilter::EventType::CHANGED});
  for (auto & it : filter.get_events()) {
    const std::string & name = it.second->name;
    const auto bp_it = biasParameters_.find(name);
    if (bp_it != biasParameters_.end()) {
      if (wrapper_) {
        // apply bias to SDK. The driver may adjust the parameter value!
        const int oldVal = wrapper_->getBias(name);
        const int val = it.second->value.integer_value;
        if (oldVal != val) {
          const int newVal = wrapper_->setBias(name, val);
          if (val != newVal) {
            // communicate adjusted value to ROS world
            this->set_parameter(rclcpp::Parameter(name, newVal));
          }
        }
      }
    }
  }
}

std::vector<std::string> split_string(const std::string & s)
{
  std::stringstream ss(s);
  std::string tmp;
  std::vector<std::string> words;
  while (getline(ss, tmp, '.')) {
    words.push_back(tmp);
  }
  return (words);
}

MetavisionWrapper::HardwarePinConfig DriverROS2::getHardwarePinConfig() const
{
  MetavisionWrapper::HardwarePinConfig config;
  // builds map, e.g:
  // config["evc3a_plugin_gen31"]["external"] = 0
  // config["evc3a_plugin_gen31"]["loopback"] = 6
  const auto params = this->list_parameters({"prophesee_pin_config"}, 10 /* 10 deep */);
  for (const auto name : params.names) {
    auto a = split_string(name);
    if (a.size() != 3) {
      LOG_ERROR("invalid pin config found: " << name);
    } else {
      long pin;
      this->get_parameter(name, pin);
      auto it_bool = config.insert({a[1], std::map<std::string, int>()});
      it_bool.first->second.insert({a[2], pin});
    }
  }
  return (config);
}

bool DriverROS2::start()
{
  std::string tInMode;
  this->get_parameter_or("trigger_in_mode", tInMode, std::string("disabled"));
  bool useMT;
  this->get_parameter_or("use_multithreading", useMT, true);
  double printInterval;
  this->get_parameter_or("statistics_print_interval", printInterval, 1.0);
  std::string biasFile;
  this->get_parameter_or("bias_file", biasFile, std::string(""));
  MetavisionWrapper::HardwarePinConfig pinConfig = getHardwarePinConfig();
  wrapper_->setHardwarePinConfig(pinConfig);
  std::string tOutMode;
  this->get_parameter_or("trigger_out_mode", tOutMode, std::string("disabled"));
  long tOutPeriod;
  this->get_parameter_or("trigger_out_period", tOutPeriod, 100000L);
  double tOutCycle;
  this->get_parameter_or("trigger_duty_cycle", tOutCycle, 0.5);
  wrapper_->setExternalTriggerInMode(tInMode);
  wrapper_->setExternalTriggerOutMode(tOutMode, tOutPeriod, tOutCycle);
  if (tOutMode != "disabled") {
    LOG_INFO("trigger out mode:       " << tOutMode);
    LOG_INFO("trigger out period:     " << tOutPeriod);
    LOG_INFO("trigger out duty cycle: " << tOutCycle);
  }
  if (tInMode != "disabled") {
    LOG_INFO("trigger in mode:        " << tInMode);
  }

  // must wait with initialize() until all trigger params have been set
  if (!wrapper_->initialize(useMT, printInterval, biasFile)) {
    LOG_ERROR("driver initialization failed!");
    return (false);
  }

  if (frameId_.empty()) {
    // default frame id to last 4 digits of serial number
    const auto sn = wrapper_->getSerialNumber();
    frameId_ = sn.substr(sn.size() - 4);
  }
  LOG_INFO("using frame id: " << frameId_);

  std::string msgType;
  this->get_parameter_or("message_type", msgType, std::string("event_array"));
  const bool pubTrig = tInMode != "disabled";
  if (msgType == "prophesee") {
    prophPub_.reset(new PropheseePublisher(this, wrapper_, frameId_, pubTrig));
    wrapper_->startCamera(prophPub_.get());
  } else if (msgType == "dvs") {
    dvsPub_.reset(new DVSPublisher(this, wrapper_, frameId_, pubTrig));
    wrapper_->startCamera(dvsPub_.get());
  } else if (msgType == "event_array") {
    eventArrayPub_.reset(new EventArrayPublisher(this, wrapper_, frameId_, pubTrig));
    wrapper_->startCamera(eventArrayPub_.get());
  } else {
    LOG_ERROR("invalid msg type: " << msgType);
    throw(std::runtime_error("invalid message type!"));
  }
  LOG_INFO("started driver with msg type: " << msgType);

  infoManager_ =
    std::make_shared<camera_info_manager::CameraInfoManager>(this, get_name(), cameraInfoURL_);
  cameraInfoMsg_ = infoManager_->getCameraInfo();
  cameraInfoMsg_.header.frame_id = frameId_;
  declareBiasParameters();
  callbackHandle_ = this->add_on_set_parameters_callback(
    std::bind(&DriverROS2::parameterChanged, this, std::placeholders::_1));
  parameterSubscription_ = rclcpp::AsyncParametersClient::on_parameter_event(
    this->get_node_topics_interface(),
    std::bind(&DriverROS2::onParameterEvent, this, std::placeholders::_1));

  saveBiasesService_ = this->create_service<std_srvs::srv::Trigger>(
    "save_biases",
    std::bind(&DriverROS2::saveBiases, this, std::placeholders::_1, std::placeholders::_2));
  return (true);
}

void DriverROS2::saveBiases(
  const std::shared_ptr<std_srvs::srv::Trigger::Request> request,
  const std::shared_ptr<std_srvs::srv::Trigger::Response> response)
{
  (void)request;
  response->success = false;
  response->message = "bias file write ";
  if (wrapper_) {
    response->success = wrapper_->saveBiases();
  }
  response->message += (response->success ? "succeeded" : "failed");
}

void DriverROS2::secondaryReady(std_msgs::msg::Header::ConstSharedPtr msg)
{
  (void)msg;
  if (secondaryReadySub_) {
    secondaryReadySub_.reset();  // unsubscribe
  }
  LOG_INFO("secondary is ready, starting primary!");
  if (!start()) {
    LOG_ERROR("startup failed!");
    throw std::runtime_error("startup of DriverROS2 node failed!");
  }
}

}  // namespace metavision_ros_driver

RCLCPP_COMPONENTS_REGISTER_NODE(metavision_ros_driver::DriverROS2)
