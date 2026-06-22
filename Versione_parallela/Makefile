CXX		= g++ -std=c++20
INCLUDES	= -I . -I miniz -I ./include
CXXFLAGS  	+= -Wall 

LDFLAGS 	= #-pthread -fopenmp
OPTFLAGS	= -O3 -march=native

TARGETS		= minizpar

.PHONY: all clean cleanall
.SUFFIXES: .cpp 


%: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(OPTFLAGS) -o $@ $< ./miniz/miniz.c $(LDFLAGS)

all		: $(TARGETS)

minizpar    : minizpar.cpp include/cmdline.hpp include/utility.hpp include/threadPool.hpp
clean		: 
	rm -f $(TARGETS) 
cleanall	: clean
	\rm -f *.o *~



