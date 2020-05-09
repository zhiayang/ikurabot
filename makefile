# Makefile for Orion-X3/Orion-X4/mx and derivatives
# Written in 2011
# This makefile is licensed under the WTFPL


WARNINGS        = -Wno-padded -Wno-cast-align -Wno-unreachable-code -Wno-switch-enum -Wno-packed -Wno-missing-noreturn -Wno-float-equal -Wno-unused-macros -Werror=return-type -Wextra -Wno-unused-parameter -Wno-trigraphs

COMMON_CFLAGS   = -Wall -O2

CXXFLAGS        = $(COMMON_CFLAGS) -Wno-old-style-cast -std=c++17 -ferror-limit=0 -g

CXXSRC          = $(shell find source -iname "*.cpp" -print)
CXXOBJ          = $(CXXSRC:.cpp=.cpp.o)
CXXDEPS         = $(CXXOBJ:.o=.d)

DEFINES         = -DKISSNET_NO_EXCEP -DKISSNET_USE_OPENSSL
INCLUDES        = $(shell pkg-config --cflags openssl) -Isource/include -Iexternal

.PHONY: all clean build
.DEFAULT_GOAL = all


all: build
	@build/ikurabot

build: build/ikurabot

build/ikurabot: $(CXXOBJ)
	@$(CXX) $(CXXFLAGS) -o $@ $^ $(shell pkg-config --libs openssl)

%.cpp.o: %.cpp makefile
	@echo "  $(notdir $<)"
	@$(CXX) $(CXXFLAGS) $(WARNINGS) $(INCLUDES) $(DEFINES) -MMD -MP -c -o $@ $<

clean:
	@find source -iname "*.cpp.d" | xargs rm
	@find source -iname "*.cpp.o" | xargs rm

-include $(CXXDEPS)













