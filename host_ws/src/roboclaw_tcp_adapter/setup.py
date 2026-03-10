import os
from glob import glob
from setuptools import setup, find_packages

package_name = "roboclaw_tcp_adapter"

setup(
    name=package_name,
    version="0.1.0",
    packages=find_packages(exclude=["test"]),
    data_files=[
        ("share/ament_index/resource_index/packages", ["resource/" + package_name]),
        ("share/" + package_name, ["package.xml"]),
        (os.path.join("share", package_name, "config"), glob("config/*.yaml")),
        (os.path.join("share", package_name, "launch"), glob("launch/*.py")),
    ],
    install_requires=["setuptools"],
    zip_safe=True,
    maintainer="Eduard Sik",
    maintainer_email="eduard@nowlab.eu",
    description="TCP socket adapter for Basicmicro controllers via USR-K6",
    license="MIT",
    entry_points={
        "console_scripts": [
            "roboclaw_tcp_node = roboclaw_tcp_adapter.roboclaw_tcp_node:main",
            "safety_bridge_node = roboclaw_tcp_adapter.safety_bridge_node:main",
            "rc_teleop_node = roboclaw_tcp_adapter.rc_teleop_node:main",
        ],
    },
)
