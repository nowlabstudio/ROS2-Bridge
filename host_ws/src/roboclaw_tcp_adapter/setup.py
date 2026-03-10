from setuptools import setup, find_packages

package_name = "roboclaw_tcp_adapter"

setup(
    name=package_name,
    version="0.2.0",
    packages=find_packages(exclude=["test"]),
    data_files=[
        ("share/ament_index/resource_index/packages", ["resource/" + package_name]),
        ("share/" + package_name, ["package.xml"]),
    ],
    install_requires=["setuptools"],
    zip_safe=True,
    maintainer="Eduard Sik",
    maintainer_email="eduard@nowlab.eu",
    description="RC teleop node for RoboClaw diff-drive robot (tank/arcade mixing + mode switch)",
    license="MIT",
    entry_points={
        "console_scripts": [
            "rc_teleop_node = roboclaw_tcp_adapter.rc_teleop_node:main",
        ],
    },
)
