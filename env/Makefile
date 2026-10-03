CXX=g++
CXXFLAGS=-std=c++17 -O2 -Wall -Wextra -I../third_party
LDFLAGS=-lssl -lcrypto

all: minishop_env

minishop_env: minishop_env.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f minishop_env
