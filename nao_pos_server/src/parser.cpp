// Copyright 2024 Antonio Bono
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

#include "parser.hpp"

#include <cmath>
#include <iostream>
#include <iterator>
#include <sstream>
#include <vector>

#include "indexes.hpp"
#include "nao_lola_command_msgs/msg/joint_indexes.hpp"
#include "rclcpp/logging.hpp"

// using namespace std;

// +2 because there is the "!" at the start, and the duration at the end
#define POSITIONS_SIZE (nao_lola_command_msgs::msg::JointIndexes::NUMJOINTS + 2)
// +1 because there is the "$" at the start
#define STIFFNESSES_SIZE (nao_lola_command_msgs::msg::JointIndexes::NUMJOINTS + 1)

/*const auto stiffnessMax = nao_lola_command_msgs::msg::JointStiffnesses()
  .set__indexes(indexes::indexes)
  .set__stiffnesses({1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1});
*/

namespace parser
{

std::string vec2str(const std::vector<uint8_t> & vec)
{
  std::stringstream ss;
  ss << "[ ";
  int tmp = 0;
  for (const uint8_t & elem : vec) {
    tmp = elem;
    ss << tmp << " ";
  }
  ss << "]";
  return ss.str();
}

static rclcpp::Logger logger = rclcpp::get_logger("parser");

// Forward declaration
std::vector<std::string> split(const std::string & line);

ParseResult parse(const std::vector<std::string> & in)
{
  ParseResult parseResult;

  unsigned keyFrameTime = 0;
  auto jointStiffnesses = nao_lola_command_msgs::msg::JointStiffnesses();
  bool customStiffnesses = false;
  bool firstPosLine = true;

  nao_lola_command_msgs::msg::JointPositions prevJointPositions;

  for (const auto & line : in) {
    if (line.front() == '$') {
      RCLCPP_DEBUG_STREAM(logger, "Stiffness: " << line);
      auto splitted_line = split(line);

      // Check size
      if (splitted_line.size() != STIFFNESSES_SIZE) {
        RCLCPP_ERROR_STREAM(
          logger, "pos file line has " << splitted_line.size() << " elements, but expected "
                                       << STIFFNESSES_SIZE);
        parseResult.successful = false;
        return parseResult;
      }

      customStiffnesses = true;

      for (unsigned int i = 1; i < nao_lola_command_msgs::msg::JointIndexes::NUMJOINTS + 1; ++i) {
        std::string stiffness_string = splitted_line.at(i);

        if (stiffness_string != "-") {
          try {
            float stiffness_float = std::stof(stiffness_string);
            jointStiffnesses.indexes.push_back(i - 1);
            jointStiffnesses.stiffnesses.push_back(stiffness_float);
          } catch (std::invalid_argument &) {
            RCLCPP_ERROR(
              logger, ("stiffness value '" + stiffness_string +
                       "' is not a valid stiffness value (cannot be converted to float)")
                        .c_str());
            parseResult.successful = false;
            return parseResult;
          }
        }
      }

    } else if (line.front() == '!') {
      RCLCPP_DEBUG_STREAM(logger, "Position: " << line);
      auto splitted_line = split(line);

      // Check size
      if (splitted_line.size() != POSITIONS_SIZE) {
        RCLCPP_ERROR_STREAM(
          logger, "pos file line has " << splitted_line.size() << " elements, but expected "
                                       << POSITIONS_SIZE);
        parseResult.successful = false;
        return parseResult;
      }

      // Convert to data type. Pos files specify angles in degrees while nao_lola uses radians
      nao_lola_command_msgs::msg::JointPositions jointPositions;

      for (unsigned int i = 1; i < nao_lola_command_msgs::msg::JointIndexes::NUMJOINTS + 1; ++i) {
        std::string position_deg_string = splitted_line.at(i);

        if (position_deg_string != "-") {
          try {
            float position_deg = std::stof(position_deg_string);
            float position_rad = position_deg * M_PI / 180;
            jointPositions.indexes.push_back(i - 1);
            jointPositions.positions.push_back(position_rad);
            if (!customStiffnesses) {
              jointStiffnesses.indexes.push_back(i - 1);
              jointStiffnesses.stiffnesses.push_back(1.0);
            }
          } catch (std::invalid_argument &) {
            RCLCPP_ERROR(
              logger, ("joint value '" + position_deg_string +
                       "' is not a valid joint value (cannot be converted to float)")
                        .c_str());
            parseResult.successful = false;
            return parseResult;
          }
        }
      }

      if (firstPosLine) {
        firstPosLine = false;
      } else {
        if (jointPositions.indexes != prevJointPositions.indexes) {
          RCLCPP_ERROR_STREAM(logger, "two or more joint positions vectors are not the same!");
          parseResult.successful = false;
          return parseResult;
        }
      }

      // add the duration of the Keyframe
      std::string duration_string = splitted_line.back();
      try {
        int duration = std::stoi(duration_string);
        keyFrameTime += duration;
      } catch (std::invalid_argument &) {
        RCLCPP_ERROR(
          logger, ("duration '" + duration_string +
                   "' is not a valid duration value (cannot be converted to int)")
                    .c_str());
        parseResult.successful = false;
        return parseResult;
      }

      if (customStiffnesses) {
        if (jointPositions.indexes.size() != jointStiffnesses.indexes.size()) {
          RCLCPP_ERROR(logger, "joint positions and joint stiffness vectors have different sizes!");
          parseResult.successful = false;
          return parseResult;
        }
        for (unsigned i = 0; i < jointPositions.indexes.size(); i++) {
          if (jointPositions.indexes.at(i) != jointStiffnesses.indexes.at(i)) {
            RCLCPP_ERROR(logger, "joint positions and joint stiffness indexes are not the same!");
            parseResult.successful = false;
            return parseResult;
          }
        }
      }

      parseResult.keyFrames.push_back(KeyFrame{keyFrameTime, jointPositions, jointStiffnesses});
      RCLCPP_INFO_STREAM(logger, "jointPositions indexes: " << vec2str(jointPositions.indexes));
      RCLCPP_INFO_STREAM(logger, "jointPositions size: " << jointPositions.indexes.size());
      RCLCPP_INFO_STREAM(logger, "jointStiffnesses indexes: " << vec2str(jointStiffnesses.indexes));
      RCLCPP_INFO_STREAM(logger, "jointStiffnesses size: " << jointStiffnesses.indexes.size());

      prevJointPositions = jointPositions;  // Using assignment operator to copy one vector to other

      jointStiffnesses.stiffnesses.clear();
      jointStiffnesses.indexes.clear();
      customStiffnesses = false;

    } else {
      RCLCPP_DEBUG_STREAM(logger, "Ignoring: " << line);
    }
  }  // for each line of in

  parseResult.successful = true;
  return parseResult;
}

std::vector<std::string> split(const std::string & line)
{
  std::istringstream ss(line);
  return std::vector<std::string>{
    std::istream_iterator<std::string>{ss}, std::istream_iterator<std::string>()};
}

}  // namespace parser
