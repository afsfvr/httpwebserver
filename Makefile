CC = g++
CXX = g++
BIN = main
SRC = $(wildcard *.cpp) 
OBJ = $(SRC:.cpp=.o)
DEP = $(OBJ:.o=.d)
SUBDIRS := root upload

LDFLAGS += -Wall -pthread -rdynamic -std=c++17
LDLIBS += -ldl
CXXFLAGS += -Wall -MMD -std=c++17

LOG ?= 1
ifeq ($(LOG), 0)
	CXXFLAGS += -DNO_LOG
endif

REDIS ?= 0
ifeq ($(REDIS), 1)
	LDLIBS += -lhiredis
	CXXFLAGS += -DUSE_REDIS
endif

HTTPS ?= 0
ifeq ($(HTTPS), 1)
	LDLIBS += -lssl -lcrypto
	CXXFLAGS += -DHTTPS
endif

NGINX ?= 0
ifeq ($(NGINX), 1)
	CXXFLAGS += -DUSE_NGINX
endif

DEBUG ?= 0
ifeq ($(DEBUG), 1)
	LDFLAGS += -g -Wl,-O0
	CXXFLAGS += -g -O0 -DDEBUG
else
	LDFLAGS += -Wl,-O2
	CXXFLAGS += -O2 -DRELEASE -DNDEBUG
endif

.PHONY: all clean cleanAll $(SUBDIRS)

$(BIN): $(OBJ)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

authorize: $(BIN)
	[ -x $(BIN) ] && sudo setcap 'cap_net_bind_service=+ep' $(BIN)

all: $(BIN) $(SUBDIRS)

$(SUBDIRS):
	@printf "\nBuilding %s ...\n" "$@"
	@$(MAKE) -C webapps/$@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

-include $(DEP)

clean:
	rm -f $(BIN) $(OBJ) $(DEP)

cleanAll: clean
	@for dir in $(SUBDIRS); do \
		printf "\n→ Cleaning %s ...\n" "$$dir"; \
		$(MAKE) -C webapps/$$dir clean; \
	done
