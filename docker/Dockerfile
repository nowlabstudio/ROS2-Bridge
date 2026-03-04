FROM zephyrprojectrtos/ci:v0.28.8

RUN pip3 install --no-cache-dir \
    rosdep \
    catkin_pkg \
    lark \
    empy \
    colcon-common-extensions

WORKDIR /workdir

CMD ["/bin/bash"]
