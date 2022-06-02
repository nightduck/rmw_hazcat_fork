RMW_HAZCAT_CPP
---------------

C++ implementation of the rmw hazcat middleware interface.

Based on iceoryx, so vestigial references may still be found

## test

```sh
/usr/local/bin/iox-roudi -l verbose

export RMW_IMPLEMENTATION=rmw_iceoryx_cpp
ros2 run demo_nodes_cpp talker
ros2 run demo_nodes_cpp listener
iox-introspection-client --all
```

Ctrl+Shift+B to build

To test:
colcon test --merge-install --packages-select rmw_hazcat_cpp