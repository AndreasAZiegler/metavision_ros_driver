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
RUN apt-get install -y ros-foxy-ament-cmake-clang-format ros-foxy-image-common ros-foxy-cv-bridge ros-foxy-cv-bridge-dbgsym python3-wstool

# Get dependencies for frankaemika 
RUN apt-get update -y && apt-get install -y --allow-unauthenticated \
    clang-6.0 \
    clang-format-6.0 \
    clang-tidy-6.0 \
    python3-pip \
    libpoco-dev \
    libeigen3-dev \
    ros-foxy-control-msgs \
    ros-foxy-xacro \
    ros-foxy-ament-cmake-clang-format \
    ros-foxy-ament-clang-format \
    ros-foxy-ament-flake8 \
    ros-foxy-ament-cmake-clang-tidy \
    ros-foxy-angles \
    ros-foxy-ros2-control \
    ros-foxy-realtime-tools \
    ros-foxy-control-toolbox \
    && rm -rf /var/lib/apt/lists/*

RUN python3 -m pip install -U \
    argcomplete \
    flake8-blind-except \
    flake8-builtins \
    flake8-class-newline \
    flake8-comprehensions \
    flake8-deprecated \
    flake8-docstrings \
    flake8-import-order \
    flake8-quotes \
    pytest-repeat \
    pytest-rerunfailures \
    pytest

# Install frankaemika 
RUN mkdir ~/source_code
RUN cd ~/source_code && git clone https://github.com/frankaemika/libfranka.git \
    && cd libfranka \
    && git submodule init \
    && git submodule update \
    && mkdir build && cd build \
    && cmake -DBUILD_EXAMPLES=OFF -DBUILD_TESTS=OFF  .. \
    && make franka -j$(nproc) \
    && make install

# Add utility scripts
RUN apt-get update -y && apt-get install -y jq
ADD merge-compile-commands /usr/local/bin/merge-compile-commands
RUN chmod +x /usr/local/bin/merge-compile-commands

ADD catkin-update-compile-commands /usr/local/bin/catkin-update-compile-commands
RUN chmod +x /usr/local/bin/catkin-update-compile-commands
