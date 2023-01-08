CXX=g++-11
CFLAGS=-std=c++2a -O3 -g -MD -Wall -I. -Isrc/ -fsanitize=address

RM = rm -f
LIB = src/libp4txn.a
SRCS = $(wildcard *.cpp)
HS = $(wildcard *.h)
TESTS = $(SRCS:.cpp=.run)
D_FILES = $(SRCS:.cpp=.d)

all: make_dep $(TESTS)

make_dep:
	make -C src/

%.run: %.cpp $(LIB) $(HS)
	$(CXX) -g -o $@ $< $(LIB) $(CFLAGS)

clean:
	${RM} ${TESTS} ${D_FILES}

