APPNAME = mycpp

# Do not override CXX if it’s already set
CXX ?= g++
CXXFLAGS ?= -Wall -O2
LDFLAGS += -lgpiod

SRC = main.cpp
OBJ = $(SRC:.cpp=.o)

all: $(APPNAME)

$(APPNAME): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(APPNAME) *.o
