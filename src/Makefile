# Copyright 2020 Hung-Hsin Chen, LSA Lab, National Tsing Hua University
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# path to CUDA installation
CUDA_PATH ?= /usr/local/cuda-10.0
PWD := $(shell pwd)
PREFIX ?= $(PWD)/..
INSTALL = /usr/bin/install
PKGCONFIG = /usr/bin/pkg-config

SMS ?= 30 35 37 50 52 60 61 70
GENCODE_FLAGS += $(foreach sm,$(SMS),-gencode arch=compute_$(sm),code=sm_$(sm))

CXX ?= g++
NVCC ?= $(CUDA_PATH)/bin/nvcc -ccbin $(CXX)

CUDA_LDFLAGS += -lcuda -L$(CUDA_PATH)/lib64 -L$(CUDA_PATH)/lib64/stubs
LDFLAGS += -ldl -lrt

CXXFLAGS += -fPIC

ifeq ($(DEBUG),1)
CXXFLAGS += -g -D_DEBUG -Wall
else
CXXFLAGS += -O2
endif

# Communication library
COMM_FILES = endpoint message request response
COMM_HEADERS = $(foreach file,$(COMM_FILES),comm/$(file).hpp)
COMM_OBJS = $(foreach file,$(COMM_FILES),comm/$(file).o)

# Third-party libraries
EXTLIBS = libzmq glib-2.0
CXXFLAGS += $(foreach ext,$(EXTLIBS),$(shell $(PKGCONFIG) --cflags $(ext)))
LDFLAGS += $(foreach ext,$(EXTLIBS),$(shell $(PKGCONFIG) --libs $(ext)))

# Target rules
.PHONY: comm

all: libgemhook.so.1 gem-schd

debug.o: debug.cpp debug.h
	$(CXX) $(CXXFLAGS) -o $@ -c $<

hook.o: hook.cpp debug.h predictor.h util.h $(COMM_HEADERS)
	$(NVCC) -m64 --compiler-options "$(CXXFLAGS)" $(GENCODE_FLAGS) -o $@ -c $<

predictor.o: predictor.cpp predictor.h debug.h
	$(CXX) $(CXXFLAGS) -o $@ -c $<

libgemhook.so.1: hook.o predictor.o debug.o comm
	$(NVCC) -shared -m64 $(GENCODE_FLAGS) $(CUDA_LDFLAGS) $(LDFLAGS) -o $@ \
		hook.o predictor.o debug.o $(COMM_OBJS)
	mkdir -p $(PREFIX)/lib
	$(INSTALL) -s -m 755 -t $(PREFIX)/lib $@

scheduler.o: scheduler.cpp debug.h util.h scheduler.h $(COMM_HEADERS)
	$(CXX) $(CXXFLAGS) -o $@ -c $<

schd-priority.o: schd-priority.cpp scheduler.h
	$(CXX) $(CXXFLAGS) -o $@ -c $<

parse-config.o: parse-config.cpp parse-config.h
	$(CXX) $(CXXFLAGS) -o $@ -c $<

gem-schd: scheduler.o schd-priority.o debug.o parse-config.o comm
	$(CXX) $(LDFLAGS) -pthread -rdynamic -o $@ \
		scheduler.o schd-priority.o debug.o parse-config.o $(COMM_OBJS)
	mkdir -p $(PREFIX)/bin
	$(INSTALL) -s -m 755 -t $(PREFIX)/bin $@

comm:
	@$(MAKE) -C comm

clean:
	@$(MAKE) -C comm clean
	rm -f *.o
