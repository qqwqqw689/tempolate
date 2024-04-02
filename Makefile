# SRC = code.c pool.c
# LFLAGS=-lm
# CC=mpicc

# all: 
# 	$(CC) -o code $(SRC) $(LFLAGS)

# clean:
# 	rm -f code

# 指定编译器
CC=mpicc
# 指定编译时的选项
CFLAGS=-I. -Wall
# 指定链接时的库，如果有的话
LDFLAGS=

# 源文件列表
SOURCES=code.c comm.c function.c pool.c worker.c
# 通过替换 .c 后缀来自动生成对象文件列表
OBJECTS=$(SOURCES:.c=.o)
# 指定最终可执行文件的名称
EXECUTABLE=code

# 默认目标
all: $(EXECUTABLE)

# 链接对象文件，生成最终的可执行文件
$(EXECUTABLE): $(OBJECTS)
	$(CC) $(LDFLAGS) $^ -o $@

# 编译每个源文件为对象文件
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# 伪目标：清理编译生成的文件
clean:
	rm -f $(OBJECTS) $(EXECUTABLE)
