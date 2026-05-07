# Simply use arduino-cli
#
FQBN=arduino:avr:uno
DEV=/dev/serial/by-id/usb-Arduino_Srl_Arduino_Uno_55431313038351C04281-if00
OPTS=--build-property "compiler.c.extra_flags=-Os" \
     --build-property "compiler.cpp.extra_flags=-Os"

compile:
	arduino-cli compile -e --fqbn $(FQBN) $(OPTS)

upload:
	arduino-cli upload -p $(DEV) --fqbn $(FQBN)
