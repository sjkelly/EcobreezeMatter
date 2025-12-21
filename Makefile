BOARD = SiliconLabs:silabs:nano_matter
PORT = /dev/ttyACM0
SKETCH = EcobreezeMatter.ino
ARDUINO_CLI = ./bin/arduino-cli

.PHONY: compile upload monitor

compile:
	$(ARDUINO_CLI) compile -v --fqbn $(BOARD) $(SKETCH)

upload:
	$(ARDUINO_CLI) upload -v -p $(PORT) --fqbn $(BOARD) $(SKETCH)

monitor:
	$(ARDUINO_CLI) monitor -p $(PORT) --config 115200
