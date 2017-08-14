#INC_FLAGS = -Iinclude -I$(OPENCV_HOME)/include -I$(JSONCPP_HOME)/include -I$(LOG4CXX_HOME)/include -I$(SEETATECH_HOME)/include -I$(BOOST_HOME)/include
INC_FLAGS = -I$(MYSQLCPPCONN_INC) -I$(BOOST_INC) -I$(CUDA_INC)
#LIB_FLAGS = -L$(OPENCV_HOME)/lib -L$(JSONCPP_HOME)/lib -L$(LOG4CXX_HOME)/lib -L$(SEETATECH_HOME)/lib 
LIB_FLAGS = -L$(MYSQLCPPCONN_LIB) -L$(CUDA_LIB)/stubs

TARGETS = updtdevinf
CFLAGS = -std=c++11 -O2 -Wall
CFLAGS += $(GPU)
CFLAGS += -o $(TARGETS)
CFLAGS += $(INC_FLAGS)

ifeq ($(GPU), -DSUPPORT_GPU)
LIB_NVML = -lnvidia-ml
endif

LIB_FLAGS += $(LIB_NVML)
CFLAGS += $(LIB_FLAGS)

all: 
	g++ updtdevinf.cc $(CFLAGS) -lmysqlcppconn $(GPU) $(OS_T)
	mv $(TARGETS) release -f

clean:
	rm release/$(TARGETS) -f


