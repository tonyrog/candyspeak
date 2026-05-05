# Simply use arduino-cli
#
# FQBN=arduino:avr:uno
# FQBN=arduino:samd:mkrzero
FQBN=arduino:samd:adafruit_circuitplayground_m0

#DEV=/dev/ttyACM0
#DEV=/dev/serial/by-id/usb-Arduino_Srl_Arduino_Uno_55431313038351C04281-if00
DEV=/dev/ttyACM0

compile:
	arduino-cli compile -e --fqbn $(FQBN)

upload:
	arduino-cli upload -p $(DEV) --fqbn $(FQBN)
