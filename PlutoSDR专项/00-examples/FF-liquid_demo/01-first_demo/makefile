include ../../toolchain/toolchain.mk

TARGET = demo
PLUTO  ?= root@pluto.local

# 3. 编译规则
build:
	$(CC) $(TARGET).c -o $(TARGET) $(CFLAGS) $(LDFLAGS)
	$(STRIP) $(TARGET)

clean:
	rm -rf $(TARGET)

# 4. 上机（先编译，scp 二进制 + 测试文件到板子，再 ssh 运行）
run: clean build
	@sshpass -p '$(SSH_PASS)' scp -O $(SSH_OPTS) $(TARGET) $(PLUTO):~/
	@echo "------------------------------------------------"
	@sshpass -p '$(SSH_PASS)' ssh $(SSH_OPTS) -t $(PLUTO) "chmod +x ~/$(TARGET); ./$(TARGET)"

.PHONY: build clean run
