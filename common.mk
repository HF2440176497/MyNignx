﻿
#.PHONY:all clean 

ifeq ($(DEBUG),true)
CC = g++ -std=c++20 -g -D_GLIBCXX_USE_CXX11_ABI=0
VERSION = debug
else
CC = g++ -std=c++20 -D_GLIBCXX_USE_CXX11_ABI=0
VERSION = release
endif

#CC = gcc

# $(wildcard *.c)表示扫描当前目录下所有.c文件
SRCS = $(wildcard *.cxx)

OBJS = $(SRCS:.cxx=.o)
DEPS = $(SRCS:.cxx=.d)

# 若被 app 目录下的 makefile 引用，则BIN = BUILD_ROOT/nginx
BIN := $(addprefix $(BUILD_ROOT)/,$(BIN))

#注意下边这个字符串，末尾不要有空格等否则会语法错误 
LINK_OBJ_DIR = $(BUILD_ROOT)/app/link_obj
DEP_DIR = $(BUILD_ROOT)/app/dep

#-p是递归创建目录，没有就创建，有就不需要创建了
$(shell mkdir -p $(LINK_OBJ_DIR))
$(shell mkdir -p $(DEP_DIR))

# := 在解析阶段直接赋值常量字符串【立即展开】，而 = 在运行阶段，实际使用变量时再进行求值【延迟展开】
OBJS := $(addprefix $(LINK_OBJ_DIR)/,$(OBJS))
DEPS := $(addprefix $(DEP_DIR)/,$(DEPS))

#找到目录中的所有.o文件（编译出来的）
LINK_OBJ = $(wildcard $(LINK_OBJ_DIR)/*.o)
#因为构建依赖关系时app目录下这个.o文件还没构建出来，所以LINK_OBJ是缺少这个.o的，我们 要把这个.o文件加进来
LINK_OBJ += $(OBJS)

#-------------------------------------------------------------------------------------------------------
all:$(DEPS) $(OBJS) $(BIN)

#这里是诸多.d文件被包含进来，每个.d文件里都记录着一个.o文件所依赖哪些.c和.h文件。内容诸如 nginx.o: nginx.c ngx_func.h
#我们做这个的最终目的说白了是，即便.h被修改了，也要让make重新编译我们的工程，否则，你修改了.h，make不会重新编译，那不行的
#有必要先判断这些文件是否存在，不然make可能会报一些.d文件找不到
ifneq ("$(wildcard $(DEPS))","")   #如果不为空,$(wildcard)是函数【获取匹配模式文件名】，这里 用于比较是否为""
include $(DEPS)  
endif
# 展开 .d 文件; 因为 .d 文件内容符合目标：依赖的格式
# 因此 .d 中记录的文件例如头文件更新，会更新目标 OBJS 的时间戳，导致 all 的依赖更新，从而有执行 all 命令


#----------------------------------------------------------------1begin------------------
#$(BIN):$(OBJS)
$(BIN):$(LINK_OBJ)
	@echo "------------------------build BIN $(VERSION) mode--------------------------------!!!"
	$(CC) -o $@ $^ -lpthread

#----------------------------------------------------------------1end-------------------


#----------------------------------------------------------------2begin-----------------
#%.o:%.cxx
$(LINK_OBJ_DIR)/%.o:%.cxx
# gcc -c是生成.o目标文件   -I可以指定头文件的路径
	@echo "------------------------build OBJ $(VERSION) mode--------------------------------!!!"
	$(CC) -I$(INCLUDE_PATH) -o $@ -c $(filter %.cxx,$^)
#----------------------------------------------------------------2end-------------------


#----------------------------------------------------------------3begin-----------------
#那一个.o依赖于哪些.h文件，我们可以用“gcc -MM c程序文件名” 来获得这些依赖信息并重定向保存到.d文件中
#.d文件中的内容可能形如：nginx.o: nginx.c ngx_func.h
#%.d:%.c
$(DEP_DIR)/%.d:%.cxx
	echo -n $(LINK_OBJ_DIR)/ > $@

# 这里输出依赖文件名到.d文件中 例如：signal.o 只依赖于 signal.c
	gcc -I$(INCLUDE_PATH) -MM $^ >> $@

#----------------------------------------------------------------4begin-----------------



#----------------------------------------------------------------nbegin-----------------
#clean:			
#rm 的-f参数是不提示强制删除
#可能gcc会产生.gch这个优化编译速度文件
#	rm -f $(BIN) $(OBJS) $(DEPS) *.gch
#----------------------------------------------------------------nend------------------

