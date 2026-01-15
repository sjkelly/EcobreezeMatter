BOARD = SiliconLabs:silabs:nano_matter
PORT = /dev/ttyACM0
SKETCH = EcobreezeMatter.ino
TEST_SKETCH = GP8211Test/GP8211Test.ino
ARDUINO_CLI = ./bin/arduino-cli

.PHONY: compile upload monitor compile-test upload-test

compile:
	$(ARDUINO_CLI) compile -v --fqbn $(BOARD) $(SKETCH)

upload: compile
	$(ARDUINO_CLI) upload -v -p $(PORT) --fqbn $(BOARD) $(SKETCH)

monitor:
	$(ARDUINO_CLI) monitor -p $(PORT) --config 115200

compile-test:
	$(ARDUINO_CLI) compile -v --fqbn $(BOARD) $(TEST_SKETCH)

# Added dependency: upload-test now runs compile-test first
upload-test: compile-test
	$(ARDUINO_CLI) upload -v -p $(PORT) --fqbn $(BOARD) $(TEST_SKETCH)