g++ *.cpp -std=c++11 -Wall -Werror -Wpedantic -Wextra -Wno-unused -O3 -g -o sim && 
./sim 2>trace | tee report
