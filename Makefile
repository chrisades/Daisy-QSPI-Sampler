# Project Name
TARGET = Example

# Sources
CPP_SOURCES = Example.cpp \ src/SamplePlayer.cpp

# Library Locations
LIBDAISY_DIR = ../../libDaisy
DAISYSP_DIR = ../../DaisySP

# Core location, and generic makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

CPP_STANDARD = -std=gnu++17