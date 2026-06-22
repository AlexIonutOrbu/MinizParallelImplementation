CXX		= g++ -std=c++20
INCLUDES	= -I . -I miniz
CXXFLAGS  	+= -Wall 

LDFLAGS 	= #-pthread -fopenmp
OPTFLAGS	= -O3 -march=native

TARGETS		= minizseq

.PHONY: all clean cleanall
.SUFFIXES: .cpp 


%: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(OPTFLAGS) -o $@ $< ./miniz/miniz.c $(LDFLAGS)

all		: $(TARGETS)

minizseq	: minizseq.cpp cmdline.hpp utility.hpp

clean		: 
	rm -f $(TARGETS) 
cleanall	: clean
	\rm -f *.o *~



