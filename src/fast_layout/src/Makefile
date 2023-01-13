CXX=g++-11
CFLAGS=-std=c++2a -O0 -g -MD -Wall -I. -fsanitize=address

RM = rm -f
TARGET_LIB = libp4txn.a
SRCS = $(wildcard *.cpp)
OBJS = $(SRCS:.cpp=.o)
D_FILES = $(SRCS:.cpp=.d)
HS = $(wildcard *.h)

.PHONY: all
	all: ${TARGET_LIB}

$(TARGET_LIB): $(OBJS)
		ar rvs $@ -o $@ $^

%.o: %.cpp $(HS)
		$(CXX) -g -c -o $@ $< ${CFLAGS}

.PHONY: clean
clean:
	${RM} ${TARGET_LIB} ${OBJS} ${D_FILES}

