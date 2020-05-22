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

CXX ?= g++
CXXFLAGS ?= -O2 -Wall -fPIC

OBJS = endpoint.o message.o request.o response.o

all: comm

comm: $(OBJS)

clean:
	rm -rf *.o

%.o: %.cpp %.hpp
	$(CXX) $(CXXFLAGS) -c $< -o $@
