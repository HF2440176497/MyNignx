
include config.mk
.PHONY: all clean

all:
#-C是指定目录
	@for dir in $(BUILD_DIR); \
	do \
		make -C $$dir; \
	done

clean:
#-rf：删除文件夹，强制删除
	rm -rf app/link_obj
