CXX = g++
LDFLAGS += -shared -fPIC

SOURCES:=$(wildcard *.cpp)

main: $(SOURCES)
	$(CXX) $^ -o main.so $(LDFLAGS)

clean:
	rm -f main.so
