#
# Setup vcan0 (need vcan1?)
#
CANBUS=vcan0

sudo modprobe vcan

sudo ip link add dev $CANBUS type vcan
sudo ip link set up $CANBUS

