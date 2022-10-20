#  export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:/home/mike/CLionProjects/autumn2022/distributedSystems/pa2/lib64"
#  LD_PRELOAD=/home/mike/CLionProjects/autumn2022/distributedSystems/pa2/lib64/libruntime.so ./a.out -p 2 10 20
all:
	clang -std=c99 -Wall -pedantic *.c -L./lib64 -lruntime