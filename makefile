CC = g++
CXX = g++
LDFLAGS += -Wall -pthread -ldl -rdynamic
CXXFLAGS += -Wall -MMD
BIN = main
SRC = $(wildcard *.cpp) 
OBJ = $(SRC:.cpp=.o)
DEP = $(OBJ:.o=.d)

LOG ?= 1

ifeq ($(LOG), 0)
	LDFLAGS += -DNO_LOG
	CXXFLAGS += -DNO_LOG
endif

REDIS ?= 0

ifeq ($(REDIS), 0)
	LDFLAGS += -DNO_REDIS
	CXXFLAGS += -DNO_REDIS
else
	LDFLAGS += -lhiredis
endif

DEBUG ?= 0
ifeq ($(DEBUG), 1)
	LDFLAGS += -g
	CXXFLAGS += -g
endif

a:
	$(CXX) $(SRC) -o $(BIN) $(LDFLAGS)

$(BIN): $(OBJ)
	$(CXX) $^ -o $@ $(LDFLAGS)

all: $(BIN) app1 upload

app1:
	@echo ""
	@$(MAKE) -C webapps/app1

upload:
	@echo ""
	@$(MAKE) -C webapps/upload

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

-include $(DEP)

clean:
	rm -f $(OBJ) $(BIN) $(DEP)

cleanApp1:
	@echo ""
	@$(MAKE) -C webapps/app1 clean

cleanUpload:
	@echo ""
	@$(MAKE) -C webapps/upload clean

cleanAll: clean cleanApp1 cleanUpload
