FROM ros:foxy

# Get dependencies
RUN apt-get update && apt-get install -y python3-vcstool python3-catkin-tools python3-osrf-pycommon git gdb cmake-curses-gui

# Install metavision
#RUN apt-get install -y libcanberra-gtk-module mesa-utils
#RUN apt-get install -y python3-pip python3-tk
#RUN apt-get install -y python3.8-dev  # where X is 6, 7 or 8 depending on your Python version (3.6, 3.7 or 3.8)
#RUN python3 -m pip install pip --upgrade
#RUN python3 -m pip install "numpy==1.19.5" "opencv-python>=4.2.0.34" matplotlib jupyter pandas

ADD essentials.list /etc/apt/sources.list.d/essentials.list
RUN apt-get update && apt-get install -y metavision-essentials

# Add ROS2 dependencies
RUN apt-get install -y ros-foxy-ament-cmake-clang-format ros-foxy-image-common

# Add utility scripts
RUN apt -y install jq
ADD merge-compile-commands /usr/local/bin/merge-compile-commands
RUN chmod +x /usr/local/bin/merge-compile-commands

ADD catkin-update-compile-commands /usr/local/bin/catkin-update-compile-commands
RUN chmod +x /usr/local/bin/catkin-update-compile-commands
