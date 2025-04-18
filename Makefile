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
ifeq ($(REDIS), 1)
	LDFLAGS += -lhiredis
	LDFLAGS += -DUSE_REDIS
	CXXFLAGS += -DUSE_REDIS
endif

DEBUG ?= 0
ifeq ($(DEBUG), 1)
	LDFLAGS += -g
	CXXFLAGS += -g
endif

$(BIN): $(OBJ)
	$(CXX) $^ -o $@ $(LDFLAGS)

all: $(BIN) root upload

root:
	@echo ""
	@$(MAKE) -C webapps/root

upload:
	@echo ""
	@$(MAKE) -C webapps/upload

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

-include $(DEP)

clean:
	rm -f $(BIN) $(OBJ) $(DEP)

cleanRoot:
	@echo ""
	@$(MAKE) -C webapps/root clean

cleanUpload:
	@echo ""
	@$(MAKE) -C webapps/upload clean

cleanAll: clean cleanRoot cleanUpload
