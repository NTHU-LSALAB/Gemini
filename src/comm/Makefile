CXX ?= g++
CXXFLAGS ?= -O2 -Wall -fPIC

OBJS = endpoint.o message.o request.o response.o

all: comm

comm: $(OBJS)

clean:
	rm -rf *.o

%.o: %.cpp %.hpp
	$(CXX) $(CXXFLAGS) -c $< -o $@
